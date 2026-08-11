// SPDX-License-Identifier: BSD-3-Clause
#include "TTTRStreamWriter.h"

#include "TTTRFormat.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace tttrlib {
namespace io {

namespace {

/// One handover from the producer. Owns its events; the writer thread frees it.
struct Batch {
    std::vector<std::uint64_t> macro;
    std::vector<std::uint16_t> micro;
    std::vector<std::int8_t> chan;
    std::vector<std::int8_t> type;
    /// The producer asked for this to be durable before it is acknowledged.
    bool durable = false;
    std::size_t size() const { return macro.size(); }
};

}  // namespace

struct TTTRStreamWriter::Impl {
    // The producer holds `mu` only long enough to move a batch in or out.
    // File I/O happens on the writer thread with `mu` released, which is what
    // keeps a slow disk off the instrument's thread.
    mutable std::mutex mu;
    std::condition_variable room;      ///< the producer waits here when full
    std::condition_variable work;      ///< the writer waits here for a batch
    std::condition_variable drained;   ///< checkpoint/close wait here

    std::deque<Batch> queue;
    std::uint64_t queued_events = 0;
    std::uint64_t committed = 0;
    std::uint64_t dropped = 0;         ///< invariant: stays 0
    std::uint64_t stalls = 0;
    std::uint64_t stall_ns = 0;

    std::uint64_t limit = 8ull * 1024 * 1024;
    std::uint64_t auto_at = 0;

    std::thread writer;
    bool running = false;
    bool open = false;
    bool writer_failed = false;
    std::string writer_error;
    std::string err;

    /// Everything handed over has reached the backend.
    bool idle() const { return queue.empty(); }
};

TTTRStreamWriter::TTTRStreamWriter() : p_(new Impl) {}

TTTRStreamWriter::~TTTRStreamWriter() {
    // The thread is stopped and joined here, and NOTHING virtual is called:
    // by the time a base destructor runs the derived part is already gone, so
    // reaching write_chunk() or close_target() is "pure virtual function
    // called", i.e. an abort. A derived class therefore closes in ITS
    // destructor, which is where the overrides still exist -- this is only the
    // backstop that guarantees no thread outlives the object it points at.
    if (!p_) return;
    {
        std::lock_guard<std::mutex> g(p_->mu);
        p_->running = false;
    }
    p_->work.notify_all();
    p_->room.notify_all();
    if (p_->writer.joinable()) p_->writer.join();
}

bool TTTRStreamWriter::fail(const std::string& why) { p_->err = why; return false; }
void TTTRStreamWriter::clear_error() { p_->err.clear(); }
const std::string& TTTRStreamWriter::error() const { return p_->err; }

std::uint64_t TTTRStreamWriter::n_committed() const {
    std::lock_guard<std::mutex> g(p_->mu);
    return p_->committed;
}
std::uint64_t TTTRStreamWriter::n_buffered() const {
    std::lock_guard<std::mutex> g(p_->mu);
    return p_->queued_events;
}
std::uint64_t TTTRStreamWriter::n_dropped() const {
    std::lock_guard<std::mutex> g(p_->mu);
    return p_->dropped;
}
std::uint64_t TTTRStreamWriter::n_stalls() const {
    std::lock_guard<std::mutex> g(p_->mu);
    return p_->stalls;
}
std::uint64_t TTTRStreamWriter::stall_nanoseconds() const {
    std::lock_guard<std::mutex> g(p_->mu);
    return p_->stall_ns;
}
bool TTTRStreamWriter::is_open() const {
    std::lock_guard<std::mutex> g(p_->mu);
    return p_->open;
}

void TTTRStreamWriter::set_buffer_limit(std::uint64_t events) {
    std::lock_guard<std::mutex> g(p_->mu);
    // Zero would mean "block forever on the first append"; one event is the
    // smallest honest reading of "as little as possible".
    p_->limit = events == 0 ? 1 : events;
    p_->room.notify_all();
}
std::uint64_t TTTRStreamWriter::buffer_limit() const {
    std::lock_guard<std::mutex> g(p_->mu);
    return p_->limit;
}
void TTTRStreamWriter::set_auto_checkpoint(std::uint64_t events) {
    std::lock_guard<std::mutex> g(p_->mu);
    p_->auto_at = events;
}
std::uint64_t TTTRStreamWriter::auto_checkpoint() const {
    std::lock_guard<std::mutex> g(p_->mu);
    return p_->auto_at;
}

bool TTTRStreamWriter::create(const std::string& filename, TTTRHeader* header,
                              const std::string& name) {
    clear_error();
    if (p_->open) return fail("this stream is already open");
    if (header == nullptr)
        return fail("a photon stream needs a header: without the clocks the "
                    "events it writes have no units");

    if (!open_target(filename, header, name)) {
        // The backend has already said why, unless it forgot.
        if (p_->err.empty()) fail("the format could not open " + filename);
        return false;
    }

    Impl& m = *p_;
    {
        std::lock_guard<std::mutex> g(m.mu);
        m.open = true;
        m.running = true;
    }
    m.writer = std::thread([this] {
        Impl& s = *p_;
        for (;;) {
            Batch b;
            {
                std::unique_lock<std::mutex> lk(s.mu);
                s.work.wait(lk, [&] { return !s.queue.empty() || !s.running; });
                if (s.queue.empty()) {
                    if (!s.running) return;
                    continue;
                }
                b = std::move(s.queue.front());
                s.queue.pop_front();
            }
            // Written with the lock released: this is the slow part, and
            // holding the lock here would put the disk back on the producer's
            // critical path, which is the whole thing being avoided.
            bool ok = true;
            if (b.size() > 0 || b.durable) {
                ok = write_chunk(b.macro.data(), b.micro.data(), b.chan.data(),
                                 b.type.data(), b.size(), b.durable);
            }
            {
                std::lock_guard<std::mutex> lk(s.mu);
                if (ok) {
                    s.committed += b.size();
                } else if (!s.writer_failed) {
                    s.writer_failed = true;
                    s.writer_error = p_->err.empty()
                            ? std::string("the format failed to write a chunk")
                            : p_->err;
                }
                s.queued_events -= b.size();
            }
            s.room.notify_all();
            s.drained.notify_all();
        }
    });
    return true;
}

bool TTTRStreamWriter::append(const unsigned long long* macro_times, std::size_t n_macro,
                              const unsigned short* micro_times, std::size_t n_micro,
                              const signed char* routing_channels, std::size_t n_routing,
                              const signed char* event_types, std::size_t n_event) {
    Impl& m = *p_;
    clear_error();
    if (!m.open) return fail("this stream is not open");
    // Equal lengths, checked rather than trusted: four arrays that disagree
    // offset the columns against each other, so every photon after the short
    // one is attributed to the wrong event and nothing downstream can tell.
    if (n_macro != n_micro || n_macro != n_routing || n_macro != n_event)
        return fail("the four event arrays have different lengths (macro " +
                    std::to_string(n_macro) + ", micro " + std::to_string(n_micro) +
                    ", routing " + std::to_string(n_routing) + ", event type " +
                    std::to_string(n_event) + "); one photon per row in each");
    if (n_macro == 0) return true;

    Batch b;
    b.macro.assign(macro_times, macro_times + n_macro);
    b.micro.assign(micro_times, micro_times + n_micro);
    b.chan.assign(reinterpret_cast<const std::int8_t*>(routing_channels),
                  reinterpret_cast<const std::int8_t*>(routing_channels) + n_routing);
    b.type.assign(reinterpret_cast<const std::int8_t*>(event_types),
                  reinterpret_cast<const std::int8_t*>(event_types) + n_event);

    {
        std::unique_lock<std::mutex> lk(m.mu);
        if (m.writer_failed) return fail(m.writer_error);
        // Backpressure. The producer waits; it never drops, and the buffer
        // never grows past the limit. A photon that arrived is a photon that
        // will be written, and the cost of a slow disk is paid in latency
        // here rather than in data.
        if (m.queued_events + n_macro > m.limit) {
            const auto t0 = std::chrono::steady_clock::now();
            m.stalls++;
            m.room.wait(lk, [&] {
                return m.queued_events + n_macro <= m.limit || !m.running ||
                       m.writer_failed;
            });
            m.stall_ns += static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now() - t0).count());
        }
        if (m.writer_failed) return fail(m.writer_error);
        if (!m.running) return fail("this stream is closing");

        m.queued_events += n_macro;
        // auto_checkpoint asks for durability at a cadence; buffer_limit is
        // what bounds memory. Two knobs because they answer different
        // questions -- "how much may a crash cost" and "how much may the
        // writer hold" -- and a caller usually wants the first much smaller.
        b.durable = m.auto_at != 0 && m.queued_events >= m.auto_at;
        m.queue.push_back(std::move(b));
    }
    m.work.notify_one();
    return true;
}

bool TTTRStreamWriter::checkpoint() {
    Impl& m = *p_;
    clear_error();
    if (!m.open) return fail("this stream is not open");

    {
        std::unique_lock<std::mutex> lk(m.mu);
        if (m.writer_failed) return fail(m.writer_error);
        // An empty marker batch, so a checkpoint with nothing buffered still
        // reaches the backend and can make what came earlier durable.
        Batch marker;
        marker.durable = true;
        m.queue.push_back(std::move(marker));
    }
    m.work.notify_one();

    std::unique_lock<std::mutex> lk(m.mu);
    m.drained.wait(lk, [&] { return m.idle() || m.writer_failed || !m.running; });
    if (m.writer_failed) return fail(m.writer_error);
    return true;
}

bool TTTRStreamWriter::close() {
    Impl& m = *p_;
    if (!m.open) return true;

    const bool flushed = checkpoint();
    {
        std::lock_guard<std::mutex> g(m.mu);
        m.running = false;
    }
    m.work.notify_all();
    m.room.notify_all();
    if (m.writer.joinable()) m.writer.join();

    const bool closed = close_target();
    {
        std::lock_guard<std::mutex> g(m.mu);
        m.open = false;
    }
    return flushed && closed;
}

bool TTTRStreamWriter::submit(const std::uint64_t* macro_times,
                              const std::uint16_t* micro_times,
                              const std::int8_t* routing_channels,
                              const std::int8_t* event_types,
                              std::size_t n) {
    // The sink face of append(). Same buffering, same backpressure, same
    // guarantee -- there is deliberately not a second path into the queue.
    return append(reinterpret_cast<const unsigned long long*>(macro_times), n,
                  reinterpret_cast<const unsigned short*>(micro_times), n,
                  reinterpret_cast<const signed char*>(routing_channels), n,
                  reinterpret_cast<const signed char*>(event_types), n);
}

bool TTTRStreamWriter::flush() { return checkpoint(); }

namespace {

/// The factory a format registered, or null. \see FileFormat::make_stream_writer
std::unique_ptr<TTTRStreamWriter> from_format(const FileFormat* f) {
    if (f == nullptr || f->make_stream_writer == nullptr) return nullptr;
    void* raw = f->make_stream_writer(f->stream_context);
    return std::unique_ptr<TTTRStreamWriter>(static_cast<TTTRStreamWriter*>(raw));
}

}  // namespace

std::unique_ptr<TTTRStreamWriter> make_stream_writer(const std::string& filename) {
    // By extension, not by content: the file does not exist yet. A stream
    // writer is asked for before there is anything to sniff.
    const int container = IORegistry::container_type_from_extension(filename);
    if (container < 0) return nullptr;
    return make_stream_writer_for(container);
}

std::unique_ptr<TTTRStreamWriter> make_stream_writer_for(int container_type) {
    return from_format(IORegistry::by_container_type(container_type));
}

bool can_stream(int container_type) {
    const FileFormat* f = IORegistry::by_container_type(container_type);
    return f != nullptr && f->make_stream_writer != nullptr;
}

}  // namespace io
}  // namespace tttrlib

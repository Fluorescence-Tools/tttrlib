// SPDX-License-Identifier: BSD-3-Clause
#include "PhotonSink.h"

#include <algorithm>
#include <mutex>

namespace tttrlib {
namespace io {

PhotonSink::~PhotonSink() = default;

bool PhotonSink::submit_events(const unsigned long long* macro_times, std::size_t n_macro,
                               const unsigned short* micro_times, std::size_t n_micro,
                               const signed char* routing_channels, std::size_t n_routing,
                               const signed char* event_types, std::size_t n_event) {
    // Four arrays that disagree would offset the columns against each other,
    // so every photon after the short one is attributed to the wrong event --
    // and nothing downstream of here can detect that.
    if (n_macro != n_micro || n_macro != n_routing || n_macro != n_event) return false;
    if (n_macro == 0) return true;
    return submit(reinterpret_cast<const std::uint64_t*>(macro_times),
                  reinterpret_cast<const std::uint16_t*>(micro_times),
                  reinterpret_cast<const std::int8_t*>(routing_channels),
                  reinterpret_cast<const std::int8_t*>(event_types), n_macro);
}

struct PhotonStreamHub::Impl {
    mutable std::mutex mu;
    std::vector<PhotonSink*> sinks;                  ///< borrowed
    std::vector<std::unique_ptr<PhotonSink>> owned;  ///< kept alive here
    std::vector<std::string> failed;
    std::uint64_t events = 0;
    std::string err;
};

PhotonStreamHub::PhotonStreamHub() : p_(new Impl) {}
PhotonStreamHub::~PhotonStreamHub() = default;

void PhotonStreamHub::add_sink(PhotonSink* sink) {
    if (sink == nullptr) return;
    std::lock_guard<std::mutex> g(p_->mu);
    p_->sinks.push_back(sink);
}

void PhotonStreamHub::add_owned_sink(std::unique_ptr<PhotonSink> sink) {
    if (!sink) return;
    std::lock_guard<std::mutex> g(p_->mu);
    p_->sinks.push_back(sink.get());
    p_->owned.push_back(std::move(sink));
}

void PhotonStreamHub::remove_sink(PhotonSink* sink) {
    std::lock_guard<std::mutex> g(p_->mu);
    p_->sinks.erase(std::remove(p_->sinks.begin(), p_->sinks.end(), sink),
                    p_->sinks.end());
    p_->owned.erase(std::remove_if(p_->owned.begin(), p_->owned.end(),
                                   [sink](const std::unique_ptr<PhotonSink>& o) {
                                       return o.get() == sink;
                                   }),
                    p_->owned.end());
}

std::size_t PhotonStreamHub::n_sinks() const {
    std::lock_guard<std::mutex> g(p_->mu);
    return p_->sinks.size();
}

std::uint64_t PhotonStreamHub::n_events() const {
    std::lock_guard<std::mutex> g(p_->mu);
    return p_->events;
}

const std::string& PhotonStreamHub::error() const {
    std::lock_guard<std::mutex> g(p_->mu);
    return p_->err;
}

std::vector<std::string> PhotonStreamHub::failed_sinks() const {
    std::lock_guard<std::mutex> g(p_->mu);
    return p_->failed;
}

void PhotonStreamHub::set_header(TTTRHeader* header) {
    std::vector<PhotonSink*> snapshot;
    {
        std::lock_guard<std::mutex> g(p_->mu);
        snapshot = p_->sinks;
    }
    for (PhotonSink* s : snapshot) s->set_header(header);
}

bool PhotonStreamHub::submit(const std::uint64_t* macro_times,
                             const std::uint16_t* micro_times,
                             const std::int8_t* routing_channels,
                             const std::int8_t* event_types,
                             std::size_t n) {
    // A snapshot, so a sink attached or detached from another thread cannot
    // invalidate the iteration -- and so the lock is not held across the
    // consumers' work, which would serialise the hub against its own accessors.
    std::vector<PhotonSink*> snapshot;
    {
        std::lock_guard<std::mutex> g(p_->mu);
        snapshot = p_->sinks;
        p_->events += n;
    }

    bool all_ok = true;
    for (PhotonSink* s : snapshot) {
        // Every sink is offered the batch even after one fails. A file writer
        // that has run out of disk must not silently stop the correlator that
        // is still perfectly able to work -- and a caller watching a live plot
        // would have no way to tell those two apart.
        if (s->submit(macro_times, micro_times, routing_channels, event_types, n))
            continue;
        all_ok = false;
        std::lock_guard<std::mutex> g(p_->mu);
        const std::string name = s->sink_name();
        if (std::find(p_->failed.begin(), p_->failed.end(), name) == p_->failed.end())
            p_->failed.push_back(name);
        if (p_->err.empty()) p_->err = "the '" + name + "' consumer failed";
    }
    return all_ok;
}

bool PhotonStreamHub::flush() {
    std::vector<PhotonSink*> snapshot;
    {
        std::lock_guard<std::mutex> g(p_->mu);
        snapshot = p_->sinks;
    }
    bool all_ok = true;
    for (PhotonSink* s : snapshot) {
        if (s->flush()) continue;
        all_ok = false;
        std::lock_guard<std::mutex> g(p_->mu);
        const std::string name = s->sink_name();
        if (std::find(p_->failed.begin(), p_->failed.end(), name) == p_->failed.end())
            p_->failed.push_back(name);
        if (p_->err.empty()) p_->err = "the '" + name + "' consumer failed to flush";
    }
    return all_ok;
}

}  // namespace io
}  // namespace tttrlib

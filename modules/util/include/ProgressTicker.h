// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_PROGRESSTICKER_H
#define TTTRLIB_PROGRESSTICKER_H

// A general progress ticker: counts units of work and reports them through a
// sink. It knows nothing about terminals, JSON or the CLI -- a caller gives it
// a sink (std::function) and receives a ProgressEvent on begin, on every
// update that matters, and on finish:
//
//   {"begin", "correlate", "window", 0,    40, 0.0,   0.00}
//   {"tick",  "correlate", "window", 17,   40, 0.425, 1.30}
//   {"finish","correlate", "window", 40,   40, 1.0,   4.20}
//
// The event is a plain value: a sink can render it as a bar, forward it as
// JSONL for a GUI, or count it into a test. The ticker itself does no I/O, so
// it is usable from any thread and any layer of the library.
//
// Rates and ETA are computed from a steady clock started at begin(), so a
// long-running job reports throughput without the caller tracking time.

#include <cstddef>
#include <functional>
#include <string>

namespace tttrlib {

struct ProgressEvent {
    const char* kind;    ///< "begin" | "tick" | "finish"
    const char* job;     ///< job name (subcommand), stable per run
    const char* phase;   ///< current sub-phase, may change between ticks
    std::size_t done;    ///< units completed
    std::size_t total;   ///< units expected (may be 0 = unknown yet)
    double fraction;     ///< done/total clamped to [0,1], 0 if total is 0
    double seconds;      ///< seconds since begin()
};

/// Sink signature: receives a plain value; keep it cheap, it is called on the
/// ticker's thread.
using ProgressSink = std::function<void(const ProgressEvent&)>;

/// Counter + reporter for a long-running job. Not copyable.
class ProgressTicker {
public:
    explicit ProgressTicker(std::string job = "job");
    ~ProgressTicker();
    ProgressTicker(const ProgressTicker&) = delete;
    ProgressTicker& operator=(const ProgressTicker&) = delete;

    /// Where events go. Default: nowhere. Replace at any time.
    void set_sink(ProgressSink sink) { sink_ = std::move(sink); }

    /// Job name shown in every event.
    void set_job(std::string job) { job_ = std::move(job); }

    /// Sub-phase label; calling this re-reports the current fraction.
    void set_phase(std::string phase) { phase_ = std::move(phase); emit("tick"); }

    /// Expected total. Setting it (re)starts the clock implicitly if begin()
    /// has not been called yet.
    void set_total(std::size_t total) { total_ = total; }

    /// Start the job: resets done to zero, starts the clock, emits "begin".
    void begin();

    /// One more unit done; emits "tick" (throttled by the sink, not here).
    void tick() { update(done_ + 1); }

    /// Absolute progress; emits "tick". Never reports past total.
    void update(std::size_t done);

    /// Done at 100%, clock stopped, emits "finish".
    void finish();

    std::size_t done() const { return done_; }
    std::size_t total() const { return total_; }
    double fraction() const;       ///< clamped [0,1]
    double elapsed_seconds() const;
    double rate_per_second() const;
    double eta_seconds() const;    ///< -1 when unknown

private:
    void emit(const char* kind);
    void ensure_started();

    std::string job_;
    std::string phase_;
    std::size_t done_{0};
    std::size_t total_{0};
    bool started_{false};
    bool finished_{false};
    long long started_ms_{0};
    long long last_emit_ms_{0};
    ProgressSink sink_;
};

}  // namespace tttrlib

#endif  // TTTRLIB_PROGRESSTICKER_H
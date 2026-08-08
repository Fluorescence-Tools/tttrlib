// SPDX-License-Identifier: BSD-3-Clause
#include "ProgressTicker.h"

#include <algorithm>
#include <chrono>

namespace tttrlib {

namespace {

long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

ProgressTicker::ProgressTicker(std::string job) : job_(std::move(job)) {}
ProgressTicker::~ProgressTicker() = default;

void ProgressTicker::begin() {
    started_ = true;
    finished_ = false;
    done_ = 0;
    started_ms_ = now_ms();
    last_emit_ms_ = started_ms_;
    emit("begin");
}

void ProgressTicker::ensure_started() {
    if (!started_) begin();
}

void ProgressTicker::update(std::size_t done) {
    ensure_started();
    if (finished_) return;
    done_ = std::min(done, total_ ? total_ : done);  // never overshoot
    emit("tick");
}

void ProgressTicker::finish() {
    if (!started_) begin();
    if (finished_) return;
    finished_ = true;
    done_ = total_;
    emit("finish");
}

double ProgressTicker::fraction() const {
    if (total_ == 0) return 0.0;
    double f = (double) done_ / (double) total_;
    return f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
}

double ProgressTicker::elapsed_seconds() const {
    if (started_ms_ == 0) return 0.0;
    return (double) (now_ms() - started_ms_) / 1000.0;
}

double ProgressTicker::rate_per_second() const {
    double el = elapsed_seconds();
    if (el <= 0.0) return 0.0;
    return (double) done_ / el;
}

double ProgressTicker::eta_seconds() const {
    if (total_ == 0 || done_ == 0 || done_ >= total_) return -1.0;
    double rate = rate_per_second();
    if (rate <= 0.0) return -1.0;
    return (double) (total_ - done_) / rate;
}

void ProgressTicker::emit(const char* kind) {
    // Throttle "tick" events from a fast inner loop: the sink (a GUI, a bar)
    // cannot keep up with per-event reporting and does not want to.
    long long now = now_ms();
    if (kind[0] != 'b' && kind[0] != 'f' && now - last_emit_ms_ < 60 && done_ < total_) {
        return;
    }
    last_emit_ms_ = now;
    if (!sink_) return;
    ProgressEvent ev;
    ev.kind = kind;
    ev.job = job_.c_str();
    ev.phase = phase_.c_str();
    ev.done = done_;
    ev.total = total_;
    ev.fraction = fraction();
    ev.seconds = elapsed_seconds();
    sink_(ev);
}

}  // namespace tttrlib
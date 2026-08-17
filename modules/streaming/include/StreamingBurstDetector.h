// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_STREAMING_BURST_DETECTOR_H
#define TTTRLIB_STREAMING_BURST_DETECTOR_H

// Validation: A/B-TESTED 2026-08-17 -- vs the batch sliding window / FRETBursts rule (identical bursts; chunked
//   delivery, flush, memory bound in test/python/streaming/test_streaming_burst_detector.py). test/python/burstfilter/test_ab_burst_reference.py.
//   Register: okf/testing/algorithm-validation.md

#include <cstdint>
#include <vector>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace tttrlib {

// StreamingBurstDetector — online burst search using a sliding photon window.
//
// The same criterion as `TTTR::burst_search_sliding_window`, evaluated one
// photon at a time: a window of `m` consecutive photons is "in a burst" when it
// spans no more than `T`, and a burst is the maximal run of such windows. Given
// the same photons and parameters the two produce identical burst boundaries,
// which is what `test/python/streaming/test_streaming_burst_detector.py`
// asserts.
//
// Three things about that equivalence are worth stating, because each was got
// wrong here before:
//
//   * The comparison is on the *span*, not on a count rate. Dividing `m` by the
//     span to get a rate has to special-case a span of zero, and the obvious
//     guard (rate := 0) inverts the test exactly where it matters most: `m`
//     photons in one macro-time tick is the highest rate the detector can ever
//     see, and it was being reported as no burst at all.
//   * The window that *fails* the test ends the burst at the last photon of the
//     preceding window — `i + m - 2` in the batch loop — not at the photon just
//     pushed. Ending it one photon later adds a background photon to every
//     burst.
//   * The threshold is compared in integer macro-time ticks, `T / dt`
//     truncated, so that a photon on the boundary falls the same side as it
//     does in the batch search.
//
// Only the last `m` macro times are kept, so memory is O(m) rather than O(N):
// this is a consumer for a live acquisition, where N is not bounded.
//
// Usage:
//   StreamingBurstDetector det(10, 5e-6, 1e-8);  // m=10, T=5µs, 10ns per tick
//   for each photon t: det.push_photon(t);
//   det.flush();                                 // close a burst still open
//   auto bursts = det.get_bursts();
//
class StreamingBurstDetector {
public:
    StreamingBurstDetector(
        int window_photons = 10,
        double window_time = 5e-6,
        double macro_time_resolution = 1.0
    ) : m_(window_photons), T_(window_time), dt_(macro_time_resolution) {
        validate();
        ring_.assign(static_cast<size_t>(m_), 0);
    }

    struct Burst {
        int64_t start_idx;    // photon index of burst start
        int64_t end_idx;      // photon index of burst end (inclusive)
        uint64_t start_time;  // macro time of first photon
        uint64_t end_time;    // macro time of last photon
        int count;            // number of photons in burst
    };

    /// Feed a single photon by macro time.
    /// @return true iff this photon closed a burst that was recorded.
    bool push_photon(uint64_t macro_time) {
        ring_[static_cast<size_t>(total_photons_ % static_cast<uint64_t>(m_))] = macro_time;
        total_photons_++;
        last_time_ = macro_time;
        if (total_photons_ < static_cast<uint64_t>(m_)) return false;

        // Oldest photon of the m-photon window ending at this one. The ring
        // slot about to be overwritten next is the one m photons back.
        const uint64_t t_back =
            ring_[static_cast<size_t>(total_photons_ % static_cast<uint64_t>(m_))];
        const bool in_window = (macro_time - t_back) <= ticks_;

        const int64_t j = static_cast<int64_t>(total_photons_) - 1;   // this photon
        bool closed = false;
        if (in_window && !in_burst_) {
            in_burst_ = true;
            burst_start_idx_ = j - m_ + 1;
            burst_start_time_ = t_back;
        } else if (!in_window && in_burst_) {
            in_burst_ = false;
            closed = record(j - 1, prev_time_);
        }
        prev_time_ = macro_time;
        return closed;
    }

    void push_photons(const uint64_t* macro_times, int n) {
        for (int i = 0; i < n; ++i) push_photon(macro_times[i]);
    }

    /// Close a burst still open at the end of the stream.
    void flush() {
        if (!in_burst_) return;
        in_burst_ = false;
        record(static_cast<int64_t>(total_photons_) - 1, last_time_);
    }

    void clear() {
        std::fill(ring_.begin(), ring_.end(), 0);
        bursts_.clear();
        in_burst_ = false;
        total_photons_ = 0;
        burst_start_idx_ = 0;
        burst_start_time_ = 0;
        prev_time_ = 0;
        last_time_ = 0;
    }

    const std::vector<Burst>& get_bursts() const { return bursts_; }

    std::vector<int64_t> get_burst_indices() const {
        std::vector<int64_t> idx;
        idx.reserve(bursts_.size() * 2);
        for (const auto& b : bursts_) {
            idx.push_back(b.start_idx);
            idx.push_back(b.end_idx);
        }
        return idx;
    }

    size_t burst_count() const { return bursts_.size(); }
    size_t photon_count() const { return static_cast<size_t>(total_photons_); }

    int window_photons() const { return m_; }
    double window_time() const { return T_; }
    void set_window_photons(int m) {
        m_ = m; validate();
        ring_.assign(static_cast<size_t>(m_), 0);
        total_photons_ = 0; in_burst_ = false;
    }
    void set_window_time(double t) { T_ = t; validate(); }
    void set_min_photons(int n) { min_photons_ = n; }

private:
    int m_;                                    // window size (photons)
    double T_;                                 // max window time (seconds)
    double dt_;                                // macro time resolution (seconds)
    uint64_t ticks_ = 0;                       // T_ / dt_, the batch search's Ti
    int min_photons_ = 1;                      // minimum photons per burst

    std::vector<uint64_t> ring_;               // last m macro times, O(m)
    std::vector<Burst> bursts_;
    bool in_burst_ = false;
    int64_t burst_start_idx_ = 0;
    uint64_t burst_start_time_ = 0;
    uint64_t prev_time_ = 0;                   // macro time of photon j-1
    uint64_t last_time_ = 0;
    uint64_t total_photons_ = 0;

    void validate() {
        if (m_ < 1)
            throw std::invalid_argument("StreamingBurstDetector: window_photons must be >= 1");
        // A non-positive resolution or window makes every threshold comparison
        // meaningless, and the failure is silent: the whole stream comes back as
        // one burst. A default-constructed TTTR reports a resolution of -1, so
        // this is reachable by accident.
        if (!(dt_ > 0.0))
            throw std::invalid_argument(
                "StreamingBurstDetector: macro_time_resolution must be > 0 "
                "(a header that has not been read reports -1)");
        if (!(T_ > 0.0))
            throw std::invalid_argument("StreamingBurstDetector: window_time must be > 0");
        const double t = T_ / dt_;
        ticks_ = (t >= 0.0) ? static_cast<uint64_t>(t) : 0;
    }

    bool record(int64_t end_idx, uint64_t end_time) {
        const int64_t count = end_idx - burst_start_idx_ + 1;
        if (count < min_photons_ || count <= 0) return false;
        bursts_.push_back({burst_start_idx_, end_idx, burst_start_time_,
                           end_time, static_cast<int>(count)});
        return true;
    }
};

} // namespace tttrlib

#endif // TTTRLIB_STREAMING_BURST_DETECTOR_H

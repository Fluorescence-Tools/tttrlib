// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_STREAMING_INTENSITY_TRACE_H
#define TTTRLIB_STREAMING_INTENSITY_TRACE_H

// Validation: EQUIVALENCE-TESTED 2026-08-17 -- vs np.bincount of the batch definition at 25 random chunk cuts per stream
//   (exact) -- test/python/streaming/test_ab_streaming_equivalence.py; vs compute_intensity_trace incl. rolling window in
//   test/python/streaming/test_streaming_intensity_trace.py.
//   Register: okf/testing/algorithm-validation.md

#include <cstdint>
#include <vector>
#include <deque>
#include <cmath>
#include <stdexcept>

namespace tttrlib {

// StreamingIntensityTrace — incremental MCS / intensity trace.
//
// The streaming twin of the batch free function `compute_intensity_trace`
// (TTTR.h), and binned to the same grid: bins are aligned to macro time 0 and
// hold `floor(time_window / macro_time_resolution)` macro-time clocks each, so
// photon `t` lands in bin `t / clocks_per_bin` whether it arrives in a live
// chunk or in one batch call at the end. That alignment is the whole reason a
// streaming version is exact rather than approximate: there is no partial
// leading bin to reconcile and no chunk boundary that shifts the grid.
//
// # Why this exists
//
// A live acquisition displays the last second or two of an intensity trace.
// Calling the batch function each refresh bins *every photon of the run* to
// show the tail, which is O(N) per refresh and O(N^2) over a measurement — a
// 30-minute run updates its display more slowly than a 1-minute one. Pushing
// each chunk once costs O(chunk).
//
// # The rolling window
//
// `set_max_bins(m)` keeps only the newest `m` bins, dropping the oldest as the
// trace grows, which is what makes memory O(1) in run length rather than O(N).
// The dropped bins are not forgotten silently: `first_bin_index()` is the
// absolute index of `counts()[0]`, so a bin's position on the time axis is
// always `(first_bin_index() + i) * bin_width()` seconds. Default 0 = keep
// everything, which is the batch-equivalent mode the oracle test uses.
//
// # Ordering
//
// Photons must arrive in non-decreasing macro time, as from any TTTR stream. A
// photon landing in a bin that is still retained is counted there (chunk
// boundaries do not have to fall on bin boundaries); one older than the oldest
// retained bin cannot be placed and throws, rather than being silently added
// to the wrong bin.
//
// Usage (live MCS):
//   StreamingIntensityTrace mcs(1e-3, 50e-9);   // 1 ms bins, 50 ns clock
//   mcs.set_max_bins(1000);                     // display the last second
//   for each chunk:
//     mcs.push_photons(macro_times, n);
//   auto y = mcs.counts();
//
class StreamingIntensityTrace {
public:
    StreamingIntensityTrace(double time_window_length = 1.0,
                            double macro_time_resolution = 1.0)
        : macro_time_resolution_(macro_time_resolution) {
        if (time_window_length <= 0.0)
            throw std::invalid_argument(
                "StreamingIntensityTrace: time_window_length must be > 0");
        if (macro_time_resolution <= 0.0)
            throw std::invalid_argument(
                "StreamingIntensityTrace: macro_time_resolution must be > 0");
        // Identical to compute_intensity_trace: a bin holds a whole number of
        // macro-time clocks, at least one.
        clocks_per_bin_ = static_cast<int64_t>(
            std::floor(time_window_length / macro_time_resolution));
        if (clocks_per_bin_ < 1) clocks_per_bin_ = 1;
    }

    void push_photon(uint64_t macro_time, double weight = 1.0) {
        int64_t bin = static_cast<int64_t>(macro_time / static_cast<uint64_t>(clocks_per_bin_));
        if (counts_.empty()) {
            first_bin_ = bin;
            counts_.push_back(weight);
        } else if (bin >= first_bin_ + static_cast<int64_t>(counts_.size())) {
            // Zero-fill the bins the stream was silent through, then this one.
            counts_.resize(static_cast<size_t>(bin - first_bin_), 0.0);
            counts_.push_back(weight);
        } else if (bin >= first_bin_) {
            counts_[static_cast<size_t>(bin - first_bin_)] += weight;
        } else {
            throw std::invalid_argument(
                "StreamingIntensityTrace: photon precedes the oldest retained "
                "bin — the stream must be non-decreasing in macro time");
        }
        n_photons_++;
        trim();
    }

    void push_photons(const uint64_t* macro_times, int n) {
        for (int i = 0; i < n; ++i) push_photon(macro_times[i]);
    }

    void push_photons(const uint64_t* macro_times, const double* weights, int n) {
        for (int i = 0; i < n; ++i)
            push_photon(macro_times[i], weights ? weights[i] : 1.0);
    }

    void clear() {
        counts_.clear();
        first_bin_ = 0;
        n_photons_ = 0;
    }

    /// Keep at most `m` bins, dropping the oldest. 0 = unbounded.
    void set_max_bins(size_t m) { max_bins_ = m; trim(); }
    size_t max_bins() const { return max_bins_; }

    /// The counts of the retained bins, oldest first.
    std::vector<double> counts() const {
        return std::vector<double>(counts_.begin(), counts_.end());
    }

    /// Bin-start times of the retained bins, in seconds.
    std::vector<double> get_time_axis() const {
        std::vector<double> t(counts_.size());
        const double w = bin_width();
        for (size_t i = 0; i < counts_.size(); ++i)
            t[i] = static_cast<double>(first_bin_ + static_cast<int64_t>(i)) * w;
        return t;
    }

    /// Absolute index of the first retained bin — the bins dropped by the
    /// rolling window are counted, not forgotten.
    long long first_bin_index() const { return static_cast<long long>(first_bin_); }
    /// Number of bins currently retained.
    size_t n_bins() const { return counts_.size(); }
    /// Bin width in seconds.
    double bin_width() const {
        return static_cast<double>(clocks_per_bin_) * macro_time_resolution_;
    }
    /// Bin width in macro-time clocks.
    long long clocks_per_bin() const { return static_cast<long long>(clocks_per_bin_); }
    long long photon_count() const { return n_photons_; }

private:
    void trim() {
        if (max_bins_ == 0) return;
        while (counts_.size() > max_bins_) {
            counts_.pop_front();
            first_bin_++;
        }
    }

    double macro_time_resolution_;
    int64_t clocks_per_bin_ = 1;
    std::deque<double> counts_;
    int64_t first_bin_ = 0;
    size_t max_bins_ = 0;
    long long n_photons_ = 0;
};

} // namespace tttrlib

#endif // TTTRLIB_STREAMING_INTENSITY_TRACE_H

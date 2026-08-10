// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_STREAMING_DECAY_HISTOGRAM_H
#define TTTRLIB_STREAMING_DECAY_HISTOGRAM_H

#include <cstdint>
#include <vector>
#include <cmath>
#include <algorithm>

namespace tttrlib {

// StreamingDecayHistogram — incremental fluorescence decay histogram.
//
// Accumulates microtimes into a per-channel histogram one photon at a time.
// Supports multiple routing channels (e.g., parallel and perpendicular) and
// provides the raw histogram at any point for online model fitting.
//
// Usage (live TCSPC):
//   StreamingDecayHistogram hist(4096, 2);  // 4096 microtime bins, 2 channels
//   for each photon:
//     hist.push_photon(microtime, routing_channel);
//   auto decay = hist.get_histogram();       // [n_channels][n_bins]
//
class StreamingDecayHistogram {
public:
    StreamingDecayHistogram(int n_microtime_bins = 4096, int n_channels = 1)
        : n_bins_(n_microtime_bins),
          n_channels_(n_channels),
          data_(static_cast<size_t>(n_channels) * n_microtime_bins, 0.0),
          counts_(n_channels, 0) {}

    void push_photon(uint16_t microtime, int channel = 0, double weight = 1.0) {
        if (channel < 0 || channel >= n_channels_) return;
        if (microtime >= n_bins_) return;
        data_[static_cast<size_t>(channel) * n_bins_ + microtime] += weight;
        counts_[channel]++;
        total_++;
    }

    void push_photons(const uint16_t* microtimes, const int* channels, int n) {
        for (int i = 0; i < n; ++i)
            push_photon(microtimes[i], channels ? channels[i] : 0);
    }

    void clear() {
        std::fill(data_.begin(), data_.end(), 0.0);
        std::fill(counts_.begin(), counts_.end(), 0);
        total_ = 0;
    }

    const double* get_channel(int channel) const {
        if (channel < 0 || channel >= n_channels_) return nullptr;
        return &data_[static_cast<size_t>(channel) * n_bins_];
    }

    std::vector<double> get_histogram() const { return data_; }
    std::vector<double> get_histogram(int channel) const {
        if (channel < 0 || channel >= n_channels_) return {};
        return std::vector<double>(
            data_.begin() + static_cast<size_t>(channel) * n_bins_,
            data_.begin() + static_cast<size_t>(channel + 1) * n_bins_);
    }

    long long get_count(int channel = 0) const {
        if (channel < 0 || channel >= n_channels_) return 0;
        return counts_[channel];
    }

    long long total_count() const { return total_; }
    int n_bins() const { return n_bins_; }
    int n_channels() const { return n_channels_; }

private:
    int n_bins_;
    int n_channels_;
    std::vector<double> data_;     // n_channels × n_bins, row-major
    std::vector<long long> counts_;
    long long total_ = 0;
};


// StreamingPhasor — incremental phasor (g, s) computation for FLIM.
//
// Accumulates cos and sin of the microtime-modulated signal, enabling
// real-time phasor plots without buffering individual photons.
//
// Usage (live FLIM):
//   StreamingPhasor phasor(80e6, 4096, 1e-9);  // 80 MHz, 4096 bins, 1ns tick
//   for each photon:
//     phasor.push_photon(microtime);
//   auto gs = phasor.get_phasor();  // {g, s}
//
class StreamingPhasor {
public:
    StreamingPhasor(double frequency_MHz, int n_microtime_bins, double microtime_resolution)
        : n_bins_(n_microtime_bins) {
        // angular frequency × time-resolution = phase per microtime bin
        double omega = 2.0 * 3.14159265358979323846 * frequency_MHz * 1e6;
        double dt = microtime_resolution;
        phase_per_bin_ = omega * dt;
    }

    void push_photon(uint16_t microtime, double weight = 1.0) {
        double phase = static_cast<double>(microtime) * phase_per_bin_;
        g_sum_ += weight * std::cos(phase);
        s_sum_ += weight * std::sin(phase);
        count_++;
    }

    void push_photons(const uint16_t* microtimes, int n) {
        for (int i = 0; i < n; ++i)
            push_photon(microtimes[i]);
    }

    void clear() { g_sum_ = s_sum_ = 0.0; count_ = 0; }

    // Returns {g, s, n} where g = Σcos/N, s = Σsin/N.
    std::vector<double> get_phasor() const {
        if (count_ == 0) return {0.0, 0.0, 0.0};
        double inv = 1.0 / static_cast<double>(count_);
        return {g_sum_ * inv, s_sum_ * inv, static_cast<double>(count_)};
    }

    double g() const { return count_ > 0 ? g_sum_ / count_ : 0.0; }
    double s() const { return count_ > 0 ? s_sum_ / count_ : 0.0; }
    long long count() const { return count_; }

private:
    int n_bins_;
    double phase_per_bin_;
    double g_sum_ = 0.0;
    double s_sum_ = 0.0;
    long long count_ = 0;
};

} // namespace tttrlib

#endif // TTTRLIB_STREAMING_DECAY_HISTOGRAM_H

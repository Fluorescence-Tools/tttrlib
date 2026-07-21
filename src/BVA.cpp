// SPDX-License-Identifier: BSD-3-Clause
#include "BVA.h"
#include "BurstFilter.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>
#include <thread>

namespace tttrlib {

namespace {
/// Portable dynamic parallel-for over ``[0, n)`` using std::thread.
template <class F>
void parallel_for(int n, F&& body) {
    unsigned hc = std::thread::hardware_concurrency();
    int nt = (hc == 0) ? 1 : static_cast<int>(hc);
    if (nt > n) nt = std::max(1, n);
    if (nt <= 1 || n <= 1) {
        for (int i = 0; i < n; ++i) body(i);
        return;
    }
    std::atomic<int> next{0};
    auto worker = [&]() {
        int i;
        while ((i = next.fetch_add(1)) < n) body(i);
    };
    std::vector<std::thread> pool;
    pool.reserve(nt - 1);
    for (int c = 1; c < nt; ++c) pool.emplace_back(worker);
    worker();
    for (auto& t : pool) t.join();
}
}  // namespace

BVA::BVA(std::shared_ptr<TTTR> tttr) : tttr_(std::move(tttr)) {}

BVA::BVA(std::shared_ptr<BurstFilter> burst_filter)
    : burst_filter_(std::move(burst_filter)) {
    if (burst_filter_) tttr_ = burst_filter_->get_tttr();
}

void BVA::compute(int number_of_photons_per_slice, double minimum_window_length) {
    if (!burst_filter_)
        throw std::runtime_error(
            "BVA::compute(): no BurstFilter — construct BVA(BurstFilter) or pass bursts explicitly");
    // get_burst_indices() is vector<int64_t>; on LP64 Linux that is a distinct
    // type from long long, so copy into the public pointer/length signature.
    std::vector<long long> b(burst_filter_->get_burst_indices().begin(),
                             burst_filter_->get_burst_indices().end());
    compute(b.data(), static_cast<int>(b.size() / 2), 2,
            number_of_photons_per_slice, minimum_window_length);
}

void BVA::set_donor(
    const std::vector<int>& channels,
    const std::vector<std::pair<int, int>>& micro_time_ranges
) {
    donor_channels_ = channels;
    donor_micro_time_ranges_ = micro_time_ranges;
}

void BVA::set_acceptor(
    const std::vector<int>& channels,
    const std::vector<std::pair<int, int>>& micro_time_ranges
) {
    acceptor_channels_ = channels;
    acceptor_micro_time_ranges_ = micro_time_ranges;
}

namespace {

inline bool in_channels(int ch, const std::vector<int>& channels) {
    for (int c : channels) {
        if (c == ch) return true;
    }
    return false;
}

inline bool in_micro_ranges(
    int mt, const std::vector<std::pair<int, int>>& ranges
) {
    if (ranges.empty()) return true;  // no micro-time gating
    for (const auto& r : ranges) {
        if (mt >= r.first && mt <= r.second) return true;
    }
    return false;
}

}  // namespace

void BVA::compute(
    long long* bursts, int n_bursts, int n_cols,
    int number_of_photons_per_slice,
    double minimum_window_length
) {
    // bursts is an (n_bursts, 2) [start, stop] array (row-major).
    const size_t n_pairs = (bursts == nullptr || n_bursts < 1 || n_cols != 2)
        ? 0 : static_cast<size_t>(n_bursts);
    prox_mean_.assign(n_pairs, std::nan(""));
    prox_std_.assign(n_pairs, std::nan(""));
    mean_slice_size_.assign(n_pairs, 0.0);

    if (n_pairs == 0 || !tttr_) return;

    const int64_t n_total = static_cast<int64_t>(tttr_->size());
    // Macro-time resolution in seconds (MeasDesc_GlobalResolution).
    double macro_res = 1.0;
    if (auto* hdr = tttr_->get_header()) {
        macro_res = hdr->get_macro_time_resolution();
    }
    // Slice duration converted to macro-time ticks (time-window mode).
    const double window_ticks =
        (macro_res > 0.0) ? (minimum_window_length / macro_res) : 0.0;
    const bool by_time = number_of_photons_per_slice <= 0;

    parallel_for(static_cast<int>(n_pairs), [&](int b) {
        int64_t s = bursts[2 * b];
        int64_t e = bursts[2 * b + 1];  // inclusive, as produced by every burst search
        // Clamp to valid range.
        if (s < 0) s = 0;
        if (e > n_total - 1) e = n_total - 1;
        const int64_t n_events = e - s + 1;
        if (n_events <= 0) return;

        // Per-photon donor/acceptor membership and cumulative counts.
        // donor_cs[k] / acceptor_cs[k] = counts over photons [s, s+k]. These are
        // within-burst counts (<= n_events, always a few thousand), so int32
        // halves the two largest per-burst buffers vs int64. The macro-time
        // buffer is only read in time-window mode, so skip it entirely in
        // photon-count mode.
        std::vector<int32_t> donor_cs(n_events);
        std::vector<int32_t> acceptor_cs(n_events);
        std::vector<int64_t> macro;
        if (by_time) macro.resize(n_events);
        int32_t dcum = 0, acum = 0;
        for (int64_t k = 0; k < n_events; ++k) {
            const int64_t idx = s + k;
            const int mt = static_cast<int>(tttr_->get_micro_time_at(idx));
            const int ch = static_cast<int>(tttr_->get_routing_channel_at(idx));
            if (by_time) macro[k] = static_cast<int64_t>(tttr_->get_macro_time_at(idx));
            const bool is_donor =
                in_channels(ch, donor_channels_) &&
                in_micro_ranges(mt, donor_micro_time_ranges_);
            const bool is_acceptor =
                in_channels(ch, acceptor_channels_) &&
                in_micro_ranges(mt, acceptor_micro_time_ranges_);
            if (is_donor) ++dcum;
            if (is_acceptor) ++acum;
            donor_cs[k] = dcum;
            acceptor_cs[k] = acum;
        }

        auto count = [](const std::vector<int32_t>& cs, int64_t a, int64_t z) -> int64_t {
            // count over half-open slice [a, z)
            return (a == 0) ? cs[z - 1] : cs[z - 1] - cs[a - 1];
        };

        // Accumulate per-slice proximity ratios (Welford for mean/std).
        int64_t n_slices = 0;
        double mean = 0.0, m2 = 0.0, size_sum = 0.0;

        auto add_slice = [&](int64_t a, int64_t z) {
            if (a >= z) return;
            const int64_t dc = count(donor_cs, a, z);
            const int64_t ac = count(acceptor_cs, a, z);
            const int64_t total = dc + ac;
            const double pr = (total > 0) ? (static_cast<double>(ac) / total) : 0.0;
            ++n_slices;
            const double delta = pr - mean;
            mean += delta / n_slices;
            m2 += delta * (pr - mean);
            size_sum += static_cast<double>(ac);
        };

        if (by_time) {
            int64_t start_idx = 0;
            while (start_idx < n_events) {
                const int64_t threshold = macro[start_idx] +
                    static_cast<int64_t>(window_ticks);
                // first index with macro > threshold (upper_bound)
                int64_t end_idx = std::upper_bound(
                    macro.begin() + start_idx, macro.end(),
                    threshold) - macro.begin();
                if (end_idx <= start_idx) end_idx = start_idx + 1;
                add_slice(start_idx, end_idx);
                start_idx = end_idx;
            }
        } else {
            const int64_t chunk = number_of_photons_per_slice;
            for (int64_t a = 0; a < n_events; a += chunk) {
                add_slice(a, std::min(a + chunk, n_events));
            }
        }

        if (n_slices > 0) {
            prox_mean_[b] = mean;
            // Population standard deviation (matches numpy np.nanstd default).
            prox_std_[b] = std::sqrt(m2 / n_slices);
            mean_slice_size_[b] = size_sum / n_slices;
        }
    });
}

std::pair<std::vector<double>, std::vector<double>> BVA::compute_static_bva_line(
    const std::vector<double>& prox_mean_bins,
    int number_of_photons_per_slice
) {
    std::vector<double> mean(prox_mean_bins.size());
    std::vector<double> stddev(prox_mean_bins.size());
    const double n = std::max(1, number_of_photons_per_slice);
    for (size_t i = 0; i < prox_mean_bins.size(); ++i) {
        const double p = prox_mean_bins[i];
        mean[i] = p;
        stddev[i] = std::sqrt(std::max(0.0, p * (1.0 - p) / n));
    }
    return {mean, stddev};
}

} // namespace tttrlib

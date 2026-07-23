// SPDX-License-Identifier: BSD-3-Clause
#include "BurstFeature.h"
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

inline bool in_channels(int ch, const std::vector<int>& channels) {
    for (int c : channels) if (c == ch) return true;
    return false;
}

inline bool in_micro_ranges(int mt, const std::vector<std::pair<int, int>>& ranges) {
    if (ranges.empty()) return true;  // no micro-time gating
    for (const auto& r : ranges) if (mt >= r.first && mt <= r.second) return true;
    return false;
}

/**
 * @brief Laplace/Gaussian KDE of ``src`` timestamps evaluated at every entry of
 *        ``axis`` (both non-decreasing), bit-exact with the FRETBursts
 *        two-pointer numba kernels (ascending-index summation, no FMA).
 *
 * Parallelised over disjoint axis chunks; each chunk seeds its window pointers
 * with a conservative lower bound and lets the monotonic while-loops advance
 * them, so the split does not change the result.
 */
std::vector<double> kde_eval(
    const std::vector<int64_t>& axis,
    const std::vector<int64_t>& src,
    double tau_ticks,
    int kernel
) {
    const int64_t n_axis = static_cast<int64_t>(axis.size());
    std::vector<double> rates(static_cast<size_t>(n_axis), 0.0);
    if (n_axis == 0 || src.empty() || tau_ticks <= 0.0) return rates;

    const double cutoff = (kernel == BurstFeature::GAUSSIAN ? 3.0 : 5.0) * tau_ticks;
    const double tau2 = 2.0 * tau_ticks * tau_ticks;
    const int64_t src_size = static_cast<int64_t>(src.size());

    unsigned hc = std::thread::hardware_concurrency();
    int nt = (hc == 0) ? 1 : static_cast<int>(hc);
    int64_t chunk = (n_axis + nt - 1) / std::max(1, nt);
    if (chunk < 1) chunk = 1;
    const int n_chunks = static_cast<int>((n_axis + chunk - 1) / chunk);

    parallel_for(n_chunks, [&](int c) {
        const int64_t it0 = static_cast<int64_t>(c) * chunk;
        const int64_t it1 = std::min(it0 + chunk, n_axis);
        const int64_t t0 = axis[it0];
        // Seed both window pointers with a *conservative lower bound* (the first
        // src at or after floor(t0 - cutoff), which is <= the true ineg <= the
        // true ipos) and let the monotonic while-loops below advance them to the
        // exact positions. Undershooting is self-correcting; overshooting would
        // not be (the loops only increment).
        int64_t seed = std::lower_bound(
            src.begin(), src.end(),
            static_cast<int64_t>(std::floor(t0 - cutoff))) - src.begin();
        int64_t ipos = seed;
        int64_t ineg = seed;
        for (int64_t it = it0; it < it1; ++it) {
            const int64_t t = axis[it];
            while (ipos < src_size && static_cast<double>(src[ipos] - t) < cutoff) ++ipos;
            while (ineg < src_size && static_cast<double>(t - src[ineg]) > cutoff) ++ineg;
            double r = 0.0;
            if (kernel == BurstFeature::GAUSSIAN) {
                for (int64_t k = ineg; k < ipos; ++k) {
                    const double d = static_cast<double>(src[k] - t);
                    r += std::exp(-(d * d) / tau2);
                }
            } else {
                for (int64_t k = ineg; k < ipos; ++k) {
                    const double d = std::fabs(static_cast<double>(src[k] - t));
                    r += std::exp(-d / tau_ticks);
                }
            }
            rates[static_cast<size_t>(it)] = r;
        }
    });
    return rates;
}

}  // namespace

BurstFeature::BurstFeature(std::shared_ptr<TTTR> tttr)
    : tttr_(std::move(tttr)) {}

BurstFeature::BurstFeature(std::shared_ptr<BurstFilter> burst_filter)
    : burst_filter_(std::move(burst_filter)) {
    if (burst_filter_) tttr_ = burst_filter_->get_tttr();
}

void BurstFeature::set_stream(
    const std::string& name,
    const std::vector<int>& channels,
    const std::vector<std::pair<int, int>>& micro_time_ranges
) {
    streams_[name] = StreamDef{channels, micro_time_ranges};
}

double BurstFeature::seconds_to_macro_ticks(double seconds) const {
    double macro_res = 1.0;
    if (tttr_) {
        if (auto* hdr = tttr_->get_header()) macro_res = hdr->get_macro_time_resolution();
    }
    return (macro_res > 0.0) ? (seconds / macro_res) : seconds;
}

void BurstFeature::build_streams() {
    membership_.clear();
    timestamps_.clear();
    kde_.clear();
    macro_all_.clear();
    if (!tttr_) return;
    const int64_t n = static_cast<int64_t>(tttr_->size());
    macro_all_.resize(static_cast<size_t>(n));

    for (const auto& kv : streams_) {
        membership_[kv.first].assign(static_cast<size_t>(n), 0);
        timestamps_[kv.first].reserve(static_cast<size_t>(n) / 2 + 1);
    }
    for (int64_t i = 0; i < n; ++i) {
        const int64_t mac = static_cast<int64_t>(tttr_->get_macro_time_at(i));
        const int mt = static_cast<int>(tttr_->get_micro_time_at(i));
        const int ch = static_cast<int>(tttr_->get_routing_channel_at(i));
        macro_all_[static_cast<size_t>(i)] = mac;
        for (const auto& kv : streams_) {
            const StreamDef& sd = kv.second;
            if (in_channels(ch, sd.channels) && in_micro_ranges(mt, sd.micro_time_ranges)) {
                membership_[kv.first][static_cast<size_t>(i)] = 1;
                timestamps_[kv.first].push_back(mac);
            }
        }
    }
}

void BurstFeature::build_kde(double tau_ticks, int kernel) {
    kde_.clear();
    for (const auto& kv : timestamps_) {
        kde_[kv.first] = kde_eval(macro_all_, kv.second, tau_ticks, kernel);
    }
}

const std::vector<uint8_t>& BurstFeature::membership(const std::string& name) const {
    auto it = membership_.find(name);
    if (it == membership_.end())
        throw std::runtime_error("BurstFeature: unknown stream '" + name + "'");
    return it->second;
}

const std::vector<int64_t>& BurstFeature::timestamps(const std::string& name) const {
    auto it = timestamps_.find(name);
    if (it == timestamps_.end())
        throw std::runtime_error("BurstFeature: unknown stream '" + name + "'");
    return it->second;
}

const std::vector<double>& BurstFeature::kde(const std::string& name) const {
    auto it = kde_.find(name);
    if (it == kde_.end())
        throw std::runtime_error("BurstFeature: KDE not built for stream '" + name + "'");
    return it->second;
}

void BurstFeature::for_each_burst(
    long long* bursts, int n_bursts, int n_cols,
    const std::function<double(int, int64_t, int64_t)>& reduce
) {
    const size_t n_pairs = (bursts == nullptr || n_bursts < 1 || n_cols != 2)
        ? 0 : static_cast<size_t>(n_bursts);
    result_.assign(n_pairs, std::nan(""));
    if (n_pairs == 0 || !tttr_) return;
    const int64_t n_total_ = n_total();

    parallel_for(static_cast<int>(n_pairs), [&](int b) {
        int64_t s = bursts[2 * b];
        int64_t e = bursts[2 * b + 1];  // inclusive
        if (s < 0) s = 0;
        if (e > n_total_ - 1) e = n_total_ - 1;
        if (e < s) return;
        result_[b] = reduce(b, s, e);
    });
}

void BurstFeature::for_each_burst_from_filter(
    const std::function<double(int, int64_t, int64_t)>& reduce
) {
    if (!burst_filter_)
        throw std::runtime_error(
            "BurstFeature: no BurstFilter — construct from a BurstFilter or pass bursts explicitly");
    std::vector<long long> b(burst_filter_->get_burst_indices().begin(),
                             burst_filter_->get_burst_indices().end());
    for_each_burst(b.data(), static_cast<int>(b.size() / 2), 2, reduce);
}

} // namespace tttrlib

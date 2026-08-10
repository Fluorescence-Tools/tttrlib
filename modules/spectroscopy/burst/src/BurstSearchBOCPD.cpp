// SPDX-License-Identifier: BSD-3-Clause
#include "BurstSearchBOCPD.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "TTTR.h"

namespace tttrlib {

namespace {

// numerically stable log(sum(exp(a)))
double logsumexp(const double* a, int n) {
    double m = a[0];
    for (int i = 1; i < n; ++i) {
        if (a[i] > m) m = a[i];
    }
    double s = 0.0;
    for (int i = 0; i < n; ++i) {
        s += std::exp(a[i] - m);
    }
    return m + std::log(s);
}

// log Poisson PMF, clamping lambda away from zero
double log_poisson_pmf(int k, double lam) {
    if (lam <= 0.0) lam = 1e-300;
    return static_cast<double>(k) * std::log(lam) - lam - std::lgamma(static_cast<double>(k) + 1.0);
}

// logsumexp(a[0..n-1] + offset) — one pass, no temp array
double logsumexp_offset(const double* a, int n, double offset) {
    double m = a[0] + offset;
    for (int i = 1; i < n; ++i) {
        double v = a[i] + offset;
        if (v > m) m = v;
    }
    double s = 0.0;
    for (int i = 0; i < n; ++i) {
        s += std::exp(a[i] + offset - m);
    }
    return m + std::log(s);
}

} // namespace

std::vector<long long> burst_search_bocpd(
    const std::vector<int64_t>& macro_times,
    const std::vector<signed char>& routing_channels,
    double macro_time_resolution,
    const BocpdBurstSettings& settings
) {
    const int64_t n_photons = static_cast<int64_t>(macro_times.size());
    if (n_photons < 2) return {};
    if (!(macro_time_resolution > 0.0) || !(settings.dt > 0.0)) return {};
    if (!(settings.changepoint_prob > 0.0) || !(settings.changepoint_prob < 1.0)) return {};

    // --- state dimensions: one per routing channel, or one pooled rate ------
    std::vector<signed char> channels;
    const bool have_channels =
        settings.per_channel &&
        static_cast<int64_t>(routing_channels.size()) == n_photons;
    if (have_channels) {
        channels.assign(routing_channels.begin(), routing_channels.end());
        std::sort(channels.begin(), channels.end());
        channels.erase(std::unique(channels.begin(), channels.end()), channels.end());
        if (static_cast<int>(channels.size()) > std::max(1, settings.max_channels)) {
            channels.clear();
        }
    }
    const int dim = channels.empty() ? 1 : static_cast<int>(channels.size());

    // Map a routing channel to its state dimension.
    std::vector<int> channel_to_dim(256, -1);
    for (int d = 0; d < static_cast<int>(channels.size()); ++d) {
        channel_to_dim[static_cast<size_t>(channels[static_cast<size_t>(d)]) + 128] = d;
    }

    // --- bin the photons -----------------------------------------------------
    const int64_t t0 = macro_times.front();
    const double span = static_cast<double>(macro_times.back() - t0) *
                        macro_time_resolution;
    int64_t n_bins = static_cast<int64_t>(std::ceil(span / settings.dt)) + 1;
    if (n_bins < 2) return {};
    const int64_t max_bins = 200000000;
    if (n_bins > max_bins) return {};

    const double ticks_per_bin = settings.dt / macro_time_resolution;
    // counts[bin * dim + ch] = photons in that bin and channel
    std::vector<int> counts(static_cast<size_t>(n_bins) * dim, 0);
    // First photon index of each bin, and one past the last.
    std::vector<int64_t> bin_first(static_cast<size_t>(n_bins) + 1, -1);

    for (int64_t i = 0; i < n_photons; ++i) {
        int64_t bin = static_cast<int64_t>(
            static_cast<double>(macro_times[static_cast<size_t>(i)] - t0) /
            ticks_per_bin);
        if (bin < 0) bin = 0;
        if (bin >= n_bins) bin = n_bins - 1;
        int d = 0;
        if (dim > 1) {
            d = channel_to_dim[
                static_cast<size_t>(routing_channels[static_cast<size_t>(i)]) + 128];
            if (d < 0) continue;
        }
        counts[static_cast<size_t>(bin) * dim + d] += 1;
        if (bin_first[static_cast<size_t>(bin)] < 0) {
            bin_first[static_cast<size_t>(bin)] = i;
        }
    }
    // Fill empty bins so every bin has a defined photon boundary.
    bin_first[static_cast<size_t>(n_bins)] = n_photons;
    for (int64_t b = n_bins - 1; b >= 0; --b) {
        if (bin_first[static_cast<size_t>(b)] < 0) {
            bin_first[static_cast<size_t>(b)] = bin_first[static_cast<size_t>(b + 1)];
        }
    }

    // --- BOCPD recursion -----------------------------------------------------
    const int R = std::min(settings.max_run, static_cast<int>(n_bins));
    if (R < 1) return {};

    const double log_h = std::log(settings.changepoint_prob);
    const double log_1h = std::log1p(-settings.changepoint_prob);

    // Run-length posterior in log space. log_R_prev is the previous step's
    // distribution; log_R is built fresh each step.  Reuse tmp across iterations
    // to avoid per-bin allocation.
    std::vector<double> log_R_prev(static_cast<size_t>(R) + 1);
    std::vector<double> log_R(static_cast<size_t>(R) + 1);
    std::vector<double> pred(static_cast<size_t>(R));
    std::vector<double> tmp(static_cast<size_t>(R));

    // Double-buffered Gamma parameters: [curr/prev][run_length * dim + channel]
    const size_t stride = static_cast<size_t>(R + 1) * dim;
    std::vector<double> alpha_buf(2 * stride, settings.prior_count);
    std::vector<double> beta_buf(2 * stride, settings.prior_duration);

    std::fill(log_R_prev.begin(), log_R_prev.end(), -HUGE_VAL);
    log_R_prev[0] = 0.0;

    std::vector<int64_t> changepoints;
    changepoints.reserve(static_cast<size_t>(n_bins) / 10);

    for (int64_t t = 0; t < n_bins; ++t) {
        const int Rmax = static_cast<int>(std::min(t + 1, static_cast<int64_t>(R)));
        const int curr = static_cast<int>(t % 2);
        const int prev = 1 - curr;
        const size_t curr_off = static_cast<size_t>(curr) * stride;
        const size_t prev_off = static_cast<size_t>(prev) * stride;

        // Predictive log-likelihood per run length, summed over channels.
        // k and lgamma(k+1) are constant per bin — compute once.
        const size_t t_off = static_cast<size_t>(t) * dim;
        double log_k_fact = 0.0;
        double k_times_log_lam = 0.0;  // accumulated as k * log(alpha/beta) per ch
        for (int ch = 0; ch < dim; ++ch) {
            const int k = counts[t_off + ch];
            log_k_fact += std::lgamma(static_cast<double>(k) + 1.0);
        }

        for (int rl = 0; rl < Rmax; ++rl) {
            const size_t rl_off = static_cast<size_t>(rl) * dim;
            double ll = 0.0;
            for (int ch = 0; ch < dim; ++ch) {
                const double alpha = alpha_buf[prev_off + rl_off + ch];
                const double beta = beta_buf[prev_off + rl_off + ch];
                const double lam = alpha / beta;
                const int k = counts[t_off + ch];
                // k*log(lam) - lam - lgamma(k+1)
                if (lam <= 0.0) {
                    ll += static_cast<double>(k) * std::log(1e-300) - 1e-300;
                } else {
                    ll += static_cast<double>(k) * std::log(lam) - lam;
                }
            }
            pred[static_cast<size_t>(rl)] = ll - log_k_fact;
        }

        // log_R_prev + pred is used for both growth and changepoint terms —
        // compute it once into tmp.
        for (int rl = 0; rl < Rmax; ++rl) {
            tmp[rl] = log_R_prev[rl] + pred[rl];
        }

        // Changepoint term: logsumexp(tmp + log_h).
        const double log_cp = logsumexp_offset(tmp.data(), Rmax, log_h);

        // Growth term: tmp + log_1h, shifted by one.
        std::fill(log_R.begin(), log_R.end(), -HUGE_VAL);
        log_R[0] = log_cp;
        for (int rl = 0; rl < Rmax; ++rl) {
            log_R[static_cast<size_t>(rl) + 1] = tmp[rl] + log_1h;
        }

        // Normalise.
        const double norm = logsumexp(log_R.data(), Rmax + 1);
        for (int i = 0; i <= Rmax; ++i) log_R[i] -= norm;

        // Update Gamma parameters for the new posterior.
        // run length 0: fresh prior + current observation
        for (int ch = 0; ch < dim; ++ch) {
            alpha_buf[curr_off + ch] =
                settings.prior_count + counts[static_cast<size_t>(t) * dim + ch];
            beta_buf[curr_off + ch] = settings.prior_duration + 1.0;
        }
        // run lengths 1..Rmax: inherit previous, accumulate current
        for (int rl = 0; rl < Rmax; ++rl) {
            const size_t prev_rl = static_cast<size_t>(rl) * dim;
            const size_t curr_rl = static_cast<size_t>(rl + 1) * dim;
            for (int ch = 0; ch < dim; ++ch) {
                alpha_buf[curr_off + curr_rl + ch] =
                    alpha_buf[prev_off + prev_rl + ch] +
                    counts[static_cast<size_t>(t) * dim + ch];
                beta_buf[curr_off + curr_rl + ch] =
                    beta_buf[prev_off + prev_rl + ch] + 1.0;
            }
        }

        // MAP run length → changepoint detection
        int rl_map = 0;
        double best = log_R[0];
        for (int i = 1; i <= Rmax; ++i) {
            if (log_R[i] > best) { best = log_R[i]; rl_map = i; }
        }
        if (rl_map == 0) changepoints.push_back(t);

        std::swap(log_R_prev, log_R);
    }

    // --- changepoints to burst ranges ----------------------------------------
    // Segments are [cp[i], cp[i+1]) in bin space. The first segment starts at 0.
    // Bursts are segments whose total photon count >= L.
    std::vector<long long> bursts;

    std::vector<int64_t> seg_starts;
    std::vector<int64_t> seg_ends;
    seg_starts.push_back(0);
    for (int64_t cp : changepoints) {
        seg_ends.push_back(cp);
        seg_starts.push_back(cp);
    }
    seg_ends.push_back(n_bins);

    for (size_t s = 0; s < seg_starts.size(); ++s) {
        const int64_t bin_lo = seg_starts[s];
        const int64_t bin_hi = seg_ends[s];
        if (bin_hi <= bin_lo) continue;

        // Count photons in this segment.
        const int64_t first_photon = bin_first[static_cast<size_t>(bin_lo)];
        const int64_t last_photon =
            bin_first[static_cast<size_t>(bin_hi)] - 1;
        if (last_photon < first_photon) continue;
        if (last_photon - first_photon + 1 < settings.L) continue;

        bursts.push_back(static_cast<long long>(first_photon));
        bursts.push_back(static_cast<long long>(last_photon));
    }

    return bursts;
}

} // namespace tttrlib

// TTTR lives at global scope, so this definition sits outside `tttrlib`.
std::vector<long long> TTTR::burst_search_bocpd(
    int L, double dt, double prior_count, double prior_duration,
    double changepoint_prob, int max_run, bool per_channel
) {
    const int64_t n = static_cast<int64_t>(size());
    std::vector<int64_t> times(static_cast<size_t>(n));
    for (int64_t i = 0; i < n; ++i) {
        times[static_cast<size_t>(i)] = static_cast<int64_t>(get_macro_time_at(i));
    }
    std::vector<signed char> channels;
    if (per_channel && routing_channels != nullptr) {
        channels.assign(routing_channels, routing_channels + n);
    }

    tttrlib::BocpdBurstSettings s;
    s.L = L;
    s.dt = dt;
    s.prior_count = prior_count;
    s.prior_duration = prior_duration;
    s.changepoint_prob = changepoint_prob;
    s.max_run = max_run;
    s.per_channel = per_channel;
    return tttrlib::burst_search_bocpd(
        times, channels, header->get_macro_time_resolution(), s);
}

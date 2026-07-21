// SPDX-License-Identifier: BSD-3-Clause
#include "BurstSearchKalman.h"

#include <algorithm>
#include <cmath>

#include "TTTR.h"

namespace tttrlib {

namespace {

/*!
 * In-place inverse of a small dense row-major matrix by Gauss-Jordan with
 * partial pivoting. The state dimension is the number of detectors — a handful —
 * so a dense inverse per bin is cheap and avoids pulling in a linear-algebra
 * dependency for a 2x2 or 4x4 solve.
 *
 * \return false if the matrix is singular, leaving `a` unusable.
 */
bool invert_in_place(std::vector<double>& a, int n) {
    std::vector<double> inv(static_cast<size_t>(n) * n, 0.0);
    for (int i = 0; i < n; ++i) inv[static_cast<size_t>(i) * n + i] = 1.0;

    for (int col = 0; col < n; ++col) {
        int pivot = col;
        double best = std::fabs(a[static_cast<size_t>(col) * n + col]);
        for (int row = col + 1; row < n; ++row) {
            const double value = std::fabs(a[static_cast<size_t>(row) * n + col]);
            if (value > best) { best = value; pivot = row; }
        }
        if (best < 1e-300) return false;
        if (pivot != col) {
            for (int k = 0; k < n; ++k) {
                std::swap(a[static_cast<size_t>(col) * n + k],
                          a[static_cast<size_t>(pivot) * n + k]);
                std::swap(inv[static_cast<size_t>(col) * n + k],
                          inv[static_cast<size_t>(pivot) * n + k]);
            }
        }
        const double diag = a[static_cast<size_t>(col) * n + col];
        for (int k = 0; k < n; ++k) {
            a[static_cast<size_t>(col) * n + k] /= diag;
            inv[static_cast<size_t>(col) * n + k] /= diag;
        }
        for (int row = 0; row < n; ++row) {
            if (row == col) continue;
            const double factor = a[static_cast<size_t>(row) * n + col];
            if (factor == 0.0) continue;
            for (int k = 0; k < n; ++k) {
                a[static_cast<size_t>(row) * n + k] -=
                    factor * a[static_cast<size_t>(col) * n + k];
                inv[static_cast<size_t>(row) * n + k] -=
                    factor * inv[static_cast<size_t>(col) * n + k];
            }
        }
    }
    a.swap(inv);
    return true;
}

} // namespace

std::vector<long long> burst_search_kalman(
    const std::vector<int64_t>& macro_times,
    const std::vector<signed char>& routing_channels,
    double macro_time_resolution,
    const KalmanBurstSettings& settings
) {
    const int64_t n_photons = static_cast<int64_t>(macro_times.size());
    if (n_photons < 2) return {};
    if (!(macro_time_resolution > 0.0) || !(settings.dt > 0.0)) return {};

    // --- state dimensions: one per routing channel, or one pooled rate ----------
    std::vector<signed char> channels;
    const bool have_channels =
        settings.per_channel &&
        static_cast<int64_t>(routing_channels.size()) == n_photons;
    if (have_channels) {
        channels.assign(routing_channels.begin(), routing_channels.end());
        std::sort(channels.begin(), channels.end());
        channels.erase(std::unique(channels.begin(), channels.end()), channels.end());
        if (static_cast<int>(channels.size()) > std::max(1, settings.max_channels)) {
            channels.clear();   // too many detectors: fall back to a pooled rate
        }
    }
    const int dim = channels.empty() ? 1 : static_cast<int>(channels.size());

    // Map a routing channel to its state dimension. Channels are signed chars, so
    // a 256-entry table beats a search per photon.
    std::vector<int> channel_to_dim(256, -1);
    for (int d = 0; d < static_cast<int>(channels.size()); ++d) {
        channel_to_dim[static_cast<size_t>(channels[static_cast<size_t>(d)]) + 128] = d;
    }

    // --- bin the photons ---------------------------------------------------------
    const int64_t t0 = macro_times.front();
    const double span = static_cast<double>(macro_times.back() - t0) *
                        macro_time_resolution;
    int64_t n_bins = static_cast<int64_t>(std::ceil(span / settings.dt)) + 1;
    if (n_bins < 2) return {};
    // A pathological dt would otherwise allocate without bound.
    const int64_t max_bins = 200000000;
    if (n_bins > max_bins) return {};

    const double ticks_per_bin = settings.dt / macro_time_resolution;
    std::vector<double> counts(static_cast<size_t>(n_bins) * dim, 0.0);
    // First photon index of each bin, and one past the last: the photon stream is
    // sorted, so bin membership is a contiguous index range and mapping a burst of
    // bins back to photons needs no search.
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
            if (d < 0) continue;   // a channel that was capped away
        }
        counts[static_cast<size_t>(bin) * dim + d] += 1.0;
        if (bin_first[static_cast<size_t>(bin)] < 0) {
            bin_first[static_cast<size_t>(bin)] = i;
        }
    }
    // Fill empty bins so every bin has a defined photon boundary, sweeping from
    // the end so an empty bin inherits the start of the next non-empty one.
    bin_first[static_cast<size_t>(n_bins)] = n_photons;
    for (int64_t b = n_bins - 1; b >= 0; --b) {
        if (bin_first[static_cast<size_t>(b)] < 0) {
            bin_first[static_cast<size_t>(b)] = bin_first[static_cast<size_t>(b + 1)];
        }
    }

    // --- Kalman recursion ---------------------------------------------------------
    // The transition and observation matrices are the identity: the model is "the
    // rate stays what it was, plus process noise", so a burst shows up as an
    // innovation the filter did not expect rather than as a fitted trend.
    const size_t dim2 = static_cast<size_t>(dim) * dim;
    std::vector<double> x(static_cast<size_t>(dim), 0.0);
    std::vector<double> P(dim2, 0.0);
    std::vector<double> P_pred(dim2), S(dim2), K(dim2), P_new(dim2);
    std::vector<double> v(static_cast<size_t>(dim)), Sv(static_cast<size_t>(dim));
    for (int i = 0; i < dim; ++i) P[static_cast<size_t>(i) * dim + i] = 1e6;

    std::vector<double> mahalanobis(static_cast<size_t>(n_bins), 0.0);

    for (int64_t b = 0; b < n_bins; ++b) {
        // Predict: P_pred = P + Q, with Q = q * I.
        for (size_t k = 0; k < dim2; ++k) P_pred[k] = P[k];
        for (int i = 0; i < dim; ++i) P_pred[static_cast<size_t>(i) * dim + i] += settings.q;

        // Innovation, and S = P_pred + R with R the Poisson rate variance.
        for (size_t k = 0; k < dim2; ++k) S[k] = P_pred[k];
        for (int i = 0; i < dim; ++i) {
            const double y = counts[static_cast<size_t>(b) * dim + i] / settings.dt;
            v[static_cast<size_t>(i)] = y - x[static_cast<size_t>(i)];
            const double rate = std::max(x[static_cast<size_t>(i)], 1e-12);
            S[static_cast<size_t>(i) * dim + i] += settings.r_scale * rate / settings.dt;
        }

        std::vector<double> S_inv(S);
        if (!invert_in_place(S_inv, dim)) {
            mahalanobis[static_cast<size_t>(b)] = 0.0;
            continue;
        }

        // K = P_pred * S_inv
        for (int i = 0; i < dim; ++i) {
            for (int j = 0; j < dim; ++j) {
                double sum = 0.0;
                for (int k = 0; k < dim; ++k) {
                    sum += P_pred[static_cast<size_t>(i) * dim + k] *
                           S_inv[static_cast<size_t>(k) * dim + j];
                }
                K[static_cast<size_t>(i) * dim + j] = sum;
            }
        }
        // x += K v ; P = (I - K) P_pred
        for (int i = 0; i < dim; ++i) {
            double sum = 0.0;
            for (int k = 0; k < dim; ++k) {
                sum += K[static_cast<size_t>(i) * dim + k] * v[static_cast<size_t>(k)];
            }
            x[static_cast<size_t>(i)] += sum;
        }
        for (int i = 0; i < dim; ++i) {
            for (int j = 0; j < dim; ++j) {
                double sum = P_pred[static_cast<size_t>(i) * dim + j];
                for (int k = 0; k < dim; ++k) {
                    sum -= K[static_cast<size_t>(i) * dim + k] *
                           P_pred[static_cast<size_t>(k) * dim + j];
                }
                P_new[static_cast<size_t>(i) * dim + j] = sum;
            }
        }
        P.swap(P_new);

        // Mahalanobis distance of the innovation, sqrt(v^T S^-1 v).
        double quad = 0.0;
        for (int i = 0; i < dim; ++i) {
            double sum = 0.0;
            for (int k = 0; k < dim; ++k) {
                sum += S_inv[static_cast<size_t>(i) * dim + k] * v[static_cast<size_t>(k)];
            }
            Sv[static_cast<size_t>(i)] = sum;
            quad += v[static_cast<size_t>(i)] * sum;
        }
        mahalanobis[static_cast<size_t>(b)] = quad > 0.0 ? std::sqrt(quad) : 0.0;
    }

    // --- runs over threshold, length filter, gap merge -----------------------------
    std::vector<std::pair<int64_t, int64_t>> runs;   // inclusive bin ranges
    int64_t run_start = -1;
    for (int64_t b = 0; b <= n_bins; ++b) {
        const bool over =
            (b < n_bins) && (mahalanobis[static_cast<size_t>(b)] > settings.z_thresh);
        if (over && run_start < 0) {
            run_start = b;
        } else if (!over && run_start >= 0) {
            if (b - run_start >= settings.min_len) runs.emplace_back(run_start, b - 1);
            run_start = -1;
        }
    }
    if (runs.empty()) return {};

    if (settings.merge_gap > 0) {
        std::vector<std::pair<int64_t, int64_t>> merged;
        merged.push_back(runs.front());
        for (size_t i = 1; i < runs.size(); ++i) {
            if (runs[i].first - merged.back().second - 1 <= settings.merge_gap) {
                merged.back().second = runs[i].second;
            } else {
                merged.push_back(runs[i]);
            }
        }
        runs.swap(merged);
    }

    // --- bins back to photon indices -----------------------------------------------
    std::vector<long long> bursts;
    bursts.reserve(runs.size() * 2);
    for (const auto& run : runs) {
        const int64_t first = bin_first[static_cast<size_t>(run.first)];
        const int64_t last = bin_first[static_cast<size_t>(run.second + 1)] - 1;
        if (last < first) continue;                    // no photons in these bins
        if (last - first + 1 < settings.L) continue;   // min photons per burst
        bursts.push_back(static_cast<long long>(first));
        bursts.push_back(static_cast<long long>(last));
    }
    return bursts;
}

} // namespace tttrlib

// TTTR lives at global scope, so this definition sits outside `tttrlib`.
std::vector<long long> TTTR::burst_search_kalman(
    int L, double dt, double q, double r_scale,
    double z_thresh, int min_len, int merge_gap, bool per_channel
) {
    const int64_t n = static_cast<int64_t>(size());
    std::vector<int64_t> times(static_cast<size_t>(n));
    for (int64_t i = 0; i < n; ++i) {
        times[static_cast<size_t>(i)] = static_cast<int64_t>(get_macro_time_at(i));
    }
    // `routing_channels` is indexed by valid-event number, exactly like
    // get_macro_time_at(), so it is read directly rather than through the
    // allocating getter.
    std::vector<signed char> channels;
    if (per_channel && routing_channels != nullptr) {
        channels.assign(routing_channels, routing_channels + n);
    }

    tttrlib::KalmanBurstSettings s;
    s.L = L;
    s.dt = dt;
    s.q = q;
    s.r_scale = r_scale;
    s.z_thresh = z_thresh;
    s.min_len = min_len;
    s.merge_gap = merge_gap;
    s.per_channel = per_channel;
    return tttrlib::burst_search_kalman(
        times, channels, header->get_macro_time_resolution(), s);
}

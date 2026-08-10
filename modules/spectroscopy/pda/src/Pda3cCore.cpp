// SPDX-License-Identifier: BSD-3-Clause
#include "Pda3cCore.h"

#include <algorithm>
#include <cmath>

namespace tttrlib {

std::vector<double> gauss_hermite_grid(
    const std::vector<double>& means,
    const std::vector<double>& cholesky,
    const std::vector<double>& nodes_1d,
    const std::vector<double>& weights_1d,
    int K,
    double truncate
) {
    int n_nodes = static_cast<int>(nodes_1d.size());

    // Build tensor grid over K dimensions
    int total = 1;
    for (int d = 0; d < K; ++d) total *= n_nodes;

    std::vector<double> z(total * K);  // quadrature points in z-space
    std::vector<double> w(total, 1.0);  // product weights

    // Odometer over n_nodes^K combinations (rightmost index varies fastest)
    std::vector<int> indices(K, 0);
    for (int m = 0; m < total; ++m) {
        for (int d = 0; d < K; ++d) {
            // Hermite abscissae -> standard normal (z = x * sqrt(2))
            z[m * K + d] = nodes_1d[indices[d]] * std::sqrt(2.0);
            w[m] *= weights_1d[indices[d]];
        }
        for (int d = K - 1; d >= 0; --d) {
            if (++indices[d] < n_nodes) break;
            indices[d] = 0;
        }
    }

    // Normalize weights
    double wsum = 0.0;
    for (int m = 0; m < total; ++m) wsum += w[m];
    if (wsum > 0) for (auto& wt : w) wt /= wsum;

    // Truncate low-weight nodes (corner nodes carry negligible mass)
    int M = total;
    if (truncate > 0.0) {
        std::vector<double> z_keep, w_keep;
        z_keep.reserve(total * K);
        w_keep.reserve(total);
        for (int m = 0; m < total; ++m) {
            if (w[m] >= truncate) {
                for (int d = 0; d < K; ++d) z_keep.push_back(z[m * K + d]);
                w_keep.push_back(w[m]);
            }
        }
        if (!w_keep.empty()) {
            M = static_cast<int>(w_keep.size());
            wsum = 0.0;
            for (auto& wt : w_keep) wsum += wt;
            if (wsum > 0) for (auto& wt : w_keep) wt /= wsum;
            z = z_keep;
            w = w_keep;
        }
    }

    // Transform: R = mu + L * z. Clip at zero (negative distance unphysical).
    std::vector<double> result(M * (K + 1));  // M*K points + M weights
    for (int m = 0; m < M; ++m) {
        for (int d = 0; d < K; ++d) {
            double val = means[d];
            for (int j = 0; j < K; ++j)
                val += cholesky[d * K + j] * z[m * K + j];
            result[m * (K + 1) + d] = std::max(val, 0.0);
        }
        result[m * (K + 1) + K] = w[m];
    }

    return result;
}

std::vector<double> transfer_matrix_3c(
    const std::vector<double>& distances,
    const std::vector<double>& forster_radii,
    int K
) {
    // For K=3, distances are [d01, d02, d12] (R_BG, R_BR, R_GR), forster_radii
    // match. First compute one-step transfer efficiencies E[i,j] = (R0/r)^6,
    // then accumulate the cascade (relay routes) so T[i,j] is the total
    // probability that excitation on dye i is finally emitted by dye j.
    // This matches chisurf's pda3c physics.transfer_matrix.

    // Upper-triangle index: (0,1)->0, (0,2)->1, (1,2)->2 for K=3.
    auto idx = [K](int i, int j) -> int {
        int p = 0;
        for (int a = 0; a < i; ++a) p += K - 1 - a;
        return p + (j - i - 1);
    };

    // One-step efficiencies E[i][j] for i < j
    std::vector<std::vector<double>> E(K, std::vector<double>(K, 0.0));
    for (int i = 0; i < K; ++i) {
        double sum_rates = 0.0;
        for (int j = i + 1; j < K; ++j) {
            int di = idx(i, j);
            double r = std::max(distances[di], 1e-10);
            double R0 = std::max(forster_radii[di], 1e-10);
            E[i][j] = std::pow(R0 / r, 6.0);
            sum_rates += E[i][j];
        }
        for (int j = i + 1; j < K; ++j)
            E[i][j] = (sum_rates > 0) ? E[i][j] / (1.0 + sum_rates) : 0.0;
    }

    // Accumulate cascade from the reddest dye upward (matches chiSurf recursion).
    // T[i] = (1 - sum_j E[i][j]) e_i + sum_{j>i} E[i][j] T[j]
    std::vector<double> T(K * K, 0.0);
    T[(K - 1) * K + (K - 1)] = 1.0;  // reddest dye emits itself
    for (int i = K - 2; i >= 0; --i) {
        double emitted_self = 1.0;
        for (int j = i + 1; j < K; ++j) emitted_self -= E[i][j];
        T[i * K + i] = emitted_self;
        for (int j = i + 1; j < K; ++j) {
            for (int c = 0; c < K; ++c)
                T[i * K + c] += E[i][j] * T[j * K + c];
        }
    }

    return T;
}

std::vector<double> channel_probabilities_3c(
    const std::vector<double>& transfer,
    const std::vector<double>& excitation,
    const std::vector<double>& emission,
    int K, int n_channels
) {
    // Emitted light per dye: sum_i excitation[i] * transfer[i, dye]
    std::vector<double> emitted(K, 0.0);
    for (int dye = 0; dye < K; ++dye) {
        double s = 0.0;
        for (int i = 0; i < K; ++i)
            s += excitation[i] * transfer[i * K + dye];
        emitted[dye] = s;
    }

    // Detected per channel: sum_dye emission[dye, ch] * emitted[dye]
    std::vector<double> channels(n_channels, 0.0);
    for (int ch = 0; ch < n_channels; ++ch) {
        double s = 0.0;
        for (int dye = 0; dye < K; ++dye)
            s += emission[dye * n_channels + ch] * emitted[dye];
        channels[ch] = s;
    }

    // Normalize
    double total = 0.0;
    for (auto& v : channels) total += v;
    if (total > 0) for (auto& v : channels) v /= total;

    return channels;
}

std::vector<double> channel_probabilities_batch(
    const std::vector<double>& distance_grid,
    const std::vector<double>& forster_radii,
    const std::vector<double>& excitation,
    const std::vector<double>& emission,
    int M, int K, int n_channels
) {
    int n_pairs = K * (K - 1) / 2;
    std::vector<double> result(M * n_channels);

    // Fast path for the common K==3, n_channels==3 case: fixed-size stack
    // buffers, no per-node allocation.
    if (K == 3 && n_channels == 3) {
        const double r01 = std::max(forster_radii[0], 1e-10);
        const double r02 = std::max(forster_radii[1], 1e-10);
        const double r12 = std::max(forster_radii[2], 1e-10);
        const double e0 = excitation[0], e1 = excitation[1], e2 = excitation[2];
        // emission[d*3+c]
        const double* em = emission.data();

        #pragma omp parallel for schedule(static)
        for (int m = 0; m < M; ++m) {
            const double* d = &distance_grid[m * 3];
            // One-step efficiencies (R0/r)^6, denominator 1 + sum
            double x01 = std::max(d[0], 1e-10), x02 = std::max(d[1], 1e-10);
            double x12 = std::max(d[2], 1e-10);
            double s01 = std::pow(r01 / x01, 6.0);
            double s02 = std::pow(r02 / x02, 6.0);
            double s12 = std::pow(r12 / x12, 6.0);
            double den0 = 1.0 + s01 + s02;
            double den1 = 1.0 + s12;
            double E01 = s01 / den0, E02 = s02 / den0, E12 = s12 / den1;

            // Cascaded transfer matrix T[i][j] = what dye i finally emits to j
            double T[9];
            T[8] = 1.0;                            // dye 2 emits itself
            T[7] = 0.0; T[6] = 0.0;
            T[4] = 1.0 - E12; T[5] = E12;          // dye 1
            T[3] = 0.0;
            T[0] = 1.0 - E01 - E02;                // dye 0
            T[1] = E01 * (1.0 - E12);
            T[2] = E01 * E12 + E02;

            // emitted[d] = sum_i excitation[i]*T[i][d]
            double emit[3];
            emit[0] = e0 * T[0] + e1 * T[3] + e2 * T[6];
            emit[1] = e0 * T[1] + e1 * T[4] + e2 * T[7];
            emit[2] = e0 * T[2] + e1 * T[5] + e2 * T[8];

            // channel[c] = sum_d emission[d][c] * emitted[d], then normalize
            double ch[3] = {
                em[0] * emit[0] + em[3] * emit[1] + em[6] * emit[2],
                em[1] * emit[0] + em[4] * emit[1] + em[7] * emit[2],
                em[2] * emit[0] + em[5] * emit[1] + em[8] * emit[2]
            };
            double tot = ch[0] + ch[1] + ch[2];
            if (tot > 0.0) { ch[0] /= tot; ch[1] /= tot; ch[2] /= tot; }
            result[m * 3 + 0] = ch[0];
            result[m * 3 + 1] = ch[1];
            result[m * 3 + 2] = ch[2];
        }
        return result;
    }

    // General K path
    #pragma omp parallel for schedule(static)
    for (int m = 0; m < M; ++m) {
        std::vector<double> dist(n_pairs);
        for (int p = 0; p < n_pairs; ++p)
            dist[p] = distance_grid[m * n_pairs + p];
        auto T = transfer_matrix_3c(dist, forster_radii, K);
        auto ch = channel_probabilities_3c(T, excitation, emission, K, n_channels);
        for (int c = 0; c < n_channels; ++c)
            result[m * n_channels + c] = ch[c];
    }
    return result;
}

std::vector<double> species_forward_model(
    const std::vector<double>& means,
    const std::vector<double>& cholesky,
    const std::vector<double>& nodes_1d,
    const std::vector<double>& weights_1d,
    const std::vector<double>& forster_radii,
    const std::vector<double>& excitation,
    const std::vector<double>& emission,
    int K, int n_channels
) {
    // Build the quadrature grid (points + normalized weights, interleaved)
    auto grid = gauss_hermite_grid(
        means, cholesky, nodes_1d, weights_1d, K, 0.0);
    int M = static_cast<int>(grid.size() / (K + 1));
    int n_pairs = K * (K - 1) / 2;

    // The quadrature grid points ARE the inter-dye distances themselves
    // (the Gaussian is over R_GR, R_BG, R_BR directly). Pass them straight to
    // the transfer computation — no dye-position subtraction.
    std::vector<double> distance_grid(M * n_pairs);
    for (int m = 0; m < M; ++m)
        for (int p = 0; p < n_pairs; ++p)
            distance_grid[m * n_pairs + p] = grid[m * (K + 1) + p];

    auto per_node = channel_probabilities_batch(
        distance_grid, forster_radii, excitation, emission, M, K, n_channels);

    // Weight-average the channels over the grid
    std::vector<double> avg(n_channels, 0.0);
    for (int m = 0; m < M; ++m) {
        double wm = grid[m * (K + 1) + K];
        for (int c = 0; c < n_channels; ++c)
            avg[c] += wm * per_node[m * n_channels + c];
    }
    return avg;
}

} // namespace tttrlib

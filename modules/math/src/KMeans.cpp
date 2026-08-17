// SPDX-License-Identifier: BSD-3-Clause
// Do not contract `acc += diff * diff` into a fused multiply-add anywhere in
// this file. The fit is a bit-exact port of ChiSurf's Python implementation:
// its Gaussian-HMM seeds from these uniforms and ranks restarts on the
// inertia, so one unit in the last place on the ranked number is a different
// answer while centres and labels stay identical. The pragma is the contract
// carried in source, deliberately, rather than a compile flag read from the
// build -- a flag silently leaves builds that do not apply it (MSVC has none
// for this today) drifting by one ulp. FMA contraction is worth nothing here
// anyway: the distance loop is memory bound.
#pragma STDC FP_CONTRACT OFF
#include "KMeans.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace tttrlib {
namespace {

void require(bool ok, const char* what) {
    if (!ok) throw std::invalid_argument(std::string("kmeans: ") + what);
}

// n_trials per centre for the greedy seeding, as in the reference:
// 2 + int(np.log(n_clusters)) -- natural log, truncated.
int seeding_trials(int n_clusters) {
    return 2 + static_cast<int>(std::log(static_cast<double>(n_clusters)));
}

// Squared distance from every row of X to `center`. Loop order is part of the
// port: accumulate along features, per sample, in order.
void squared_distances(const double* X, int n_samples, int n_features,
                       const double* center, double* out) {
    for (int t = 0; t < n_samples; ++t) {
        const double* row = X + static_cast<size_t>(t) * n_features;
        double acc = 0.0;
        for (int j = 0; j < n_features; ++j) {
            const double diff = row[j] - center[j];
            acc += diff * diff;
        }
        out[t] = acc;
    }
}

// The greedy k-means++ seeding over one restart's slice of the uniforms.
// `u` points at n_clusters * n_trials values, consumed in the reference's
// order: centre 0 takes u[0]; centre c's trial r takes u[c * n_trials + r].
void kmeanspp_seed(const double* X, int n_samples, int n_features,
                   int n_clusters, int n_trials, const double* u,
                   double* centers) {
    std::vector<double> closest(n_samples);
    std::vector<double> candidate(n_samples);

    // int(u[0] * n) truncated toward zero, clamped -- matches
    // min(int(uniform * n_samples), n_samples - 1) for uniform in [0,1).
    int index = static_cast<int>(u[0] * static_cast<double>(n_samples));
    if (index > n_samples - 1) index = n_samples - 1;
    std::memcpy(centers, X + static_cast<size_t>(index) * n_features,
                sizeof(double) * static_cast<size_t>(n_features));
    squared_distances(X, n_samples, n_features, centers, closest.data());

    for (int c = 1; c < n_clusters; ++c) {
        double total = 0.0;
        for (int t = 0; t < n_samples; ++t) total += closest[t];

        double best_potential = std::numeric_limits<double>::infinity();
        int best_index = -1;
        for (int trial = 0; trial < n_trials; ++trial) {
            const double uniform = u[c * n_trials + trial];
            if (total <= 0.0) {
                index = static_cast<int>(uniform * static_cast<double>(n_samples));
                if (index > n_samples - 1) index = n_samples - 1;
            } else {
                const double target = uniform * total;
                double acc = 0.0;
                index = n_samples - 1;
                for (int t = 0; t < n_samples; ++t) {
                    acc += closest[t];
                    if (acc >= target) { index = t; break; }
                }
            }
            squared_distances(X, n_samples, n_features,
                              X + static_cast<size_t>(index) * n_features,
                              candidate.data());
            double potential = 0.0;
            for (int t = 0; t < n_samples; ++t)
                potential += candidate[t] < closest[t] ? candidate[t] : closest[t];
            if (potential < best_potential) {
                best_potential = potential;
                best_index = index;
            }
        }
        std::memcpy(centers + static_cast<size_t>(c) * n_features,
                    X + static_cast<size_t>(best_index) * n_features,
                    sizeof(double) * static_cast<size_t>(n_features));
        squared_distances(X, n_samples, n_features,
                          centers + static_cast<size_t>(c) * n_features,
                          candidate.data());
        for (int t = 0; t < n_samples; ++t)
            if (candidate[t] < closest[t]) closest[t] = candidate[t];
    }
}

// Lloyd sweeps in place on centers/labels. Returns (inertia, n_iter) -- the
// inertia of the RETURNED centres, measured by a final assignment pass.
void kmeans_lloyd(const double* X, int n_samples, int n_features,
                  double* centers, int n_clusters, long long* labels,
                  int max_iter, double tol,
                  double* out_inertia, int* out_n_iter) {
    std::vector<double> sums(static_cast<size_t>(n_clusters) * n_features);
    std::vector<double> counts(n_clusters);
    int n_iter = 0;

    for (int t = 0; t < n_samples; ++t) labels[t] = -1;

    for (int sweep = 0; sweep < max_iter; ++sweep) {
        ++n_iter;
        std::fill(sums.begin(), sums.end(), 0.0);
        std::fill(counts.begin(), counts.end(), 0.0);
        long long n_changed = 0;
        double worst_distance = -1.0;
        int worst_index = 0;

        for (int t = 0; t < n_samples; ++t) {
            const double* row = X + static_cast<size_t>(t) * n_features;
            double best = std::numeric_limits<double>::infinity();
            int best_c = 0;
            for (int c = 0; c < n_clusters; ++c) {
                const double* ctr = centers + static_cast<size_t>(c) * n_features;
                double acc = 0.0;
                for (int j = 0; j < n_features; ++j) {
                    const double diff = row[j] - ctr[j];
                    acc += diff * diff;
                }
                if (acc < best) { best = acc; best_c = c; }
            }
            if (labels[t] != best_c) { labels[t] = best_c; ++n_changed; }
            counts[best_c] += 1.0;
            double* srow = sums.data() + static_cast<size_t>(best_c) * n_features;
            for (int j = 0; j < n_features; ++j) srow[j] += row[j];
            if (best > worst_distance) { worst_distance = best; worst_index = t; }
        }

        double shift = 0.0;
        for (int c = 0; c < n_clusters; ++c) {
            if (counts[c] > 0.0) {
                double* ctr = centers + static_cast<size_t>(c) * n_features;
                const double* srow = sums.data() + static_cast<size_t>(c) * n_features;
                for (int j = 0; j < n_features; ++j) {
                    const double updated = srow[j] / counts[c];
                    const double diff = updated - ctr[j];
                    shift += diff * diff;
                    ctr[j] = updated;
                }
            } else {
                // re-seed an emptied cluster on the worst-explained sample,
                // then retire that sample: the next empty cluster must look
                // past it (the reference recomputes the worst excluding the
                // newly claimed cluster's members)
                double* ctr = centers + static_cast<size_t>(c) * n_features;
                const double* row = X + static_cast<size_t>(worst_index) * n_features;
                for (int j = 0; j < n_features; ++j) ctr[j] = row[j];
                worst_distance = -1.0;
                for (int t = 0; t < n_samples; ++t) {
                    if (labels[t] == c) continue;
                    const double* xrow = X + static_cast<size_t>(t) * n_features;
                    const double* lctr =
                        centers + static_cast<size_t>(labels[t]) * n_features;
                    double acc = 0.0;
                    for (int j = 0; j < n_features; ++j) {
                        const double diff = xrow[j] - lctr[j];
                        acc += diff * diff;
                    }
                    if (acc > worst_distance) { worst_distance = acc; worst_index = t; }
                }
                shift += tol + 1.0;
            }
        }
        if (n_changed == 0 || shift <= tol * tol) break;
    }

    // final assignment pass: the sweep's inertia was accumulated against the
    // centres the sweep started with
    double inertia = 0.0;
    for (int t = 0; t < n_samples; ++t) {
        const double* row = X + static_cast<size_t>(t) * n_features;
        double best = std::numeric_limits<double>::infinity();
        int best_c = 0;
        for (int c = 0; c < n_clusters; ++c) {
            const double* ctr = centers + static_cast<size_t>(c) * n_features;
            double acc = 0.0;
            for (int j = 0; j < n_features; ++j) {
                const double diff = row[j] - ctr[j];
                acc += diff * diff;
            }
            if (acc < best) { best = acc; best_c = c; }
        }
        labels[t] = best_c;
        inertia += best;
    }
    *out_inertia = inertia;
    *out_n_iter = n_iter;
}

}  // anonymous namespace

void kmeans(
        const double* data, int n_samples, int n_features,
        int n_clusters,
        const double* uniforms, int n_uniforms,
        int n_init, int max_iter, double tol,
        double** out_centers, int* out_n1, int* out_n2,
        long long** out_labels, int* out_n_labels,
        double** out_stats, int* out_n_stats) {
    require(data != nullptr, "X is null");
    require(uniforms != nullptr, "uniforms is null");
    require(n_samples >= 0, "n_samples must not be negative");
    require(n_features >= 1, "n_features must be positive");
    require(n_clusters >= 1, "n_clusters must be at least 1");
    require(n_init >= 1, "n_init must be at least 1");
    require(max_iter >= 0, "max_iter must not be negative");

    const int n_trials = seeding_trials(n_clusters);
    const int per_restart = n_clusters * n_trials;
    if (n_uniforms != n_init * per_restart) {
        throw std::invalid_argument(
            "kmeans: uniforms must hold exactly n_init * n_clusters * "
            "(2 + floor(ln(n_clusters))) = " + std::to_string(n_init * per_restart) +
            " values for these parameters, got " + std::to_string(n_uniforms));
    }

    *out_n1 = 0; *out_n2 = n_features; *out_n_labels = 0; *out_n_stats = 2;
    *out_centers = nullptr; *out_labels = nullptr;
    *out_stats = new double[2]{0.0, 0.0};

    // Degenerate-but-defined, as in the reference: fewer samples than
    // centres -> the data itself padded by repeats of the mean.
    if (n_samples <= n_clusters) {
        double* centers = new double[static_cast<size_t>(n_clusters) * n_features];
        std::vector<double> mean(n_features, 0.0);
        if (n_samples > 0) {
            for (int t = 0; t < n_samples; ++t)
                for (int j = 0; j < n_features; ++j)
                    mean[j] += data[static_cast<size_t>(t) * n_features + j];
            for (int j = 0; j < n_features; ++j) mean[j] /= n_samples;
        }
        for (int c = 0; c < n_clusters; ++c)
            std::memcpy(centers + static_cast<size_t>(c) * n_features,
                        mean.data(), sizeof(double) * n_features);
        for (int t = 0; t < n_samples; ++t)
            std::memcpy(centers + static_cast<size_t>(t) * n_features,
                        data + static_cast<size_t>(t) * n_features,
                        sizeof(double) * n_features);
        *out_centers = centers;
        *out_n1 = n_clusters;
        long long* labels = new long long[n_samples > 0 ? n_samples : 1];
        for (int t = 0; t < n_samples; ++t) labels[t] = t % n_clusters;
        *out_labels = labels;
        *out_n_labels = n_samples;
        (*out_stats)[0] = 0.0;
        (*out_stats)[1] = 0.0;
        return;
    }

    std::vector<double> centers(static_cast<size_t>(n_clusters) * n_features);
    std::vector<long long> labels(n_samples);
    std::vector<double> best_centers(centers.size());
    std::vector<long long> best_labels(n_samples);
    double best_inertia = std::numeric_limits<double>::infinity();
    int best_n_iter = 0;

    for (int r = 0; r < n_init; ++r) {
        kmeanspp_seed(data, n_samples, n_features, n_clusters, n_trials,
                      uniforms + static_cast<size_t>(r) * per_restart,
                      centers.data());
        double inertia = 0.0;
        int n_iter = 0;
        kmeans_lloyd(data, n_samples, n_features, centers.data(), n_clusters,
                     labels.data(), max_iter, tol, &inertia, &n_iter);
        if (inertia < best_inertia) {
            best_inertia = inertia;
            best_n_iter = n_iter;
            best_centers = centers;
            best_labels = labels;
        }
    }

    *out_centers = new double[centers.size()];
    std::memcpy(*out_centers, best_centers.data(), sizeof(double) * centers.size());
    *out_n1 = n_clusters;
    *out_labels = new long long[n_samples];
    std::memcpy(*out_labels, best_labels.data(), sizeof(long long) * n_samples);
    *out_n_labels = n_samples;
    (*out_stats)[0] = best_inertia;
    (*out_stats)[1] = static_cast<double>(best_n_iter);
}

}  // namespace tttrlib

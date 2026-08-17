// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_KMEANS_H
#define TTTRLIB_KMEANS_H

// Validation: A/B-TESTED 2026-08-17 -- benchmarked vs sklearn (5.9x same job, 2.9x Lloyd-only) with identical
//   centres/labels/inertia (benchmarks/bench_sciref.py, check_sciref.py); vs sklearn KMeans(lloyd) from the same seed (centres 1e-14, labels equal) and
//   ChiSurf _kmeans live (bit-identical). test/python/misc/test_math_ab_clustering.py.
//   Register: okf/testing/math-kernel-validation.md

// KMeans.h -- k-means++ seeding and Lloyd iterations, one whole fit per call.
//
// Port of the reference Python implementation in ChiSurf
// (`core/ml/cluster/_kmeans.py`), loop order included: the same uniforms must
// produce the same centres bit for bit, because ChiSurf seeds this from a
// caller-controlled stream so a fit is reproducible, and its Gaussian-HMM
// initialisation consumes the labels. Nothing here draws randomness -- the
// uniforms arrive as an array (requirement, not courtesy: determinism is the
// contract).
//
// Exactness caveat, honest version: bit-for-bit agreement is a source-level
// contract, not a build-level one. The translation unit opens with the
// standard `#pragma STDC FP_CONTRACT OFF` — clang and gcc honor it on their
// default builds, and a build that ever contracts `acc += diff*diff` into a
// fused multiply-add rounds once instead of twice and the ranked inertia —
// the number restarts are selected on — drifts by one ulp while centres and
// labels stay identical; the fixture test fails loudly if that happens. A
// caller passing `-ffp-contract=fast` opts out of IEEE arithmetic
// library-wide and overrides the pragma, which is their call.
//
// Why a compiled kernel at all: since ChiSurf dropped numba these loops run
// as plain Python, and they are O(n_init * n_clusters * n_samples * n_dims)
// per Lloyd sweep with no allocation to vectorise away (the point of the
// shared assignment+accumulation pass is that no (n, k) distance matrix is
// ever materialised).
//
// The three ChiSurf kernels map onto this surface as follows:
// `_squared_distances` is inlined (three operations, pure marshalling as a
// binding); `_kmeanspp_seed` and `_kmeans_lloyd` are the two halves of the
// one call, kept together because a restart is seed-then-sweep and the
// caller never wants one without the other.

#include <vector>

namespace tttrlib {

/*! \brief The whole k-means fit: n_init restarts of greedy k-means++ seeding
 *         followed by Lloyd sweeps; the lowest-inertia restart wins.
 *
 * A faithful port, including the parts that look like quirks:
 *
 * - Seeding is the *greedy* k-means++ variant: several candidates are drawn
 *   per centre (2 + floor(ln(n_clusters)) of them, from the uniforms array)
 *   and the one that lowers the total squared distance most is kept.
 * - An emptied cluster is re-seeded on the worst-explained sample, and that
 *   sample is excluded from the next re-seed -- otherwise two empty clusters
 *   land on the same point.
 * - Inertia is re-measured with a final assignment pass after convergence,
 *   because the sweep accumulates it against the centres the sweep *started*
 *   with; returning that value would rank restarts on a stale number.
 * - `n_samples <= n_clusters` is degenerate-but-defined: the centres are the
 *   data itself padded by repeats of the mean, exactly as the reference.
 *
 * \param data         row-major (n_samples x n_features) table.
 * \param n_clusters   number of centres; must be at least 1.
 * \param uniforms     the caller's random stream, one flat array. Must hold
 *                     exactly n_init * n_clusters * (2 + floor(ln(n_clusters)))
 *                     values in [0, 1); consumed in order, restart by restart.
 *                     Rejected otherwise, with the required length in the
 *                     error -- a wrong-length stream is a caller bug, not a
 *                     degraded fit.
 * \param n_init       restarts; the lowest-inertia restart wins.
 * \param max_iter     Lloyd sweeps per restart.
 * \param tol          centre-shift (Frobenius) convergence threshold.
 * \param out_centers  (*out_n1 x n_features) winning centres, allocated here.
 * \param out_labels   (*out_n2) winning closest-centre index per sample.
 * \param out_stats    2 doubles: [inertia of the returned assignment,
 *                     sweeps the winning restart performed].
 */
void kmeans(
        const double* data, int n_samples, int n_features,
        int n_clusters,
        const double* uniforms, int n_uniforms,
        int n_init, int max_iter, double tol,
        double** out_centers, int* out_n1, int* out_n2,
        long long** out_labels, int* out_n_labels,
        double** out_stats, int* out_n_stats);

}  // namespace tttrlib

#endif  // TTTRLIB_KMEANS_H

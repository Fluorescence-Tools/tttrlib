// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_KALMAN_H
#define TTTRLIB_KALMAN_H

// Kalman.h -- the Kalman filter recursion over a count-rate trace, one whole
// trace per call.
//
// Port of the reference Python implementation in ChiSurf
// (`core/fluorescence/burst/kalman.py`, `_kalman_filter_loop` plus the
// `_inv2x2` helper it inlines), loop order included: the same trace, same
// initial state and same noise matrices must produce the same filtered
// states, covariances and Mahalanobis distances bit for bit. ChiSurf's fcs
// plugin builds its burst table from the Mahalanobis distance directly, so a
// one-ulp difference in the recursion is a different burst the next time the
// filter runs. The transition and observation matrices are the identity --
// the model is "the rate stays what it was, plus process noise" -- which is
// why a burst shows up as an innovation the filter did not expect rather than
// as a fitted trend.
//
// Why a compiled kernel at all: since ChiSurf dropped numba the recursion
// runs as plain Python -- T iterations, each with two matrix products and an
// inverse, over traces of tens of thousands of bins -- and nothing about it
// vectorises away.
//
// Exactness contract, carried in source: this translation unit opens with
// `#pragma STDC FP_CONTRACT OFF`, the same device `KMeans.cpp` and
// `Cluster.cpp` use, because a fused multiply-add in the P or K updates would
// round once instead of twice and drift every subsequent bin by a ulp. A
// caller passing `-ffp-contract=fast` opts out of IEEE arithmetic library-wide
// and overrides the pragma, which is their call.

#include <cstddef>

namespace tttrlib {

/*! \brief Run the Kalman filter recursion over an entire count-rate trace.
 *
 * A faithful port of ChiSurf's `_kalman_filter_loop`, including the part
 * that looks like a quirk: for a two-channel trace the innovation
 * covariance is inverted with the closed-form `_inv2x2` (which rescues a
 * zero determinant by nudging it to 1e-300), and only channels beyond two
 * fall back to the general inverse. Keeping the branch means a two-channel
 * fit agrees with the reference digit for digit, and two channels are the
 * single-molecule bulk of the use.
 *
 * \param y           (T x dim) measured count rates, in counts per second:
 *                    raw bin counts divided by the bin width.
 * \param T           number of time bins.
 * \param dim         state dimension; must be at least 1.
 * \param x0          (dim) initial state estimate, usually one count rate.
 * \param n_x0        length of x0; must equal dim.
 * \param P0          (dim x dim) initial state covariance.
 * \param n_P1, n_P2  extent of P0; each must equal dim.
 * \param Q           (dim x dim) process-noise covariance, q * I in practice.
 * \param n_Q1, n_Q2  extent of Q; each must equal dim.
 * \param dt          bin width in seconds; the Poisson measurement variance
 *                    is rate * r_scale / dt.
 * \param r_scale     measurement-noise scale on the Poisson variance.
 * \param out_x_filt  (T x dim) filtered state per bin, allocated here.
 * \param out_P_filt  (T x dim x dim) filtered covariance per bin, allocated
 *                    here.
 * \param out_D       (T) Mahalanobis distance of the innovation per bin,
 *                    allocated here. A burst is a run of bins where this
 *                    exceeds a caller-chosen threshold.
 */
void kalman_filter(
        const double* y, int T, int dim,
        const double* x0, int n_x0,
        const double* P0, int n_P1, int n_P2,
        const double* Q, int n_Q1, int n_Q2,
        double dt, double r_scale,
        double** out_x_filt, int* out_T1, int* out_dim1,
        double** out_P_filt, int* out_T2, int* out_dim2a, int* out_dim2b,
        double** out_D, int* out_T3);

}  // namespace tttrlib

#endif  // TTTRLIB_KALMAN_H
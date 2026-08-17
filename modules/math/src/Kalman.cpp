// SPDX-License-Identifier: BSD-3-Clause
// Do not contract `acc += a * b` into a fused multiply-add anywhere in this
// file. The filter is a bit-exact port of ChiSurf's `_kalman_filter_loop`,
// and ChiSurf's fcs plugin thresholds the Mahalanobis distance to find
// bursts, so one unit in the last place in a K or P update drifts every
// subsequent bin by a ulp and a burst edge moves. A compile flag was tried
// for this class of contract once and rejected (it silently skipped MSVC);
// the pragma is portable across clang, gcc and MSVC and needs no build
// support. FMA contraction is worth nothing here anyway: the per-bin work is
// a handful of tiny products, memory bound.
#pragma STDC FP_CONTRACT OFF
#include "Kalman.h"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "Mat.h"

namespace tttrlib {
namespace {

void require(bool ok, const char* what) {
    if (!ok) throw std::invalid_argument(std::string("kalman_filter: ") + what);
}

// Closed-form 2x2 inverse, exactly as the reference: det is nudged to 1e-300
// when it is exactly zero, and the four entries are divided by det in the
// reference's order (that division order is load-bearing for bit parity).
void inv2x2(const double* A, double* out) {
    const double det = A[0] * A[3] - A[1] * A[2];
    const double d = (det == 0.0) ? 1e-300 : det;
    out[0] =  A[3] / d;
    out[1] = -A[1] / d;
    out[2] = -A[2] / d;
    out[3] =  A[0] / d;
}

// Two-term dot in the reference's BLAS order. Measured against numpy 1.26.4
// + Accelerate (the env CHiSurf runs in): `a @ b` for 2x2 forms the sum as
// fma(a0, b0, rounded a1*b1), i.e. the SECOND product rides in the
// addend and stays exact. A plain `a0*b0 + a1*b1` disagrees ~44% of the
// time -- an ulp in K, and every later bin drifts. Verified over 2e5 random
// pairs: this form and numpy agree bit for bit. Ported because the last
// product is fused and the first rounded, exactly as std::fma puts it.
inline double dot2(const double a, const double b, const double c,
                   const double d) {
    return std::fma(c, d, a * b);
}

// Ordered (non-fused) `row @ x` for dim > 2, where BLAS parity is not
// claimed and any consistent accumulation is fine.  `row` points at row i of
// the matrix (row-major, stride dim).
double matvec(const double* row, int dim, const double* x) {
    double sum = 0.0;
    for (int k = 0; k < dim; ++k) sum += row[static_cast<size_t>(k)] * x[static_cast<size_t>(k)];
    return sum;
}

}  // anonymous namespace

void kalman_filter(
        const double* y, int T, int dim,
        const double* x0, int n_x0,
        const double* P0, int n_P1, int n_P2,
        const double* Q, int n_Q1, int n_Q2,
        double dt, double r_scale,
        double** out_x_filt, int* out_T1, int* out_dim1,
        double** out_P_filt, int* out_T2, int* out_dim2a, int* out_dim2b,
        double** out_D, int* out_T3) {
    require(y != nullptr, "y is null");
    require(x0 != nullptr, "x0 is null");
    require(P0 != nullptr, "P0 is null");
    require(Q != nullptr, "Q is null");
    require(T >= 0, "T must not be negative");
    require(dim >= 1, "dim must be at least 1");
    require(dt > 0.0, "dt must be positive");
    require(n_x0 == dim, "x0 must have length dim");
    require(n_P1 == dim && n_P2 == dim, "P0 must be dim x dim");
    require(n_Q1 == dim && n_Q2 == dim, "Q must be dim x dim");

    *out_T1 = 0; *out_dim1 = dim;
    *out_T2 = 0; *out_dim2a = dim; *out_dim2b = dim;
    *out_T3 = 0;
    *out_x_filt = nullptr; *out_P_filt = nullptr; *out_D = nullptr;

    if (T == 0) {
        *out_x_filt = new double[0];
        *out_P_filt = new double[0];
        *out_D = new double[0];
        return;
    }

    const size_t dim2 = static_cast<size_t>(dim) * dim;
    std::vector<double> x(static_cast<size_t>(dim), 0.0);
    std::vector<double> P(dim2, 0.0);
    std::vector<double> P_pred(dim2), S(dim2), K(dim2), P_new(dim2);
    std::vector<double> ImK(dim2);
    std::vector<double> v(static_cast<size_t>(dim)), Sv(static_cast<size_t>(dim));
    std::vector<double> S_inv(dim2);
    std::memcpy(x.data(), x0, sizeof(double) * static_cast<size_t>(dim));
    std::memcpy(P.data(), P0, sizeof(double) * dim2);

    const bool use_analytic = (dim == 2);

    double* x_filt = new double[static_cast<size_t>(T) * dim];
    double* P_filt = new double[static_cast<size_t>(T) * dim2];
    double* D = new double[static_cast<size_t>(T)];

    for (int t = 0; t < T; ++t) {
        // predict: P_pred = P + Q
        for (size_t k = 0; k < dim2; ++k) P_pred[k] = P[k] + Q[k];

        // measurement noise R from Poisson statistics: r_scale * (rate / dt),
        // division before scaling exactly as the reference, with the state
        // clamped for the reference's floor of 1e-12.
        for (size_t k = 0; k < dim2; ++k) S[k] = P_pred[k];
        for (int i = 0; i < dim; ++i) {
            const double rate = x[static_cast<size_t>(i)] > 1e-12
                                ? x[static_cast<size_t>(i)] : 1e-12;
            S[static_cast<size_t>(i) * dim + i] +=
                r_scale * (rate / dt);
            v[static_cast<size_t>(i)] =
                y[static_cast<size_t>(t) * dim + i] - x[static_cast<size_t>(i)];
        }

        // S inverse: closed form for two channels, as in the reference.
        if (use_analytic) {
            inv2x2(S.data(), S_inv.data());
        } else {
            // The general branch of the reference calls np.linalg.inv (LAPACK),
            // which no std-only inverse reproduces bit for bit. This is the
            // library's own GE inverse; parity holds for dim == 2, the common
            // single-molecule case, and not for dim > 2. Recorded in the
            // module docstring.
            S_inv = S;
            if (!mat_inverse_inplace(S_inv, dim)) {
                // Singular innovation covariance: the recursion has nowhere to
                // go, so freeze the Mahalanobis distance at zero this bin, as
                // the reference's inverse would degenerate.
                for (int i = 0; i < dim; ++i) {
                    x_filt[static_cast<size_t>(t) * dim + i] = x[static_cast<size_t>(i)];
                    for (int j = 0; j < dim; ++j)
                        P_filt[(static_cast<size_t>(t) * dim + i) * dim + j] = P[static_cast<size_t>(i) * dim + j];
                }
                D[t] = 0.0;
                continue;
            }
        }

        // K = P_pred @ S_inv
        for (int i = 0; i < dim; ++i) {
            for (int j = 0; j < dim; ++j) {
                if (dim == 2) {
                    K[static_cast<size_t>(i) * 2 + j] =
                        dot2(P_pred[i * 2 + 0], S_inv[0 * 2 + j],
                             P_pred[i * 2 + 1], S_inv[1 * 2 + j]);
                } else if (dim == 3) {
                    const size_t ij = static_cast<size_t>(i) * 3 + j;
                    K[ij] = P_pred[i * 3 + 0] * S_inv[0 * 3 + j] +
                            P_pred[i * 3 + 1] * S_inv[1 * 3 + j] +
                            P_pred[i * 3 + 2] * S_inv[2 * 3 + j];
                } else {
                    // General dim (1, or > 3). Until 2026-08-17 this branch was
                    // written for dim == 4 (strides of 4, four terms): for
                    // dim == 1 it read past the 1-element vectors -- undefined
                    // behaviour that usually met heap slack holding zeros and
                    // now and then did not, an intermittent one-channel failure
                    // in the A/B against the textbook filter -- and for dim >= 5
                    // it would have been silently wrong.
                    double sum = 0.0;
                    for (int k = 0; k < dim; ++k)
                        sum += P_pred[static_cast<size_t>(i) * dim + k] * S_inv[static_cast<size_t>(k) * dim + j];
                    K[static_cast<size_t>(i) * dim + j] = sum;
                }
            }
        }

        // x += K @ v ; P = (I - K) @ P_pred
        for (int i = 0; i < dim; ++i) {
            if (dim == 2) {
                x[static_cast<size_t>(i)] +=
                    dot2(K[i * 2 + 0], v[0], K[i * 2 + 1], v[1]);
            } else {
                double sum = 0.0;
                for (int k = 0; k < dim; ++k) sum += K[static_cast<size_t>(i) * dim + k] * v[static_cast<size_t>(k)];
                x[static_cast<size_t>(i)] += sum;
            }
        }
        // Build I - K elementwise first: the reference rounds 1.0 - K[i,i] on
        // the diagonal before it multiplies, and bit parity rides on that.
        for (int i = 0; i < dim; ++i) {
            for (int j = 0; j < dim; ++j) {
                const size_t ij = static_cast<size_t>(i) * dim + j;
                ImK[ij] = (i == j) ? (1.0 - K[ij]) : (-K[ij]);
            }
        }
        for (int i = 0; i < dim; ++i) {
            for (int j = 0; j < dim; ++j) {
                if (dim == 2) {
                    P_new[static_cast<size_t>(i) * 2 + j] =
                        dot2(ImK[i * 2 + 0], P_pred[0 * 2 + j],
                             ImK[i * 2 + 1], P_pred[1 * 2 + j]);
                } else {
                    double sum = 0.0;
                    for (int k = 0; k < dim; ++k) sum += ImK[static_cast<size_t>(i) * dim + k] * P_pred[static_cast<size_t>(k) * dim + j];
                    P_new[static_cast<size_t>(i) * dim + j] = sum;
                }
            }
        }
        P.swap(P_new);

        std::memcpy(x_filt + static_cast<size_t>(t) * dim, x.data(),
                    sizeof(double) * static_cast<size_t>(dim));
        std::memcpy(P_filt + static_cast<size_t>(t) * dim2, P.data(),
                    sizeof(double) * dim2);

        // Mahalanobis distance: sqrt(v^T S^-1 v), clamped at zero.  S_inv @ v is
        // a matmul in the reference, so it is BLAS-fused; the v . Sv inner
        // product is a plain Python loop, so it is not.
        double quad = 0.0;
        for (int i = 0; i < dim; ++i) {
            const double sindot = (dim == 2)
                ? dot2(S_inv[i * 2 + 0], v[0], S_inv[i * 2 + 1], v[1])
                : matvec(S_inv.data() + static_cast<size_t>(i) * dim, dim, v.data());
            Sv[static_cast<size_t>(i)] = sindot;
            quad += v[static_cast<size_t>(i)] * sindot;
        }
        D[t] = quad > 0.0 ? std::sqrt(quad) : 0.0;
    }

    *out_x_filt = x_filt;
    *out_T1 = T;
    *out_P_filt = P_filt;
    *out_T2 = T;
    *out_D = D;
    *out_T3 = T;
}

}  // namespace tttrlib
// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_QREIGEN_H
#define TTTRLIB_QREIGEN_H

/// QR-based eigendecomposition for real non-symmetric matrices and
/// complex matrix operations (multiply, inverse, matrix-vector).
///
/// The Faddeev-LeVerrier approach used in GopichSzabo is O(n^4) per call,
/// acceptable for n ~ 2-5 (conformational states) but too slow for the
/// ~100x100 combined diffusion-kinetics matrices in BurstML.
///
/// This header implements the standard Francis double-shift QR algorithm
/// (Golub & Van Loan, Algorithm 7.5.2) at O(n^3), plus the complex dense
/// matrix kernels that BurstML needs:
///   - zmatmul:  C = A * B          (complex, n*n)
///   - zmatvec:  y = A * x          (complex matrix * complex vector)
///   - zinv:     inv = A^{-1}       (complex, via LU with partial pivoting)
///
/// All matrices are row-major, matching the rest of tttrlib.

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <vector>

namespace tttrlib {

using cdouble = std::complex<double>;

/// Matrix dimension below which the per-eigenvector and per-column loops run
/// serially: under it the OpenMP fork/join costs more than the work itself.
inline constexpr int QREIGEN_PARALLEL_MIN_N = 32;

// ---------------------------------------------------------------------------
// Complex dense matrix kernels
// ---------------------------------------------------------------------------

/// C = A * B  (all n*n, row-major complex)
inline void zmatmul(const cdouble* A, const cdouble* B, cdouble* C, int n) {
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) C[i * n + j] = cdouble(0.0);
        for (int k = 0; k < n; ++k) {
            cdouble aik = A[i * n + k];
            if (aik == cdouble(0.0)) continue;
            const cdouble* Brow = B + k * n;
            cdouble* Crow = C + i * n;
            for (int j = 0; j < n; ++j)
                Crow[j] += aik * Brow[j];
        }
    }
}

/// y = A * x  (A is n*n row-major complex, x and y are length-n complex)
inline void zmatvec(const cdouble* A, const cdouble* x, cdouble* y, int n) {
    for (int i = 0; i < n; ++i) {
        cdouble s(0.0);
        const cdouble* Arow = A + i * n;
        for (int j = 0; j < n; ++j)
            s += Arow[j] * x[j];
        y[i] = s;
    }
}

/// inv = A^{-1} via LU decomposition with partial pivoting (complex).
/// Returns false if the matrix is (numerically) singular.
inline bool zinv(const cdouble* A_in, cdouble* inv, int n) {
    std::vector<cdouble> LU(A_in, A_in + n * n);
    std::vector<int> piv(n);
    for (int i = 0; i < n; ++i) piv[i] = i;

    for (int k = 0; k < n; ++k) {
        int piv_row = k;
        double best = std::norm(LU[k * n + k]);
        for (int i = k + 1; i < n; ++i) {
            double m = std::norm(LU[i * n + k]);
            if (m > best) { best = m; piv_row = i; }
        }
        if (best < 1e-300) return false;
        if (piv_row != k) {
            std::swap(piv[k], piv[piv_row]);
            for (int j = 0; j < n; ++j) std::swap(LU[k * n + j], LU[piv_row * n + j]);
        }
        cdouble diag_inv = cdouble(1.0) / LU[k * n + k];
        for (int i = k + 1; i < n; ++i) {
            cdouble f = LU[i * n + k] * diag_inv;
            LU[i * n + k] = f;
            for (int j = k + 1; j < n; ++j)
                LU[i * n + j] -= f * LU[k * n + j];
        }
    }

    // Solve LU * x = e_col for each column of the identity. The n solves are
    // independent and are O(n^2) each, so together they are the larger half of
    // the inverse — the factorisation above is only O(n^3/3).
    auto solve_column = [&](int col, std::vector<cdouble>& x) {
        // Forward substitution (L has unit diagonal)
        for (int i = 0; i < n; ++i) {
            cdouble s = (piv[i] == col) ? cdouble(1.0) : cdouble(0.0);
            const cdouble* row = LU.data() + static_cast<size_t>(i) * n;
            for (int j = 0; j < i; ++j) s -= row[j] * x[j];
            x[i] = s;
        }
        // Back substitution (U)
        for (int i = n - 1; i >= 0; --i) {
            const cdouble* row = LU.data() + static_cast<size_t>(i) * n;
            cdouble s = x[i];
            for (int j = i + 1; j < n; ++j) s -= row[j] * x[j];
            x[i] = s / row[i];
        }
        for (int i = 0; i < n; ++i) inv[static_cast<size_t>(i) * n + col] = x[i];
    };

#ifdef _OPENMP
    #pragma omp parallel if (n >= QREIGEN_PARALLEL_MIN_N)
    {
        std::vector<cdouble> x(n);
        #pragma omp for schedule(static)
        for (int col = 0; col < n; ++col) solve_column(col, x);
    }
#else
    std::vector<cdouble> x(n);
    for (int col = 0; col < n; ++col) solve_column(col, x);
#endif
    return true;
}

// ---------------------------------------------------------------------------
// QR-based eigendecomposition for real non-symmetric matrices
// ---------------------------------------------------------------------------

namespace qreigen_detail {

/// Balance a real matrix in-place so that adjacent row/column norms are
/// comparable. Stores the diagonal scaling in ``scale`` (Parlett & Reinsch).
inline void balance(double* A, int n, std::vector<double>& scale) {
    scale.assign(n, 1.0);
    const double radix = 2.0;
    const double b2 = radix * radix;
    bool converged = false;
    while (!converged) {
        converged = true;
        for (int i = 0; i < n; ++i) {
            double r = 0.0, c = 0.0;
            for (int j = 0; j < n; ++j) {
                if (j == i) continue;
                c += std::abs(A[j * n + i]);  // column i
                r += std::abs(A[i * n + j]);  // row i
            }
            if (c == 0.0 || r == 0.0) continue;
            double g = r / radix;
            double f = 1.0;
            double s = c + r;
            while (c < g) { f *= radix; c *= b2; }
            g = r * radix;
            while (c > g) { f /= radix; c /= b2; }
            if ((c + r) / f < 0.95 * s) {
                converged = false;
                g = 1.0 / f;
                scale[i] *= f;
                for (int j = 0; j < n; ++j) A[i * n + j] *= g;
                for (int j = 0; j < n; ++j) A[j * n + i] *= f;
            }
        }
    }
}

/// Reduce A to upper Hessenberg form using Householder reflections.
/// Stores accumulated similarity transform in Z (n*n, row-major).
/// On return, A is upper Hessenberg and Z contains Q such that
/// original_A = Z * H * Z^T.
inline void hessenberg(double* A, int n, double* Z) {
    // Initialize Z = I
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            Z[i * n + j] = (i == j) ? 1.0 : 0.0;

    std::vector<double> v(n);
    for (int k = 0; k < n - 2; ++k) {
        // Build Householder vector for subdiagonal column k, rows k+1..n-1
        double norm_x = 0.0;
        for (int i = k + 1; i < n; ++i)
            norm_x += A[i * n + k] * A[i * n + k];
        norm_x = std::sqrt(norm_x);
        if (norm_x < 1e-300) continue;

        double alpha = -std::copysign(norm_x, A[(k + 1) * n + k]);
        // v = x - alpha*e_1, where x = A[k+1:n, k]
        for (int i = 0; i < n; ++i) v[i] = 0.0;
        v[k + 1] = A[(k + 1) * n + k] - alpha;
        for (int i = k + 2; i < n; ++i) v[i] = A[i * n + k];

        double vnorm2 = 0.0;
        for (int i = k + 1; i < n; ++i) vnorm2 += v[i] * v[i];
        if (vnorm2 < 1e-300) continue;
        double beta = 2.0 / vnorm2;

        // Apply H from left: A = H * A  (rows k+1..n-1)
        for (int j = 0; j < n; ++j) {
            double dot = 0.0;
            for (int i = k + 1; i < n; ++i) dot += v[i] * A[i * n + j];
            double f = beta * dot;
            for (int i = k + 1; i < n; ++i) A[i * n + j] -= f * v[i];
        }
        // Apply H from right: A = A * H  (cols k+1..n-1)
        for (int i = 0; i < n; ++i) {
            double dot = 0.0;
            for (int j = k + 1; j < n; ++j) dot += A[i * n + j] * v[j];
            double f = beta * dot;
            for (int j = k + 1; j < n; ++j) A[i * n + j] -= f * v[j];
        }
        // Accumulate: Z = Z * H
        for (int i = 0; i < n; ++i) {
            double dot = 0.0;
            for (int j = k + 1; j < n; ++j) dot += Z[i * n + j] * v[j];
            double f = beta * dot;
            for (int j = k + 1; j < n; ++j) Z[i * n + j] -= f * v[j];
        }

        // Force exact zero below subdiagonal
        for (int i = k + 2; i < n; ++i) A[i * n + k] = 0.0;
        // Update subdiagonal for numerical consistency
        A[(k + 1) * n + k] = alpha;
    }
}

/// Francis double-shift QR iteration on upper Hessenberg matrix H.
/// Accumulates Schur vectors in Z. Eigenvalues in wr (real) and wi (imag).
/// Returns false if a block failed to deflate within the iteration budget, in
/// which case the eigenvalues below that block are not computed.
inline bool francis_qr(double* H, int n, double* Z,
                       std::vector<double>& wr, std::vector<double>& wi) {
    wr.assign(n, 0.0);
    wi.assign(n, 0.0);
    if (n == 0) return true;
    if (n == 1) { wr[0] = H[0]; return true; }

    const double eps = std::numeric_limits<double>::epsilon();
    int p = n;  // active submatrix is H[0..p-1, 0..p-1]
    int iter_count = 0;
    int max_total_iter = 30 * n;

    while (p > 0) {
        // search for negligible subdiagonal element
        int q = p - 1;
        while (q > 0) {
            double s = std::abs(H[(q - 1) * n + (q - 1)]) + std::abs(H[q * n + q]);
            if (s == 0.0) s = 1.0; // guard against exact zero norm
            if (std::abs(H[q * n + (q - 1)]) <= eps * s) {
                H[q * n + (q - 1)] = 0.0;
                break;
            }
            q--;
        }

        if (q == p - 1) {
            // 1x1 block deflates: real eigenvalue
            wr[p - 1] = H[(p - 1) * n + (p - 1)];
            wi[p - 1] = 0.0;
            p--;
            iter_count = 0;
        } else if (q == p - 2) {
            // 2x2 block deflates: solve quadratic
            double a = H[(p - 2) * n + (p - 2)];
            double b = H[(p - 2) * n + (p - 1)];
            double c = H[(p - 1) * n + (p - 2)];
            double d = H[(p - 1) * n + (p - 1)];
            double tr = a + d;
            double det = a * d - b * c;
            double disc = tr * tr - 4.0 * det;
            if (disc >= 0.0) {
                double sq = std::sqrt(disc);
                double e1 = (tr + sq) / 2.0;
                double e2 = (tr - sq) / 2.0;
                wr[p - 2] = e1; wi[p - 2] = 0.0;
                wr[p - 1] = e2; wi[p - 1] = 0.0;
            } else {
                double sq = std::sqrt(-disc) / 2.0;
                wr[p - 2] = tr / 2.0; wi[p - 2] = sq;
                wr[p - 1] = tr / 2.0; wi[p - 1] = -sq;
            }
            p -= 2;
            iter_count = 0;
        } else {
            // Apply double-shift QR step on active block [q, p-1]
            iter_count++;
            if (iter_count > max_total_iter) return false;

            // Wilkinson double-shift from trailing 2x2
            int nn = p - 1;
            double a = H[(nn - 1) * n + (nn - 1)];
            double b = H[(nn - 1) * n + nn];
            double c = H[nn * n + (nn - 1)];
            double d = H[nn * n + nn];
            double s_shift = a + d;  // trace
            double t_shift = a * d - b * c;  // det

            // Exceptional shift (LAPACK dlahqr's, DAT1 = 0.75, DAT2 = -0.4375).
            // A matrix whose eigenvalues sit symmetrically about the trailing
            // 2x2 — a cyclic permutation is the standard example — is a fixed
            // point of the Wilkinson shift: the same shift comes back every
            // iteration and nothing ever deflates. Perturbing the shift every
            // tenth iteration breaks that symmetry.
            if (iter_count % 10 == 0) {
                double sx = std::abs(H[nn * n + (nn - 1)]);
                if (nn - 2 >= q) sx += std::abs(H[(nn - 1) * n + (nn - 2)]);
                if (sx > 0.0) {
                    const double h11 = 0.75 * sx + H[nn * n + nn];
                    const double h12 = -0.4375 * sx;
                    const double h21 = sx;
                    const double h22 = h11;
                    s_shift = h11 + h22;
                    t_shift = h11 * h22 - h12 * h21;
                }
            }

            // first column of (H - s*I)(H - t*I) restricted to [q..q+2]
            double h00 = H[q * n + q];
            double h01 = H[q * n + (q + 1)];
            double h10 = H[(q + 1) * n + q];
            double h11 = H[(q + 1) * n + (q + 1)];

            double x = h00 * h00 + h00 * (a + d - s_shift) + t_shift
                       + h01 * h10 - h00 * (a + d);
            // Simplified: x = H[q,q]^2 + H[q,q]*(a+d) + H[q,q+1]*H[q+1,q] - s_shift*H[q,q] + t_shift
            // Actually, the standard formula:
            // M = H^2 - s*H + t*I  (restricted to first column)
            // M[0,0] = h00^2 + h01*h10 - s*h00 + t
            x = h00 * h00 + h01 * h10 - s_shift * h00 + t_shift;
            // M[1,0] = h10*(h00 + h11 - s)
            double y = h10 * (h00 + h11 - s_shift);
            // M[2,0] = h10 * H[q+2, q+1]
            double z2 = h10 * H[(q + 2) * n + (q + 1)];

            // Bulge chase: apply Householder transforms of size 3
            for (int k = q; k < p - 2; ++k) {
                // Build 3-element Householder vector [x, y, z]
                double norm3 = std::sqrt(x * x + y * y + z2 * z2);
                if (norm3 < 1e-300) continue;

                double sgn = (x >= 0.0) ? 1.0 : -1.0;
                double alpha = -sgn * norm3;
                double v0 = x - alpha;
                double v1 = y;
                double v2 = z2;
                double vnorm2 = v0 * v0 + v1 * v1 + v2 * v2;
                if (vnorm2 < 1e-300) continue;
                double beta = 2.0 / vnorm2;

                // Row index range for the transform: [k, k+1, k+2]
                // Apply from left to rows k, k+1, k+2 (columns from max(q, k-1) to p-1)
                int col_start = (k > q) ? k - 1 : q;
                for (int j = col_start; j < p; ++j) {
                    double d0 = H[k * n + j];
                    double d1 = H[(k + 1) * n + j];
                    double d2 = H[(k + 2) * n + j];
                    double dot = v0 * d0 + v1 * d1 + v2 * d2;
                    double f = beta * dot;
                    H[k * n + j]       = d0 - f * v0;
                    H[(k + 1) * n + j] = d1 - f * v1;
                    H[(k + 2) * n + j] = d2 - f * v2;
                }

                // Apply from right to columns k, k+1, k+2 (rows q to min(k+3, p-1))
                int row_end = std::min(k + 4, p);
                for (int i = q; i < row_end; ++i) {
                    double d0 = H[i * n + k];
                    double d1 = H[i * n + (k + 1)];
                    double d2 = H[i * n + (k + 2)];
                    double dot = v0 * d0 + v1 * d1 + v2 * d2;
                    double f = beta * dot;
                    H[i * n + k]       = d0 - f * v0;
                    H[i * n + (k + 1)] = d1 - f * v1;
                    H[i * n + (k + 2)] = d2 - f * v2;
                }

                // Accumulate into Z: Z = Z * H_k. Z is null when the caller
                // does not need Schur vectors.
                for (int i = 0; i < (Z ? n : 0); ++i) {
                    double d0 = Z[i * n + k];
                    double d1 = Z[i * n + (k + 1)];
                    double d2 = Z[i * n + (k + 2)];
                    double dot = v0 * d0 + v1 * d1 + v2 * d2;
                    double f = beta * dot;
                    Z[i * n + k]       = d0 - f * v0;
                    Z[i * n + (k + 1)] = d1 - f * v1;
                    Z[i * n + (k + 2)] = d2 - f * v2;
                }

                // Update x, y, z for next iteration
                x = H[(k + 1) * n + k];
                y = H[(k + 2) * n + k];
                if (k < p - 3) z2 = H[(k + 3) * n + k];
            }

            // Final 2-element Householder
            {
                int k = p - 2;
                double norm2 = std::sqrt(x * x + y * y);
                if (norm2 > 1e-300) {
                    double sgn = (x >= 0.0) ? 1.0 : -1.0;
                    double alpha = -sgn * norm2;
                    double v0 = x - alpha;
                    double v1 = y;
                    double vnorm2 = v0 * v0 + v1 * v1;
                    if (vnorm2 > 1e-300) {
                        double beta = 2.0 / vnorm2;

                        int col_start = (k > q) ? k - 1 : q;
                        for (int j = col_start; j < p; ++j) {
                            double d0 = H[k * n + j];
                            double d1 = H[(k + 1) * n + j];
                            double f = beta * (v0 * d0 + v1 * d1);
                            H[k * n + j]       = d0 - f * v0;
                            H[(k + 1) * n + j] = d1 - f * v1;
                        }
                        int row_end = p;
                        for (int i = q; i < row_end; ++i) {
                            double d0 = H[i * n + k];
                            double d1 = H[i * n + (k + 1)];
                            double f = beta * (v0 * d0 + v1 * d1);
                            H[i * n + k]       = d0 - f * v0;
                            H[i * n + (k + 1)] = d1 - f * v1;
                        }
                        for (int i = 0; i < (Z ? n : 0); ++i) {
                            double d0 = Z[i * n + k];
                            double d1 = Z[i * n + (k + 1)];
                            double f = beta * (v0 * d0 + v1 * d1);
                            Z[i * n + k]       = d0 - f * v0;
                            Z[i * n + (k + 1)] = d1 - f * v1;
                        }
                    }
                }
            }
        }
    }
    return true;
}

/// Eigenvectors of a real matrix by inverse iteration on ``A - lambda*I``,
/// in complex arithmetic. On return, ``evecs[i*n + j]`` is component i of
/// eigenvector j, normalised to unit length.
///
/// Inverse iteration is what LAPACK does, and for a reason. Reading the null
/// vector straight off the elimination cannot work here: with lambda an
/// eigenvalue, ``A - lambda*I`` is singular only up to rounding, so the
/// "zero" pivot is a rounding-level number whose *value* is noise, and the
/// back-substituted vector inherits that noise in every component. One solve
/// against a perturbed factorisation instead amplifies the null direction by
/// 1/eps and drowns the rest; two more solves clean up what is left.
///
/// The pivot floor is the whole mechanism, not a guard: a pivot smaller than
/// eps*||A|| is replaced by that floor, which is precisely the perturbation
/// that turns the singular solve into a large, well-defined step along the
/// eigenvector.
/// @param Hess  the upper Hessenberg form, n*n row-major
/// @param Q     the orthogonal factor with ``A = Q * Hess * Q^T``
///
/// Iterating on the Hessenberg form rather than on A is what keeps this
/// affordable. A dense LU costs O(n^3) per eigenvalue and so O(n^4) for the
/// basis; a Hessenberg LU has exactly one nonzero to eliminate per column, so
/// it costs O(n^2), and the whole basis costs O(n^3) — the same order as the
/// QR iteration that produced the eigenvalues. At n = 100 that is the
/// difference between 42 ms and under 1 ms.
inline void compute_eigenvectors(
    const double* Hess, const double* Q, int n,
    const std::vector<double>& wr, const std::vector<double>& wi,
    cdouble* evecs
) {
    using cdbl = std::complex<double>;
    const double eps = std::numeric_limits<double>::epsilon();

    double anorm = 0.0;
    for (int i = 0; i < n; ++i) {
        double rowsum = 0.0;
        for (int j = 0; j < n; ++j) rowsum += std::abs(Hess[i * n + j]);
        anorm = std::max(anorm, rowsum);
    }
    if (anorm == 0.0) anorm = 1.0;
    const double pivot_floor = eps * anorm;

    // A repeated eigenvalue breaks plain inverse iteration: every copy solves
    // the same singular system from the same start, so all of them converge to
    // the same vector and the eigenvector matrix is rank deficient — even when
    // the true eigenspace is the whole space. The zero generator of a
    // disconnected kinetic scheme is the canonical case: one zero eigenvalue
    // per component, full eigenspace, and the best-conditioned input there is.
    // LAPACK's dhsein handles it by orthogonalising each iterate against the
    // vectors already found for that eigenvalue; same here. Columns whose
    // eigenvalues coincide numerically are grouped, and only groups of one —
    // the usual case — stay on the parallel path. A defective eigenvalue has
    // no second eigenvector to find, so its group still comes back near
    // parallel and the caller's condition-number gate rejects it as before.
    const double cluster_tol = 100.0 * n * eps * anorm;
    std::vector<int> head(n);
    std::vector<int> group_size(n, 0);
    bool clustered = false;
    for (int c = 0; c < n; ++c) {
        head[c] = c;
        for (int j = 0; j < c; ++j) {
            if (std::hypot(wr[c] - wr[j], wi[c] - wi[j]) <= cluster_tol) {
                head[c] = head[j];
                clustered = true;
                break;
            }
        }
        group_size[head[c]]++;
    }
    // Unit eigenvectors in the Hessenberg basis, kept only for grouped columns
    // so later members of the group have something to orthogonalise against.
    std::vector<std::vector<cdbl>> basis(n);

    // Per-eigenvalue scratch. Each eigenvector is independent of every other,
    // so this loop parallelises with no sharing beyond the read-only inputs;
    // the buffers are per thread rather than per eigenvalue so the factorisation
    // does not pay for an allocation.
    struct Scratch {
        std::vector<cdbl> LU, mult, v, w;
        std::vector<char> swapped;
        explicit Scratch(int n_)
            : LU(static_cast<size_t>(n_) * n_, cdbl(0.0)), mult(n_), v(n_), w(n_),
              swapped(n_, 0) {}
    };

    auto eigenvector_of = [&](int col, Scratch& s, int member, bool keep) {
        std::vector<cdbl>& LU = s.LU;
        std::vector<cdbl>& mult = s.mult;
        std::vector<char>& swapped = s.swapped;
        std::vector<cdbl>& v = s.v;
        std::vector<cdbl>& w = s.w;
        const cdbl lambda(wr[col], wi[col]);

        // Only the Hessenberg part is refreshed: everything strictly below the
        // subdiagonal is zero on entry and the factorisation never writes there,
        // so re-zeroing it for every eigenvalue would be half the copy for
        // nothing.
        for (int i = 0; i < n; ++i) {
            const int jlo = (i > 0) ? i - 1 : 0;
            for (int j = jlo; j < n; ++j)
                LU[static_cast<size_t>(i) * n + j] =
                    cdbl(Hess[i * n + j]) - (i == j ? lambda : cdbl(0.0));
        }

        // Hessenberg LU: column k has a single entry below the diagonal, so a
        // pivot is a comparison of two numbers and elimination touches one row.
        for (int k = 0; k < n - 1; ++k) {
            cdbl* rk = LU.data() + static_cast<size_t>(k) * n;
            cdbl* rk1 = LU.data() + static_cast<size_t>(k + 1) * n;
            if (std::abs(rk1[k]) > std::abs(rk[k])) {
                swapped[k] = 1;
                for (int j = k; j < n; ++j) std::swap(rk[j], rk1[j]);
            } else {
                swapped[k] = 0;
            }
            if (std::abs(rk[k]) < pivot_floor) rk[k] = cdbl(pivot_floor);
            const cdbl f = rk1[k] / rk[k];
            mult[k] = f;
            rk1[k] = cdbl(0.0);
            if (f != cdbl(0.0))
                for (int j = k + 1; j < n; ++j) rk1[j] -= f * rk[j];
        }
        {
            cdbl& last = LU[static_cast<size_t>(n - 1) * n + (n - 1)];
            if (std::abs(last) < pivot_floor) last = cdbl(pivot_floor);
        }

        // Start from a vector with no symmetry an eigenvector is likely to be
        // orthogonal to, scaled so the first solve cannot overflow. Later
        // members of an eigenvalue group start from a shifted ramp: iterating
        // the same singular system from the same point can only return the
        // same vector.
        auto iterate = [&](bool ortho) {
            const double start = 1.0 / std::sqrt(static_cast<double>(n));
            for (int i = 0; i < n; ++i) {
                const int r = (i + member) % n;
                v[i] = cdbl(start * (1.0 + 0.1 * r), start * 0.01 * (r + 1));
            }
            for (int it = 0; it < 3; ++it) {
                w = v;
                for (int k = 0; k < n - 1; ++k) {   // forward: L is bidiagonal
                    if (swapped[k]) std::swap(w[k], w[k + 1]);
                    w[k + 1] -= mult[k] * w[k];
                }
                for (int i = n - 1; i >= 0; --i) {  // back: U is dense triangular
                    const cdbl* row = LU.data() + static_cast<size_t>(i) * n;
                    cdbl s = w[i];
                    for (int j = i + 1; j < n; ++j) s -= row[j] * w[j];
                    w[i] = s / row[i];
                }
                // Project out what the group has already found, so this copy
                // of the eigenvalue converges to a new direction in its
                // eigenspace.
                if (ortho) {
                    for (int j = 0; j < col; ++j) {
                        if (head[j] != head[col] || basis[j].empty()) continue;
                        const std::vector<cdbl>& u = basis[j];
                        cdbl proj(0.0);
                        for (int i = 0; i < n; ++i) proj += std::conj(u[i]) * w[i];
                        for (int i = 0; i < n; ++i) w[i] -= proj * u[i];
                    }
                }
                double nrm = 0.0;
                for (int i = 0; i < n; ++i) nrm += std::norm(w[i]);
                nrm = std::sqrt(nrm);
                if (!(nrm > 0.0) || !std::isfinite(nrm)) break;
                for (int i = 0; i < n; ++i) v[i] = w[i] / nrm;
            }
        };
        iterate(member > 0);

        // A defective eigenvalue has fewer eigenvectors than copies, and
        // projection then manufactures a direction that is not one: the
        // residual says so. Fall back to the unprojected iterate, which is
        // near parallel to the ones before it, so the rank-deficient basis is
        // rejected by the caller's condition gate instead of being returned
        // as a diagonalisation that does not reconstruct A.
        if (member > 0) {
            double rnorm = 0.0;
            for (int i = 0; i < n; ++i) {
                cdbl s(0.0);
                const int jlo = (i > 0) ? i - 1 : 0;
                for (int j = jlo; j < n; ++j) s += cdbl(Hess[i * n + j]) * v[j];
                s -= lambda * v[i];
                rnorm += std::norm(s);
            }
            if (std::sqrt(rnorm) > anorm * std::sqrt(eps)) iterate(false);
        }
        if (keep) basis[col].assign(v.begin(), v.end());

        // Back to the original basis: eigenvector of A is Q * (that of Hess).
        for (int i = 0; i < n; ++i) {
            cdbl s(0.0);
            const double* qrow = Q + static_cast<size_t>(i) * n;
            for (int j = 0; j < n; ++j) s += qrow[j] * v[j];
            w[i] = s;
        }

        // Fix the phase so a real eigenvalue gets a real eigenvector and a
        // conjugate pair gets conjugate vectors: rotate the largest component
        // onto the positive real axis.
        int imax = 0;
        double vmax = 0.0;
        for (int i = 0; i < n; ++i) {
            const double m = std::abs(w[i]);
            if (m > vmax) { vmax = m; imax = i; }
        }
        const cdbl phase = (vmax > 0.0) ? std::conj(w[imax]) / vmax : cdbl(1.0);
        for (int i = 0; i < n; ++i) evecs[static_cast<size_t>(i) * n + col] = w[i] * phase;
    };

    // Group heads and singletons are mutually independent and run in
    // parallel; later members of a group need the vectors found before them,
    // so they run in order afterwards. With no repeated eigenvalue this is
    // exactly the old loop.
#ifdef _OPENMP
    // Below a few dozen states the fork/join costs more than the work — a
    // 10x10 decomposition is three times slower on eight threads than on one.
    #pragma omp parallel if (n >= QREIGEN_PARALLEL_MIN_N)
    {
        Scratch s(n);
        #pragma omp for schedule(static)
        for (int col = 0; col < n; ++col)
            if (head[col] == col) eigenvector_of(col, s, 0, group_size[col] > 1);
    }
#else
    {
        Scratch s(n);
        for (int col = 0; col < n; ++col)
            if (head[col] == col) eigenvector_of(col, s, 0, group_size[col] > 1);
    }
#endif
    if (clustered) {
        Scratch s(n);
        for (int col = 0; col < n; ++col) {
            if (head[col] == col) continue;
            int member = 0;
            for (int j = 0; j < col; ++j) if (head[j] == head[col]) ++member;
            eigenvector_of(col, s, member, true);
        }
    }
}

} // namespace qreigen_detail

/// Full eigendecomposition of a real non-symmetric matrix.
///
/// @param A_real  Row-major n*n real matrix (copied internally, not modified)
/// @param n       Matrix dimension
/// @param evals   Output: n complex eigenvalues
/// @param evecs   Output: n*n complex eigenvectors (column j = eigenvector j)
/// @param inv     Output: n*n inverse of eigenvector matrix
/// @return        true on success, false if eigenvector matrix is too ill-conditioned
inline bool qr_eigendecompose(
    const double* A_real, int n,
    std::vector<cdouble>& evals,
    std::vector<cdouble>& evecs,
    std::vector<cdouble>& inv
) {
    std::vector<double> H(A_real, A_real + n * n);
    std::vector<double> Z(static_cast<size_t>(n) * n);
    std::vector<double> scale;

    // Balance: H becomes D^-1 A D with D = diag(scale).
    qreigen_detail::balance(H.data(), n, scale);

    // Hessenberg reduction: A_balanced = Z * H * Z^T
    qreigen_detail::hessenberg(H.data(), n, Z.data());
    const std::vector<double> Hess(H);

    // Francis double-shift QR. The Schur vectors are not used — eigenvectors
    // come from inverse iteration on the Hessenberg form — so accumulation is
    // switched off, which is an O(n^3) saving in the iteration itself.
    std::vector<double> wr, wi;
    if (!qreigen_detail::francis_qr(H.data(), n, nullptr, wr, wi)) return false;

    // Assemble eigenvalues. A similarity transform does not move them, so
    // these are the eigenvalues of A itself.
    evals.resize(n);
    for (int i = 0; i < n; ++i)
        evals[i] = cdouble(wr[i], wi[i]);

    // Eigenvectors of the *balanced* matrix, then transformed back. Doing it
    // on the balanced matrix is the point of balancing: a matrix whose entries
    // span many decades has an eigenvector basis of the same condition number,
    // and inverting that basis loses every digit. Balanced, the basis is well
    // conditioned; the decades go back in afterwards as an exact diagonal.
    evecs.resize(static_cast<size_t>(n) * n);
    qreigen_detail::compute_eigenvectors(Hess.data(), Z.data(), n, wr, wi, evecs.data());

    // inv(D Y) = inv(Y) D^-1: invert the well-conditioned factor, then apply
    // the diagonal, rather than inverting the product.
    inv.resize(static_cast<size_t>(n) * n);
    if (!zinv(evecs.data(), inv.data(), n)) return false;
    for (int i = 0; i < n; ++i) {
        const double s = scale[i];
        for (int j = 0; j < n; ++j) {
            evecs[static_cast<size_t>(i) * n + j] *= s;   // row i of V = D Y
            inv[static_cast<size_t>(j) * n + i] /= s;     // column i of inv(Y) D^-1
        }
    }
    return true;
}

} // namespace tttrlib

#endif // TTTRLIB_QREIGEN_H

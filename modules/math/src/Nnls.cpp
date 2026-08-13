// SPDX-License-Identifier: BSD-3-Clause
#include "Nnls.h"
#include "Mat.h"

#include <algorithm>
#include <limits>

namespace tttrlib {

namespace {

// residual = b - A x, over the m rows of the n-col row-major A.
void compute_residual(const std::vector<double>& A, const std::vector<double>& b,
                       const std::vector<double>& x, int m, int n,
                       std::vector<double>& residual) {
    for (int i = 0; i < m; ++i) {
        double s = 0.0;
        const double* row = &A[static_cast<size_t>(i) * n];
        for (int j = 0; j < n; ++j) s += row[j] * x[j];
        residual[i] = b[i] - s;
    }
}

// w = A^T residual.
void compute_gradient(const std::vector<double>& A, const std::vector<double>& residual,
                       int m, int n, std::vector<double>& w) {
    std::fill(w.begin(), w.end(), 0.0);
    for (int i = 0; i < m; ++i) {
        const double r = residual[i];
        if (r == 0.0) continue;
        const double* row = &A[static_cast<size_t>(i) * n];
        for (int j = 0; j < n; ++j) w[j] += row[j] * r;
    }
}

} // anonymous namespace

std::vector<double> nnls(
    const std::vector<double>& A, const std::vector<double>& b,
    int m, int n, int max_iter, double tol
) {
    if (max_iter <= 0) max_iter = 3 * n;
    std::vector<double> x(n, 0.0);
    if (n == 0 || m == 0) return x;

    std::vector<char> in_p(n, 0);   // passive-set membership
    std::vector<int> p_idx;         // passive-set indices, in insertion order
    std::vector<double> residual(m), w(n);

    compute_residual(A, b, x, m, n, residual);
    compute_gradient(A, residual, m, n, w);

    int outer = 0;
    while (outer < max_iter) {
        // Pick the active-set index with the largest gradient component.
        int t = -1;
        double best = tol;
        for (int j = 0; j < n; ++j) {
            if (in_p[j]) continue;
            if (w[j] > best) { best = w[j]; t = j; }
        }
        if (t < 0) break;  // dual-feasible: every active-set gradient <= tol

        in_p[t] = 1;
        p_idx.push_back(t);
        ++outer;

        // Inner loop: solve the unconstrained LS problem on the passive set,
        // then repair any non-positive entries by backtracking toward the
        // last feasible x and dropping the offending index(es) back to Z.
        for (int inner = 0; inner < n + 1; ++inner) {
            const int np = static_cast<int>(p_idx.size());
            std::vector<double> Ap(static_cast<size_t>(m) * np);
            for (int i = 0; i < m; ++i)
                for (int k = 0; k < np; ++k)
                    Ap[static_cast<size_t>(i) * np + k] = A[static_cast<size_t>(i) * n + p_idx[k]];
            std::vector<double> zp(b.begin(), b.begin() + m);
            mat_lstsq_minnorm(Ap, zp, m, np);

            std::vector<double> z(n, 0.0);
            for (int k = 0; k < np; ++k) z[p_idx[k]] = zp[k];

            bool feasible = true;
            for (int k = 0; k < np; ++k) if (z[p_idx[k]] <= 0.0) { feasible = false; break; }
            if (feasible) {
                x = z;
                break;
            }

            // alpha = min over passive j with z_j <= 0 of x_j / (x_j - z_j).
            double alpha = std::numeric_limits<double>::infinity();
            for (int k = 0; k < np; ++k) {
                const int j = p_idx[k];
                if (z[j] <= 0.0) {
                    const double denom = x[j] - z[j];
                    if (denom > 0.0) alpha = std::min(alpha, x[j] / denom);
                }
            }
            if (!(alpha < std::numeric_limits<double>::infinity())) alpha = 0.0;

            for (int j = 0; j < n; ++j) x[j] += alpha * (z[j] - x[j]);

            // Drop every passive index that landed at (numerically) zero.
            std::vector<int> kept;
            kept.reserve(p_idx.size());
            for (int j : p_idx) {
                if (x[j] <= 1e-12) { in_p[j] = 0; x[j] = 0.0; }
                else kept.push_back(j);
            }
            p_idx.swap(kept);
            if (p_idx.empty()) break;
        }

        compute_residual(A, b, x, m, n, residual);
        compute_gradient(A, residual, m, n, w);
    }

    return x;
}

} // namespace tttrlib

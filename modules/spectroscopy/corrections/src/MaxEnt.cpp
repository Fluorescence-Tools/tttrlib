// SPDX-License-Identifier: BSD-3-Clause
#include "MaxEnt.h"

#include <cmath>
#include <algorithm>
#include <limits>

namespace tttrlib {

// Solve min_x ||Ax - b||^2 - l2 * S(x) with S(x) = sum x ln(prior*x) - x
// (Shannon entropy), gradient 2 A^T (Ax-b) - l2*(1 + ln(prior*x)).
// Projected gradient with backtracking line search, matching the chisurf
// objective (mem.py) up to the L-BFGS-B optimizer choice.
std::vector<double> maxent_invert(
    const std::vector<double>& A,
    const std::vector<double>& b,
    double nu,
    int n_rows, int n_cols,
    int max_iter, double tol
) {
    const double l2 = nu * nu;           // (nu * reg_scale)^2, reg_scale = 1
    const double xmin = 1e-12;           // positivity bound

    // Start close to the least-squares amplitude, slightly positive.
    std::vector<double> x(n_cols, xmin);
    // prior = ones (uniform), as in chisurf default
    std::vector<double> prior(n_cols, 1.0);

    std::vector<double> residual(n_rows);
    std::vector<double> grad(n_cols);
    std::vector<double> AtAx1(n_rows);

    // Barzilai-Borwein-ish starting step guided by the chi2 Hessian scale.
    double sum_diag = 0.0;
    for (int j = 0; j < n_cols; ++j) {
        double a = 0.0;
        for (int i = 0; i < n_rows; ++i) a += A[i * n_cols + j] * A[i * n_cols + j];
        sum_diag += a;
    }
    double lr = (sum_diag > 0.0) ? 1.0 / sum_diag : 1e-4;

    for (int iter = 0; iter < max_iter; ++iter) {
        // r = A x - b
        for (int i = 0; i < n_rows; ++i) {
            double s = 0.0;
            for (int j = 0; j < n_cols; ++j) s += A[i * n_cols + j] * x[j];
            residual[i] = s - b[i];
        }

        // gradient = 2 A^T r - l2*(1 + ln(prior*x))
        double gnorm = 0.0;
        for (int j = 0; j < n_cols; ++j) {
            double g = 0.0;
            for (int i = 0; i < n_rows; ++i)
                g += A[i * n_cols + j] * residual[i];
            double px = std::max(prior[j] * x[j], xmin);
            grad[j] = 2.0 * g - l2 * (1.0 + std::log(px));
            gnorm += grad[j] * grad[j];
        }
        gnorm = std::sqrt(gnorm);
        if (gnorm < tol) break;

        // Backtracking line search on the objective
        double obj_old = 0.0;
        for (int i = 0; i < n_rows; ++i) obj_old += residual[i] * residual[i];
        double S_old = 0.0;
        for (int j = 0; j < n_cols; ++j) {
            double px = std::max(prior[j] * x[j], xmin);
            S_old += x[j] * std::log(px) - x[j];
        }
        obj_old -= l2 * S_old;

        std::vector<double> x_new(n_cols);
        double step = lr;
        bool accepted = false;
        for (int ls = 0; ls < 40; ++ls) {
            for (int j = 0; j < n_cols; ++j)
                x_new[j] = std::max(x[j] - step * grad[j], xmin);

            double obj_new = 0.0;
            for (int i = 0; i < n_rows; ++i) {
                double s = 0.0;
                for (int j = 0; j < n_cols; ++j) s += A[i * n_cols + j] * x_new[j];
                double r = s - b[i];
                obj_new += r * r;
            }
            double S_new = 0.0;
            for (int j = 0; j < n_cols; ++j) {
                double px = std::max(prior[j] * x_new[j], xmin);
                S_new += x_new[j] * std::log(px) - x_new[j];
            }
            obj_new -= l2 * S_new;

            if (obj_new < obj_old) { accepted = true; break; }
            step *= 0.5;
        }
        lr = accepted ? step * 2.0 : lr * 0.5;  // grow cautiously on success
        x = x_new;
    }

    return x;
}

} // namespace tttrlib
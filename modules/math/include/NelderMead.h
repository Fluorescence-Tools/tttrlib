// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_NELDER_MEAD_H
#define TTTRLIB_NELDER_MEAD_H

/// Header-only Nelder-Mead simplex optimizer.
///
/// A derivative-free minimiser suitable for noisy or non-smooth objective
/// functions. This replaces GSL's ``gsl_multimin_fminimizer_nmsimplex2`` used
/// in the original FRET_burstML MEX code, with no external dependency.
///
/// The algorithm maintains a simplex of n+1 points in n dimensions and
/// iteratively replaces the worst vertex via reflection, expansion, and
/// contraction until the simplex collapses below a size threshold.
///
/// Convergence codes (matching the i_lbfgs convention):
///   0 = aborted (max iterations), 2 = step below tol_x, 3 = simplex size below tol.

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

namespace tttrlib {

struct NelderMeadResult {
    std::vector<double> x;       // best vertex found
    double fval;                 // objective at best vertex
    int iterations;              // iterations performed
    int status;                  // convergence code
};

/// @brief Minimise f(x) using Nelder-Mead simplex.
///
/// @param f        Objective function (returns double, takes vector<double>)
/// @param x0       Initial guess (n-dimensional)
/// @param lb       Lower bounds (size n). Use -inf for unconstrained.
/// @param ub       Upper bounds (size n). Use +inf for unconstrained.
/// @param init_step Initial simplex edge length per dimension
/// @param max_iter  Maximum iterations (default 2000)
/// @param tol_x     Convergence: simplex size below this (default 1e-3)
/// @param tol_f     Convergence: function range below this (default 1e-6)
/// @return          NelderMeadResult with best vertex and status
inline NelderMeadResult nelder_mead(
    const std::function<double(const std::vector<double>&)>& f,
    const std::vector<double>& x0,
    const std::vector<double>& lb,
    const std::vector<double>& ub,
    const std::vector<double>& init_step,
    int max_iter = 2000,
    double tol_x = 1e-3,
    double tol_f = 1e-6
) {
    const int n = static_cast<int>(x0.size());
    NelderMeadResult result;
    result.x = x0;
    result.fval = std::numeric_limits<double>::infinity();
    result.iterations = 0;
    result.status = 0;

    if (n == 0) { result.status = -1; return result; }

    // standard Nelder-Mead coefficients
    const double alpha = 1.0;   // reflection
    const double gamma = 2.0;   // expansion
    const double rho   = 0.5;   // contraction
    const double sigma = 0.5;   // shrink

    auto clamp = [&](std::vector<double>& v) {
        for (int i = 0; i < n; ++i) {
            if (v[i] < lb[i]) v[i] = lb[i];
            if (v[i] > ub[i]) v[i] = ub[i];
        }
    };

    // build initial simplex: n+1 vertices
    std::vector<std::vector<double>> simplex(n + 1, x0);
    std::vector<double> fvals(n + 1);

    for (int i = 0; i < n; ++i) {
        simplex[i + 1][i] += init_step[i];
        clamp(simplex[i + 1]);
    }
    for (int i = 0; i <= n; ++i)
        fvals[i] = f(simplex[i]);

    for (int iter = 0; iter < max_iter; ++iter) {
        result.iterations = iter + 1;

        // sort vertices by f value
        std::vector<int> idx(n + 1);
        for (int i = 0; i <= n; ++i) idx[i] = i;
        std::sort(idx.begin(), idx.end(), [&](int a, int b) { return fvals[a] < fvals[b]; });

        int best  = idx[0];
        int worst = idx[n];
        int second_worst = idx[n - 1];

        // convergence check: simplex size and function range
        double max_dist = 0.0;
        for (int i = 1; i <= n; ++i) {
            double d = 0.0;
            for (int j = 0; j < n; ++j) {
                double diff = simplex[idx[i]][j] - simplex[best][j];
                d += diff * diff;
            }
            max_dist = std::max(max_dist, d);
        }
        double frange = std::abs(fvals[worst] - fvals[best]);
        if (std::sqrt(max_dist) < tol_x || frange < tol_f) {
            result.x = simplex[best];
            result.fval = fvals[best];
            result.status = (frange < tol_f) ? 3 : 2;
            return result;
        }

        // centroid of all but worst
        std::vector<double> centroid(n, 0.0);
        for (int i = 0; i <= n; ++i) {
            if (i == worst) continue;
            for (int j = 0; j < n; ++j) centroid[j] += simplex[i][j];
        }
        for (int j = 0; j < n; ++j) centroid[j] /= n;

        // reflection
        std::vector<double> xr(n);
        for (int j = 0; j < n; ++j)
            xr[j] = centroid[j] + alpha * (centroid[j] - simplex[worst][j]);
        clamp(xr);
        double fr = f(xr);

        if (fr < fvals[best]) {
            // expansion
            std::vector<double> xe(n);
            for (int j = 0; j < n; ++j)
                xe[j] = centroid[j] + gamma * (xr[j] - centroid[j]);
            clamp(xe);
            double fe = f(xe);
            if (fe < fr) {
                simplex[worst] = xe;
                fvals[worst] = fe;
            } else {
                simplex[worst] = xr;
                fvals[worst] = fr;
            }
        } else if (fr < fvals[second_worst]) {
            simplex[worst] = xr;
            fvals[worst] = fr;
        } else {
            // contraction
            std::vector<double> xc(n);
            for (int j = 0; j < n; ++j)
                xc[j] = centroid[j] + rho * (simplex[worst][j] - centroid[j]);
            clamp(xc);
            double fc = f(xc);
            if (fc < fvals[worst]) {
                simplex[worst] = xc;
                fvals[worst] = fc;
            } else {
                // shrink toward best
                for (int i = 0; i <= n; ++i) {
                    if (i == best) continue;
                    for (int j = 0; j < n; ++j)
                        simplex[i][j] = simplex[best][j] + sigma * (simplex[i][j] - simplex[best][j]);
                    clamp(simplex[i]);
                    fvals[i] = f(simplex[i]);
                }
            }
        }
    }

    // return best after max_iter
    int best = 0;
    for (int i = 1; i <= n; ++i)
        if (fvals[i] < fvals[best]) best = i;
    result.x = simplex[best];
    result.fval = fvals[best];
    result.status = 0;
    return result;
}

} // namespace tttrlib

#endif // TTTRLIB_NELDER_MEAD_H

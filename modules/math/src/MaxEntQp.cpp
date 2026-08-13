// SPDX-License-Identifier: BSD-3-Clause
#include "MaxEntQp.h"
#include "Mat.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace tttrlib {

namespace {

constexpr double LOG_FLOOR = -1e300;

double log_clip(double v) {
    return v > 1e-300 ? std::log(v) : LOG_FLOOR;
}

} // anonymous namespace

std::vector<double> quadpr_bound(
    const std::vector<double>& C_in, const std::vector<double>& d_in,
    double lower_bound
) {
    int n = static_cast<int>(d_in.size());
    std::vector<double> C(C_in);
    // Symmetrise
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < i; ++j) {
            double s = 0.5 * (C[static_cast<size_t>(i) * n + j]
                              + C[static_cast<size_t>(j) * n + i]);
            C[static_cast<size_t>(i) * n + j] = s;
            C[static_cast<size_t>(j) * n + i] = s;
        }
    double lb = lower_bound;
    std::vector<double> x(n, 0.0);
    std::vector<char> active(n, 0);

    for (int sweep = 0; sweep < 50; ++sweep) {
        // Gather free indices
        std::vector<int> free_idx;
        for (int i = 0; i < n; ++i) if (!active[i]) free_idx.push_back(i);
        int nf = static_cast<int>(free_idx.size());
        if (nf > 0) {
            std::vector<double> Cff(static_cast<size_t>(nf) * nf);
            std::vector<double> df(nf);
            for (int a = 0; a < nf; ++a) {
                df[a] = d_in[free_idx[a]];
                for (int b = 0; b < nf; ++b)
                    Cff[static_cast<size_t>(a) * nf + b] =
                        C[static_cast<size_t>(free_idx[a]) * n + free_idx[b]];
            }
            for (int a = 0; a < nf; ++a) df[a] = -df[a];
            // mat_solve eliminates in place, so the fallback needs the system
            // as it was: copy first, or least squares gets handed a half-
            // triangularised matrix and a partly updated right-hand side.
            std::vector<double> Cff_tmp(Cff), df_tmp(df);
            if (!mat_solve(Cff, df, nf)) {
                // Near-singular free block: fall back to min-norm least squares
                // (matches numpy's np.linalg.lstsq fallback in chisurf).
                mat_lstsq_minnorm(Cff_tmp, df_tmp, nf, nf);
                df = std::move(df_tmp);
            }
            for (int a = 0; a < nf; ++a) x[free_idx[a]] = df[a];
        }
        // Enforce bound on active set
        for (int i = 0; i < n; ++i) if (active[i]) x[i] = lb;

        bool any_new = false;
        for (int i = 0; i < n; ++i) {
            if (x[i] < lb && !active[i]) { active[i] = 1; any_new = true; }
        }
        if (!any_new) break;
    }
    for (int i = 0; i < n; ++i) if (x[i] < lb) x[i] = lb;
    return x;
}

MaxEntResult run_mem(
    const std::vector<double>& H_in, const std::vector<double>& g0,
    const std::vector<double>& m, double const_chi2, double nu,
    int max_iter, double tol, double min_prob
) {
    MaxEntResult res;
    int n = static_cast<int>(m.size());
    if (n == 0) { res.success = false; return res; }

    std::vector<double> H(H_in);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < i; ++j) {
            double s = 0.5 * (H[static_cast<size_t>(i) * n + j]
                              + H[static_cast<size_t>(j) * n + i]);
            H[static_cast<size_t>(i) * n + j] = s;
            H[static_cast<size_t>(j) * n + i] = s;
        }

    // H_eps = H + diag(diag(H)*1e-12)
    std::vector<double> H_eps(H);
    for (int i = 0; i < n; ++i)
        H_eps[static_cast<size_t>(i) * n + i] +=
            H[static_cast<size_t>(i) * n + i] * 1e-12;

    std::vector<double> ng0(g0);
    for (auto& v : ng0) v = -v;
    auto p_esm = quadpr_bound(H_eps, ng0, min_prob);

    auto quadratic = [&](const std::vector<double>& p) -> double {
        // 1/2 p^T H p - g0^T p + const, as a single Kahan-accumulated sum of
        // 0.5*p^T H p (Kahan over the outer index), minus the g0 term. Two
        // independent compensations; do NOT let one loop's comp leak into
        // another (that is a real bug: it turns a ~1e-2 fp error into an
        // order-of-magnitude one when the terms nearly cancel).
        double s = 0.0, comp = 0.0;
        for (int i = 0; i < n; ++i) {
            double hrow = 0.0;
            for (int j = 0; j < n; ++j)
                hrow += H[static_cast<size_t>(i) * n + j] * p[j];
            double y = 0.5 * p[i] * hrow - comp;
            double t = s + y;
            comp = (t - s) - y;
            s = t;
        }
        double gsum = 0.0, gcomp = 0.0;
        for (int i = 0; i < n; ++i) {
            double y = g0[i] * p[i] - gcomp;
            double t = gsum + y;
            gcomp = (t - gsum) - y;
            gsum = t;
        }
        return (s - gsum) + const_chi2;
    };
    auto entropy = [&](const std::vector<double>& p) -> double {
        double S = 0.0;
        for (int i = 0; i < n; ++i)
            S += (-log_clip(p[i] / m[i]) + 1.0) * p[i];
        return S - std::accumulate(m.begin(), m.end(), 0.0);
    };

    double chisq_esm = quadratic(p_esm);
    double S_esm = entropy(p_esm);
    double Q_esm = chisq_esm - 0.5 * nu * S_esm;

    std::vector<double> p = m;
    double chisq = quadratic(p);
    double S = entropy(p);
    double Q = chisq - 0.5 * nu * S;

    double dgrad = 1.0;
    int niter = 0;
    for (int iter = 1; iter <= max_iter; ++iter) {
        if (dgrad <= tol) break;
        niter = iter;

        // Delta_diag = 0.5 / max(p, min_prob); C_eff = H + diag(nu*Delta)
        std::vector<double> C_eff(H);
        std::vector<double> d_eff(g0);  // start as g0, then transform
        for (int i = 0; i < n; ++i) {
            double Delta = 0.5 / std::max(p[i], min_prob);
            C_eff[static_cast<size_t>(i) * n + i] += nu * Delta;
            d_eff[i] = -g0[i] + 0.5 * nu * (log_clip(p[i] / m[i]) - 1.0);
        }

        p = quadpr_bound(C_eff, d_eff, min_prob);
        chisq = quadratic(p);
        S = entropy(p);
        Q = chisq - 0.5 * nu * S;

        // dgrad from normalised gradient difference
        std::vector<double> grad_chi2(n), grad_S(n);
        for (int i = 0; i < n; ++i) {
            double hrow = 0.0;
            for (int j = 0; j < n; ++j)
                hrow += H[static_cast<size_t>(i) * n + j] * p[j];
            grad_chi2[i] = hrow - g0[i];
            grad_S[i] = -log_clip(p[i] / m[i]);
        }
        double mask_lim = -1.1 * min_prob;
        for (int i = 0; i < n; ++i) {
            if (!(p[i] > mask_lim)) { grad_chi2[i] = 0.0; grad_S[i] = 0.0; }
        }
        double nrm_c = 0.0, nrm_s = 0.0;
        for (int i = 0; i < n; ++i) { nrm_c += grad_chi2[i] * grad_chi2[i]; nrm_s += grad_S[i] * grad_S[i]; }
        nrm_c = std::sqrt(nrm_c); nrm_s = std::sqrt(nrm_s);
        if (nrm_c == 0.0 || nrm_s == 0.0) dgrad = 0.0;
        else {
            double diff = 0.0;
            for (int i = 0; i < n; ++i) {
                double dc = grad_chi2[i] / nrm_c - grad_S[i] / nrm_s;
                diff += dc * dc;
            }
            dgrad = 0.5 * std::sqrt(diff);
        }
    }

    res.p = p;
    res.chisq = chisq;
    res.S = S;
    res.Q = Q;
    res.p_esm = p_esm;
    res.chisq_esm = chisq_esm;
    res.S_esm = S_esm;
    res.Q_esm = Q_esm;
    res.niter = niter;
    res.success = true;
    return res;
}

void build_normal_equations(
    const std::vector<double>& A, const std::vector<double>& b,
    const std::vector<double>& weights, int n_rows, int n_cols,
    std::vector<double>& H, std::vector<double>& g0, double& const_term
) {
    const bool weighted = !weights.empty();
    H.assign(static_cast<size_t>(n_cols) * n_cols, 0.0);
    g0.assign(n_cols, 0.0);
    const_term = 0.0;

    for (int i = 0; i < n_rows; ++i) {
        const double w = weighted ? weights[i] : 1.0;
        const double* row = &A[static_cast<size_t>(i) * n_cols];
        for (int a = 0; a < n_cols; ++a) {
            g0[a] += 2.0 * w * row[a] * b[i];
            for (int bcol = 0; bcol < n_cols; ++bcol)
                H[static_cast<size_t>(a) * n_cols + bcol] += 2.0 * w * row[a] * row[bcol];
        }
        const_term += w * b[i] * b[i];
    }
}

} // namespace tttrlib

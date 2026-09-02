// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file MaxEntQp.h
 * \brief Shared engine: a bounded quadratic program and the Skilling-Bryan
 * maximum-entropy iteration built on it -- the numerical core every
 * maximum-entropy inversion in this library uses.
 *
 * Two callers needed exactly this (a bound-constrained QP solved by an
 * active-set sweep, wrapped in an outer Newton-like MEM loop that trades off
 * chi^2 against Shannon entropy relative to a prior) and had it twice, with
 * one of the two copies wrong: `MaxEntTcspc.cpp` (modules/spectroscopy/decay)
 * ported the real Skilling & Bryan (1984) algorithm from chisurf's
 * `maxent_decay.core.solver`; `MaxEnt.cpp` (modules/spectroscopy/corrections)
 * was a separate, simpler projected-gradient implementation of what its own
 * header called "Shannon entropy" but whose sign is backwards -- its entropy
 * term is *larger*, not smaller, the further a solution moves from the prior
 * (measured: S=-3 at the uniform prior, S=+13 for a spiky, far-from-prior
 * solution, the wrong direction for a term that is supposed to *penalise*
 * moving away from the prior). Both callers now share this one engine, which
 * is the one that was actually verified: `test_maxent_tcspc.py::TestTcspcMem*`
 * recovers a known lifetime/distance from simulated Poisson data through it.
 *
 * The objective this solves, given a quadratic chi^2 form:
 *
 *   Q(p) = 1/2 p^T H p - g0^T p + const - 1/2 nu * S(p)
 *
 * with the Skilling-Bryan entropy relative to a prior m:
 *
 *   S(p) = sum_i [ (p_i - m_i) - p_i * log(p_i / m_i) ]
 *
 * which is maximised (S=0) exactly at p=m and decreases (more negative) the
 * further p moves from the prior in either direction -- so `-1/2 nu * S(p)`
 * in Q *penalises* moving away from the prior, which is what a maximum-entropy
 * regulariser is for. Each outer MEM iterate linearises and re-solves a bound-
 * constrained QP `min 1/2 x^T C x + d^T x, x >= lb` by an active-set method.
 */
#ifndef TTTRLIB_MAXENTQP_H
#define TTTRLIB_MAXENTQP_H

// Validation: A/B-TESTED 2026-08-17 -- vs scipy L-BFGS-B on the same objectives (run_mem: KKT to 1e-9 and Q not
//   lowerable; quadpr_bound: equal whenever the sweep's answer is a KKT point,
//   feasible and never below the true minimum otherwise -- the non-KKT caveat this
//   header states). No valid external MEM reference (ChiSurf mem.py is
//   value/gradient-inconsistent). test/python/misc/test_math_ab_probabilistic.py.
//   Register: okf/testing/math-kernel-validation.md

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

#include "Mat.h"

namespace tttrlib {

/*! Result of a maximum-entropy quadratic-program fit. */
struct MaxEntResult {
    std::vector<double> p;        ///< recovered amplitudes (length n)
    double chisq = 0.0;
    double S = 0.0;
    double Q = 0.0;
    std::vector<double> p_esm;    ///< early-stop-maximum (unregularised) amplitudes
    double chisq_esm = 0.0;
    double S_esm = 0.0;
    double Q_esm = 0.0;
    int niter = 0;
    bool success = false;
};

/*!
 * \brief Bounded quadratic program: min 1/2 x^T C x + d^T x, x >= lower_bound.
 *
 * Active-set method: repeatedly solves the free block via `mat_solve`
 * (falling back to `mat_lstsq_minnorm` for a near-singular free block),
 * clamps any variable that would violate the bound, and repeats until no new
 * variable is clamped (or 50 sweeps). This is a simple sweep, not a proof of
 * KKT optimality -- it never *releases* a variable once clamped, and does not
 * check dual feasibility on the active set. That is adequate for what every
 * current caller uses it for (a single MEM Newton step, itself iterated to
 * convergence by the outer loop in `run_mem`) but it is not a general-purpose,
 * KKT-correct bounded QP solver; do not reach for it as one.
 *
 * \param C symmetric n x n matrix, row-major (need not be pre-symmetrised --
 *          this symmetrises its own copy).
 * \param d length-n vector.
 * \param lower_bound the same lower bound applied to every coordinate.
 * \return x, length n, with every entry >= lower_bound.
 */
std::vector<double> quadpr_bound(
    const std::vector<double>& C, const std::vector<double>& d,
    double lower_bound
);

/*!
 * \brief Run the Skilling-Bryan MEM iteration given a quadratic chi^2 form.
 *
 * \param H n x n matrix, row-major -- the Hessian of chi^2 (i.e. chi^2(p) =
 *          1/2 p^T H p - g0^T p + const).
 * \param g0 length-n linear term.
 * \param m prior amplitudes, length n; the entropy `S(p)` is maximised at
 *          p = m. Must be strictly positive.
 * \param const_chi2 the constant term of chi^2 at p=0.
 * \param nu entropy regularisation strength (nu=0 is the unregularised,
 *           bound-constrained least-squares solution).
 * \param max_iter, tol, min_prob outer-loop iteration cap, convergence
 *        tolerance on the normalised gradient-direction difference (the
 *        classic Skilling-Bryan "TEST" quantity), and the positivity floor.
 */
MaxEntResult run_mem(
    const std::vector<double>& H, const std::vector<double>& g0,
    const std::vector<double>& m, double const_chi2, double nu,
    int max_iter = 200, double tol = 1e-4, double min_prob = 1e-12
);

/*! Result of `run_mem_target_chisq`: the MEM fit at the nu found, plus
 *  what the search itself did. */
struct MemTargetChisqResult {
    MaxEntResult result;     ///< the fit at `nu` (or the best-observed
                              ///< iterate -- see the function)
    double nu = 0.0;         ///< the nu found; +infinity on the unreachable-high
                              ///< endpoint (p pinned to the prior)
    int outer_niter = 0;     ///< controller iterations actually used
    bool converged = false;  ///< |result.chisq - target| within tolerance
};

/*!
 * \brief "Historic MaxEnt": find nu such that the MEM fit lands at
 * `target_chisq`, then return that fit. An explicit opt-in mode, never a
 * default -- see the note below.
 *
 * chisq(nu) is monotonically non-decreasing in exact arithmetic (Q is jointly
 * convex in p for nu >= 0; the standard Pareto trade-off argument), which is
 * what makes a 1-D search on nu well posed. How this function runs that
 * search is the part worth reading:
 *
 * It is NOT an outer root-find that cold-starts `run_mem` once per nu probe.
 * That was the first design (bisection in log nu), and it fails on real
 * problems: on a 1e6-photon simulated FRET decay over a 100-point distance
 * grid, where the chisq(nu) transition is steep, the bisection exhausted 500
 * cold-started MEM solves (about 140-195 s, replicated over two runs) stuck
 * at chisq 0.98 against a target of 1.0, while the joint controller below
 * landed at chisq 1.0000 using 300 warm-started QP steps (1.4 s even from
 * the Python prototype it was designed in). The failure mode of the outer
 * root-find is structural: each probe pays for a full MEM convergence just to
 * sample chisq(nu) once, and near the transition one sample per ~40x nu step
 * is too little information for the tolerance demanded.
 *
 * Instead, nu is updated INSIDE the MEM outer loop, Gull-Skilling style: each
 * iterate takes one bound-QP Newton step on the amplitudes at the current nu,
 * then moves log(nu) toward the value whose chi-square matches the target --
 * a secant step in (log nu, log chisq) space over the last two iterates,
 * falling back to a damped multiplicative move when the secant is
 * unusable, and clamped to a factor of 30 per iterate so the warm start
 * stays useful. The amplitudes carry across nu changes, so the whole search
 * costs about one `run_mem`, not twenty. In the concrete implementation the
 * monotonicity above is empirical, not certified -- `quadpr_bound` is not
 * KKT-correct and the loop stops at finite tol -- so the controller tracks
 * the best-observed iterate across everything it evaluated and returns that,
 * never trusting a single sample.
 *
 * Two measured facts shape the endpoints (found prototyping against the
 * Python reference in test_maxent_tcspc.py, which this function must agree
 * with):
 *
 *  - There is NO nu=0 floor precheck. run_mem at exactly nu=0 lands ABOVE
 *    the truly reachable chi-square floor (measured 1.679 vs 0.919 at
 *    nu=1e-8 on a 60-column lifetime grid), because the unregularised QP on
 *    a near-singular H is ill-conditioned while a tiny nu > 0 acts as an
 *    interior-point regulariser. The floor is therefore discovered by the
 *    controller driving nu down to its clamp; if chisq still exceeds the
 *    target there, the target is unreachable-low and the best-observed
 *    iterate comes back with `converged = false`.
 *  - The nu -> infinity ceiling IS analytic: p -> m, so the ceiling is the
 *    quadratic form evaluated at the prior -- no solve needed. A target at
 *    or above it returns the prior itself with `nu = +infinity`.
 *
 * Honesty note: chisurf -- whose solver this engine was ported from --
 * deliberately does NOT implement target-chi^2 nu selection; its docs argue
 * the technique is statistically naive for decay data ("the wrong answer
 * fits better") and offer an L-curve sweep that only *suggests* a nu. This
 * function exists because it was explicitly requested as a capability beyond
 * the reference; it is not a recommendation.
 *
 * \param target_chisq the chi^2 to aim for, in whatever units the caller's
 *        H/g0/const_chi2 encode. The two existing conventions differ:
 *        MaxEntTcspc's design path is a MEAN chi^2 (natural target ~1.0);
 *        `build_normal_equations` is the raw SUM (natural target ~n_rows).
 * \param nu0 seed nu for the controller. The `solve_tcspc_mem_*` entry
 *        points pass their own `nu` argument here, so in target mode the
 *        caller's nu seeds the search rather than being used directly.
 * \param max_iter cap on the joint iteration. Each step is one bound-QP
 *        solve; a fixed-nu `run_mem` converges in ~20 of those on the test
 *        fixtures, but interleaving the nu moves stretches the same
 *        convergence to several hundred (measured: 647 on the 60-column
 *        lifetime fixture from seed nu=1e-5, ~260 on a 100-column FRET
 *        fixture), so the default is 1000.
 * \param chisq_tol RELATIVE tolerance: converged when
 *        |chisq - target| <= chisq_tol * max(target, 1).
 * \param tol stationarity tolerance for the amplitude iterate -- the same
 *        normalised gradient-direction criterion `run_mem` uses. Both
 *        conditions must hold at once for `converged`.
 * \param min_prob positivity floor, as in `run_mem`.
 */
MemTargetChisqResult run_mem_target_chisq(
    const std::vector<double>& H, const std::vector<double>& g0,
    const std::vector<double>& m, double const_chi2, double target_chisq,
    double nu0 = 1e-5, int max_iter = 1000, double chisq_tol = 1e-2,
    double tol = 1e-4, double min_prob = 1e-12
);

/*!
 * \brief Build the weighted normal-equation quadratic form for an arbitrary
 * design matrix: chi^2(p) = sum_i w_i (A_i . p - b_i)^2, in the
 * `1/2 p^T H p - g0^T p + const` form `run_mem` expects.
 *
 * `weights` is 1/sigma_i^2 per row; empty means every row weighted 1
 * (ordinary, unweighted least squares).
 *
 * \param A n_rows x n_cols design matrix, row-major.
 * \param b length-n_rows measurements.
 * \param weights length-n_rows per-row weights (1/sigma^2), or empty for
 *        unweighted.
 * \param H, g0, const_term out.
 */
void build_normal_equations(
    const std::vector<double>& A, const std::vector<double>& b,
    const std::vector<double>& weights, int n_rows, int n_cols,
    std::vector<double>& H, std::vector<double>& g0, double& const_term
);


// ---------------------------------------------------------------------------
// The engine, header-only (2026-09-02).
//
// The bodies moved here from MaxEntQp.cpp so a consumer holding only the
// header -- imp.bff, which vendors tttrlib headers byte-identically
// (DecayConvolution.h, ExpressionEngine.h) rather than linking -- runs THIS
// Skilling-Bryan engine and not a second one of its own. Everything the
// engine needs is itself header-only (Mat.h's mat_solve/mat_lstsq_minnorm);
// the anonymous-namespace helpers get per-TU copies, exactly as
// ExpressionEngine.h's do.
// ---------------------------------------------------------------------------


namespace {

constexpr double LOG_FLOOR = -1e300;

double log_clip(double v) {
    return v > 1e-300 ? std::log(v) : LOG_FLOOR;
}

// 1/2 p^T H p - g0^T p + const, as a single Kahan-accumulated sum of
// 0.5*p^T H p (Kahan over the outer index), minus the g0 term. Two
// independent compensations; do NOT let one loop's comp leak into
// another (that is a real bug: it turns a ~1e-2 fp error into an
// order-of-magnitude one when the terms nearly cancel).
double mem_chisq(const std::vector<double>& H, const std::vector<double>& g0,
                 double const_chi2, const std::vector<double>& p, int n) {
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
}

// Skilling-Bryan entropy relative to the prior: 0 at p=m, negative and
// decreasing the further p moves away. Shared by run_mem and the
// target-chisq controller so the two cannot drift on what S means.
double mem_entropy(const std::vector<double>& p,
                   const std::vector<double>& m, int n) {
    double S = 0.0;
    for (int i = 0; i < n; ++i)
        S += (-log_clip(p[i] / m[i]) + 1.0) * p[i];
    return S - std::accumulate(m.begin(), m.end(), 0.0);
}

// The classic Skilling-Bryan "TEST" quantity: 0.5 * the norm of the
// difference between the chi^2 gradient and the entropy gradient, each
// normalised, over coordinates not clamped to the floor. Identical arithmetic
// to what run_mem has always computed inline -- extracted, not rewritten.
double mem_dgrad(const std::vector<double>& H, const std::vector<double>& g0,
                 const std::vector<double>& p,
                 const std::vector<double>& m, int n, double min_prob) {
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
    if (nrm_c == 0.0 || nrm_s == 0.0) return 0.0;
    double diff = 0.0;
    for (int i = 0; i < n; ++i) {
        double dc = grad_chi2[i] / nrm_c - grad_S[i] / nrm_s;
        diff += dc * dc;
    }
    return 0.5 * std::sqrt(diff);
}

} // anonymous namespace

inline std::vector<double> quadpr_bound(
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

inline MaxEntResult run_mem(
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
        return mem_chisq(H, g0, const_chi2, p, n);
    };
    auto entropy = [&](const std::vector<double>& p) -> double {
        return mem_entropy(p, m, n);
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

        dgrad = mem_dgrad(H, g0, p, m, n, min_prob);
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

inline MemTargetChisqResult run_mem_target_chisq(
    const std::vector<double>& H_in, const std::vector<double>& g0,
    const std::vector<double>& m, double const_chi2, double target_chisq,
    double nu0, int max_iter, double chisq_tol,
    double tol, double min_prob
) {
    MemTargetChisqResult res;
    const int n = static_cast<int>(m.size());
    if (n == 0 || !std::isfinite(target_chisq) ||
        !std::isfinite(nu0) || nu0 <= 0.0) {
        res.result.success = false;
        return res;
    }

    std::vector<double> H(H_in);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < i; ++j) {
            double s = 0.5 * (H[static_cast<size_t>(i) * n + j]
                              + H[static_cast<size_t>(j) * n + i]);
            H[static_cast<size_t>(i) * n + j] = s;
            H[static_cast<size_t>(j) * n + i] = s;
        }

    const double tol_abs = chisq_tol * std::max(target_chisq, 1.0);
    // nu controller constants, fixed by the prototype measurements -- not
    // knobs: kappa damps the fallback move, max_factor stops one secant step
    // from jumping so far the warm start is useless.
    constexpr double kappa = 0.7;
    constexpr double max_factor = 30.0;
    constexpr double log_max_factor = 3.4011973816621555;  // log(30), load-bearing: keeps the clamp finite at compile time

    // Analytic ceiling only -- deliberately no nu=0 floor precheck (see the
    // header docstring for the measurement behind that). The raw H is fine
    // here: p^T H p only ever sees the symmetric part.
    const double chisq_ceiling = mem_chisq(H, g0, const_chi2, m, n);
    if (target_chisq >= chisq_ceiling) {
        res.result.p = m;
        res.result.chisq = chisq_ceiling;
        res.result.S = 0.0;
        // Q at nu=infinity is chisq - inf*S with S=0: indeterminate, so
        // report the finite limit rather than a NaN.
        res.result.Q = chisq_ceiling;
        res.result.niter = 0;
        res.result.success = true;
        res.nu = std::numeric_limits<double>::infinity();
        res.converged = std::fabs(chisq_ceiling - target_chisq) <= tol_abs;
        return res;
    }

    // ESM (unregularised) solve once, for the result fields -- same H_eps
    // regularisation run_mem applies.
    std::vector<double> H_eps(H);
    for (int i = 0; i < n; ++i)
        H_eps[static_cast<size_t>(i) * n + i] +=
            H[static_cast<size_t>(i) * n + i] * 1e-12;
    std::vector<double> ng0(g0);
    for (auto& v : ng0) v = -v;
    res.result.p_esm = quadpr_bound(H_eps, ng0, min_prob);
    res.result.chisq_esm = mem_chisq(H, g0, const_chi2, res.result.p_esm, n);
    res.result.S_esm = mem_entropy(res.result.p_esm, m, n);

    std::vector<double> p = m;
    double nu = nu0;
    // last two (log nu, chisq) samples feeding the secant controller
    double l1 = 0.0, c1 = -1.0, l2 = 0.0, c2 = -1.0;
    struct Best {
        std::vector<double> p; double nu = 0.0, chisq = 0.0, S = 0.0;
        int niter = 0; double err = std::numeric_limits<double>::infinity();
    } best;
    int niter_used = 0;

    for (int it = 1; it <= max_iter; ++it) {
        niter_used = it;
        // one MEM Newton step at the current nu -- same step run_mem takes
        std::vector<double> C_eff(H);
        std::vector<double> d_eff(n);
        for (int i = 0; i < n; ++i) {
            double Delta = 0.5 / std::max(p[i], min_prob);
            C_eff[static_cast<size_t>(i) * n + i] += nu * Delta;
            d_eff[i] = -g0[i] + 0.5 * nu * (log_clip(p[i] / m[i]) - 1.0);
        }
        p = quadpr_bound(C_eff, d_eff, min_prob);

        const double chisq = mem_chisq(H, g0, const_chi2, p, n);
        const double S = mem_entropy(p, m, n);
        const double dgrad = mem_dgrad(H, g0, p, m, n, min_prob);
        const double err = std::fabs(chisq - target_chisq);

        if (err < best.err) {
            best.p = p; best.nu = nu; best.chisq = chisq;
            best.S = S; best.niter = it; best.err = err;
        }
        if (err <= tol_abs && dgrad <= tol) {
            res.result.p = p;
            res.result.chisq = chisq;
            res.result.S = S;
            res.result.Q = chisq - 0.5 * nu * S;
            res.result.niter = it;
            res.result.success = true;
            res.nu = nu;
            res.outer_niter = it;
            res.converged = true;
            return res;
        }

        // --- nu controller: secant in (log nu, log chisq), damped fallback ---
        l1 = l2; c1 = c2;
        l2 = std::log(nu); c2 = chisq;
        double new_log_nu = std::numeric_limits<double>::quiet_NaN();
        if (c1 > 0.0 && c2 > 0.0 && std::fabs(l2 - l1) > 1e-14 &&
            std::fabs(std::log(c2) - std::log(c1)) > 1e-12) {
            const double slope =
                (std::log(c2) - std::log(c1)) / (l2 - l1);
            if (slope > 1e-6)   // chisq must actually respond to nu
                new_log_nu = l2 + (std::log(target_chisq) - std::log(c2))
                             / slope;
        }
        if (std::isnan(new_log_nu))
            new_log_nu = std::log(nu) + kappa * std::log(
                std::max(target_chisq, 1e-300) / std::max(chisq, 1e-300));
        double step = new_log_nu - std::log(nu);
        if (step >  log_max_factor) step =  log_max_factor;
        if (step < -log_max_factor) step = -log_max_factor;
        const double nu_before = nu;
        nu = std::exp(std::log(nu) + step);
        if (nu < 1e-30) nu = 1e-30;
        if (nu > 1e30) nu = 1e30;

        // Floor clamp: nu pinned by its bound (bitwise unchanged after the
        // update) AND the amplitudes stationary means every later iterate
        // repeats this one -- stop instead of spinning to max_iter (an
        // unreachable-low target lands here).
        if (nu == nu_before && dgrad <= tol) break;
    }

    // best-observed iterate, converged judged on chisq alone (a dgrad-only
    // miss at the right chisq still leaves the caller an honest fit)
    res.result.p = best.p.empty() ? m : best.p;
    res.result.chisq = best.err == std::numeric_limits<double>::infinity()
        ? mem_chisq(H, g0, const_chi2, m, n) : best.chisq;
    res.result.S = best.p.empty() ? 0.0 : best.S;
    res.result.Q = res.result.chisq
        - 0.5 * best.nu * res.result.S;
    res.result.niter = best.niter;
    res.result.success = true;
    res.nu = best.nu;
    res.outer_niter = niter_used;
    res.converged = best.err <= tol_abs;
    return res;
}

inline void build_normal_equations(
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

#endif // TTTRLIB_MAXENTQP_H

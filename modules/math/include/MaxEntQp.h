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

#include <vector>

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

} // namespace tttrlib

#endif // TTTRLIB_MAXENTQP_H

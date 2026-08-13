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

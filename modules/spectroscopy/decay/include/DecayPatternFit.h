// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file DecayPatternFit.h
 * \brief General N-arbitrary-pattern fit: decompose a decay into a
 * non-negative combination of caller-supplied fixed reference patterns.
 *
 * `DecayFit26` already fits a mixture, but of exactly two fixed patterns with
 * one free mixing fraction. `DecayFitProblem::patterns` carries an arbitrary
 * *list* of fixed patterns but, before this file, no built-in model read it:
 * it was scaffolding for the C-ABI plugin interface only. This is that
 * consumer -- N patterns in, N non-negative amplitudes out, no constraint
 * that they sum to one.
 *
 * Three ways to resolve the amplitudes, selected by `PatternFitMode`:
 *
 *  - `kNone`: plain non-negative least squares (exact KKT solution, via
 *    `tttrlib::nnls`, Nnls.h) -- no regularisation, the amplitudes that best
 *    explain the data and nothing else.
 *  - `kTikhonov`: L2-regularised, non-negative (`||Ax-b||^2 + lambda*||x||^2`,
 *    bound-constrained via the shared active-set QP, MaxEntQp.h) -- shrinks
 *    every amplitude toward zero, useful when patterns are collinear and the
 *    unregularised solution is ill-conditioned.
 *  - `kMaxEnt`: Skilling-Bryan maximum-entropy regularisation toward a
 *    uniform (or caller-supplied) prior (the same engine `MaxEntTcspc.cpp`
 *    and the now-fixed `maxent_invert` use) -- shrinks toward the prior
 *    shape rather than toward zero, useful when a physically motivated
 *    "expected mixture" exists.
 *
 * All three share one design matrix (`build_normal_equations`, MaxEntQp.h):
 * column `k` is pattern `k`, row `i` is bin `i`. Regularisation strength
 * (`lambda` for Tikhonov, `nu` for MaxEnt) is meaningless across modes and is
 * the caller's to choose per mode; `kNone` ignores it.
 */
#ifndef TTTRLIB_DECAYPATTERNFIT_H
#define TTTRLIB_DECAYPATTERNFIT_H

#include <vector>

namespace tttrlib {

enum class PatternFitMode {
    kNone = 0,      ///< plain NNLS, no regularisation
    kTikhonov = 1,  ///< L2-regularised, non-negative
    kMaxEnt = 2,     ///< Skilling-Bryan maximum-entropy, non-negative
    /*!
     * Historic MaxEnt: `reg_strength` is REINTERPRETED as the target
     * chi-square (raw-sum units, so the natural value is ~data.size()), and
     * nu is found by `run_mem_target_chisq` instead of being supplied.
     *
     * Footgun, stated loudly: switching a call site from kMaxEnt to
     * kMaxEntTargetChisq without updating `reg_strength` silently reads a
     * small nu (say 1e-5) as a target chi-square, which is below the
     * reachable floor for any real dataset -- the fit then degrades
     * gracefully (`target_converged = false`, amplitudes at the floor), it
     * does not crash, so watch `target_converged`.
     */
    kMaxEntTargetChisq = 3
};

/*! Result of a pattern-mixture fit. */
struct PatternFitResult {
    std::vector<double> amplitudes;  ///< one non-negative weight per pattern
    double chisq = 0.0;              ///< ||sum_k amplitude_k * pattern_k - data||^2
    bool success = false;
    double nu_used = 0.0;            ///< the entropy weight actually used: the
                                     ///< given `reg_strength` under kMaxEnt, the
                                     ///< nu the search found under
                                     ///< kMaxEntTargetChisq, 0 otherwise
    bool target_converged = true;    ///< kMaxEntTargetChisq only: did chisq land
                                     ///< at the target? Always true elsewhere.
};

/*!
 * \brief Fit non-negative amplitudes of N fixed patterns against `data`.
 *
 * \param data measured histogram, length n_bins.
 * \param patterns N fixed reference patterns, each length n_bins (e.g.
 *        `DecayFitProblem::patterns`, or an IRF/background/donor-only set
 *        assembled by the caller).
 * \param mode which regularisation to apply -- see the file docstring.
 * \param reg_strength regularisation strength: `lambda` (kTikhonov), `nu`
 *        (kMaxEnt), or the TARGET CHI-SQUARE (kMaxEntTargetChisq, raw-sum
 *        units, natural value ~data.size()). Ignored by kNone.
 * \param prior MaxEnt prior, length == patterns.size(); empty means uniform
 *        (all ones). Ignored by kNone/kTikhonov.
 * \param max_iter, tol iteration cap and convergence tolerance, passed
 *        through to the selected engine (nnls / quadpr_bound / run_mem).
 *        Under kMaxEntTargetChisq the cap is floored at 1000 internally:
 *        the joint nu controller interleaves nu moves with the amplitude
 *        steps and needs several hundred iterations even though a fixed-nu
 *        run_mem converges in tens.
 */
PatternFitResult decay_pattern_fit(
    const std::vector<double>& data,
    const std::vector<std::vector<double>>& patterns,
    PatternFitMode mode,
    double reg_strength = 0.0,
    const std::vector<double>& prior = {},
    int max_iter = 200,
    double tol = 1e-8
);

} // namespace tttrlib

#endif // TTTRLIB_DECAYPATTERNFIT_H

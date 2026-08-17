// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file DecayFitContext.h
 * \brief What the Fit2x objective kernels read while an optimiser runs.
 *
 * Internal to the Fit2x family. The optimiser takes a `void*` user pointer and
 * hands it back to the objective on every evaluation; this is what that pointer
 * points at, replacing the `DecayFitData` the kernels used to be handed.
 *
 * It exists so the *numerics* need not change. `modelf`, `correct_input`,
 * `twoIstar` and the rest already take raw pointers and know nothing about any
 * container, so pointing them at a `DecayFitProblem` instead is a change of
 * plumbing rather than of physics — which is the only way to move to the new
 * interface without putting the measured fit results at risk.
 *
 * \par Why integer counts survive here
 * `DecayFitProblem::data` is `double`, because pooled, rebinned or
 * background-subtracted data is not integral and the container has to describe
 * all of it. The Poisson statistics, though, index a precomputed factorial table
 * by count (`init_fact`), so they genuinely want integers. Rather than soften
 * the statistics — which would change every fit result the library has ever
 * produced — the model converts once per fit into `counts`, and the kernels see
 * exactly what they always saw.
 *
 * \par Nchannels
 * The kernels' `Nchannels` means *bins per polarization*, not the number of
 * detectors: they index `[0, Nchannels)` as parallel and
 * `[Nchannels, 2*Nchannels)` as perpendicular. That is `DecayFitProblem::n_bins`.
 * The old `DecayFitData::n_channels()` returned `data.size() / 2`, which happened
 * to equal it only because that container was always two-channel — the ambiguity
 * this field's name is meant to end.
 */
#ifndef TTTRLIB_DECAYFITCONTEXT_H
#define TTTRLIB_DECAYFITCONTEXT_H

#include "DecayFitProblem.h"

/*! \brief Borrowed pointers the Fit2x objective needs; owns nothing. */
struct DecayFitContext {

    /*! The measurement; `model` is written on every evaluation. */
    DecayFitProblem *problem = nullptr;

    /*!
     * \brief Integer view of `problem->data`, for the counting statistics.
     *
     * Mutable, and owned by the model for the duration of one fit: the
     * unpolarized fast path rewrites it in place into the (Cp+Cs, Cp) form its
     * closed-form score needs, rather than allocating a second buffer per row.
     */
    int *counts = nullptr;

    /*! `[period, g, l1, l2, convolution_stop]`, as the kernels expect them. */
    const double *corrections = nullptr;

    /*! Bins per polarization — the kernels' `Nchannels`. */
    int n_bins = 0;

    /*! Micro-time bin width. */
    double dt = 1.0;

    /*!
     * \brief Optional hard bounds per parameter, borrowed; may be null.
     *
     * A bound *is* a uniform prior in this interface, so these come from the
     * priors attached to the constraints and there is no second bounds concept
     * to disagree with them. Entries may be infinite, meaning unbounded on that
     * side; null means no bounds at all.
     *
     * They live here because the kernels build their own optimiser internally
     * and never see `DecayFitConstraints`. Without this the documented
     * "bounds are priors" behaviour was silently inert on the single-row path
     * while working correctly in `fit_linked` — and a feature that quietly does
     * nothing is worse than one that is absent, because the caller believes the
     * bound held.
     */
    const double *lower = nullptr;
    const double *upper = nullptr;

    /*!
     * \brief Objective evaluations since the context was bound.
     *
     * Counted by the objective itself, so it is what the optimiser actually
     * cost rather than a nominal iteration count. Reported through
     * `results_schema`, which gives every model an answer to "did this row work
     * hard, or give up?" — previously only the multi-exponential fit could say.
     */
    int iterations = 0;

    /*!
     * \brief Which statistic the kernel minimises (DecayObjective code:
     *        0 poisson_mle, 1 p2s_mle, 2 neyman_lsq, 3 gehrels_lsq).
     *
     * Set by the Fit2x model adapter from the setup's `objective`; the packed
     * parameter vector's legacy p2s flag still selects 1 for direct kernel
     * callers.
     */
    int objective = 0;

    /*! Optimiser status of the last fit; model-specific, 0 when unset. */
    int info = 0;

    const double *irf() const { return problem ? problem->irf.data() : nullptr; }
    const double *background() const { return problem ? problem->background.data() : nullptr; }
    double *model() const { return problem ? problem->model.data() : nullptr; }

    /*!
     * \brief Whether the arrays are sized consistently enough to evaluate.
     *
     * The objective is reachable directly from the language bindings, so it
     * cannot assume `fit()` validated first. A short IRF against a longer decay
     * used to read past the end and crash.
     */
    bool is_usable() const {
        if (problem == nullptr || counts == nullptr || corrections == nullptr) return false;
        if (n_bins <= 0) return false;
        const std::size_t needed = static_cast<std::size_t>(2 * n_bins);
        return problem->data.size() >= needed && problem->irf.size() >= needed &&
               problem->background.size() >= needed && problem->model.size() >= needed;
    }
};


/*!
 * \brief Apply a context's prior-derived bounds to an optimiser.
 *
 * Templated on the optimiser so this header does not have to pull in the
 * optimiser's own. A side that is infinite is left unbounded; a context with no
 * bounds at all is a no-op, so every kernel can call this unconditionally.
 */
template <typename Optimiser>
inline void apply_context_bounds(Optimiser &optimiser,
                                 const DecayFitContext *p,
                                 int n_parameters) {
    if (p == nullptr || p->lower == nullptr || p->upper == nullptr) return;
    for (int i = 0; i < n_parameters; ++i) {
        if (std::isfinite(p->lower[i]) || std::isfinite(p->upper[i])) {
            optimiser.set_bounds(i, p->lower[i], p->upper[i]);
        }
    }
}

#endif // TTTRLIB_DECAYFITCONTEXT_H

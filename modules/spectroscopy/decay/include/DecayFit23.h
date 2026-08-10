// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file DecayFit23.h
 * \brief Single-lifetime Poisson MLE with a time-resolved anisotropy.
 *
 * The single-molecule burst workhorse: one lifetime fitted to a
 * polarisation-resolved decay, modelling the anisotropy so the parallel and
 * perpendicular channels are described jointly rather than independently.
 *
 * These are the **numerical kernels only**, not the way to run the fit. That is
 * ``make_decay_fit("fit23")``, which returns a ``DecayFitModel``
 * (see DecayFitModel.h). The kernels deliberately kept their raw-pointer shape
 * when the library moved to that interface, so the change was one of plumbing
 * rather than of physics: ``modelf`` and ``correct_input`` are untouched, and the
 * objective still sees exactly the arrays it always saw — including integer
 * counts, because the Poisson statistics index a factorial table by count.
 *
 * The packed ``x`` the kernels use (parameters, two setup flags and two outputs
 * in one array) is a property of *these functions*, not of the library's
 * interface: the model wrapper presents parameters, setup and results as three
 * separate registry-described vectors and packs them on the way in.
 */
#ifndef TTTRLIB_DECAYFIT23_H
#define TTTRLIB_DECAYFIT23_H

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>
#include <sstream>

#include "i_lbfgs.h"
#include "DecayFitContext.h"
#include "DecayConvolution.h"
#include "DecayStatistics.h"
#include "DecayFit.h"


class DecayFit23 {

public:

    /*!
     * \brief Build the model decay for \p param into \p mfunction.
     *
     * \param param ``[tau, gamma, r0, rho]``, already through ``correct_input``.
     * \param irf Instrument response, ``2 * Nchannels`` (parallel then perpendicular).
     * \param bg Background pattern, same layout.
     * \param Nchannels Bins **per polarization**.
     * \param dt Micro-time bin width.
     * \param corrections ``[period, g, l1, l2, convolution_stop]``.
     * \param mfunction Output, ``2 * Nchannels``.
     */
    static int modelf(
            double *param,
            double *irf,
            double *bg,
            int Nchannels,
            double dt,
            double *corrections,
            double *mfunction
    );

    /*!
     * \brief Objective handed to the optimiser.
     *
     * \param x Packed parameter vector.
     * \param pv A ``DecayFitContext*``.
     */
    static double targetf(double *x, void *pv);

    /*!
     * \brief Score \p x without optimising.
     *
     * Runs the same preamble as ``fit`` — integrated signals, held-parameter
     * state — which ``targetf`` needs and does not do itself, so calling
     * ``targetf`` cold yields NaN rather than a score.
     */
    static double evaluate(double *x, short *fixed, DecayFitContext *p);

    /*!
     * \brief Optimise \p x in place; returns 2I* at the optimum.
     *
     * \param x ``[tau, gamma, r0, rho, softbifl_flag, p2s_flag, r_scatter,
     *        r_experimental]``; the last two are outputs.
     * \param fixed Which of the first four parameters are held.
     * \param p Borrowed data context.
     */
    static double fit(double *x, short *fixed, DecayFitContext *p);

    /*!
     * \brief Fast path for the unpolarized, tau-only case.
     *
     * Used by the model when gamma and r0 are zero, only tau is free, and the two
     * IRF halves are identical — the shape a batch of bursts normally has. The
     * general ``fit`` would give the same answer; this exists because the batched
     * per-fit cost is a number people rely on.
     *
     * \return false when the preconditions do not hold, so the caller falls back
     *         to ``fit``.
     */
    static bool fit_tau_only_unpolarized_row(
            const double *data,
            int n_cols,
            const double *x0,
            int n_x0,
            const short *fixed,
            int n_fixed,
            double bifl_scatter,
            double p2s_flag,
            DecayFitContext *p,
            double *out,
            int n_out_cols,
            bool retain_model = true);

    /*!
     * \brief Map the user-facing parameters onto the model's internal ones.
     *
     * \param return_r when non-zero, also writes the anisotropy outputs into \p x.
     */
    static void correct_input(double *x, double *xm, double *corrections, int return_r);
};


#endif // TTTRLIB_DECAYFIT23_H

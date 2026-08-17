// SPDX-License-Identifier: BSD-3-Clause
#include "DecayFit24.h"

#include <limits>


static thread_local DecayFitCorrections fit_corrections;
static thread_local DecayFitIntegrateSignals fit_signals;
static thread_local DecayFitSettings fit_settings;

namespace {
constexpr double kMinTau24 = 0.001;
}  // namespace

void DecayFit24::correct_input(double *x, double *xm, double *corrections, int return_r) {
    fit_signals.corrections = &fit_corrections;
    // correct input parameters (take care of unreasonable values)
    //
    // tau1/tau2 go through soft_floor rather than a hard clamp -- same reason
    // and same guarantee as DecayFit23's tau (DecayFit.h): exp(-dt/tau)
    // overflows for a small negative tau, the floor is arithmetically
    // required, and a hard clamp made the model identical (so the gradient
    // exactly zero) for every value below it. soft_floor is bit-for-bit the
    // identity at and above the floor, so no ordinary fit moves.
    xm[0] = soft_floor(x[0], kMinTau24);    // tau1 > 0
    xm[2] = soft_floor(x[2], kMinTau24);    // tau2 > 0

    if (x[3] < 0.) xm[3] = 0.;        // 0 < A2 < 0.999
    else if (x[3] > 0.999) xm[3] = 0.999;
    else xm[3] = x[3];

    if (x[1] < 0.) xm[1] = 0.;        // 0 < gamma < 0.999
    else if (x[1] > 0.999 - xm[3]) xm[1] = 0.999;
    else xm[1] = x[1];

    xm[4] = x[4];
    if (xm[4] < 0.) xm[4] = 0.;        // background > 0

    // anisotropy
    if (return_r) {
        fit_corrections.g = corrections[1];
        fit_corrections.l1 = corrections[2];
        fit_corrections.l2 = corrections[3];
        x[7] = fit_signals.rs();
        x[6] = fit_signals.r();
    }

}


// An exact forward-mode gradient (tttrlib::Dual<GradVec<5>>, matching
// DecayFit23's decay23_gradient) was implemented here, reusing fconv_per_cs_ad
// and Wcm_ad unchanged, and TESTED: value and gradient agree with central
// differences to the finite-difference floor (test/cpp/test_ad_gradient.cpp,
// decay24 section -- kept, and still passing, as the record that this was
// verified correct). It is not wired in. Measured A/B at 8000 rows
// (benchmarks/bench_decayfit24_batch_ad.py) found no clear win: wall clock
// 1.08x-1.15x faster, but total CPU across worker threads -- the more
// repeatable metric on a shared machine -- roughly flat to 6% *slower*,
// against DecayFit23's clear 1.3x-1.7x. Declined on that measurement, the
// same call this PRD already made for DecayFit26 at N=1 -- see PRD-010's
// Phase 7 for the numbers and the likely cause (i_lbfgs's own per-iteration
// overhead absorbing the gradient-level saving on this comparatively cheap
// model). tau1/tau2's soft_floor above stays: it is a real improvement to the
// central-difference gradient too, independent of this decision.


int DecayFit24::modelf(double *param,            // here: [tau1 gamma tau2 A2 offset]
                              double *irf,
                              double *bg,
                              int Nchannels,
                              double dt,            // time per channel
                              double *corrections,        // [period g l1 l2]
                              double *mfunction)        // out: model function in Jordi-girl format

{
    fit_signals.corrections = &fit_corrections;
    double x[4];
    double tau1, gamma, tau2, A2, offset,
            period,
            sum_m = 0., sum_s = 0.;
    int i, conv_stop;

/************************ Input arguments ***********************/

    tau1 = param[0];
    gamma = param[1];
    tau2 = param[2];
    A2 = param[3];
    offset = param[4] / (double) Nchannels;

    period = corrections[0];
    conv_stop = (int) corrections[4];

/************************* Model function ***********************/

    /// vv
    x[0] = 1. - A2;
    x[1] = tau1;
    x[2] = A2;
    x[3] = tau2;
    fconv_per_cs(mfunction, x, irf, 2, Nchannels - 1, Nchannels, period, conv_stop, dt);

    /// vh
    fconv_per_cs(mfunction + Nchannels, x, irf + Nchannels, 2, Nchannels - 1, Nchannels, period, conv_stop, dt);

    /// add scatter and background

    for (i = 0; i < 2 * Nchannels; i++) {
        sum_m += mfunction[i];
        sum_s += bg[i];
    }
    for (i = 0; i < 2 * Nchannels; i++)
        mfunction[i] = mfunction[i] * (1. - gamma) / sum_m + bg[i] * gamma / sum_s + offset;

    return 0;

}


//////////////////////////////////////////// fit24 ////////////////////////////////////////////

double DecayFit24::targetf(double *x, void *pv) {
    if (((DecayFitContext *) pv) != nullptr) ((DecayFitContext *) pv)->iterations++;
    fit_signals.corrections = &fit_corrections;

    double w, xm[5], Bgamma;
    DecayFitContext *p = (DecayFitContext *) pv;

    const int *expdata = p->counts;
    int Nchannels = p->n_bins;
    double *irf = const_cast<double *>(p->irf()), *bg = const_cast<double *>(p->background()),
            *corrections = const_cast<double *>(p->corrections), *M = p->model();

    correct_input(x, xm, corrections, 0);
    modelf(xm, irf, bg, Nchannels, p->dt, corrections, M);
    fit_signals.normM_p2s(M, Nchannels);

    w = decay_objective_score(p->objective, expdata, M, Nchannels, false);

    if (fit_settings.softbifl & (fit_signals.Bexpected > 0.)) {
        Bgamma = xm[1] * (fit_signals.Sp + fit_signals.Ss);
        w -= Bgamma * log(fit_signals.Bexpected) - loggammaf(Bgamma + 1.);
    }
    return w / Nchannels;

}

/*!
 * Score `x` without optimising, with the same preamble `fit` runs.
 *
 * `targetf` is not a complete evaluation on its own: `correct_input` reads the
 * integrated signals, and which statistic is used is thread-local state that
 * `fit` establishes. A cold `targetf` therefore returns NaN rather than a score.
 */
double DecayFit24::evaluate(double *x, short *fixed, DecayFitContext *p) {
    if (p == nullptr || x == nullptr || fixed == nullptr || !p->is_usable()) {
        return std::numeric_limits<double>::infinity();
    }
    fit_signals.corrections = &fit_corrections;
    if (fit_settings.firstcall) init_fact();
    fit_settings.firstcall = 0;
    fit_settings.softbifl = (x[5] < 0.);
    fit_signals.compute_signal_and_background(p->counts, p->background(), p->n_bins);
    return DecayFit24::targetf(x, p);
}


double DecayFit24::fit(double *x, short *fixed, DecayFitContext *p) {
    // x is:
    // [0] tau1
    // [1] gamma
    // [2] tau2
    // [3] A2 (A1 + A2 = 1)
    // [4] background (offset)
    // [5] BIFL scatter fit? (flag)
    // [6] r Scatter (output only)
    // [7] r Experimental (output only)
    fit_signals.corrections = &fit_corrections;

    double tIstar, xm[5], B;
    int i, info;
    (void)B; // silence unused variable warning
    (void)i; // silence unused variable warning

    // Fail safely on inconsistently sized Jordi arrays instead of reading past
    // the end of irf/background (see DecayFitContext::is_usable).
    if (p == nullptr || x == nullptr || fixed == nullptr ||
        !p->is_usable()) {
        if (x != nullptr) x[0] = -1.0;
        return std::numeric_limits<double>::infinity();
    }

    if (fit_settings.firstcall) init_fact();
    fit_settings.firstcall = 0;
    fit_settings.softbifl = (x[5] < 0.);

    const int *expdata = p->counts;
    int Nchannels = p->n_bins;
    double *irf = const_cast<double *>(p->irf()), *bg = const_cast<double *>(p->background()),
            *corrections = const_cast<double *>(p->corrections), *M = p->model();

    // total signal and background
    fit_signals.compute_signal_and_background(expdata, bg, Nchannels);

    bfgs bfgs_o(targetf, 5);

        // No set_gradient here: an exact forward-mode gradient was tested
        // (see the note above modelf) and declined on measurement, so this
        // stays on i_lbfgs's central-difference default.

        // Bounds are priors in this interface, so anything the caller attached
        // to a slot has to reach the optimiser that actually moves it. Without
        // this the bound was accepted, stored, serialised — and ignored.
        apply_context_bounds(bfgs_o, p, 5);

    if (fixed[0]) bfgs_o.fix(0);
    if (fixed[2]) bfgs_o.fix(2);
    if (fixed[3]) bfgs_o.fix(3);
    if (fixed[1] || (x[5] > 0.)) bfgs_o.fix(1);
    if (fixed[4]) bfgs_o.fix(4);

    // fix gamma and offset, try to fit
//  bfgs_o.fix(1);	// gamma
//  bfgs_o.fix(4); 	// background
//  if (!(fixed[0] && fixed[2] && fixed[3])) bfgs_o.minimize(x,p);

    // fit with free gamma and bg
//  if (!fixed[4]) bfgs_o.free(4);
//  if (!fixed[1] && (x[5]<=0.)) bfgs_o.free(1);
//  if ((!fixed[1] && (x[5]<=0.)) || (!fixed[4]))
    info = bfgs_o.minimize(x, p);

    correct_input(x, xm, corrections, 1);
    modelf(xm, irf, bg, Nchannels, p->dt, corrections, M);
    fit_signals.normM_p2s(M, Nchannels);

    tIstar = decay_objective_score(p->objective, expdata, M, Nchannels, true);

    if (info == 5 || x[0] < 0.) x[0] = -1.;        // for report
    if (info == 5 || x[2] < 0.) x[2] = -1.;
    x[1] = xm[1];
    x[4] = xm[4];

    return tIstar;
}

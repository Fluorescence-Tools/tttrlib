// SPDX-License-Identifier: BSD-3-Clause
#include "DecayFit25.h"
#include "Verbose.h"

#include <limits>


// normalization
static thread_local int fixedrho = 0;
static thread_local int softbifl = 0;
static thread_local int p2s_twoIstar = 0;
static thread_local int firstcall = 1;


static thread_local DecayFitCorrections fit_corrections;
static thread_local DecayFitIntegrateSignals fit_signals;
static thread_local DecayFitSettings fit_settings;



void DecayFit25::correct_input(double* x, double* xm, double* corrections, int return_r)
{
    // correct input parameters (take care of unreasonable values)
    // here x = [tau gamma r0 rho] + outputs
    fit_signals.corrections = &fit_corrections;

    xm[0] = x[0];
    xm[2] = x[2];
    // gamma is taken from x[1] (clamped like fit23). Previously xm[1] was
    // read here before ever being written — an uninitialized stack read that
    // made fit25 results depend on the process memory layout.
    xm[1] = std::max(0.0, std::min(x[1], 0.999));
    fit_corrections.set_gamma(xm[1]);
    fit_corrections.g = corrections[1];
    fit_corrections.l1 = corrections[2];
    fit_corrections.l2 = corrections[3];

    if (!fixedrho) {
        xm[3] = fit_signals.rho(x[0], x[2]); // rho = tau/(r0/r-1)
        x[3] = xm[3];
    } else xm[3] = x[3];

    if (return_r) {
        x[7] = fit_signals.r();
        x[8] = fit_signals.rs();
    }

if (is_verbose()) {
    std::cout << "correct_input25" << std::endl;
    std::cout<< "xm[1]:" << xm[1] << std::endl;
    std::cout << fit_corrections.str();
    std::cout << fit_signals.str();
    std::cout<< "rho:" << x[3] << std::endl;
    std::cout<< "tau:" << x[0] << std::endl;
    std::cout<< "r0:" << x[2] << std::endl;
}

}

double DecayFit25::targetf(double* x, void* pv)
{
    if (((DecayFitContext *) pv) != nullptr) ((DecayFitContext *) pv)->iterations++;
    fit_signals.corrections = &fit_corrections;

    double w, xm[4], Bgamma;
    DecayFitContext* p = (DecayFitContext*)pv;

    const int *expdata = p->counts;
    int Nchannels = p->n_bins;
    double *irf = const_cast<double *>(p->irf()), *bg = const_cast<double *>(p->background()),
            *corrections = const_cast<double *>(p->corrections), *M = p->model();

    correct_input(x, xm, corrections, 0);
    DecayFit23::modelf(xm, irf, bg, Nchannels, p->dt, corrections, M);
    fit_signals.normM(M, Nchannels);

    w = decay_objective_score(p->objective >= kObjNeymanLsq ? p->objective
                              : (p2s_twoIstar ? kObjP2sMle : kObjPoissonMle),
                              expdata, M, Nchannels, false);

    if (softbifl & (fit_signals.Bexpected > 0.)) {
        Bgamma = xm[1]*(fit_signals.Sp+fit_signals.Ss);
        w -= Bgamma*log(fit_signals.Bexpected) - loggammaf(Bgamma+1.);
    }
    return w/Nchannels;

}


/*!
 * Score `x` without optimising, with the same preamble `fit` runs.
 *
 * See DecayFit24::evaluate — `targetf` alone is not a complete evaluation.
 */
double DecayFit25::evaluate(double *x, short *fixed, DecayFitContext *p) {
    if (p == nullptr || x == nullptr || fixed == nullptr || !p->is_usable()) {
        return std::numeric_limits<double>::infinity();
    }
    fit_signals.corrections = &fit_corrections;
    if (firstcall) init_fact();
    firstcall = 0;
    softbifl = (x[6] < 0.);
    p2s_twoIstar = 1;
    fit_signals.compute_signal_and_background(p->counts, p->background(), p->n_bins);
    return DecayFit25::targetf(x, p);
}


double DecayFit25::fit (double* x, short* fixed, DecayFitContext* p)
{
    // x is:
    // [0] tau1 always fixed
    // [1] tau2 always fixed
    // [2] tau3 always fixed
    // [3] tau4 always fixed
    // [4] gamma
    // [5] r0
    // [6] BIFL scatter fit? (flag)
    // [7] r Scatter (output only)
    // [8] r Experimental (output only)
    fit_signals.corrections = &fit_corrections;

    double tIstar, tIstarbest = 1.E6, taubest = -1, gammabest = 0., xtmp[9], xm[4], B;
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

    if (firstcall) init_fact();
    firstcall = 0;
    softbifl = (x[6]<0.);
    p2s_twoIstar = 1;

    const int *expdata = p->counts;
    int Nchannels = p->n_bins;
    double *irf = const_cast<double *>(p->irf()), *bg = const_cast<double *>(p->background()),
            *corrections = const_cast<double *>(p->corrections), *M = p->model();

    // total signal and background

    fixedrho = 0;
    fit_signals.compute_signal_and_background(expdata, bg, Nchannels);

    // xtmp: same order as for fit23: [tau gamma r0 rho]
    xtmp[0] = x[0]; xtmp[1] = x[4]; xtmp[2] = x[5]; xtmp[3] = 1.;

    bfgs bfgs_o(targetf, 4);
        // Bounds are priors in this interface, so anything the caller attached
        // to a slot has to reach the optimiser that actually moves it. Without
        // this the bound was accepted, stored, serialised — and ignored.
        apply_context_bounds(bfgs_o, p, 4);

    bfgs_o.fix(0);	// tau
    bfgs_o.fix(2); 	// r0
    bfgs_o.fix(3); 	// rho is set in targetf23

    // choose tau
    info = 0;
    for (int i=0; i<4; i++) {
        xtmp[0] = x[i]; xtmp[1] = x[4];
        // fit gamma if unfixed
        if (!fixed[4] && (x[6]<=0.)) bfgs_o.minimize(xtmp,p);

        // calculate 2I*
        correct_input(xtmp, xm, corrections, 1);
        DecayFit23::modelf(xm, irf, bg, Nchannels, p->dt, corrections, M);
        fit_signals.normM(M, Nchannels);
        tIstar = decay_objective_score(p->objective >= kObjNeymanLsq ? p->objective
                                       : (p2s_twoIstar ? kObjP2sMle : kObjPoissonMle),
                                       expdata, M, Nchannels, true);
if (is_verbose()) {
        std::cout<< x[i] << "\t" << tIstar << "\t"  << std::endl;
}
        if (tIstar < tIstarbest) {
            tIstarbest = tIstar;
            taubest = x[i];
            gammabest = xm[1];
        }
    }

    x[0] = taubest;
    x[4] = gammabest;
    xtmp[0] = x[0]; xtmp[1] = x[4];

    // calculate model function for taubest
    correct_input(xtmp, xm, corrections, 1);
    DecayFit23::modelf(xm, irf, bg, Nchannels, p->dt, corrections, M);
    fit_signals.normM(M, Nchannels);

    x[7] = xtmp[7]; x[8] = xtmp[8];
    return tIstarbest;

}

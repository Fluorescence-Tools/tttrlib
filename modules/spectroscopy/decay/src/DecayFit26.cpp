// SPDX-License-Identifier: BSD-3-Clause
#include "DecayFit26.h"
#include "Verbose.h"

#include <limits>


static thread_local double Sp, Ss, Bp, Bs;
void DecayFit26::correct_input(double* x, double* xm)
{
if (is_verbose()) {
    std::cout<<"correct_input26"<<std::endl;
}
    // x[0] is the fraction of pattern 1 and belongs in [0, 1]. The clamp here
    // is only what keeps the model arithmetically sensible; the *bound* is the
    // optimiser's, via set_bounds in fit() below.
    //
    // This used to also set a thread-local `penalty` (-x[0] below zero,
    // x[0]-1 above one) that targetf added to the objective. Unlike fit23's
    // version of the same idea that one was correctly signed, so this is a
    // tidy-up rather than a bug fix -- but it had fit23's other two problems:
    // it duplicated a mechanism i_lbfgs already provides, and being added to
    // the objective outside the model it would be invisible to an analytic
    // gradient, which sees only what the registered callback returns. i_lbfgs
    // adds a soft bound's penalty *and its gradient* to both the
    // finite-difference and the analytic path.
    xm[0] = x[0] < 0.0 ? 0.0 : (x[0] > 1.0 ? 1.0 : x[0]);
if (is_verbose()) {
    std::cout<<"x[0]: " << x[0] <<std::endl;
    std::cout<<"xm[0]: " << xm[0] <<std::endl;
}
}


double DecayFit26::targetf(double* x, void* pv)
{
    if (((DecayFitContext *) pv) != nullptr) ((DecayFitContext *) pv)->iterations++;

    double s = 0., xm[1], w, f;
    int i;
    DecayFitContext* p = (DecayFitContext*)pv;

    const int *expdata = p->counts;
    int Nchannels = 2 * p->n_bins;
    double *irf = const_cast<double *>(p->irf()), *bg = const_cast<double *>(p->background()), *M = p->model();

    correct_input(x, xm);
    f = xm[0];
    // irf is pattern 1, bg is pattern 2
    for(i=0; i<Nchannels; i++)
    {
        M[i] = f*irf[i] + (1.-f)*bg[i];
        s += expdata[i];
    }
    for(i=0; i<Nchannels; i++) M[i] *= s;

    // divide here Nchannels / 2, because Wcm multiplies Nchannels by two
    w = decay_objective_score(p->objective, expdata, M, Nchannels / 2, false);

    return w/Nchannels;

}


/*!
 * Score `x` without optimising.
 *
 * Unlike the other Fit2x models this one needs no preamble: its objective
 * derives the scale from the data itself and reads no thread-local state, so it
 * is already a complete evaluation. The entry point exists so every model is
 * reached the same way.
 */
double DecayFit26::evaluate(double *x, short *fixed, DecayFitContext *p) {
    if (p == nullptr || x == nullptr || fixed == nullptr || !p->is_usable()) {
        return std::numeric_limits<double>::infinity();
    }
    return DecayFit26::targetf(x, p);
}


double DecayFit26::fit(double* x, short* fixed, DecayFitContext* p)
{
    // x is:
    // [0] fraction of pattern 1
    double tIstar, xm[1], f, s = 0., s1 = 0., s2 = 0.;
    int i, info;

    // Fail safely on inconsistently sized arrays instead of reading past the end
    // of irf/background (see DecayFitContext::is_usable). fit26 uses
    // the full data length as its channel count, so irf/background must be at
    // least that long.
    if (p == nullptr || x == nullptr || fixed == nullptr ||
        !p->is_usable()) {
        if (x != nullptr) x[0] = -1.0;
        return std::numeric_limits<double>::infinity();
    }

    const int *expdata = p->counts;
    int Nchannels = 2 * p->n_bins;
    double *irf = const_cast<double *>(p->irf()), *bg = const_cast<double *>(p->background()), *M = p->model();
    for(i=0; i<Nchannels; i++)
    {
        s1 += irf[i];
        s2 += bg[i];
    }
    s1 = 1./s1; s2 = 1./s2;
    for(i=0; i<Nchannels; i++)
    {
        irf[i]*=s1;
        bg[i]*=s2;
    }
    bfgs bfgs_o(targetf, 1);
    // The fraction is in [0, 1]. A soft bound, not the clamp in correct_input:
    // a clamped objective is flat outside the box, so nothing points back in.
    bfgs_o.set_bounds(0, 0.0, 1.0);
        // Bounds are priors in this interface, so anything the caller attached
        // to a slot has to reach the optimiser that actually moves it. Without
        // this the bound was accepted, stored, serialised — and ignored.
        // Applied after the default above so a caller-supplied prior wins.
        apply_context_bounds(bfgs_o, p, 1);
    info = bfgs_o.minimize(x,p);

    correct_input(x, xm);
    f = xm[0];

    // irf is pattern 1, bg is pattern 2
    for(i=0; i<Nchannels; i++)
    {
        M[i] = f*irf[i] + (1.-f)*bg[i];
        s += expdata[i];
    }
    for(i=0; i<Nchannels; i++) M[i] *= s;

    // divide here Nchannels / 2, because twoIstar multiplies Nchannels by two
    tIstar = decay_objective_score(p->objective, expdata, M, Nchannels / 2, true);
    if (info==5) x[0] = -1.;		// for report
    x[1]=1.-x[0];
    return tIstar;

}

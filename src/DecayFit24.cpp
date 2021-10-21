#include "DecayFit24.h"


static DecayFitCorrections fit_corrections;
static DecayFitIntegrateSignals fit_signals;
static DecayFitSettings fit_settings;



void DecayFit24::correct_input(double *x, double *xm, LVDoubleArray *corrections, int return_r) {
    fit_signals.corrections = &fit_corrections;
    // correct input parameters (take care of unreasonable values)
    xm[0] = x[0];
    if (xm[0] < 0.001) xm[0] = 0.001;    // tau1 > 0
    xm[2] = x[2];
    if (xm[2] < 0.001) xm[2] = 0.001;    // tau2 > 0

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
        fit_corrections.g = corrections->data[1];
        fit_corrections.l1 = corrections->data[2];
        fit_corrections.l2 = corrections->data[3];
        x[7] = fit_signals.rs();
        x[6] = fit_signals.r();
    }

}


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
    fit_signals.corrections = &fit_corrections;

    double w, xm[5], Bgamma;
    MParam *p = (MParam *) pv;

    LVI32Array *expdata = *(p->expdata);
    int Nchannels = expdata->length / 2;
    LVDoubleArray
        *irf = *(p->irf), *bg = *(p->bg),
        *corrections = *(p->corrections), *M = *(p->M);

    correct_input(x, xm, corrections, 0);
    modelf(xm, irf->data, bg->data, Nchannels, p->dt, corrections->data, M->data);
    fit_signals.normM_p2s(M->data, Nchannels);

    w = Wcm(expdata->data, M->data, Nchannels);

    if (fit_settings.softbifl & (fit_signals.Bexpected > 0.)) {
        Bgamma = xm[1] * (fit_signals.Sp + fit_signals.Ss);
        w -= Bgamma * log(fit_signals.Bexpected) - loggammaf(Bgamma + 1.);
    }
    return w / Nchannels;

}

double DecayFit24::fit(double *x, short *fixed, MParam *p) {
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

    if (fit_settings.firstcall) init_fact();
    fit_settings.firstcall = 0;
    fit_settings.softbifl = (x[5] < 0.);

    LVI32Array *expdata = *(p->expdata);
    int Nchannels = expdata->length / 2;
    LVDoubleArray *irf = *(p->irf), *bg = *(p->bg),
            *corrections = *(p->corrections), *M = *(p->M);

    // total signal and background
    fit_signals.compute_signal_and_background(p);

    bfgs bfgs_o(targetf, 5);

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
    modelf(xm, irf->data, bg->data, Nchannels, p->dt, corrections->data, M->data);
    fit_signals.normM_p2s(M->data, Nchannels);

    tIstar = twoIstar(expdata->data, M->data, Nchannels);

    if (info == 5 || x[0] < 0.) x[0] = -1.;        // for report
    if (info == 5 || x[2] < 0.) x[2] = -1.;
    x[1] = xm[1];
    x[4] = xm[4];

    return tIstar;
}

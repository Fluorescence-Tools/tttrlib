#include "DecayFit23.h"


static DecayFitIntegrateSignals fit_signals;
static DecayFitCorrections fit_corrections;
static DecayFitSettings fit_settings;



void DecayFit23::correct_input(double *x, double *xm, LVDoubleArray *corrections, int return_r) {
    xm[0] = x[0];
    if (xm[0] < 0.001) {
        xm[0] = 0.001;    // tau > 0
        fit_settings.penalty = -x[0];
    } else fit_settings.penalty = 0.;

    fit_corrections.set_gamma(x[1]);
    fit_corrections.period = corrections->data[0];
    fit_corrections.g = corrections->data[1];
    fit_corrections.l1 = corrections->data[2];
    fit_corrections.l2 = corrections->data[3];
    fit_corrections.convolution_stop = (int) corrections->data[4];

    xm[2] = x[2];
    // anisotropy
    double r = fit_signals.r();
    if (!fit_settings.fixedrho) {
        xm[3] = fit_signals.rho(x[0], x[2]); // rho = tau/(r0/r-1)
        x[3] = xm[3];
    } else xm[3] = x[3];

    if (return_r) {
        x[7] = fit_signals.rs();
        x[6] = r;
    }
#if VERBOSE_FIT2X
    std::cout << "CORRECT_INPUT23" << std::endl;
    std::cout << fit_corrections.str();
    std::cout << fit_signals.str();
    std::cout << "-- Corrected parameters:" << std::endl;
    std::cout << "-- tau / tau_c: " << x[0] << "/" << xm[0] << std::endl;
    std::cout << "-- gamma / gamma_c: " << x[1] << "/" << xm[1] << std::endl;
    std::cout << "-- r0 / r0_c: " << x[2] << "/" << xm[2] << std::endl;
    std::cout << "-- rho / rho_c: " << x[3] << "/" << xm[3] << std::endl;
#endif
}

int DecayFit23::modelf(
        double *param,            // here: [tau gamma r0 rho]
        double *irf,
        double *bg,
        int Nchannels,
        double dt,                // time per channel
        double *corrections,      // [period g l1 l2]
        double *mfunction)        // out: model function in Jordi-girl format
{
    double x[4]; // amplitude, relaxation time array
    double tau, gamma, r0, rho, taurho;
    int i;
/************************ Input arguments ***********************/
    tau = param[0];
    gamma = param[1];
    r0 = param[2];
    rho = param[3] * dt / dt;

    fit_corrections.set_gamma(gamma);
    fit_corrections.period = corrections[0];
    fit_corrections.g = corrections[1];
    fit_corrections.l1 = corrections[2];
    fit_corrections.l2 = corrections[3];
    fit_corrections.convolution_stop = (int) corrections[4];

/************************* Model function ***********************/

    taurho = 1. / (1. / tau + 1. / rho);

    /// vv
    x[0] = 1.;
    x[1] = tau;
    x[2] = r0 * (2. - 3. * fit_corrections.l1);
    x[3] = taurho;
    fconv_per_cs(
            mfunction, x, irf,
            2, Nchannels - 1, Nchannels,
            fit_corrections.period, fit_corrections.convolution_stop, dt);

    /// vh
    x[0] = 1. / fit_corrections.g;
    x[2] = 1. / fit_corrections.g * r0 * (-1. + 3. * fit_corrections.l2);
    fconv_per_cs(mfunction + Nchannels, x, irf + Nchannels,
                 2, Nchannels - 1, Nchannels,
                 fit_corrections.period, fit_corrections.convolution_stop, dt);

    /// add background
    double sum_m = 0., tmpf;
    for (i = 0; i < 2 * Nchannels; i++) sum_m += mfunction[i];
    tmpf = (1. - gamma) / sum_m;
    for (i = 0; i < 2 * Nchannels; i++) mfunction[i] = mfunction[i] * tmpf + bg[i] * gamma;

#if VERBOSE_FIT2X
    std::cout << "COMPUTE MODEL23" << std::endl;
    std::cout << "-- tau: " << tau << std::endl;
    std::cout << "-- gamma: " << gamma << std::endl;
    std::cout << "-- r0: " << r0 << std::endl;
    std::cout << "-- rho: " << rho << std::endl;
    std::cout << fit_corrections.str();
#endif

    return 0;
}


double DecayFit23::targetf(double *x, void *pv) {

    double w, xm[8], Bgamma;
    MParam *p = (MParam *) pv;

    LVI32Array *expdata = *(p->expdata);
    int Nchannels = expdata->length / 2;
    LVDoubleArray *irf = *(p->irf), *bg = *(p->bg), *corrections = *(p->corrections), *M = *(p->M);
    DecayFit23::correct_input(x, xm, corrections, 0);
    fit_signals.compute_signal_and_background(p);

    DecayFit23::modelf(xm, irf->data, bg->data, Nchannels, p->dt, corrections->data, M->data);
    fit_signals.normM(M->data, 1., Nchannels);
    if (fit_settings.p2s_twoIstar)
        w = Wcm_p2s(expdata->data, M->data, Nchannels);
    else
        w = Wcm(expdata->data, M->data, Nchannels);

    if (fit_settings.softbifl && (fit_signals.Bexpected > 0.)) {
        w -= fit_signals.Bexpected * log(fit_signals.Bexpected) - loggammaf(Bgamma + 1.);
    }
    double v = w / Nchannels + fit_settings.penalty;
#if VERBOSE_FIT2X
    std::cout << "COMPUTING TARGET23" << std::endl;
    std::cout << "xm:" ; for(int i=0; i<8;i++) std::cout << xm[i] << " "; std::cout << std::endl;
    std::cout << "corrections:" ;
    std::cout << corrections->str() << std::endl;
    std::cout << "irf:" ;
    std::cout << irf->str() << std::endl;
    std::cout << "bg:" ;
    std::cout << bg->str() << std::endl;
    std::cout << "Data:" ;
    std::cout << expdata->str() << std::endl;
    std::cout << "Model:" ;
    std::cout << M->str() << std::endl;
    std::cout << "score:"  << v << std::endl;
#endif
    return v;
}


double DecayFit23::fit(double *x, short *fixed, MParam *p) {
    double tIstar, xm[8];
    int info = -1;

    if (fit_settings.firstcall) init_fact();

    fit_settings.firstcall = 0;
    fit_settings.softbifl = (x[4] < 0.);
    fit_settings.p2s_twoIstar = (x[5] > 0.);
    fit_signals.corrections = &fit_corrections;
    fit_settings.fixedrho = fixed[3];

    LVDoubleArray *corrections = *(p->corrections), *M = *(p->M);
    fit_signals.compute_signal_and_background(p);
    correct_input(x, xm, corrections, 1);

    LVI32Array *expdata = *(p->expdata);
    int Nchannels = expdata->length / 2;

    bfgs bfgs_o(DecayFit23::targetf, 4);

    bfgs_o.fix(1);    // gamma
    bfgs_o.fix(2);    // r0
    bfgs_o.fix(3);    // rho is set in targetf23

    // pre-fit with fixed gamma
    //  bfgs_o.maxiter = 20;
    if(!fixed[0]){
        bfgs_o.minimize(x, p);
    }else {
        bfgs_o.fix(0);
    }

    // fit with free gamma
    // bfgs_o.maxiter = 100;
    if (!fixed[1] && (x[4] <= 0.)) {
        bfgs_o.free(1);
        info = bfgs_o.minimize(x, p);
    }

    // use return_r to get the anisotropy in x
    correct_input(x, xm, corrections, 1);
    if (fit_settings.p2s_twoIstar)
        tIstar = twoIstar_p2s(expdata->data, M->data, Nchannels);
    else
        tIstar = twoIstar(expdata->data, M->data, Nchannels);

    if (info == 5 || x[0] < 0.) x[0] = -1.;        // for report
    x[1] = xm[1];
#if VERBOSE_FIT2X
    std::cout << "FIT23" << std::endl;
    std::cout << "-- Initial parameters / fixed: " << std::endl;
    std::cout << "-- tau: " << x[0] << " / " << fixed[0] << std::endl;
    std::cout << "-- gamma: " << x[1] << " / " << fixed[1] << std::endl;
    std::cout << "-- r0: " << x[2] << " / " << fixed[2] << std::endl;
    std::cout << "-- rho: " << x[3] << " / " << fixed[3] << std::endl;
    std::cout << "-- Soft BIFL scatter fit?: " << x[4] << std::endl;
    std::cout << "-- 2I*: P+2S?: " << x[5] << std::endl;
    std::cout << "-- r Scatter (output only): " << x[6] << std::endl;
    std::cout << "-- r Experimental (output only): " << x[7] << std::endl;
#endif
    return tIstar;
}


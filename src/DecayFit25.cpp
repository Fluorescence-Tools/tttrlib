#include "DecayFit25.h"


// normalization
static int fixedrho = 0;
static int softbifl = 0;
static int p2s_twoIstar = 0;
static int firstcall = 1;
static double penalty = 0.;


static DecayFitCorrections fit_corrections;
static DecayFitIntegrateSignals fit_signals;
static DecayFitSettings fit_settings;



void DecayFit25::correct_input(double* x, double* xm, LVDoubleArray* corrections, int return_r)
{
    // correct input parameters (take care of unreasonable values)
    // here x = [tau gamma r0 rho] + outputs
    fit_signals.corrections = &fit_corrections;

    xm[0] = x[0];
    penalty = 0.;
    xm[2] = x[2];
    fit_corrections.set_gamma(xm[1]);
    fit_corrections.g = corrections->data[1];
    fit_corrections.l1 = corrections->data[2];
    fit_corrections.l2 = corrections->data[3];

    if (!fixedrho) {
        xm[3] = fit_signals.rho(x[0], x[2]); // rho = tau/(r0/r-1)
        x[3] = xm[3];
    } else xm[3] = x[3];

    if (return_r) {
        x[7] = fit_signals.r();
        x[8] = fit_signals.rs();
    }

#ifdef VERBOSE_TTTRLIB
    std::cout << "correct_input25" << std::endl;
    std::cout<< "xm[1]:" << xm[1] << std::endl;
    std::cout << fit_corrections.str();
    std::cout << fit_signals.str();
    std::cout<< "rho:" << x[3] << std::endl;
    std::cout<< "tau:" << x[0] << std::endl;
    std::cout<< "r0:" << x[2] << std::endl;
#endif

}

double DecayFit25::targetf(double* x, void* pv)
{
    fit_signals.corrections = &fit_corrections;

    double w, xm[4], Bgamma;
    MParam* p = (MParam*)pv;

    LVI32Array* expdata = *(p->expdata);
    int Nchannels = expdata->length/2;
    LVDoubleArray *irf = *(p->irf), *bg = *(p->bg),
            *corrections = *(p->corrections), *M = *(p->M);

    correct_input(x, xm, corrections, 0);
    DecayFit23::modelf(xm, irf->data, bg->data, Nchannels, p->dt, corrections->data, M->data);
    fit_signals.normM(M->data, Nchannels);

    if (p2s_twoIstar)
        w = Wcm_p2s(expdata->data, M->data, Nchannels);
    else
        w = Wcm(expdata->data, M->data, Nchannels);

    if (softbifl & (fit_signals.Bexpected > 0.)) {
        Bgamma = xm[1]*(fit_signals.Sp+fit_signals.Ss);
        w -= Bgamma*log(fit_signals.Bexpected) - loggammaf(Bgamma+1.);
    }
    return w/Nchannels + penalty;

}


double DecayFit25::fit (double* x, short* fixed, MParam* p)
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

    if (firstcall) init_fact();
    firstcall = 0;
    softbifl = (x[6]<0.);
    p2s_twoIstar = 1;

    LVI32Array* expdata = *(p->expdata);
    int Nchannels = expdata->length/2;
    LVDoubleArray *irf = *(p->irf), *bg = *(p->bg),
            *corrections = *(p->corrections), *M = *(p->M);

    // total signal and background

    fixedrho = 0;
    fit_signals.compute_signal_and_background(p);

    // xtmp: same order as for fit23: [tau gamma r0 rho]
    xtmp[0] = x[0]; xtmp[1] = x[4]; xtmp[2] = x[5]; xtmp[3] = 1.;

    bfgs bfgs_o(targetf, 4);

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
        DecayFit23::modelf(xm, irf->data, bg->data, Nchannels, p->dt, corrections->data, M->data);
        fit_signals.normM(M->data, Nchannels);
        if (p2s_twoIstar) tIstar = twoIstar_p2s(expdata->data, M->data, Nchannels);
        else tIstar = twoIstar(expdata->data, M->data, Nchannels);
#ifdef VERBOSE_TTTRLIB
        std::cout<< x[i] << "\t" << tIstar << "\t"  << std::endl;
#endif
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
    DecayFit23::modelf(xm, irf->data, bg->data, Nchannels, p->dt, corrections->data, M->data);
    fit_signals.normM(M->data, Nchannels);

    x[7] = xtmp[7]; x[8] = xtmp[8];
    return tIstarbest;

}

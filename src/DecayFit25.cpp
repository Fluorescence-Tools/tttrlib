// SPDX-License-Identifier: BSD-3-Clause
#include "DecayFit25.h"
#include "include/Verbose.h"


// normalization
static thread_local int fixedrho = 0;
static thread_local int softbifl = 0;
static thread_local int p2s_twoIstar = 0;
static thread_local int firstcall = 1;
static thread_local double penalty = 0.;


static thread_local DecayFitCorrections fit_corrections;
static thread_local DecayFitIntegrateSignals fit_signals;
static thread_local DecayFitSettings fit_settings;



void DecayFit25::correct_input(double* x, double* xm, double* corrections, int return_r)
{
    // correct input parameters (take care of unreasonable values)
    // here x = [tau gamma r0 rho] + outputs
    fit_signals.corrections = &fit_corrections;

    xm[0] = x[0];
    penalty = 0.;
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
    fit_signals.corrections = &fit_corrections;

    double w, xm[4], Bgamma;
    DecayFitData* p = (DecayFitData*)pv;

    int *expdata = p->data.data();
    int Nchannels = p->n_channels();
    double *irf = p->irf.data(), *bg = p->background.data(),
            *corrections = p->corrections.data(), *M = p->model.data();

    correct_input(x, xm, corrections, 0);
    DecayFit23::modelf(xm, irf, bg, Nchannels, p->dt, corrections, M);
    fit_signals.normM(M, Nchannels);

    if (p2s_twoIstar)
        w = Wcm_p2s(expdata, M, Nchannels);
    else
        w = Wcm(expdata, M, Nchannels);

    if (softbifl & (fit_signals.Bexpected > 0.)) {
        Bgamma = xm[1]*(fit_signals.Sp+fit_signals.Ss);
        w -= Bgamma*log(fit_signals.Bexpected) - loggammaf(Bgamma+1.);
    }
    return w/Nchannels + penalty;

}


double DecayFit25::fit (double* x, short* fixed, DecayFitData* p)
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

    if (firstcall) init_fact();
    firstcall = 0;
    softbifl = (x[6]<0.);
    p2s_twoIstar = 1;

    int *expdata = p->data.data();
    int Nchannels = p->n_channels();
    double *irf = p->irf.data(), *bg = p->background.data(),
            *corrections = p->corrections.data(), *M = p->model.data();

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
        DecayFit23::modelf(xm, irf, bg, Nchannels, p->dt, corrections, M);
        fit_signals.normM(M, Nchannels);
        if (p2s_twoIstar) tIstar = twoIstar_p2s(expdata, M, Nchannels);
        else tIstar = twoIstar(expdata, M, Nchannels);
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


std::string DecayFit25::to_json(const double *x,
                               const short *fixed,
                               const DecayFitData *p,
                               double result) {
    json j;

    if (x != nullptr) {
        j["parameters"] = json::array();
        for (int i = 0; i < 9; i++) {
            j["parameters"].push_back(x[i]);
        }
    }

    if (fixed != nullptr) {
        j["fixed"] = json::array();
        for (int i = 0; i < 5; i++) {
            j["fixed"].push_back(static_cast<int>(fixed[i]));
        }
    }

    j["result"] = result;

    if (p != nullptr) {
        json jp;
        jp["dt"] = p->dt;
        jp["data_length"] = static_cast<int>(p->data.size());
        {
            json jcorr = json::array();
            for (double v: p->corrections) jcorr.push_back(v);
            jp["corrections"] = jcorr;
        }
        jp["irf_length"] = static_cast<int>(p->irf.size());
        jp["background_length"] = static_cast<int>(p->background.size());
        j["mparam"] = jp;
    }

    return j.dump();
}


void DecayFit25::from_json(const json &j,
                          double *x,
                          short *fixed) {
    if (j.contains("parameters") && j.at("parameters").is_array()) {
        const auto &params = j.at("parameters");
        for (int i = 0; i < std::min(9, static_cast<int>(params.size())); ++i) {
            x[i] = params.at(i);
        }
    }

    if (j.contains("fixed") && j.at("fixed").is_array()) {
        const auto &fixed_arr = j.at("fixed");
        for (int i = 0; i < std::min(5, static_cast<int>(fixed_arr.size())); ++i) {
            fixed[i] = static_cast<short>(fixed_arr.at(i));
        }
    }
}

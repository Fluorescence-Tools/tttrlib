// SPDX-License-Identifier: BSD-3-Clause
#include "DecayFit26.h"
#include "include/Verbose.h"


static thread_local double Sp, Ss, Bp, Bs;
static thread_local double penalty = 0.;


void DecayFit26::correct_input(double* x, double* xm)
{
if (is_verbose()) {
    std::cout<<"correct_input26"<<std::endl;
}
    // correct input parameters (take care of unreasonable values)
    xm[0] = x[0]; // fraction of pattern 1 is between 0 and 1
    if (xm[0]<0.0) {
        xm[0] = 0.0; // tau > 0
        penalty = -x[0];
    }
    else if (xm[0]>1.0) {
        xm[0] = 1.0; // tau > 0
        penalty = x[0]-1.0;
    }
    else penalty = 0.;
if (is_verbose()) {
    std::cout<<"x[0]: " << x[0] <<std::endl;
    std::cout<<"xm[0]: " << xm[0] <<std::endl;
}
}


double DecayFit26::targetf(double* x, void* pv)
{

    double s = 0., xm[1], w, f;
    int i;
    DecayFitData* p = (DecayFitData*)pv;

    int *expdata = p->data.data();
    int Nchannels = static_cast<int>(p->data.size());
    double *irf = p->irf.data(), *bg = p->background.data(), *M = p->model.data();

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
    w = Wcm(expdata, M, Nchannels / 2);

    return w/Nchannels + penalty;

}


double DecayFit26::fit(double* x, short* fixed, DecayFitData* p)
{
    // x is:
    // [0] fraction of pattern 1
    double tIstar, xm[1], f, s = 0., s1 = 0., s2 = 0.;
    int i, info;

    int *expdata = p->data.data();
    int Nchannels = static_cast<int>(p->data.size());
    double *irf = p->irf.data(), *bg = p->background.data(), *M = p->model.data();
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
    tIstar = twoIstar(expdata, M, Nchannels / 2);
    if (info==5) x[0] = -1.;		// for report
    x[1]=1.-x[0];
    return tIstar;

}


std::string DecayFit26::to_json(const double *x,
                               const short *fixed,
                               const DecayFitData *p,
                               double result) {
    json j;

    if (x != nullptr) {
        j["parameters"] = json::array();
        for (int i = 0; i < 2; i++) {
            j["parameters"].push_back(x[i]);
        }
    }

    if (fixed != nullptr) {
        j["fixed"] = json::array();
        for (int i = 0; i < 1; i++) {
            j["fixed"].push_back(static_cast<int>(fixed[i]));
        }
    }

    j["result"] = result;

    if (p != nullptr) {
        json jp;
        jp["data_length"] = static_cast<int>(p->data.size());
        jp["irf_length"] = static_cast<int>(p->irf.size());
        jp["background_length"] = static_cast<int>(p->background.size());
        j["mparam"] = jp;
    }

    return j.dump();
}


void DecayFit26::from_json(const json &j,
                          double *x,
                          short *fixed) {
    if (j.contains("parameters") && j.at("parameters").is_array()) {
        const auto &params = j.at("parameters");
        for (int i = 0; i < std::min(2, static_cast<int>(params.size())); ++i) {
            x[i] = params.at(i);
        }
    }

    if (j.contains("fixed") && j.at("fixed").is_array()) {
        const auto &fixed_arr = j.at("fixed");
        for (int i = 0; i < std::min(1, static_cast<int>(fixed_arr.size())); ++i) {
            fixed[i] = static_cast<short>(fixed_arr.at(i));
        }
    }
}

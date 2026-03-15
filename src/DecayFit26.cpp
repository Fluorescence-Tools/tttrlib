#include "DecayFit26.h"
#include "include/Verbose.h"


static double Sp, Ss, Bp, Bs;
static double penalty = 0.;


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
    MParam* p = (MParam*)pv;

    LVI32Array* expdata = *(p->expdata);
    int Nchannels = expdata->length;
    LVDoubleArray *irf = *(p->irf), *bg = *(p->bg), *M = *(p->M);

    correct_input(x, xm);
    f = xm[0];
    // irf is pattern 1, bg is pattern 2
    for(i=0; i<Nchannels; i++)
    {
        M->data[i] = f*irf->data[i] + (1.-f)*bg->data[i];
        s += expdata->data[i];
    }
    for(i=0; i<Nchannels; i++) M->data[i] *= s;

    // divide here Nchannels / 2, because Wcm multiplies Nchannels by two
    w = Wcm(expdata->data, M->data, Nchannels / 2);

    return w/Nchannels + penalty;

}


double DecayFit26::fit(double* x, short* fixed, MParam* p)
{
    // x is:
    // [0] fraction of pattern 1
    double tIstar, xm[1], f, s = 0., s1 = 0., s2 = 0.;
    int i, info;

    LVI32Array* expdata = *(p->expdata);
    int Nchannels = expdata->length;
    LVDoubleArray *irf = *(p->irf), *bg = *(p->bg), *M = *(p->M);
    for(i=0; i<Nchannels; i++)
    {
        s1 += irf->data[i];
        s2 += bg->data[i];
    }
    s1 = 1./s1; s2 = 1./s2;
    for(i=0; i<Nchannels; i++)
    {
        irf->data[i]*=s1;
        bg->data[i]*=s2;
    }
    bfgs bfgs_o(targetf, 1);
    info = bfgs_o.minimize(x,p);

    correct_input(x, xm);
    f = xm[0];

    // irf is pattern 1, bg is pattern 2
    for(i=0; i<Nchannels; i++)
    {
        M->data[i] = f*irf->data[i] + (1.-f)*bg->data[i];
        s += expdata->data[i];
    }
    for(i=0; i<Nchannels; i++) M->data[i] *= s;

    // divide here Nchannels / 2, because twoIstar multiplies Nchannels by two
    tIstar = twoIstar(expdata->data, M->data, Nchannels / 2);
    if (info==5) x[0] = -1.;		// for report
    x[1]=1.-x[0];
    return tIstar;

}


std::string DecayFit26::to_json(const double *x,
                               const short *fixed,
                               const MParam *p,
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
        if (p->expdata && *(p->expdata)) {
            jp["data_length"] = (*(p->expdata))->length;
        }
        if (p->irf && *(p->irf)) {
            jp["irf_length"] = (*(p->irf))->length;
        }
        if (p->bg && *(p->bg)) {
            jp["background_length"] = (*(p->bg))->length;
        }
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

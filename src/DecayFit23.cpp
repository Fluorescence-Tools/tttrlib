#include "DecayFit23.h"
#include "include/Verbose.h"

#include <algorithm>
#include <limits>


static DecayFitIntegrateSignals fit_signals;
static DecayFitCorrections fit_corrections;
static DecayFitSettings fit_settings;

namespace {

constexpr double kMinTau = 1.0e-3;
constexpr double kMinRho = 1.0e-6;
constexpr double kMinGamma = 0.0;
constexpr double kMaxGamma = 0.999;

inline double clamp_value(double value, double lower, double upper) {
    return std::max(lower, std::min(value, upper));
}

struct Decay23Parameters {
    double tau;
    double gamma;
    double r0;
    double rho;
};

inline Decay23Parameters sanitise_parameters(const double *param) {
    Decay23Parameters result{};
    result.tau = param[0] < kMinTau ? kMinTau : param[0];
    result.gamma = clamp_value(param[1], kMinGamma, kMaxGamma);
    result.r0 = param[2];
    result.rho = param[3] < kMinRho ? kMinRho : param[3];
    return result;
}

inline void apply_corrections(double *corrections) {
    fit_corrections.period = corrections[0];
    fit_corrections.g = corrections[1];
    fit_corrections.l1 = corrections[2];
    fit_corrections.l2 = corrections[3];
    fit_corrections.convolution_stop = static_cast<int>(corrections[4]);
}

inline double safe_harmonic_mean(double a, double b) {
    if (b <= 0.) {
        return a;
    }
    const double denom = 1. / a + 1. / b;
    if (denom <= std::numeric_limits<double>::min()) {
        return a;
    }
    return 1. / denom;
}

} // namespace



void DecayFit23::correct_input(double *x, double *xm, LVDoubleArray *corrections, int return_r) {
    fit_signals.corrections = &fit_corrections;

    const Decay23Parameters initial = sanitise_parameters(x);

    xm[0] = initial.tau;
    fit_settings.penalty = (x[0] < kMinTau) ? -x[0] : 0.;

    fit_corrections.set_gamma(initial.gamma);
    apply_corrections(corrections->data);

    xm[1] = initial.gamma;
    xm[2] = initial.r0;

    double rho_value = initial.rho;
    if (!fit_settings.fixedrho) {
        rho_value = fit_signals.rho(xm[0], xm[2]);
        x[3] = rho_value;
    }
    if (rho_value < kMinRho) {
        rho_value = kMinRho;
    }
    xm[3] = rho_value;

    if (return_r) {
        x[7] = fit_signals.rs();
        x[6] = fit_signals.r();
    }
if (is_verbose()) {
    std::cout << "CORRECT_INPUT23" << std::endl;
    std::cout << fit_corrections.str();
    std::cout << fit_signals.str();
    std::cout << "-- Corrected parameters:" << std::endl;
    std::cout << "-- tau / tau_c: " << x[0] << "/" << xm[0] << std::endl;
    std::cout << "-- gamma / gamma_c: " << x[1] << "/" << xm[1] << std::endl;
    std::cout << "-- r0 / r0_c: " << x[2] << "/" << xm[2] << std::endl;
    std::cout << "-- rho / rho_c: " << x[3] << "/" << xm[3] << std::endl;
}
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
    (void)dt; // parameter kept for API compatibility
    fit_signals.corrections = &fit_corrections;

    double x[4]; // amplitude, relaxation time array
    const Decay23Parameters safe_param = sanitise_parameters(param);
    const double tau = safe_param.tau;
    const double gamma = safe_param.gamma;
    const double r0 = safe_param.r0;
    const double rho = safe_param.rho;

    fit_corrections.set_gamma(gamma);
    apply_corrections(corrections);

    const double taurho = safe_harmonic_mean(tau, rho);

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
    x[2] = x[0] * r0 * (-1. + 3. * fit_corrections.l2);
    fconv_per_cs(mfunction + Nchannels, x, irf + Nchannels,
                 2, Nchannels - 1, Nchannels,
                 fit_corrections.period, fit_corrections.convolution_stop, dt);

    /// add background
    double sum_m = 0.;
    for (int i = 0; i < 2 * Nchannels; i++) sum_m += mfunction[i];
    if (sum_m <= 0.) {
        for (int i = 0; i < 2 * Nchannels; i++) {
            mfunction[i] = bg[i] * gamma;
        }
    } else {
        const double scale = (1. - gamma) / sum_m;
        for (int i = 0; i < 2 * Nchannels; i++) {
            mfunction[i] = mfunction[i] * scale + bg[i] * gamma;
        }
    }

if (is_verbose()) {
    std::cout << "COMPUTE MODEL23" << std::endl;
    std::cout << "-- tau: " << tau << std::endl;
    std::cout << "-- gamma: " << gamma << std::endl;
    std::cout << "-- r0: " << r0 << std::endl;
    std::cout << "-- rho: " << rho << std::endl;
    std::cout << fit_corrections.str();
}

    return 0;
}


double DecayFit23::targetf(double *x, void *pv) {

    double w, xm[8], Bgamma;
    (void)Bgamma; // silence unused variable warning on MSVC
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
        w -= fit_signals.Bexpected * log(fit_signals.Bexpected) - loggammaf(fit_signals.Bexpected + 1.);
    }
    double v = w / Nchannels + fit_settings.penalty;
if (is_verbose()) {
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
}
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
        info = bfgs_o.minimize(x, p);
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
if (is_verbose()) {
    std::cout << "FIT23" << std::endl;
    std::cout << "-- BFGS info: " << info << std::endl;
    std::cout << "-- Initial parameters / fixed: " << std::endl;
    std::cout << "-- tau: " << x[0] << " / " << fixed[0] << std::endl;
    std::cout << "-- gamma: " << x[1] << " / " << fixed[1] << std::endl;
    std::cout << "-- r0: " << x[2] << " / " << fixed[2] << std::endl;
    std::cout << "-- rho: " << x[3] << " / " << fixed[3] << std::endl;
    std::cout << "-- Soft BIFL scatter fit?: " << x[4] << std::endl;
    std::cout << "-- 2I*: P+2S?: " << x[5] << std::endl;
    std::cout << "-- r Scatter (output only): " << x[6] << std::endl;
    std::cout << "-- r Experimental (output only): " << x[7] << std::endl;
}
    return tIstar;
}


std::string DecayFit23::fit_to_json(const double *x,
                                   const short *fixed,
                                   const MParam *p,
                                   double result) {
    json j;

    if (x != nullptr) {
        j["parameters"] = json::array();
        for (int i = 0; i < 8; i++) {
            j["parameters"].push_back(x[i]);
        }
    }

    if (fixed != nullptr) {
        j["fixed"] = json::array();
        for (int i = 0; i < 4; i++) {
            j["fixed"].push_back(static_cast<int>(fixed[i]));
        }
    }

    j["result"] = result;

    if (p != nullptr) {
        json jp;
        jp["dt"] = p->dt;
        if (p->expdata && *(p->expdata)) {
            jp["data_length"] = (*(p->expdata))->length;
        }
        if (p->corrections && *(p->corrections)) {
            LVDoubleArray *corr = *(p->corrections);
            json jcorr = json::array();
            for (int i = 0; i < corr->length; ++i) {
                jcorr.push_back(corr->data[i]);
            }
            jp["corrections"] = jcorr;
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


std::string DecayFit23::to_json(const double *x,
                               const short *fixed,
                               const MParam *p,
                               double result) {
    return fit_to_json(x, fixed, p, result);
}


void DecayFit23::from_json(const json &j,
                          double *x,
                          short *fixed) {
    if (j.contains("parameters") && j.at("parameters").is_array()) {
        const auto &params = j.at("parameters");
        for (int i = 0; i < std::min(8, static_cast<int>(params.size())); ++i) {
            x[i] = params.at(i);
        }
    }

    if (j.contains("fixed") && j.at("fixed").is_array()) {
        const auto &fixed_arr = j.at("fixed");
        for (int i = 0; i < std::min(4, static_cast<int>(fixed_arr.size())); ++i) {
            fixed[i] = static_cast<short>(fixed_arr.at(i));
        }
    }
}


std::string DecayFit23::modelf_to_json(const double *param,
                                      const double *irf,
                                      const double *bg,
                                      int Nchannels,
                                      double dt,
                                      const double *corrections,
                                      const double *mfunction,
                                      int result) {
    json j;

    if (param != nullptr) {
        j["parameters"] = json::array();
        for (int i = 0; i < 4; i++) {
            j["parameters"].push_back(param[i]);
        }
    }

    if (corrections != nullptr) {
        j["corrections"] = json::array();
        for (int i = 0; i < 5; i++) {
            j["corrections"].push_back(corrections[i]);
        }
    }

    j["Nchannels"] = Nchannels;
    j["dt"] = dt;
    j["result"] = result;

    if (irf != nullptr) {
        json jirf = json::array();
        for (int i = 0; i < 2 * Nchannels; ++i) {
            jirf.push_back(irf[i]);
        }
        j["irf"] = jirf;
    }

    if (bg != nullptr) {
        json jbg = json::array();
        for (int i = 0; i < 2 * Nchannels; ++i) {
            jbg.push_back(bg[i]);
        }
        j["background"] = jbg;
    }

    if (mfunction != nullptr) {
        json jm = json::array();
        for (int i = 0; i < 2 * Nchannels; ++i) {
            jm.push_back(mfunction[i]);
        }
        j["model"] = jm;
    }

    return j.dump();
}


// SPDX-License-Identifier: BSD-3-Clause

#ifndef TTTRLIB_DECAYFIT_H
#define TTTRLIB_DECAYFIT_H

#include "Verbose.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <sstream>

#include <nlohmann/json_fwd.hpp>

#include "i_lbfgs.h"
#include "DecayFitContext.h"
#include "DecayConvolution.h"
#include "DecayStatistics.h"

using json = nlohmann::json;


struct DecayFitCorrections {

    double gamma = 0.0;
    double g = 1.0;
    double l1 = 0.0;
    double l2 = 0.0;
    double period = 1000;
    int convolution_stop = 0;

    void set_gamma(double v) {
        if (v < 0.)
            gamma = 0.;        // 0 < gamma < 0.999
        else if (v > 0.999)
            gamma = 0.999;
        else
            gamma = v;
    }

    std::string str() {
        auto s = std::stringstream();
        s << "-- Correction factors:\n";
        s << "-- g-factor: " << g << std::endl;
        s << "-- l1, l2: " << l1 << ", " << l2 << std::endl;
        s << "-- period: " << period << std::endl;
        s << "-- convolution_stop: " << convolution_stop << std::endl;
        return s.str();
    }

    json to_json() const;

    void from_json(const json &j);

    explicit DecayFitCorrections(
            double gamma = 0.0,
            double g = 1.0,
            double l1 = 0.0,
            double l2 = 0.0,
            double period = 1000,
            int convolution_stop = 0
    ) {
        this->gamma = gamma;
        this->g = g;
        this->l1 = l1;
        this->l2 = l2;
        this->period = period;
        this->convolution_stop = convolution_stop;
    }

};


struct DecayFitSettings {

    int fixedrho = 0;
    int softbifl = 0;
    int p2s_twoIstar = 0;
    int firstcall = 1;
    double penalty = 0.0;

    std::string str() {
        auto s = std::stringstream();
        s << "DECAYFITSETTINGS: " << std::endl;
        s << "-- fixedrho: " << fixedrho << std::endl;
        s << "-- softbifl: " << softbifl << std::endl;
        s << "-- p2s_twoIstar: " << p2s_twoIstar << std::endl;
        s << "-- firstcall: " << firstcall << std::endl;
        s << "-- penalty: " << penalty << std::endl;
        return s.str();
    }

    json to_json() const;

    void from_json(const json &j);
};


struct DecayFitIntegrateSignals {

    DecayFitCorrections *corrections = nullptr;

    /// Total signal parallel
    double Sp = 0.0;

    /// Total signal perpendicular
    double Ss = 0.0;

    /// Total background signal parallel
    double Bp = 0.0;

    /// Total background signal perpendicular
    double Bs = 0.0;

    /// Total background
    double B = 0.0;

    /// expected <B> corresponding to the mean Bg signal
    double Bexpected = 0.0;


    double Fp() {
        double g = 1.0;
        if (corrections != nullptr) {
            g = corrections->gamma;
        }
        if (g == 1.0) {
            return (Sp - Bp);
        } else {
            return (Sp - g * Bp) / (1. - g);
        }
    }

    double Fs() {
        double g = 1.0, r;
        if (corrections != nullptr) {
            g = corrections->gamma;
        }
        if (g == 1.0) {
            r = (Ss - Bs);
        } else {
            r = (Ss - g * Bs) / (1. - g);
        }
        if (is_verbose()) {
            std::cout << "Fs()" << std::endl;
            std::cout << "g:" << g << std::endl;
            std::cout << "Ss:" << Ss << std::endl;
            std::cout << "Bs:" << Bs << std::endl;
            std::cout << "Fs:" << r << std::endl;
        }
        return r;
    }

    double r() {
        double fp = Fp();
        double fs = Fs();
        double g = 1.0, l1 = 0.0, l2 = 0.0;
        if (corrections != nullptr) {
            g = corrections->g;
            l1 = corrections->l1;
            l2 = corrections->l2;
        }

        if (is_verbose()) {
            std::cout << "fp:" << fp << std::endl;
            std::cout << "fs:" << fs << std::endl;
            std::cout << "g:" << g << std::endl;
            std::cout << "l1:" << l1 << std::endl;
            std::cout << "l2:" << l2 << std::endl;
        }

        double nom = (fp - g * fs);
        double denom = (fp * (1. - 3. * l2) + (2. - 3. * l1) * g * fs);
        return nom / denom;
    }

    double rho(double tau, double r0) {
        double rh = tau / (r0 / r() - 1.);
        return std::max(rh, 1.e-4);
    }

    double rs() {
        double g = 1.0, l1 = 0.0, l2 = 0.0;
        if (corrections != nullptr) {
            g = corrections->g;
            l1 = corrections->l1;
            l2 = corrections->l2;
        }
        return (Sp - g * Ss) / (Sp * (1. - 3. * l2) + (2. - 3. * l1) * g * Ss);
    }

    /*!
     * \brief Integrate the parallel/perpendicular signal and background.
     *
     * Takes the arrays rather than a container so it serves every model and does
     * not tie the shared statistics to one model family's data layout.
     *
     * \param counts Integer counts, `2 * n_bins`, parallel then perpendicular.
     * \param bg Background pattern, same layout.
     * \param n_bins Bins per polarization.
     */
    void compute_signal_and_background(const int *counts, const double *bg, int n_bins);

    void normM(double *M, int Nchannels);

    void normM(double *M, double s, int Nchannels);

    void normM_p2s(double *M, int Nchannels);


    std::string str() {
        auto s = std::stringstream();
        s << "-- Signals: " << std::endl;
        s << "-- Bp, Bs: " << Bp << ", " << Bs << std::endl;
        s << "-- Sp, Ss: " << Sp << ", " << Ss << std::endl;
        return s.str();
    }

    json to_json() const;

    void from_json(const json &j);

    explicit DecayFitIntegrateSignals(DecayFitCorrections *corrections = nullptr) {
        this->corrections = corrections;
    }

};


class DecayFit {


public:

    static int modelf(double *param,
                      double *irf,
                      double *bg,
                      int Nchannels,
                      double dt,
                      double *corrections,
                      double *mfunction
    ) {
        return 0;
    };

    static double targetf(double *x, void *pv) {
        return 0.0;
    };


    static double fit(double *x, short *fixed, DecayFitContext *p) {
        return 0.0;
    };

    // The former ``fit_matrix_generic`` lived here: a batch loop parameterised by
    // a per-model function pointer, with each model wrapping it in its own
    // ``fit_matrix``. ``fit_batch`` (DecayFitModel.h) replaces both — it is
    // parameterised by the model *interface* instead, so there is one batch loop
    // and no per-model wrapper to keep in step with it.

    static void correct_input(double *x, double *xm, double *corrections, int return_r) {};

    static std::string parameters_to_json(double *param, int n_param);

    static void parameters_from_json(const json &j, double *param, int n_param);

    static std::string data_to_json(int *data, int n_data);

    static void data_from_json(const json &j, int *data, int n_data);

    static std::string model_to_json(double *model, int n_model);

    static void model_from_json(const json &j, double *model, int n_model);

};


#endif // TTTRLIB_DECAYFIT_H

// SPDX-License-Identifier: MIT

#ifndef TTTRLIB_DECAYFIT_H
#define TTTRLIB_DECAYFIT_H

#include "include/Verbose.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <sstream>

#include <nlohmann/json.hpp>

#include "i_lbfgs.h"
#include "LvArrays.h"
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

    json to_json() const {
        json j;
        j["gamma"] = gamma;
        j["g"] = g;
        j["l1"] = l1;
        j["l2"] = l2;
        j["period"] = period;
        j["convolution_stop"] = convolution_stop;
        return j;
    }

    void from_json(const json &j) {
        if (j.contains("gamma")) gamma = j.at("gamma");
        if (j.contains("g")) g = j.at("g");
        if (j.contains("l1")) l1 = j.at("l1");
        if (j.contains("l2")) l2 = j.at("l2");
        if (j.contains("period")) period = j.at("period");
        if (j.contains("convolution_stop")) convolution_stop = j.at("convolution_stop");
        set_gamma(gamma);
    }

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

    json to_json() const {
        json j;
        j["fixedrho"] = fixedrho;
        j["softbifl"] = softbifl;
        j["p2s_twoIstar"] = p2s_twoIstar;
        j["firstcall"] = firstcall;
        j["penalty"] = penalty;
        return j;
    }

    void from_json(const json &j) {
        if (j.contains("fixedrho")) fixedrho = j.at("fixedrho");
        if (j.contains("softbifl")) softbifl = j.at("softbifl");
        if (j.contains("p2s_twoIstar")) p2s_twoIstar = j.at("p2s_twoIstar");
        if (j.contains("firstcall")) firstcall = j.at("firstcall");
        if (j.contains("penalty")) penalty = j.at("penalty");
    }
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

    void compute_signal_and_background(MParam *p);

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

    json to_json() const {
        json j;
        j["Sp"] = Sp;
        j["Ss"] = Ss;
        j["Bp"] = Bp;
        j["Bs"] = Bs;
        j["B"] = B;
        j["Bexpected"] = Bexpected;
        j["Fp"] = const_cast<DecayFitIntegrateSignals*>(this)->Fp();
        j["Fs"] = const_cast<DecayFitIntegrateSignals*>(this)->Fs();
        j["r"] = const_cast<DecayFitIntegrateSignals*>(this)->r();
        j["rs"] = const_cast<DecayFitIntegrateSignals*>(this)->rs();
        if (corrections != nullptr) {
            j["corrections"] = corrections->to_json();
        }
        return j;
    }

    void from_json(const json &j) {
        if (j.contains("Sp")) Sp = j.at("Sp");
        if (j.contains("Ss")) Ss = j.at("Ss");
        if (j.contains("Bp")) Bp = j.at("Bp");
        if (j.contains("Bs")) Bs = j.at("Bs");
        if (j.contains("B")) B = j.at("B");
        if (j.contains("Bexpected")) Bexpected = j.at("Bexpected");
    }

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


    static double fit(double *x, short *fixed, MParam *p) {
        return 0.0;
    };


    static void correct_input(double *x, double *xm, LVDoubleArray *corrections, int return_r) {};

    static std::string parameters_to_json(double *param, int n_param) {
        json j;
        j["parameters"] = json::array();
        for (int i = 0; i < n_param; i++) {
            j["parameters"].push_back(param[i]);
        }
        return j.dump();
    }

    static void parameters_from_json(const json &j, double *param, int n_param) {
        if (!j.contains("parameters") || !j.at("parameters").is_array()) {
            return;
        }
        const auto &values = j.at("parameters");
        const auto copy_count = std::min<int>(n_param, static_cast<int>(values.size()));
        for (int i = 0; i < copy_count; i++) {
            param[i] = values.at(i);
        }
    }

    static std::string data_to_json(int *data, int n_data) {
        json j;
        j["data"] = json::array();
        for (int i = 0; i < n_data; i++) {
            j["data"].push_back(data[i]);
        }
        return j.dump();
    }

    static void data_from_json(const json &j, int *data, int n_data) {
        if (!j.contains("data") || !j.at("data").is_array()) {
            return;
        }
        const auto &values = j.at("data");
        const auto copy_count = std::min<int>(n_data, static_cast<int>(values.size()));
        for (int i = 0; i < copy_count; i++) {
            data[i] = values.at(i);
        }
    }

    static std::string model_to_json(double *model, int n_model) {
        json j;
        j["model"] = json::array();
        for (int i = 0; i < n_model; i++) {
            j["model"].push_back(model[i]);
        }
        return j.dump();
    }

    static void model_from_json(const json &j, double *model, int n_model) {
        if (!j.contains("model") || !j.at("model").is_array()) {
            return;
        }
        const auto &values = j.at("model");
        const auto copy_count = std::min<int>(n_model, static_cast<int>(values.size()));
        for (int i = 0; i < copy_count; i++) {
            model[i] = values.at(i);
        }
    }

};


#endif // TTTRLIB_DECAYFIT_H

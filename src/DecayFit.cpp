// SPDX-License-Identifier: BSD-3-Clause
#include "DecayFit.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <vector>



void DecayFitIntegrateSignals::compute_signal_and_background(
        const int *counts, const double *bg, int n_bins) {
    const int *expdata = counts;
    int Nchannels_exp = n_bins;

    Sp = 0.; Ss = 0.;
    Bp = 0.; Bs = 0.;

    int i;
    for (i = 0; i < Nchannels_exp; i++) {
        Sp += expdata[i];
        Bp += bg[i];
    }
    for (; i < 2 * Nchannels_exp; i++) {
        Ss += expdata[i];
        Bs += bg[i];
    }
    B = std::max(1.0, Bp + Bs);
    Bp *= (Sp + Ss) / std::max(1., B);
    Bs *= (Sp + Ss) / std::max(1., B);
    if (corrections != nullptr) {
        Bexpected = corrections->gamma * B;
    } else {
        Bexpected = 0.0;
    }

    if (is_verbose()) {
        std::cout << "COMPUTE_SIGNAL_AND_BACKGROUND" << std::endl;
        std::cout << "-- Nchannels_exp:" << Nchannels_exp << std::endl;
        std::cout << "-- Bp, Bs: " << Bp << ", " << Bs << std::endl;
        std::cout << "-- Sp, Ss: " << Sp << ", " << Ss << std::endl;
        std::cout << "-- Bexpected: " << Bexpected << std::endl;
    }
}


void DecayFitIntegrateSignals::normM(double *M, int Nchannels) {
    double s = 0.;
    double Sexp = Sp + Ss;
    for (int i = 0; i < 2 * Nchannels; i++) s += M[i];
    if (s <= 0.) {
        return;
    }
    for (int i = 0; i < 2 * Nchannels; i++) M[i] *= Sexp / s;
}

void DecayFitIntegrateSignals::normM(double *M, double s, int Nchannels) {
    double Sexp = Sp + Ss;
    if (s <= 0.) {
        return;
    }
    for (int i = 0; i < 2 * Nchannels; i++) M[i] *= Sexp / s;
}

void DecayFitIntegrateSignals::normM_p2s(double *M, int Nchannels) {
    double s = 0.;

    for (int i = 0; i < Nchannels; i++) s += M[i];
    if (s > 0.) {
        for (int i = 0; i < Nchannels; i++) M[i] *= Sp / s;
    }

    s = 0.;
    for (int i = Nchannels; i < 2 * Nchannels; i++) s += M[i];
    if (s > 0.) {
        for (int i = Nchannels; i < 2 * Nchannels; i++) M[i] *= Ss / s;
    }
}



// ---------------------------------------------------------------------------
// JSON serialisation.
//
// These bodies live here rather than in DecayFit.h so the header needs only
// <nlohmann/json_fwd.hpp>. json.hpp costs ~41k preprocessed lines and DecayFit.h
// is pulled in by every decay model.
// ---------------------------------------------------------------------------

json DecayFitCorrections::to_json() const {
    json j;
    j["gamma"] = gamma;
    j["g"] = g;
    j["l1"] = l1;
    j["l2"] = l2;
    j["period"] = period;
    j["convolution_stop"] = convolution_stop;
    return j;
}

void DecayFitCorrections::from_json(const json &j) {
    if (j.contains("gamma")) gamma = j.at("gamma");
    if (j.contains("g")) g = j.at("g");
    if (j.contains("l1")) l1 = j.at("l1");
    if (j.contains("l2")) l2 = j.at("l2");
    if (j.contains("period")) period = j.at("period");
    if (j.contains("convolution_stop")) convolution_stop = j.at("convolution_stop");
    set_gamma(gamma);
}

json DecayFitSettings::to_json() const {
    json j;
    j["fixedrho"] = fixedrho;
    j["softbifl"] = softbifl;
    j["p2s_twoIstar"] = p2s_twoIstar;
    j["firstcall"] = firstcall;
    j["penalty"] = penalty;
    return j;
}

void DecayFitSettings::from_json(const json &j) {
    if (j.contains("fixedrho")) fixedrho = j.at("fixedrho");
    if (j.contains("softbifl")) softbifl = j.at("softbifl");
    if (j.contains("p2s_twoIstar")) p2s_twoIstar = j.at("p2s_twoIstar");
    if (j.contains("firstcall")) firstcall = j.at("firstcall");
    if (j.contains("penalty")) penalty = j.at("penalty");
}

json DecayFitIntegrateSignals::to_json() const {
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

void DecayFitIntegrateSignals::from_json(const json &j) {
    if (j.contains("Sp")) Sp = j.at("Sp");
    if (j.contains("Ss")) Ss = j.at("Ss");
    if (j.contains("Bp")) Bp = j.at("Bp");
    if (j.contains("Bs")) Bs = j.at("Bs");
    if (j.contains("B")) B = j.at("B");
    if (j.contains("Bexpected")) Bexpected = j.at("Bexpected");
}

std::string DecayFit::parameters_to_json(double *param, int n_param) {
    json j;
    j["parameters"] = json::array();
    for (int i = 0; i < n_param; i++) {
        j["parameters"].push_back(param[i]);
    }
    return j.dump();
}

void DecayFit::parameters_from_json(const json &j, double *param, int n_param) {
    if (!j.contains("parameters") || !j.at("parameters").is_array()) {
        return;
    }
    const auto &values = j.at("parameters");
    const auto copy_count = std::min<int>(n_param, static_cast<int>(values.size()));
    for (int i = 0; i < copy_count; i++) {
        param[i] = values.at(i);
    }
}

std::string DecayFit::data_to_json(int *data, int n_data) {
    json j;
    j["data"] = json::array();
    for (int i = 0; i < n_data; i++) {
        j["data"].push_back(data[i]);
    }
    return j.dump();
}

void DecayFit::data_from_json(const json &j, int *data, int n_data) {
    if (!j.contains("data") || !j.at("data").is_array()) {
        return;
    }
    const auto &values = j.at("data");
    const auto copy_count = std::min<int>(n_data, static_cast<int>(values.size()));
    for (int i = 0; i < copy_count; i++) {
        data[i] = values.at(i);
    }
}

std::string DecayFit::model_to_json(double *model, int n_model) {
    json j;
    j["model"] = json::array();
    for (int i = 0; i < n_model; i++) {
        j["model"].push_back(model[i]);
    }
    return j.dump();
}

void DecayFit::model_from_json(const json &j, double *model, int n_model) {
    if (!j.contains("model") || !j.at("model").is_array()) {
        return;
    }
    const auto &values = j.at("model");
    const auto copy_count = std::min<int>(n_model, static_cast<int>(values.size()));
    for (int i = 0; i < copy_count; i++) {
        model[i] = values.at(i);
    }
}

// SPDX-License-Identifier: BSD-3-Clause
//
// DecayFitProblem is otherwise header-only; only its JSON serialisation lives
// here, so DecayFitProblem.h needs <nlohmann/json_fwd.hpp> rather than the
// ~41k preprocessed lines of json.hpp.
#include "DecayFitProblem.h"

#include <nlohmann/json.hpp>

json DecayFitProblem::to_json() const {
    json j;
    j["n_channels"] = n_channels;
    j["n_bins"] = n_bins;
    j["dt"] = dt;
    j["fit_start"] = fit_start;
    j["fit_stop"] = fit_stop;
    j["setup"] = setup;
    j["n_patterns"] = static_cast<int>(patterns.size());
    return j;
}

void DecayFitProblem::from_json(const json &j) {
    if (j.contains("n_channels")) n_channels = j.at("n_channels");
    if (j.contains("n_bins")) n_bins = j.at("n_bins");
    if (j.contains("dt")) dt = j.at("dt");
    if (j.contains("fit_start")) fit_start = j.at("fit_start");
    if (j.contains("fit_stop")) fit_stop = j.at("fit_stop");
    if (j.contains("setup")) setup = j.at("setup").get<std::vector<double>>();
}

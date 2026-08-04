// SPDX-License-Identifier: BSD-3-Clause
#include "HMMRestraints.h"

#include <algorithm>

namespace tttrlib {

void HmmRestraints::add_pseudocounts(std::vector<double>& counts,
                                     const std::vector<double>& alpha) const {
    // Dirichlet is exactly conjugate to the E-step's raw counts, so the MAP
    // M-step is the MLE one with alpha-1 added.  Clamping at 0 handles alpha < 1,
    // where the mode sits on the boundary.
    //
    // The parenthesisation is load-bearing.  Written `counts + alpha - 1.0` the
    // addition happens first, so a small count against alpha near 1 cancels
    // catastrophically: 1e-20 + 1.0 rounds to 1.0, and subtracting 1.0 yields
    // exactly zero -- silently destroying a state's rare-symbol evidence, worst
    // precisely where a fine micro-time alphabet puts most of its bins.
    // Subtracting first makes a flat restraint add exactly 0.0, which also keeps
    // the MAP path bit-identical to plain EM when nothing is restrained.
    for (std::size_t i = 0; i < counts.size(); ++i)
        counts[i] = std::max(counts[i] + (alpha[i] - 1.0), 0.0);
}

double HmmRestraints::log_prior(const std::vector<double>& prior,
                                const std::vector<double>& trans,
                                const std::vector<double>& obs) const {
    const double tiny = std::numeric_limits<double>::min();
    double total = 0.0;
    auto add = [&](const std::vector<double>& a, const std::vector<double>& th) {
        for (std::size_t i = 0; i < a.size() && i < th.size(); ++i)
            total += (a[i] - 1.0) * std::log(std::max(th[i], tiny));
    };
    add(alpha_prior_, prior);
    add(alpha_trans_, trans);
    add(alpha_obs_, obs);
    return total;
}

json HmmRestraints::to_json() const {
    json j;
    j["n_states"] = n_states_;
    j["n_symbols"] = p_;
    j["alpha_prior"] = alpha_prior_;
    j["alpha_trans"] = alpha_trans_;
    j["alpha_obs"] = alpha_obs_;

    json dp = json::object();
    for (const auto& kv : decay_priors_) {
        json row = json::array();
        for (const auto& p : kv.second) row.push_back(p ? p->to_json() : json());
        dp[std::to_string(kv.first)] = row;
    }
    j["decay_priors"] = dp;
    return j;
}

HmmRestraints HmmRestraints::from_json(const json& state) {
    HmmRestraints r(state.at("n_states").get<int>(), state.at("n_symbols").get<int>());
    if (state.contains("alpha_prior")) r.set_alpha_prior(state.at("alpha_prior").get<std::vector<double>>());
    if (state.contains("alpha_trans")) r.set_alpha_trans(state.at("alpha_trans").get<std::vector<double>>());
    if (state.contains("alpha_obs")) r.set_alpha_obs(state.at("alpha_obs").get<std::vector<double>>());
    if (state.contains("decay_priors")) {
        for (auto it = state.at("decay_priors").begin(); it != state.at("decay_priors").end(); ++it) {
            int k = std::stoi(it.key());
            int idx = 0;
            for (const auto& e : it.value()) {
                if (!e.is_null()) r.set_decay_prior(k, idx, DecayFitPrior::from_json(e));
                ++idx;
            }
        }
    }
    return r;
}

std::string HmmRestraints::to_json_string(int indent) const { return to_json().dump(indent); }

HmmRestraints HmmRestraints::from_json_string(const std::string& text) {
    return from_json(json::parse(text));
}

} // namespace tttrlib

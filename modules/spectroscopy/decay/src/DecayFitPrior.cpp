// SPDX-License-Identifier: BSD-3-Clause
//
// The priors are otherwise header-only; only their JSON serialisation and the
// `from_json` factory live here, so DecayFitPrior.h needs
// <nlohmann/json_fwd.hpp> rather than the ~41k preprocessed lines of json.hpp.
#include "DecayFitPrior.h"

#include <nlohmann/json.hpp>

json UniformPrior::to_json() const {
    return json{{"kind", kind()}, {"lb", lb_}, {"ub", ub_}};
}

json NormalPrior::to_json() const {
    return json{{"kind", kind()}, {"mu", mu_}, {"sigma", sigma_}};
}

json TruncatedNormalPrior::to_json() const {
    return json{{"kind", kind()}, {"mu", mu_}, {"sigma", sigma_},
                {"lb", lb_}, {"ub", ub_}};
}

json HalfNormalPrior::to_json() const {
    return json{{"kind", kind()}, {"sigma", sigma_}, {"loc", loc_}};
}

json LogNormalPrior::to_json() const {
    return json{{"kind", kind()}, {"mu", mu_}, {"sigma", sigma_}};
}

json ExponentialPrior::to_json() const {
    return json{{"kind", kind()}, {"scale", scale_}, {"loc", loc_}};
}

json GammaPrior::to_json() const {
    return json{{"kind", kind()}, {"alpha", alpha_}, {"beta", beta_}, {"loc", loc_}};
}

json BetaPrior::to_json() const {
    return json{{"kind", kind()}, {"alpha", alpha_}, {"beta", beta_}};
}

json ProductPrior::to_json() const {
    json parts = json::array();
    for (const auto &p : priors_) {
        if (p) parts.push_back(p->to_json());
    }
    return json{{"kind", kind()}, {"priors", parts}};
}

namespace {
double num_or(const json &state, const char *key, double fallback) {
    return state.contains(key) ? state.at(key).get<double>() : fallback;
}
constexpr double kInf = std::numeric_limits<double>::infinity();
}  // namespace

std::map<std::string, DecayFitPrior::Factory> &DecayFitPrior::kinds_table() {
    static std::map<std::string, Factory> table = [] {
        std::map<std::string, Factory> t;
        t["uniform"] = [](const json &s) -> std::shared_ptr<DecayFitPrior> {
            return std::make_shared<UniformPrior>(num_or(s, "lb", -kInf), num_or(s, "ub", kInf)); };
        t["normal"] = [](const json &s) -> std::shared_ptr<DecayFitPrior> {
            return std::make_shared<NormalPrior>(num_or(s, "mu", 0.0), num_or(s, "sigma", 1.0)); };
        t["truncated_normal"] = [](const json &s) -> std::shared_ptr<DecayFitPrior> {
            return std::make_shared<TruncatedNormalPrior>(num_or(s, "mu", 0.0), num_or(s, "sigma", 1.0),
                                                          num_or(s, "lb", -kInf), num_or(s, "ub", kInf)); };
        t["half_normal"] = [](const json &s) -> std::shared_ptr<DecayFitPrior> {
            return std::make_shared<HalfNormalPrior>(num_or(s, "sigma", 1.0), num_or(s, "loc", 0.0)); };
        t["lognormal"] = [](const json &s) -> std::shared_ptr<DecayFitPrior> {
            return std::make_shared<LogNormalPrior>(num_or(s, "mu", 0.0), num_or(s, "sigma", 1.0)); };
        t["exponential"] = [](const json &s) -> std::shared_ptr<DecayFitPrior> {
            return std::make_shared<ExponentialPrior>(num_or(s, "scale", 1.0), num_or(s, "loc", 0.0)); };
        t["gamma"] = [](const json &s) -> std::shared_ptr<DecayFitPrior> {
            return std::make_shared<GammaPrior>(num_or(s, "alpha", 1.0), num_or(s, "beta", 1.0), num_or(s, "loc", 0.0)); };
        t["beta"] = [](const json &s) -> std::shared_ptr<DecayFitPrior> {
            return std::make_shared<BetaPrior>(num_or(s, "alpha", 1.0), num_or(s, "beta", 1.0)); };
        t["product"] = [](const json &s) -> std::shared_ptr<DecayFitPrior> {
            std::vector<std::shared_ptr<DecayFitPrior>> parts;
            if (s.contains("priors"))
                for (const auto &sub : s.at("priors")) parts.push_back(DecayFitPrior::from_json(sub));
            return std::make_shared<ProductPrior>(std::move(parts)); };
        t["callable"] = [](const json &) -> std::shared_ptr<DecayFitPrior> {
            // A live Python callback. Reported rather than approximated: silently
            // substituting a flat prior would change the posterior without saying so.
            throw std::invalid_argument(
                "prior kind 'callable' is a Python callback and cannot be evaluated in C++; "
                "replace it with an analytic kind to use it in a native fit"); };
        return t;
    }();
    return table;
}

void DecayFitPrior::register_kind(const std::string &kind, Factory factory) {
    if (kind.empty() || !factory)
        throw std::invalid_argument("DecayFitPrior::register_kind: a kind and a factory are required");
    kinds_table()[kind] = std::move(factory);
}

std::vector<std::string> DecayFitPrior::kinds() {
    std::vector<std::string> out;
    for (const auto &kv : kinds_table()) out.push_back(kv.first);
    return out;
}

std::shared_ptr<DecayFitPrior> DecayFitPrior::from_json(const json &state) {
    if (state.is_null()) return nullptr;
    if (!state.contains("kind")) {
        throw std::invalid_argument("prior state has no 'kind' key");
    }
    const std::string kind = state.at("kind").get<std::string>();
    auto it = kinds_table().find(kind);
    if (it == kinds_table().end()) {
        std::string known;
        for (const auto &k : kinds()) { if (!known.empty()) known += ", "; known += k; }
        throw std::invalid_argument("unknown prior kind '" + kind + "'; registered: " + known);
    }
    return it->second(state);
}

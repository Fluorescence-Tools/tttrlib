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

std::shared_ptr<DecayFitPrior> DecayFitPrior::from_json(const json &state) {
    if (state.is_null()) return nullptr;
    if (!state.contains("kind")) {
        throw std::invalid_argument("prior state has no 'kind' key");
    }
    const std::string kind = state.at("kind").get<std::string>();

    auto num = [&state](const char *key, double fallback) -> double {
        return state.contains(key) ? state.at(key).get<double>() : fallback;
    };
    const double inf = std::numeric_limits<double>::infinity();

    if (kind == "uniform") {
        return std::make_shared<UniformPrior>(num("lb", -inf), num("ub", inf));
    }
    if (kind == "normal") {
        return std::make_shared<NormalPrior>(num("mu", 0.0), num("sigma", 1.0));
    }
    if (kind == "truncated_normal") {
        return std::make_shared<TruncatedNormalPrior>(
            num("mu", 0.0), num("sigma", 1.0), num("lb", -inf), num("ub", inf));
    }
    if (kind == "half_normal") {
        return std::make_shared<HalfNormalPrior>(num("sigma", 1.0), num("loc", 0.0));
    }
    if (kind == "lognormal") {
        return std::make_shared<LogNormalPrior>(num("mu", 0.0), num("sigma", 1.0));
    }
    if (kind == "exponential") {
        return std::make_shared<ExponentialPrior>(num("scale", 1.0), num("loc", 0.0));
    }
    if (kind == "gamma") {
        return std::make_shared<GammaPrior>(num("alpha", 1.0), num("beta", 1.0), num("loc", 0.0));
    }
    if (kind == "beta") {
        return std::make_shared<BetaPrior>(num("alpha", 1.0), num("beta", 1.0));
    }
    if (kind == "product") {
        std::vector<std::shared_ptr<DecayFitPrior>> parts;
        if (state.contains("priors")) {
            for (const auto &sub : state.at("priors")) parts.push_back(from_json(sub));
        }
        return std::make_shared<ProductPrior>(std::move(parts));
    }
    if (kind == "callable") {
        // A live Python callback. Reported rather than approximated: silently
        // substituting a flat prior would change the posterior without saying so.
        throw std::invalid_argument(
            "prior kind 'callable' is a Python callback and cannot be evaluated in C++; "
            "replace it with an analytic kind to use it in a native fit");
    }
    throw std::invalid_argument("unknown prior kind '" + kind + "'");
}

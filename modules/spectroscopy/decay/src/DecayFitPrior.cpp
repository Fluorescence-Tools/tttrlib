// SPDX-License-Identifier: BSD-3-Clause
//
// The priors are otherwise header-only; only their JSON serialisation and the
// `from_json` factory live here, so DecayFitPrior.h needs
// <nlohmann/json_fwd.hpp> rather than the ~41k preprocessed lines of json.hpp.
#include "DecayFitPrior.h"
#include "PluginHost.h"
#include "Registry.h"

#include <nlohmann/json.hpp>
#include <algorithm>

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

namespace {

/*!
 * A prior kind a drop-in plugin contributed (`tttrlib_decay_prior_v1`). One
 * instance owns one plugin handle; the state that built it is kept verbatim
 * so `to_json` round-trips whatever keys the plugin understands.
 */
class PluginPrior : public DecayFitPrior {
public:
    PluginPrior(const tttrlib_decay_prior_v1 *table, void *handle, json state)
        : table_(table), handle_(handle), state_(std::move(state)) {}
    ~PluginPrior() override {
        if (table_->destroy) table_->destroy(handle_);
    }
    const char *kind() const override { return table_->kind; }
    double lnpdf(double x) const override { return table_->lnpdf(handle_, x); }
    double mode() const override { return table_->mode ? table_->mode(handle_) : 0.0; }
    std::pair<double, double> support() const override {
        if (!table_->support) return DecayFitPrior::support();
        double lo = -kInf, hi = kInf;
        if (table_->support(handle_, &lo, &hi) != TTTRLIB_OK) return DecayFitPrior::support();
        return {lo, hi};
    }
    json to_json() const override { return state_; }

private:
    const tttrlib_decay_prior_v1 *table_;
    void *handle_;
    json state_;
};

}  // namespace

// ---- registry("prior") ---------------------------------------------------
//
// The built-in kinds of kinds_table(), described for the one registry (a
// plugin's kind registers into the same category when it loads).
namespace {
const char* const kUniformPriorEntry = R"JSON({
  "name": "uniform",
  "api": ["UniformPrior", "DecayFitPrior"],
  "label": "Uniform (box)",
  "summary": "Flat inside [lb, ub], -inf outside; the optimiser sees it as box bounds.",
  "description": "Flat inside [lb, ub], -inf outside; the optimiser sees it as box bounds. State: {\"kind\": \"uniform\", ...} as accepted by DecayFitPrior.from_json_string.",
  "params_schema": {
    "type": "object",
    "properties": {
      "lb": {
        "type": "number",
        "default": "-inf"
      },
      "ub": {
        "type": "number",
        "default": "inf"
      }
    }
  }
})JSON";
const char* const kNormalPriorEntry = R"JSON({
  "name": "normal",
  "api": ["NormalPrior"],
  "label": "Normal",
  "summary": "Gaussian prior; enters the fit as one deviance residual (x-mu)/sigma.",
  "description": "Gaussian prior; enters the fit as one deviance residual (x-mu)/sigma. State: {\"kind\": \"normal\", ...} as accepted by DecayFitPrior.from_json_string.",
  "params_schema": {
    "type": "object",
    "properties": {
      "mu": {
        "type": "number",
        "default": 0.0
      },
      "sigma": {
        "type": "number",
        "default": 1.0,
        "exclusiveMinimum": 0
      }
    }
  }
})JSON";
const char* const kTruncatedNormalPriorEntry = R"JSON({
  "name": "truncated_normal",
  "api": ["TruncatedNormalPrior"],
  "label": "Truncated normal",
  "summary": "Gaussian inside [lb, ub], -inf outside.",
  "description": "Gaussian inside [lb, ub], -inf outside. State: {\"kind\": \"truncated_normal\", ...} as accepted by DecayFitPrior.from_json_string.",
  "params_schema": {
    "type": "object",
    "properties": {
      "mu": {
        "type": "number",
        "default": 0.0
      },
      "sigma": {
        "type": "number",
        "default": 1.0,
        "exclusiveMinimum": 0
      },
      "lb": {
        "type": "number",
        "default": "-inf"
      },
      "ub": {
        "type": "number",
        "default": "inf"
      }
    }
  }
})JSON";
const char* const kHalfNormalPriorEntry = R"JSON({
  "name": "half_normal",
  "api": ["HalfNormalPrior"],
  "label": "Half-normal",
  "summary": "Gaussian on x >= loc, -inf below; a soft non-negativity prior.",
  "description": "Gaussian on x >= loc, -inf below; a soft non-negativity prior. State: {\"kind\": \"half_normal\", ...} as accepted by DecayFitPrior.from_json_string.",
  "params_schema": {
    "type": "object",
    "properties": {
      "sigma": {
        "type": "number",
        "default": 1.0,
        "exclusiveMinimum": 0
      },
      "loc": {
        "type": "number",
        "default": 0.0
      }
    }
  }
})JSON";
const char* const kLognormalPriorEntry = R"JSON({
  "name": "lognormal",
  "api": ["LogNormalPrior"],
  "label": "Log-normal",
  "summary": "ln x ~ N(mu, sigma); positive support.",
  "description": "ln x ~ N(mu, sigma); positive support. State: {\"kind\": \"lognormal\", ...} as accepted by DecayFitPrior.from_json_string.",
  "params_schema": {
    "type": "object",
    "properties": {
      "mu": {
        "type": "number",
        "default": 0.0
      },
      "sigma": {
        "type": "number",
        "default": 1.0,
        "exclusiveMinimum": 0
      }
    }
  }
})JSON";
const char* const kExponentialPriorEntry = R"JSON({
  "name": "exponential",
  "api": ["ExponentialPrior"],
  "label": "Exponential",
  "summary": "p(x) ~ exp(-(x-loc)/scale) for x >= loc.",
  "description": "p(x) ~ exp(-(x-loc)/scale) for x >= loc. State: {\"kind\": \"exponential\", ...} as accepted by DecayFitPrior.from_json_string.",
  "params_schema": {
    "type": "object",
    "properties": {
      "scale": {
        "type": "number",
        "default": 1.0,
        "exclusiveMinimum": 0
      },
      "loc": {
        "type": "number",
        "default": 0.0
      }
    }
  }
})JSON";
const char* const kGammaPriorEntry = R"JSON({
  "name": "gamma",
  "api": ["GammaPrior"],
  "label": "Gamma",
  "summary": "Gamma(alpha, beta) on x >= loc.",
  "description": "Gamma(alpha, beta) on x >= loc. State: {\"kind\": \"gamma\", ...} as accepted by DecayFitPrior.from_json_string.",
  "params_schema": {
    "type": "object",
    "properties": {
      "alpha": {
        "type": "number",
        "default": 1.0,
        "exclusiveMinimum": 0
      },
      "beta": {
        "type": "number",
        "default": 1.0,
        "exclusiveMinimum": 0
      },
      "loc": {
        "type": "number",
        "default": 0.0
      }
    }
  }
})JSON";
const char* const kBetaPriorEntry = R"JSON({
  "name": "beta",
  "api": ["BetaPrior"],
  "label": "Beta",
  "summary": "Beta(alpha, beta) on [0, 1].",
  "description": "Beta(alpha, beta) on [0, 1]. State: {\"kind\": \"beta\", ...} as accepted by DecayFitPrior.from_json_string.",
  "params_schema": {
    "type": "object",
    "properties": {
      "alpha": {
        "type": "number",
        "default": 1.0,
        "exclusiveMinimum": 0
      },
      "beta": {
        "type": "number",
        "default": 1.0,
        "exclusiveMinimum": 0
      }
    }
  }
})JSON";
const char* const kProductPriorEntry = R"JSON({
  "name": "product",
  "api": ["ProductPrior"],
  "label": "Product",
  "summary": "The product of other priors on the same parameter (their log densities add).",
  "description": "The product of other priors on the same parameter (their log densities add). State: {\"kind\": \"product\", ...} as accepted by DecayFitPrior.from_json_string.",
  "params_schema": {
    "type": "object",
    "properties": {
      "priors": {
        "type": "array",
        "items": {
          "type": "object"
        }
      }
    }
  }
})JSON";
const char* const kCallablePriorEntry = R"JSON({
  "name": "callable",
  "label": "Python callable",
  "summary": "A live Python callback; usable from Python only, refused in a native fit.",
  "description": "A live Python callback; usable from Python only, refused in a native fit. State: {\"kind\": \"callable\", ...} as accepted by DecayFitPrior.from_json_string.",
  "params_schema": {
    "type": "object",
    "properties": {}
  }
})JSON";
}  // namespace

namespace tttrlib {
/// Register the built-in prior kinds' registry entries. Idempotent.
void register_prior_descriptors() {
    tttrlib::register_algorithm_json("prior", "uniform", kUniformPriorEntry);
    tttrlib::register_algorithm_json("prior", "normal", kNormalPriorEntry);
    tttrlib::register_algorithm_json("prior", "truncated_normal", kTruncatedNormalPriorEntry);
    tttrlib::register_algorithm_json("prior", "half_normal", kHalfNormalPriorEntry);
    tttrlib::register_algorithm_json("prior", "lognormal", kLognormalPriorEntry);
    tttrlib::register_algorithm_json("prior", "exponential", kExponentialPriorEntry);
    tttrlib::register_algorithm_json("prior", "gamma", kGammaPriorEntry);
    tttrlib::register_algorithm_json("prior", "beta", kBetaPriorEntry);
    tttrlib::register_algorithm_json("prior", "product", kProductPriorEntry);
    tttrlib::register_algorithm_json("prior", "callable", kCallablePriorEntry);
}
}  // namespace tttrlib

std::vector<std::string> DecayFitPrior::kinds() {
    std::vector<std::string> out;
    for (const auto &kv : kinds_table()) out.push_back(kv.first);
    for (const tttrlib_decay_prior_v1 *p : tttrlib::PluginHost::decay_priors())
        if (kinds_table().count(p->kind) == 0) out.push_back(p->kind);
    std::sort(out.begin(), out.end());
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
        // Not built in: a plugin may own the kind. Looked up per call, so a
        // plugin rolled back after a failed init is simply not found.
        if (const tttrlib_decay_prior_v1 *p = tttrlib::PluginHost::decay_prior(kind)) {
            const std::string payload = state.dump();
            void *handle = nullptr;
            if (p->create(p->ctx, payload.c_str(), &handle) != TTTRLIB_OK || handle == nullptr)
                throw std::invalid_argument("prior kind '" + kind + "' (plugin) refused its state: " +
                                            tttrlib::PluginHost::last_error());
            return std::make_shared<PluginPrior>(p, handle, state);
        }
        std::string known;
        for (const auto &k : kinds()) { if (!known.empty()) known += ", "; known += k; }
        throw std::invalid_argument("unknown prior kind '" + kind + "'; registered: " + known);
    }
    return it->second(state);
}

// SPDX-License-Identifier: BSD-3-Clause
#include "HMMConstraints.h"

#include <nlohmann/json.hpp>

namespace tttrlib {

void HmmConstraints::impose(std::vector<double>& value, int n_rows, int n_cols,
                            const std::vector<double>& fixed) {
    for (int r = 0; r < n_rows; ++r) {
        double* row = value.data() + std::size_t(r) * n_cols;
        const double* fx = fixed.data() + std::size_t(r) * n_cols;

        double budget = 1.0, free_sum = 0.0;
        int n_free = 0;
        bool any_fixed = false;
        for (int c = 0; c < n_cols; ++c) {
            if (std::isnan(fx[c])) { free_sum += row[c]; ++n_free; }
            else { budget -= fx[c]; any_fixed = true; }
        }
        if (!any_fixed) continue;
        if (budget < 0.0) budget = 0.0;

        for (int c = 0; c < n_cols; ++c) {
            if (!std::isnan(fx[c])) {
                row[c] = fx[c];                       // exact, not approached
            } else if (free_sum > 0.0) {
                row[c] = row[c] / free_sum * budget;  // free remainder rescaled
            } else if (n_free > 0) {
                row[c] = budget / n_free;
            }
        }
    }
}

void HmmConstraints::validate() const {
    auto rows_ok = [](const std::vector<double>& f, int n_rows, int n_cols, const char* what) {
        for (int r = 0; r < n_rows; ++r) {
            double s = 0.0;
            for (int c = 0; c < n_cols; ++c) {
                double v = f[std::size_t(r) * n_cols + c];
                if (std::isnan(v)) continue;
                if (v < 0.0 || v > 1.0)
                    throw std::invalid_argument(std::string(what) + " entries must lie in [0, 1]");
                s += v;
            }
            if (s > 1.0 + 1e-12)
                throw std::invalid_argument(std::string(what) + " fixed entries exceed 1 in a row");
        }
    };
    rows_ok(fixed_prior_, 1, n_states_, "fixed_prior");
    rows_ok(fixed_trans_, n_states_, n_states_, "fixed_trans");
    rows_ok(fixed_obs_, n_states_, p_, "fixed_obs");
}

json HmmConstraints::to_json() const {
    // NaN has no JSON representation, so a pinned entry travels as an explicit
    // (index, value) pair.  A null would round-trip as "free" and silently drop
    // the constraint -- the one failure mode a serialised constraint must not have.
    auto pack = [](const std::vector<double>& f) {
        json out = json::array();
        for (std::size_t i = 0; i < f.size(); ++i)
            if (!std::isnan(f[i])) out.push_back(json{{"i", i}, {"v", f[i]}});
        return out;
    };
    json j;
    j["n_states"] = n_states_;
    j["n_symbols"] = p_;
    j["fixed_prior"] = pack(fixed_prior_);
    j["fixed_trans"] = pack(fixed_trans_);
    j["fixed_obs"] = pack(fixed_obs_);
    return j;
}

HmmConstraints HmmConstraints::from_json(const json& state) {
    HmmConstraints c(state.at("n_states").get<int>(), state.at("n_symbols").get<int>());
    auto unpack = [](const json& arr, std::vector<double>& dst) {
        for (const auto& e : arr) dst[e.at("i").get<std::size_t>()] = e.at("v").get<double>();
    };
    if (state.contains("fixed_prior")) unpack(state.at("fixed_prior"), c.fixed_prior_);
    if (state.contains("fixed_trans")) unpack(state.at("fixed_trans"), c.fixed_trans_);
    if (state.contains("fixed_obs")) unpack(state.at("fixed_obs"), c.fixed_obs_);
    c.validate();
    return c;
}

std::string HmmConstraints::to_json_string(int indent) const { return to_json().dump(indent); }

HmmConstraints HmmConstraints::from_json_string(const std::string& text) {
    return from_json(json::parse(text));
}

} // namespace tttrlib

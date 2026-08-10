// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file DecayFitSetup.cpp
 * \brief Build a model's flat setup vector from named values.
 *
 * The setup block crosses the boundary as a flat `double` array, which is fast
 * and uniform across languages but only safe if something turns names into
 * slots. That something has to live *here*, in C++, rather than in any one
 * binding's helper module: a caller in R or Java assembling the vector by hand
 * would be back to counting positions, which is the failure the registry's
 * flattening rule exists to prevent.
 *
 * Values are supplied as a JSON object — the one structured type every binding
 * already has a string for — so the signature stays `(name, ojson)` in all four.
 */
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "DecayFitModel.h"
#include "Registry.h"

// **ordered**_json, not plain json: the flattening rule says slots are laid out in
// *declaration* order, and plain nlohmann::json sorts object keys
// alphabetically. Parsing the registry with it silently reorders every setup and
// parameter block — dt lands in the period slot, and a fit "works" while
// describing a different instrument.
using ojson = nlohmann::ordered_json;

namespace {

/*! The `fit` entry for \p name, or a message naming the alternatives. */
ojson fit_entry(const std::string &name) {
    const ojson fits = ojson::parse(tttrlib::fit_models_json());
    if (!fits.contains(name)) {
        std::string known;
        for (const auto &item : fits.items()) {
            if (!known.empty()) known += ", ";
            known += item.key();
        }
        throw std::invalid_argument("unknown fit '" + name + "'; available: " + known);
    }
    return fits.at(name);
}

}  // namespace


std::vector<double> decay_fit_setup_vector(const std::string &name,
                                           const std::string &values_json) {
    const ojson entry = fit_entry(name);
    const ojson link = entry.at("setup");
    const ojson setups = ojson::parse(tttrlib::fit_setup_json());
    const std::string setup_name = link.at("name").get<std::string>();
    if (!setups.contains(setup_name)) {
        throw std::invalid_argument(
            "fit '" + name + "' points at setup '" + setup_name +
            "', which is not registered");
    }
    const ojson properties = setups.at(setup_name).at("params_schema").at("properties");

    ojson values = ojson::object();
    if (!values_json.empty()) {
        values = ojson::parse(values_json);
        if (!values.is_object()) {
            throw std::invalid_argument("setup values must be a JSON object");
        }
    }

    // A name the schema does not have is a mistake worth reporting: silently
    // ignoring it would leave the caller believing a value was applied.
    for (const auto &item : values.items()) {
        if (!properties.contains(item.key())) {
            std::string known;
            for (const auto &prop : properties.items()) {
                if (!known.empty()) known += ", ";
                known += prop.key();
            }
            throw std::invalid_argument(
                "fit '" + name + "' setup has no '" + item.key() +
                "'; available: " + known);
        }
    }

    std::vector<double> out;
    for (const auto &prop : properties.items()) {
        const ojson &schema = prop.value();
        const std::string type = schema.value("type", "number");

        ojson value = values.contains(prop.key()) ? values.at(prop.key())
                                                 : schema.value("default", ojson(0.0));

        if (type == "string" && schema.contains("enum")) {
            // Carried as the index of the value within the enum, per the
            // flattening rule; a name is accepted so callers need not know it.
            if (value.is_string()) {
                const std::string wanted = value.get<std::string>();
                int index = -1, i = 0;
                for (const auto &choice : schema.at("enum")) {
                    if (choice.get<std::string>() == wanted) { index = i; break; }
                    ++i;
                }
                if (index < 0) {
                    throw std::invalid_argument(
                        "fit '" + name + "' setup " + prop.key() + "='" + wanted +
                        "' is not one of the declared choices");
                }
                out.push_back(static_cast<double>(index));
                continue;
            }
        }
        if (type == "boolean") {
            out.push_back(value.is_boolean() ? (value.get<bool>() ? 1.0 : 0.0)
                                             : (value.get<double>() != 0.0 ? 1.0 : 0.0));
            continue;
        }
        out.push_back(value.get<double>());
    }
    return out;
}


std::vector<std::string> decay_fit_setup_names(const std::string &name) {
    const ojson entry = fit_entry(name);
    const ojson link = entry.at("setup");
    const ojson setups = ojson::parse(tttrlib::fit_setup_json());
    const ojson properties =
        setups.at(link.at("name").get<std::string>()).at("params_schema").at("properties");

    std::vector<std::string> names;
    for (const auto &prop : properties.items()) names.push_back(prop.key());
    return names;
}


std::vector<std::string> decay_fit_result_names(const std::string &name, int n) {
    const ojson entry = fit_entry(name);
    if (!entry.contains("results_schema")) return {};
    const ojson properties = entry.at("results_schema").at("properties");

    std::vector<std::string> names;
    for (const auto &prop : properties.items()) {
        const ojson &schema = prop.value();
        if (schema.value("type", "") == "array") {
            for (int i = 0; i < n; ++i) {
                names.push_back(prop.key() + "[" + std::to_string(i) + "]");
            }
        } else {
            names.push_back(prop.key());
        }
    }
    return names;
}


std::vector<std::string> decay_fit_parameter_names(const std::string &name, int n) {
    const ojson entry = fit_entry(name);
    const ojson properties = entry.at("params_schema").at("properties");

    std::vector<std::string> names;
    for (const auto &prop : properties.items()) {
        const ojson &schema = prop.value();
        if (schema.value("type", "") == "array") {
            for (int i = 0; i < n; ++i) {
                names.push_back(prop.key() + "[" + std::to_string(i) + "]");
            }
        } else {
            names.push_back(prop.key());
        }
    }
    return names;
}


std::vector<int> decay_fit_default_links(const std::string &name, int n) {
    const ojson entry = fit_entry(name);
    const ojson properties = entry.at("params_schema").at("properties");

    std::vector<int> codes;
    for (const auto &prop : properties.items()) {
        const ojson &schema = prop.value();
        if (schema.value("type", "") == "array") {
            const bool fixed = schema.at("items").value("fixed_default", false);
            for (int i = 0; i < n; ++i) codes.push_back(fixed ? -1 : 0);
        } else {
            codes.push_back(schema.value("fixed_default", false) ? -1 : 0);
        }
    }
    return codes;
}

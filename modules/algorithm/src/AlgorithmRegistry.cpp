// SPDX-License-Identifier: BSD-3-Clause
#include "AlgorithmRegistry.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace tttrlib {

namespace {

using json = nlohmann::ordered_json;

struct Store {
    std::mutex m;
    std::vector<AlgorithmDescriptor> order;                 // registration order
    std::unordered_map<std::string, size_t> by_name;
    bool builtins_registered = false;
};

Store& store() {
    static Store s;
    return s;
}

/// Parse a JSON field, or fall back. A descriptor written by hand -- or by a
/// plugin author who is not obliged to be careful -- can carry a malformed
/// schema; that must degrade to an empty object in the registry rather than
/// throw out of `registry_json()` and take the whole library's introspection
/// with it.
json parse_or(const std::string& text, json fallback) {
    if (text.empty()) return fallback;
    json v = json::parse(text, nullptr, false);
    if (v.is_discarded()) return fallback;
    return v;
}

json entry_of(const AlgorithmDescriptor& d) {
    // Key order matters only for readability, but the *set* of keys is a
    // compatibility surface: the burst_search and fit categories predate the
    // descriptor and their consumers (ChiSurf, ndx, the web UI) read `method`,
    // `params_schema` and `provider`. Those are emitted under their original
    // names so a category can migrate onto registrations without its entries
    // changing shape. Everything else is additive, which a consumer ignores.
    json e = json::object();
    e["name"] = algorithm_key(d);
    e["label"] = d.display_name.empty() ? d.operation_type : d.display_name;
    // Emitted only when there is one. Its ABSENCE is what routes a plugin's
    // search through the by-name path in every consumer that reads this, so an
    // empty string here would be a behaviour change, not a cosmetic one.
    if (!d.dispatch_name.empty()) e["method"] = d.dispatch_name;
    e["summary"] = d.summary;
    e["description"] = d.description;
    const json schema = parse_or(d.settings_schema, json::object());
    e["params_schema"] = schema;      // the name these categories have always used
    e["settings_schema"] = schema;    // the descriptor's own name for it
    e["capability"] = d.capability;
    e["operation_type"] = d.operation_type;
    e["row_grain"] = d.row_grain;
    e["inputs"] = parse_or(d.inputs_json, json::object());
    e["outputs"] = parse_or(d.outputs_json, json::object());
    e["references"] = parse_or(d.references_json, json::array());
    e["provider"] = d.provider.empty() ? std::string("builtin") : d.provider;
    e["can_replay"] = d.can_replay;
    // Capability-specific keys last, so they win over the generic spelling of
    // the same key (a fit's `params_schema` is its own, not `settings_schema`).
    const json extra = parse_or(d.extra_json, json::object());
    if (extra.is_object())
        for (auto it = extra.begin(); it != extra.end(); ++it) e[it.key()] = it.value();
    return e;
}

} // namespace

bool register_algorithm(const AlgorithmDescriptor& desc) {
    if (desc.operation_type.empty() || desc.capability.empty()) return false;
    Store& s = store();
    std::lock_guard<std::mutex> lock(s.m);
    const std::string& key = algorithm_key(desc);
    if (s.by_name.find(key) != s.by_name.end()) return false;
    s.by_name.emplace(key, s.order.size());
    s.order.push_back(desc);
    return true;
}

bool register_algorithm_json(const std::string& capability, const std::string& key,
                             const std::string& entry_json) {
    json e = json::parse(entry_json, nullptr, false);
    if (e.is_discarded() || !e.is_object()) return false;
    auto str = [&](const char* k) -> std::string {
        auto it = e.find(k);
        return (it != e.end() && it->is_string()) ? it->get<std::string>() : std::string();
    };
    auto obj = [&](const char* k) -> std::string {
        auto it = e.find(k);
        return it != e.end() ? it->dump() : std::string();
    };
    AlgorithmDescriptor d;
    d.capability = capability;
    d.name = key;
    d.operation_type = str("operation_type").empty() ? key : str("operation_type");
    d.display_name = str("label");
    d.summary = str("summary");
    d.description = str("description");
    d.dispatch_name = str("method");
    d.provider = str("provider").empty() ? std::string("builtin") : str("provider");
    d.settings_schema = e.contains("settings_schema") ? obj("settings_schema") : obj("params_schema");
    d.inputs_json = obj("inputs");
    d.outputs_json = obj("outputs");
    d.row_grain = str("row_grain");
    d.references_json = obj("references");
    auto cr = e.find("can_replay");
    d.can_replay = cr != e.end() && cr->is_boolean() && cr->get<bool>();
    d.extra_json = entry_json;
    return register_algorithm(d);
}

bool unregister_algorithm(const std::string& key) {
    Store& s = store();
    std::lock_guard<std::mutex> lock(s.m);
    auto it = s.by_name.find(key);
    if (it == s.by_name.end()) return false;
    s.order.erase(s.order.begin() + static_cast<std::ptrdiff_t>(it->second));
    s.by_name.clear();
    for (size_t i = 0; i < s.order.size(); ++i) s.by_name.emplace(algorithm_key(s.order[i]), i);
    return true;
}

std::string algorithms_json(const std::string& capability) {
    register_builtin_algorithms();
    Store& s = store();
    std::lock_guard<std::mutex> lock(s.m);
    json out = json::object();
    for (const auto& d : s.order)
        if (d.capability == capability) out[algorithm_key(d)] = entry_of(d);
    return out.dump(2);
}

std::vector<std::string> algorithm_capabilities() {
    register_builtin_algorithms();
    Store& s = store();
    std::lock_guard<std::mutex> lock(s.m);
    std::vector<std::string> out;
    for (const auto& d : s.order)
        if (std::find(out.begin(), out.end(), d.capability) == out.end())
            out.push_back(d.capability);
    return out;
}

const AlgorithmDescriptor* find_algorithm(const std::string& key) {
    register_builtin_algorithms();
    Store& s = store();
    std::lock_guard<std::mutex> lock(s.m);
    auto it = s.by_name.find(key);
    if (it == s.by_name.end()) return nullptr;
    // Stable: `order` only grows, and a registration is never replaced.
    return &s.order[it->second];
}

std::string algorithm_operations_json() {
    register_builtin_algorithms();
    Store& s = store();
    std::lock_guard<std::mutex> lock(s.m);
    json out = json::object();
    for (const auto& d : s.order) {
        if (!d.can_replay) continue;
        json e = entry_of(d);
        // The operation category carries `kind` and `data_format` alongside the
        // shared fields; a live registration that does not declare them still
        // has to render in the same table as a hand-authored entry.
        if (!e.contains("kind")) e["kind"] = d.capability;
        out[algorithm_key(d)] = e;
    }
    return out.dump(2);
}

} // namespace tttrlib

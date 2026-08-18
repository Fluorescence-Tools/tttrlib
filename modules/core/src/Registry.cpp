// SPDX-License-Identifier: BSD-3-Clause
#include "Registry.h"
#include "PluginHost.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include "TTTRFormat.h"
#include "TTTR.h"

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

// A plugin's declarations enter the same table the built-ins registered in.
// The host sits beneath core and only records them; here they are pulled --
// idempotently by key -- before any enumeration. Done outside the store lock:
// loading a plugin may run arbitrary init code.
void pull_plugin_declarations() {
    for (const PluginHost::RegistryEntry& e : PluginHost::registry_entries())
        register_algorithm_json(e.capability, e.name, e.entry_json);   // false = key taken by a built-in
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


std::string algorithms_json(const std::string& capability) {
    pull_plugin_declarations();
    Store& s = store();
    std::lock_guard<std::mutex> lock(s.m);
    json out = json::object();
    for (const auto& d : s.order)
        if (d.capability == capability) out[algorithm_key(d)] = entry_of(d);
    return out.dump(2);
}

std::vector<std::string> algorithm_capabilities() {
    pull_plugin_declarations();
    Store& s = store();
    std::lock_guard<std::mutex> lock(s.m);
    std::vector<std::string> out;
    for (const auto& d : s.order)
        if (std::find(out.begin(), out.end(), d.capability) == out.end())
            out.push_back(d.capability);
    return out;
}

const AlgorithmDescriptor* find_algorithm(const std::string& key) {
    pull_plugin_declarations();
    Store& s = store();
    std::lock_guard<std::mutex> lock(s.m);
    auto it = s.by_name.find(key);
    if (it == s.by_name.end()) return nullptr;
    // Stable: `order` only grows, and a registration is never replaced.
    return &s.order[it->second];
}

std::string algorithm_operations_json() {
    pull_plugin_declarations();
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


// ---------------------------------------------------------------- assembly --

namespace {

/*!
 * The readable/writable file containers, as registry entries.
 *
 * `TTTR::container_names` already answers "what can be read", but only as a
 * name-to-integer map: a caller building a file dialog still had to hard-code the
 * human label and the extension. Registering the containers here puts them in the
 * same shape as everything else, so one piece of code can render any category.
 */
json file_container_entries() {
    // Derived from the format table rather than restated. This was the second
    // of six copies of the same knowledge, and the comment it replaces admitted
    // as much: "Labels and extensions are not derivable from it at all, so the
    // table is written out here". They are derivable now.
    json out = json::object();
    for (const auto& f : tttrlib::IORegistry::formats()) {
        std::string extensions;
        for (const auto& e : f.extensions) {
            if (!extensions.empty()) extensions += ",";
            extensions += "." + e;
        }
        json entry = json::object();
        entry["name"] = f.name;
        entry["label"] = f.label;
        entry["summary"] = f.summary;
        entry["extensions"] = extensions;
        entry["container_type"] = f.container_type;
        entry["can_read"] = f.can_read;
        entry["can_write"] = f.can_write;
        // The record encodings valid inside this container, and the one used
        // when transcoding into it. An empty list means "any", which is true of
        // Photon-HDF5: it stores decoded arrays rather than records. This was
        // only expressed inside an if/else chain in TTTR.cpp before, so a caller
        // choosing a transcode target had no way to ask.
        entry["record_types"] = f.record_types;
        entry["default_record_type"] = f.default_record_type;
        // What the reader has to be told, for the formats that cannot tell you
        // themselves. Same shape and same key as the fit models' params_schema,
        // so a frontend that renders one renders this one; an empty object
        // means the format takes no parameters. This is the whole point of
        // declaring container parameters here rather than as a per-format
        // struct: no language binding needs to know that BrightEyes exists.
        entry["params_schema"] = f.parameters_schema.empty()
                ? json::object()
                : json::parse(f.parameters_schema, nullptr, false);
        // Whether the container can be read in pieces, which is what decides
        // whether the range in params_schema means anything. A progress bar, a
        // first look at a large file and a live view of one still being written
        // are all this flag; without it a caller has to try and see.
        entry["ranged_reads"] = f.ranged_reads;
        entry["canonical_extension"] = f.write_extension();
        // Tells a consumer which container ints are safe to persist. Built-in
        // formats own 0-999 permanently; a plugin's id is session-local, so for
        // those the NAME is the stable identifier.
        entry["stable"] = f.stable;
        out[f.name] = entry;
    }
    return out;
}

/*!
 * What was found in the plugin directories, and what became of it.
 *
 * A failed plugin is reported here rather than raised or printed. Two reasons:
 * `import tttrlib` must not be breakable by a stranger's binary sitting in a
 * directory, and a diagnostic that scrolled past is not a diagnostic. So the
 * outcome is data, queryable after the fact — including for the plugins that
 * loaded fine, because "which binary produced this result" is a question a
 * published figure has to be able to answer.
 */
json plugin_entries() {
    json out = json::object();
    for (const auto& p : tttrlib::PluginHost::plugins()) {
        json entry = json::object();
        entry["name"] = p.name;
        // label/summary because every registry entry carries them: a tool that
        // can render one category can render this one, which is the whole
        // reason the registry has a shape at all.
        entry["label"] = p.description.empty() ? p.name : p.description;
        switch (p.status) {
            case tttrlib::PluginStatus::Loaded:      entry["status"] = "loaded"; break;
            case tttrlib::PluginStatus::Failed:      entry["status"] = "failed"; break;
            case tttrlib::PluginStatus::Quarantined: entry["status"] = "quarantined"; break;
            case tttrlib::PluginStatus::Shadowed:    entry["status"] = "shadowed"; break;
            case tttrlib::PluginStatus::Disabled:    entry["status"] = "disabled"; break;
        }
        entry["summary"] =
                p.status == tttrlib::PluginStatus::Loaded
                        ? ("version " + (p.version.empty() ? std::string("?") : p.version) +
                           ", from " + p.path)
                        : (entry["status"].get<std::string>() + ": " +
                           (p.message.empty() ? p.path : p.message));
        entry["version"] = p.version;
        entry["description"] = p.description;
        entry["path"] = p.path;
        entry["sha256"] = p.sha256;
        entry["message"] = p.message;
        // What it actually contributed, so the answer to "where did this format
        // come from" is one lookup rather than a guess.
        entry["containers"] = p.containers;
        // The plugin's own idea of its name, when it disagrees with the
        // filename. Discovery goes by filename, so the two differing is worth
        // seeing rather than silently resolving.
        if (!p.declared_name.empty() && p.declared_name != p.name) {
            entry["declared_name"] = p.declared_name;
        }
        out[p.name] = entry;
    }
    return out;
}

/// Assemble the whole registry once; cheap enough to rebuild per call, and doing
/// so avoids a static initialisation order dependency on container_names.
/*!
 * \brief What each table format can be asked for, so a caller can ask rather
 *        than try.
 *
 * The same shape as `file_container` above, and here for the same reason: the
 * knowledge existed only inside the dispatcher's switch, so a caller choosing
 * where to put a table had to attempt the call and catch.
 *
 * `rewrites_on_partial_write` is the one entry that is not a capability but a
 * COST, and it is the one worth publishing most. Writing one group of a
 * `.dstore` reads the file, replaces that group and writes it back -- the
 * caller is not told, because the resulting file is the same either way, and
 * on four gigabytes they are entitled to know before they call rather than
 * after. HDF5 replaces the group in place and does not.
 */
json table_format_entries() {
    struct Row {
        const char* name;
        const char* label;
        const char* summary;
        const char* extensions;
        bool groups, columns, row_range, write_group, rewrites;
    };
    // Written out rather than derived: unlike a container format, these are
    // properties of the READER this library has for each, not of the file
    // format, so there is nothing to derive them from.
    static const Row rows[] = {
        {"dstore", "the native store file",
         "Speed and exact fidelity, for what only this library reads. Keeps "
         "every dtype including bool, the row selection and the label.",
         ".dstore", true,  true,  true,  true,  true},
        {"hdf5",   "columnar HDF5",
         "Interoperability: one 1-D dataset per column, readable by h5py, "
         "pandas and MATLAB. No bool type and no label.",
         ".h5,.hdf5", true, true, true,  true,  false},
        {"pto",    "a store inside a container",
         "A table as one object of a PTO, addressed as `file.pto|name`. The "
         "container holds many, each of which is a whole tree.",
         ".pto",  true,  true,  true,  true,  true},
        {"csv",    "one flat table",
         "The lowest common denominator: no tree, no dtypes, no masks, and "
         "readable by anything. Identified by extension, having no magic bytes.",
         ".csv,.tsv", false, true, false, false, true},
    };
    json out = json::object();
    for (const Row& r : rows) {
        json entry = json::object();
        entry["name"] = r.name;
        entry["label"] = r.label;
        entry["summary"] = r.summary;
        entry["extensions"] = r.extensions;
        entry["groups"] = r.groups;
        entry["columns"] = r.columns;
        entry["row_range"] = r.row_range;
        entry["write_group"] = r.write_group;
        entry["rewrites_on_partial_write"] = r.rewrites;
        out[r.name] = entry;
    }
    return out;
}

json build() {
    // Before anything is enumerated, not after. Asking the registry what
    // tttrlib can do is one of the three moments a plugin has to already be
    // loaded -- the others being constructing a TTTR and inferring a file type.
    // Loading further down, next to the `plugin` category that obviously needs
    // it, left `file_container` listing the built-ins only: the plugin was
    // loaded and its format registered a few lines too late to be seen.
    tttrlib::PluginHost::ensure_loaded();

    json root = json::object();
    root["burst_search"] = json::parse(tttrlib::algorithms_json("burst_search"));
    root["file_container"] = file_container_entries();
    root["table_format"] = table_format_entries();
    root["plugin"] = plugin_entries();
    // Everything else is a category of the ONE registry: every built-in
    // algorithm, fit model, setup block, objective and pipeline operation
    // registered itself there (next to its code), and so did every plugin
    // capability when the plugin host loaded it. Nothing is spliced.
    // Category order is part of the surface (the conformance suite pins it):
    // the historical order first, then anything new in first-registration order.
    std::vector<std::string> capabilities = {"fit", "fit_setup", "objective", "operation",
                                             "fcs", "hmm", "pda", "prior", "correlation_method"};
    for (const std::string& c : tttrlib::algorithm_capabilities())
        if (std::find(capabilities.begin(), capabilities.end(), c) == capabilities.end())
            capabilities.push_back(c);
    for (const std::string& capability : capabilities) {
        json entries = json::parse(tttrlib::algorithms_json(capability));
        if (entries.empty()) continue;
        if (root.contains(capability)) {
            json merged = root[capability];
            for (auto it = entries.begin(); it != entries.end(); ++it)
                if (!merged.contains(it.key())) merged[it.key()] = it.value();
            root[capability] = merged;
        } else {
            root[capability] = entries;
        }
    }
    // The `operation` category is every entry declared as an operation plus
    // every `can_replay` registration of any capability, so a consumer reads
    // one category whichever way an operation was declared.
    {
        json ops = root.contains("operation") ? root["operation"] : json::object();
        json live = json::parse(tttrlib::algorithm_operations_json());
        for (auto it = live.begin(); it != live.end(); ++it)
            if (!ops.contains(it.key())) ops[it.key()] = it.value();
        root["operation"] = ops;
    }
    return root;
}

} // namespace

std::string registry_json() {
    return build().dump(2);
}

std::string registry_category_json(const std::string& category) {
    const json root = build();
    if (!root.contains(category)) return "{}";
    return root[category].dump(2);
}

std::vector<std::string> registry_categories() {
    std::vector<std::string> out;
    // `build()` must be bound to a named object first. Writing
    // `for (auto& item : build().items())` compiles and returns nothing:
    // items() hands back an iteration_proxy holding a REFERENCE to the json,
    // and a range-for lifetime-extends only the range expression itself (the
    // proxy), not the temporary the proxy points at. The json is destroyed at
    // the end of the full expression and the loop iterates a dangling object --
    // which is why this function returned an empty list in every binding while
    // registry_json(), built from the same call, returned five categories.
    const json root = build();
    out.reserve(root.size());
    for (const auto& item : root.items()) out.push_back(item.key());
    return out;
}

// Category views over the one registry, kept because they are the API the
// bindings and the decay module have always called.
std::string fit_models_json() {
    return algorithms_json("fit");
}

std::string fit_setup_json() {
    return algorithms_json("fit_setup");
}

std::string fit_objectives_json() {
    return algorithms_json("objective");
}

std::string operation_registry_json() {
    return algorithms_json("operation");
}

} // namespace tttrlib

// SPDX-License-Identifier: BSD-3-Clause
#include "Registry.h"

#include <nlohmann/json.hpp>

#include "TTTRFormat.h"
#include "PluginHost.h"
#include "TTTR.h"

namespace tttrlib {

namespace {

using json = nlohmann::ordered_json;   // ordered: a form renders in declared order

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
json build() {
    // Before anything is enumerated, not after. Asking the registry what
    // tttrlib can do is one of the three moments a plugin has to already be
    // loaded -- the others being constructing a TTTR and inferring a file type.
    // Loading further down, next to the `plugin` category that obviously needs
    // it, left `file_container` listing the built-ins only: the plugin was
    // loaded and its format registered a few lines too late to be seen.
    tttrlib::PluginHost::ensure_loaded();

    json root = json::object();
    root["burst_search"] = json::parse(TTTR::burst_search_algorithms_json());
    root["file_container"] = file_container_entries();
    root["plugin"] = plugin_entries();
    root["fit"] = json::parse(fit_models_json());
    root["fit_setup"] = json::parse(fit_setup_json());
    root["objective"] = json::parse(fit_objectives_json());
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

} // namespace tttrlib

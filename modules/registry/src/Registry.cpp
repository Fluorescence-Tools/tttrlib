// SPDX-License-Identifier: BSD-3-Clause
#include "Registry.h"

#include <nlohmann/json.hpp>

#include "TTTRFormat.h"
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
        entry["canonical_extension"] = f.write_extension();
        // Tells a consumer which container ints are safe to persist. Built-in
        // formats own 0-999 permanently; a plugin's id is session-local, so for
        // those the NAME is the stable identifier.
        entry["stable"] = f.stable;
        out[f.name] = entry;
    }
    return out;
}

/// Assemble the whole registry once; cheap enough to rebuild per call, and doing
/// so avoids a static initialisation order dependency on container_names.
json build() {
    json root = json::object();
    root["burst_search"] = json::parse(TTTR::burst_search_algorithms_json());
    root["file_container"] = file_container_entries();
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

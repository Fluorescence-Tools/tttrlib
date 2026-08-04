// SPDX-License-Identifier: BSD-3-Clause
#include "Registry.h"

#include <nlohmann/json.hpp>

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
    json out = json::object();
    // The container *names* exist in TTTR::container_names, but that member is
    // private and maps only name to integer. Labels and extensions are not
    // derivable from it at all, so the table is written out here; the container
    // type is taken from the public constants in TTTRHeaderTypes.h so the two
    // cannot disagree about which integer means which format.
    const struct {
        const char* name; const char* label; const char* extensions; int type;
    } kInfo[] = {
        {"PTU",          "PicoQuant PTU",                ".ptu",       PQ_PTU_CONTAINER},
        {"HT3",          "PicoQuant HT3",                ".ht3",       PQ_HT3_CONTAINER},
        {"SPC-130",      "Becker & Hickl SPC-130",       ".spc",       BH_SPC130_CONTAINER},
        {"SPC-600_256",  "Becker & Hickl SPC-600 (256)", ".spc",       BH_SPC600_256_CONTAINER},
        {"SPC-600_4096", "Becker & Hickl SPC-600 (4096)",".spc",       BH_SPC600_4096_CONTAINER},
        {"SPC-QC",   "Becker & Hickl SPC-QC",".spc",      BH_SPCQC_CONTAINER},
        {"PHOTON-HDF5",  "Photon-HDF5",                  ".h5,.hdf5",  PHOTON_HDF_CONTAINER},
        {"CZ-RAW",       "Zeiss ConfoCor3 raw",          ".raw",       CZ_CONFOCOR3_CONTAINER},
        {"SM",           "Single-molecule (SM)",         ".sm",        SM_CONTAINER},
        {"PHOTONS",      "Photonscore LINCam",           ".photons",   PS_PHOTONS_CONTAINER},
    };
    for (const auto& info : kInfo) {
        json entry = json::object();
        entry["name"] = info.name;
        entry["label"] = info.label;
        entry["summary"] = std::string("TTTR container: ") + info.label;
        entry["extensions"] = info.extensions;
        entry["container_type"] = info.type;
        out[info.name] = entry;
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
    for (const auto& item : build().items()) out.push_back(item.key());
    return out;
}

} // namespace tttrlib

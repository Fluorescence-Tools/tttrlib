// SPDX-License-Identifier: BSD-3-Clause
//
// chiSurf-compatible detector setups (see detector_setup.h). The JSON schema
// here mirrors chisurf/core/data_io/detector_setups.py: everything the CLI
// needs is `setups.<name>.detectors.<name>.chs` (a routing-channel list) and
// optionally `windows`/`micro_time_ranges`; all other fields are chiSurf state
// and are accepted without complaint so a real chiSurf file parses verbatim.

#include "detector_setup.h"

#include <algorithm>
#include <fstream>

#include "nlohmann/json.hpp"

namespace tttrlib {
namespace cli {

using nlohmann::json;

std::vector<int> DetectorSetup::all_channels() const {
    std::vector<int> out;
    for (auto& d : detectors)
        out.insert(out.end(), d.channels.begin(), d.channels.end());
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

bool load_detector_setups(const std::string& path,
                          DetectorSetups* out,
                          std::string* err) {
    std::ifstream ifs(path);
    if (!ifs) {
        if (err) *err = "cannot read detector setups file " + path;
        return false;
    }
    json root;
    try {
        root = json::parse(ifs);
    } catch (const json::parse_error& e) {
        if (err) *err = std::string("detector setups parse error: ") + e.what();
        return false;
    }

    if (out == nullptr) {
        if (err) *err = "load_detector_setups: null output";
        return false;
    }
    out->setups.clear();
    out->last_used.clear();
    if (root.contains("last_used") && root["last_used"].is_string())
        out->last_used = root["last_used"].get<std::string>();

    if (!root.contains("setups") || !root["setups"].is_object()) {
        if (err) *err = "detector setups file has no \"setups\" object";
        return false;
    }
    for (auto& [name, s] : root["setups"].items()) {
        DetectorSetup setup;
        setup.name = name;
        if (s.contains("windows") && s["windows"].is_object()) {
            for (auto& [wn, w] : s["windows"].items()) {
                if (!w.is_array() || w.size() < 2) continue;
                setup.windows[wn] = {w[0].get<int>(), w[1].get<int>()};
            }
        }
        if (s.contains("detectors") && s["detectors"].is_object()) {
            for (auto& [dn, d] : s["detectors"].items()) {
                DetectorDef def;
                def.name = dn;
                if (d.contains("chs") && d["chs"].is_array()) {
                    for (auto& c : d["chs"])
                        def.channels.push_back(c.get<int>());
                }
                if (d.contains("micro_time_ranges") && d["micro_time_ranges"].is_array()) {
                    for (auto& r : d["micro_time_ranges"]) {
                        if (r.is_array() && r.size() >= 2)
                            def.micro_time_ranges.emplace_back(
                                    r[0].get<int>(), r[1].get<int>());
                    }
                }
                setup.detectors.push_back(std::move(def));
            }
        }
        out->setups.push_back(std::move(setup));
    }
    return true;
}

std::vector<int> resolve_setup_channels(const std::string& setup_path,
                                        const std::string& setup_name,
                                        const std::string& detector_name,
                                        std::string* err) {
    DetectorSetups setups;
    if (!load_detector_setups(setup_path, &setups, err)) return {};
    const DetectorSetup* setup = nullptr;
    if (!setup_name.empty()) {
        setup = setups.find(setup_name);
        if (!setup && err) *err = "no detector setup '" + setup_name + "' in " + setup_path;
    } else {
        setup = setups.default_setup();
    }
    if (!setup) {
        if (err && err->empty()) *err = "no detector setup in " + setup_path;
        return {};
    }
    if (!detector_name.empty()) {
        const DetectorDef* d = setup->find_detector(detector_name);
        if (!d) {
            if (err) *err = "no detector '" + detector_name + "' in setup '" + setup->name + "'";
            return {};
        }
        return d->channels;
    }
    return setup->all_channels();
}

bool save_detector_setups(const std::string& path,
                          const DetectorSetups& setups,
                          std::string* err) {
    json root;
    root["last_used"] = setups.last_used;
    json ss = json::object();
    for (auto& s : setups.setups) {
        json dets = json::object();
        for (auto& d : s.detectors) {
            json jd;
            jd["chs"] = d.channels;
            if (!d.micro_time_ranges.empty()) {
                json r = json::array();
                for (auto& m : d.micro_time_ranges)
                    r.push_back({m.first, m.second});
                jd["micro_time_ranges"] = std::move(r);
            }
            dets[d.name] = std::move(jd);
        }
        json js;
        js["detectors"] = std::move(dets);
        if (!s.windows.empty()) {
            json w = json::object();
            for (auto& [wn, p] : s.windows) w[wn] = {p.first, p.second};
            js["windows"] = std::move(w);
        }
        ss[s.name] = std::move(js);
    }
    root["setups"] = std::move(ss);
    std::ofstream ofs(path);
    if (!ofs) {
        if (err) *err = "cannot write detector setups file " + path;
        return false;
    }
    ofs << root.dump(2) << std::endl;
    return true;
}

}  // namespace cli
}  // namespace tttrlib
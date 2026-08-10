// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_CLI_DETECTOR_SETUP_H
#define TTTRLIB_CLI_DETECTOR_SETUP_H

// Instrument description shared with chiSurf: a detector setup maps routing
// channels to named detectors and optionally PIE micro-time windows. The
// canonical chiSurf file is `detector_setups.json`:
//
//   {
//     "last_used": "Abberior 2-detect",
//     "setups": {
//       "Abberior 2-detect": {
//         "detectors": {
//           "Green": {"chs": [0, 1], "micro_time_ranges": [[0, 1023]]},
//           "Red":   {"chs": [2, 3]}
//         },
//         "windows": {"prompt": [0, 1023], "delayed": [1024, 4095]}
//       }
//     }
//   }
//
// A detector's `chs` is the routing (= photon-detector) channel list, in the
// same units as TTTR records and as `tttr sm --channels`. Everything else in
// the file (LUTs, shifts, calibration) is chiSurf state that the CLI does not
// act on; it is accepted and ignored so a real chiSurf file works as-is.
//
// One file, many setups, a named detector in each: the CLI picks a setup by
// name (or the file's last_used), then a detector by name. Commands that count
// work per detector (sm, image export) therefore accept the same three flags:
//
//   --setup FILE  --setup-name NAME  --detector NAME

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace tttrlib {
namespace cli {

/// One named detector inside a setup.
struct DetectorDef {
    std::string name;
    std::vector<int> channels;       ///< routing channels (chs)
    std::vector<std::pair<int, int>> micro_time_ranges;  ///< optional gates
};

/// One PIE micro-time window: a name and an inclusive micro-time range.
struct WindowDef {
    std::string name;
    int lo = 0;
    int hi = 0;
};

/// One named setup: a set of detectors plus optional PIE windows.
struct DetectorSetup {
    std::string name;
    /// In file order — the burst table names its columns after these, and a
    /// `.bur` written here should put them in the order chiSurf would.
    std::vector<DetectorDef> detectors;
    /// In file order, for the same reason the detectors are: the burst table
    /// names a column per window x detector, and a `.bur` written here should
    /// put them in the order chiSurf would. A `std::map` sorted them
    /// alphabetically, so `prompt`/`delayed` came out `delayed`/`prompt` and
    /// the values were right while the column order was not — which only shows
    /// up when another tool reads the table positionally.
    std::vector<WindowDef> windows;

    /// The window of this name, or null.
    const WindowDef* window(const std::string& n) const {
        for (const auto& w : windows) if (w.name == n) return &w;
        return nullptr;
    }
    /// Set the range of a named window, appending it in file order if new.
    void set_window(const std::string& n, int lo, int hi) {
        for (auto& w : windows) {
            if (w.name == n) { w.lo = lo; w.hi = hi; return; }
        }
        windows.push_back(WindowDef{n, lo, hi});
    }

    /// Polarization corrections, as chiSurf keeps them on the setup.
    ///
    /// Instrument constants, so they belong to the file rather than to a flag:
    /// `g` is the detection-efficiency ratio of the parallel and perpendicular
    /// arms, `l1`/`l2` the objective's depolarisation factors. Defaults are the
    /// ideal instrument (g = 1, no mixing), which is also what a setup that does
    /// not mention them is claiming.
    double g_factor = 1.0;
    double l1 = 0.0;
    double l2 = 0.0;
    bool polarization_resolved = false;

    /// A detector's parallel channels — `chs[::2]`, chiSurf's interleave.
    ///
    /// Getting this backwards inverts every anisotropy computed from the file
    /// without changing a single photon count, so it is written down once here
    /// rather than re-derived at each call site.
    static std::vector<int> parallel_channels(const DetectorDef& d) {
        std::vector<int> out;
        for (std::size_t i = 0; i < d.channels.size(); i += 2)
            out.push_back(d.channels[i]);
        return out;
    }

    /// A detector's perpendicular channels — `chs[1::2]`.
    static std::vector<int> perpendicular_channels(const DetectorDef& d) {
        std::vector<int> out;
        for (std::size_t i = 1; i < d.channels.size(); i += 2)
            out.push_back(d.channels[i]);
        return out;
    }

    const DetectorDef* find_detector(const std::string& n) const {
        for (auto& d : detectors)
            if (d.name == n) return &d;
        return nullptr;
    }

    /// Union of all configured routing channels, sorted, unique.
    std::vector<int> all_channels() const;
};

/// Everything one detector_setups.json holds.
struct DetectorSetups {
    std::vector<DetectorSetup> setups;  ///< in file order
    std::string last_used;              ///< "" when the file has none

    const DetectorSetup* find(const std::string& name) const {
        for (auto& s : setups)
            if (s.name == name) return &s;
        return nullptr;
    }

    /// The setup to use when the user gave no --setup-name: last_used, else
    /// the first in the file, else nullptr.
    const DetectorSetup* default_setup() const {
        if (!last_used.empty()) {
            if (auto* s = find(last_used)) return s;
        }
        return setups.empty() ? nullptr : &setups[0];
    }
};

/// Parse a chiSurf-compatible detector_setups.json. On failure returns false
/// and fills *err.
bool load_detector_setups(const std::string& path,
                          DetectorSetups* out,
                          std::string* err);

/// Resolve the routing channels a subcommand should count on. With a detector
/// name that detector's channels win; without one the whole setup (union of
/// all its detectors, sorted). Errors (missing file, setup, detector) set
/// *err and the result is empty.
std::vector<int> resolve_setup_channels(const std::string& setup_path,
                                        const std::string& setup_name,
                                        const std::string& detector_name,
                                        std::string* err);

/// The setup a subcommand should work against, with its named detectors kept.
///
/// `resolve_setup_channels` answers "which channels", which is all a filter
/// needs; a burst table needs "which detectors, called what", because the
/// column names are the detector names. Same three flags, same selection rules
/// — with `--detector` the returned setup holds that one detector, without it
/// all of them, in file order.
///
/// Returns false and fills *err on a missing file, setup or detector.
bool resolve_setup(const std::string& setup_path,
                   const std::string& setup_name,
                   const std::string& detector_name,
                   DetectorSetup* out,
                   std::string* err);

/// Serialize setups back into the chiSurf detector_setups.json schema. chiSurf
/// state the loader ignored (LUTs, shifts, calibration) is not carried.
bool save_detector_setups(const std::string& path,
                          const DetectorSetups& setups,
                          std::string* err);

}  // namespace cli
}  // namespace tttrlib

#endif  // TTTRLIB_CLI_DETECTOR_SETUP_H
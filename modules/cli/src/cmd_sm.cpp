// SPDX-License-Identifier: BSD-3-Clause
//
// tttr sm FILE [--config CONFIG.json] [flags] [--output out.json|out.mmfdb.pto]
//
// Single-molecule / burst processing from the command line. A TTTR file in, a
// burst table out: the burst search parameters can come from a JSON config
// file, from inline flags, or both (flags win).
//
// An output name ending in `.pto` writes a container instead of JSON. Prefer
// `<stem>.mmfdb.pto` for it: `.pto` is the container — an EBML document that
// says nothing about its contents — and `.mmfdb` on the stem says this one also
// carries the PTO.MFDB profile, which is what everything written below is. The
// tag goes on the stem and never on the suffix, so the file is still a
// container to anything dispatching on the extension. The suffix is a courtesy;
// what makes it conformant is `_mmfdb_container.profile` inside it.
//
// The JSON config matches the shape the burst-search registry advertises, so a
// settings block that drives tttrlib's own Python objects drives this tool too:
//
//   {
//     "method": "sliding_window",
//     "min_photons": 20,
//     "rate_window": 10,
//     "time_separation": 0.0005,
//     "channels": [0, 1],
//     "output": "bursts.pto"
//   }
//
// -- The burst table is named after the detectors, not after a colour --------
//
// Which columns a burst table has is a property of the *instrument*, so it is
// read from `--setup` (chiSurf's detector_setups.json) and nothing here knows
// what "green" means. A setup whose detectors are `green` and `red` produces
// `Duration (green) (ms)`; one whose detectors are `parallel` and
// `perpendicular` produces `Duration (parallel) (ms)`; a four-channel MFD setup
// produces four sets. Without a setup only the aggregate columns are written,
// which is the single-colour / "I just want bursts" path.
//
// The names, the sentinels and the arithmetic are ChiSurf's
// `generate_burst_dataframe` (chisurf/core/fio/fluorescence/burst.py) — this is
// a port of that function, not a second dialect of it. Two spellings there look
// like slips and are not: the aggregate column is `Mean Macro Time (ms)` while
// the per-detector one is `Mean Macrotime (<d>) (ms)`, and both name a
// *midpoint* of the burst's first and last photon rather than a mean. They are
// the format.
//
// What is deliberately NOT ported is the `.bur` file's 2N+1 zero-row interleave
// and its trailing blank column. Those exist so a `.bur` can be merged with its
// companions by counting rows; inside a container the relation is a declared
// key, and ChiSurf's own container writer deinterleaves before it writes.

#include "tttr_cli.h"
#include "cli_progress.h"
#include "detector_setup.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "cxxopts.hpp"
#include "nlohmann/json.hpp"

#include "BVA.h"
#include "DecayFitModel.h"
#include "DecayFitProblem.h"
#include "Sha256.h"
#include "TTTR.h"
#include "TTTRHeader.h"
#include "TwoCDE.h"
#include "io_csv_writer.h"
#include "io_pto.h"
#include "io_store.h"

namespace tttr = tttrlib;
using nlohmann::json;
using tttrlib::cli::DetectorDef;
using tttrlib::cli::DetectorSetup;
using tttrlib::data::ColumnType;
using tttrlib::data::DataStore;

namespace {

const double kNaN = std::numeric_limits<double>::quiet_NaN();

/// Sentinel for a per-detector column a burst has no photons in. ChiSurf's
/// DETECTOR_SENTINEL: -1.0 everywhere except the photon count, which is 0.
const double kSentinel = -1.0;

json load_config(const std::string& path, std::string* err) {
    std::ifstream ifs(path);
    if (!ifs) {
        *err = "cannot read config " + path;
        return json::object();
    }
    try {
        return json::parse(ifs);
    } catch (const json::parse_error& e) {
        *err = std::string("config parse error: ") + e.what();
        return json::object();
    }
}

/// A pipeline document, from a `.json` file or from the
/// `_mmfdb_workflow.definition` tag of a `.pto` container -- the two carriers
/// `tttrlib.Pipeline` writes. The format is checked, and a document from a
/// newer format version is refused rather than half-understood.
json load_pipeline_document(const std::string& path, std::string* err) {
    json doc = json::object();
    if (tttr::io::is_pto_file(path)) {
        tttr::io::PtoFile pto;
        if (!pto.open(path)) {
            *err = "cannot read pipeline container " + path + ": " + pto.error();
            return doc;
        }
        std::string text;
        for (const auto& tag : pto.tags()) {
            if (tag.name == "_mmfdb_workflow.definition") { text = tag.text; break; }
        }
        pto.close();
        if (text.empty()) {
            *err = path + " carries no _mmfdb_workflow.definition";
            return doc;
        }
        try {
            doc = json::parse(text);
        } catch (const json::parse_error& e) {
            *err = std::string("pipeline parse error: ") + e.what();
            return doc;
        }
    } else {
        std::ifstream ifs(path);
        if (!ifs) {
            *err = "cannot read pipeline " + path;
            return doc;
        }
        try {
            doc = json::parse(ifs);
        } catch (const json::parse_error& e) {
            *err = std::string("pipeline parse error: ") + e.what();
            return doc;
        }
    }
    const std::string format = doc.value("format", std::string("tttrlib.pipeline"));
    if (format != "tttrlib.pipeline") {
        *err = path + " is not a tttrlib.pipeline document (format=" + format + ")";
        return json::object();
    }
    if (doc.value("format_version", 1) > 1) {
        *err = path + " is a pipeline of format version " +
               std::to_string(doc.value("format_version", 1)) +
               ", newer than this tttrlib understands (1); written by tttrlib " +
               doc.value("software", json::object()).value("version", std::string("unknown"));
        return json::object();
    }
    return doc;
}

/// Python's `str.capitalize`: first character upper, the rest lower. The rate
/// column is spelled `Green Count Rate (KHz)`, so a detector named `GREEN` and
/// one named `green` must produce the same header.
std::string capitalize(const std::string& s) {
    std::string out = s;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(out[i]);
        out[i] = static_cast<char>(i == 0 ? std::toupper(c) : std::tolower(c));
    }
    return out;
}

/// The identity of a run: SHA-256 of the settings as canonical JSON. Same rule
/// as ChiSurf's `_settings_hash` — keys sorted, no whitespace — so the two
/// agree on when a re-run is the same run.
std::string settings_hash(const json& settings) {
    return tttrlib::util::sha256_hex(settings.dump());
}

/// Which photons belong to one named detector, over the whole stream.
struct DetectorMask {
    std::string name;
    std::vector<unsigned char> in;
};

/// A detector accepts a photon whose routing channel is one of its own AND
/// whose micro time falls in one of its windows. **No windows accepts every
/// micro time** — a detector defined by routing channels alone is ungated, not
/// empty. (Getting that backwards writes a table in which no detector ever saw
/// a photon, which is a bug ChiSurf has already had.)
std::vector<DetectorMask> detector_masks(const DetectorSetup& setup,
                                         const signed char* rout,
                                         const unsigned short* micro,
                                         std::size_t n) {
    std::vector<DetectorMask> out;
    out.reserve(setup.detectors.size());
    for (const DetectorDef& d : setup.detectors) {
        DetectorMask m;
        m.name = d.name;
        m.in.assign(n, 0);
        for (std::size_t i = 0; i < n; ++i) {
            const int ch = rout != nullptr ? static_cast<int>(rout[i]) : 0;
            if (std::find(d.channels.begin(), d.channels.end(), ch) ==
                d.channels.end())
                continue;
            if (d.micro_time_ranges.empty()) {
                m.in[i] = 1;
                continue;
            }
            const int mt = micro != nullptr ? static_cast<int>(micro[i]) : 0;
            for (const auto& r : d.micro_time_ranges) {
                if (mt >= r.first && mt < r.second) { m.in[i] = 1; break; }
            }
        }
        out.push_back(std::move(m));
    }
    return out;
}

/// A path's final component, extension kept. The `First File` / `Last File`
/// columns name the instrument file a row came from, and ChiSurf writes
/// `Path(filename).name` — dropping the suffix there would make two runs over
/// `run.spc` and `run.ptu` indistinguishable in a merged table.
std::string file_name(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

/// An instrument file's `_mmfdb_artifact.data_format` term, from its suffix.
///
/// Same table as ChiSurf's `_FORMAT_BY_SUFFIX`, and the same rule for a suffix
/// it does not know: carry `unknown` rather than guess. The bytes round-trip
/// either way, and a wrong term is worse than no term.
std::string tttr_format_term(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "unknown";
    std::string ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (ext == ".ptu" || ext == ".ht3" || ext == ".pt3") return "ptu";
    if (ext == ".spc" || ext == ".set") return "spc";
    if (ext == ".sm" || ext == ".ttr") return "tttr";
    if (ext == ".h5" || ext == ".hdf5") return "photon_hdf5";
    if (ext == ".bin") return "bin";
    return "unknown";
}

/// Half-open [lo, hi) over the micro times, as ChiSurf's window masks are.
std::vector<unsigned char> window_mask(const unsigned short* micro,
                                       std::size_t n, int lo, int hi) {
    std::vector<unsigned char> m(n, 0);
    if (micro == nullptr) return m;
    for (std::size_t i = 0; i < n; ++i) {
        const int mt = static_cast<int>(micro[i]);
        m[i] = static_cast<unsigned char>(mt >= lo && mt < hi);
    }
    return m;
}

// -- column units ------------------------------------------------------------
//
// A column carries a name and a dtype, which is enough to read it and not
// enough to understand it: `Duration (ms)` and `Tau` sit in the same table. The
// PTO.MFDB profile records the unit as a column attribute, `_mmfdb_column.units`,
// so it travels with the column and a column-subset read gets it too.
//
// This is a port of ChiSurf's rule (`chisurf/core/units.py::split_label` and
// `burst_container.py::units_for`), not a second one: two writers disagreeing
// about the unit of the same column is the failure the vocabulary exists to
// end. The authority is the `_mmfdb_units` category of the MMFDB dictionary,
// which this cannot read — so what is ported is the subset a burst table needs,
// and a test pins the two writers to the same answer.
//
// The rule: read the unit out of the label if the label says one; otherwise the
// table below; otherwise none, which means *unknown*, not dimensionless.

/// The legacy spellings a column name uses, mapped to their dictionary code.
/// Reading these is supported because the files exist; nothing writes them.
std::string canonical_unit(const std::string& text) {
    std::string s;
    for (char c : text)
        if (!std::isspace(static_cast<unsigned char>(c))) s.push_back(c);
    std::string lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    static const std::pair<const char*, const char*> kUnits[] = {
        {"s", "seconds"},       {"sec", "seconds"},
        {"ms", "milliseconds"}, {"us", "microseconds"},
        {"ns", "nanoseconds"},  {"ps", "picoseconds"},
        {"hz", "hertz"},        {"khz", "kilohertz"},
        {"mhz", "megahertz"},   {"cps", "counts_per_second"},
        {"nm", "nanometres"},   {"um", "micrometres"},
        {"deg", "degrees"},     {"rad", "radians"},
        {"px", "pixels"},
        // codes pass through unchanged
        {"seconds", "seconds"},           {"milliseconds", "milliseconds"},
        {"microseconds", "microseconds"}, {"nanoseconds", "nanoseconds"},
        {"picoseconds", "picoseconds"},   {"kilohertz", "kilohertz"},
        {"hertz", "hertz"},               {"photons", "photons"},
        {"counts", "counts"},             {"dimensionless", "dimensionless"},
        {"pixels", "pixels"},
    };
    for (const auto& u : kUnits)
        if (lower == u.first) return u.second;
    return {};
}

/// Match a trailing `(unit)` / `[unit]`, as ChiSurf's `_SUFFIX` does. Returns
/// false when the label does not end in a bracket.
bool split_suffix(const std::string& label, std::string* name, std::string* inside) {
    std::size_t end = label.find_last_not_of(" \t");
    if (end == std::string::npos) return false;
    const char close = label[end];
    const char open = close == ')' ? '(' : (close == ']' ? '[' : '\0');
    if (open == '\0') return false;
    const std::size_t at = label.rfind(open, end);
    if (at == std::string::npos) return false;
    *inside = label.substr(at + 1, end - at - 1);
    std::size_t stop = label.find_last_not_of(" \t", at == 0 ? 0 : at - 1);
    *name = at == 0 ? std::string() : label.substr(0, stop + 1);
    return true;
}

/// The unit of a burst-table column, as an `_mmfdb_column.units` code, or "".
std::string column_unit(const std::string& label) {
    std::string name, inside;

    // `Name | unit`, and the one label that carries both conventions at once:
    // `S prompt green (kHz) | 0-2048`, where the bar separates a *range* and the
    // unit is in the bracket to its left.
    const std::size_t bar = label.find('|');
    if (bar != std::string::npos) {
        std::string code = canonical_unit(label.substr(bar + 1));
        if (!code.empty()) return code;
        if (split_suffix(label.substr(0, bar), &name, &inside)) {
            code = canonical_unit(inside);
            if (!code.empty()) return code;
        }
    }

    if (split_suffix(label, &name, &inside)) {
        const std::string code = canonical_unit(inside);
        if (!code.empty()) return code;
    }

    // The columns the naming convention never covered. A column in neither
    // place gets no unit, which means the unit is *unknown* — not dimensionless.
    // Those are different claims and only one is safe to make by default.
    static const std::pair<const char*, const char*> kByName[] = {
        {"Tau", "nanoseconds"},
        {"Lifetime", "nanoseconds"},
        {"Number of Photons", "photons"},
        {"Fused Gap Photons", "photons"},
        {"Fused Bursts", "counts"},
        {"Fusion Group Size", "counts"},
        {"Proximity Ratio", "dimensionless"},
        {"Proximity Ratio Mean", "dimensionless"},
        {"Proximity Ratio Std", "dimensionless"},
        {"Confidence (sigma)", "dimensionless"},
        // A 2CDE value is a score on a fixed scale (~10 for a static burst),
        // not a measurement in anything — dimensionless, which is a different
        // claim from the unit being unknown.
        {"FRET 2CDE", "dimensionless"},
        {"ALEX 2CDE", "dimensionless"},
    };
    for (const auto& e : kByName)
        if (label == e.first) return e.second;
    // A per-detector column is the same quantity as the one it qualifies, so
    // the qualifier is dropped and the table asked again: without this,
    // `Number of Photons` is photons and `Number of Photons (green)`, in the
    // same row, is unitless.
    if (split_suffix(label, &name, &inside) && !name.empty()) {
        for (const auto& e : kByName)
            if (name == e.first) return e.second;
    }
    return {};
}

/// Attach `_mmfdb_column.units` to every column whose unit is known.
void describe_columns(DataStore& ds) {
    for (int i = 0; i < ds.n_columns(); ++i) {
        auto& col = ds.column(i);
        const std::string unit = column_unit(col.name());
        if (unit.empty()) continue;
        col.set_attribute("units", unit);
        col.set_attribute("name", col.name());
    }
}

void add_i64(DataStore& ds, const std::string& name,
             const std::vector<std::int64_t>& v) {
    const int idx = ds.add_column(name, ColumnType::Int64);
    auto& col = ds.column(idx);
    col.resize(static_cast<int>(v.size()));
    auto* p = reinterpret_cast<std::int64_t*>(col.data_ptr());
    if (p != nullptr && !v.empty()) std::copy(v.begin(), v.end(), p);
}

void add_f64(DataStore& ds, const std::string& name,
             const std::vector<double>& v) {
    const int idx = ds.add_column(name, ColumnType::Float64);
    auto& col = ds.column(idx);
    col.resize(static_cast<int>(v.size()));
    auto* p = reinterpret_cast<double*>(col.data_ptr());
    if (p != nullptr && !v.empty()) std::copy(v.begin(), v.end(), p);
}

void add_str(DataStore& ds, const std::string& name, const std::string& value,
             std::size_t n) {
    const int idx = ds.add_column(name, ColumnType::String);
    auto& col = ds.column(idx);
    for (std::size_t i = 0; i < n; ++i) col.push_string(value);
}

// -- per-burst lifetime fitting ----------------------------------------------
//
// One lifetime per burst per detector, by Poisson MLE over the detector's
// parallel and perpendicular arms jointly (`fit23`). The columns are ChiSurf's
// `.bg4`/`.br4` set, in its order, including the two spaces in `2I*  (green)`
// which are part of the format rather than a slip.
//
// **A fit needs an instrument response and this cannot invent one.** A delta at
// t = 0 is a claim about the instrument, not a neutral default, and a lifetime
// fitted against the wrong prompt is wrong by roughly the prompt's width with
// nothing in the output saying so. So `--mle` requires `--irf`, and `delta` is
// spelled out by whoever wants it.
//
// The **background** pattern is measured rather than assumed: the detector's
// photons that no burst contains are exactly the background of that detector,
// they are already in hand, and using them costs nothing.

/// What `--irf` was given: a measured curve, a synthetic prompt, or a delta.
struct IrfSpec {
    enum Kind { Delta, Gaussian, SkewGaussian, File } kind = Delta;
    double fwhm_ns = 0.0;
    double t0_ns = 0.0;
    /// Skew of `sgauss`. 0 is the symmetric Gaussian; positive tails to later
    /// times, which is the direction a real prompt leans.
    double alpha = 1.5;
    std::string path;
    std::string text;   ///< what to record in the settings
};

/// Split "name:a,b,c" into its numbers.
bool irf_numbers(const std::string& rest, std::vector<double>* out) {
    std::size_t at = 0;
    while (at <= rest.size()) {
        const std::size_t comma = rest.find(',', at);
        const std::string field =
                rest.substr(at, comma == std::string::npos ? std::string::npos
                                                           : comma - at);
        if (field.empty()) return false;
        try {
            std::size_t used = 0;
            out->push_back(std::stod(field, &used));
            if (used != field.size()) return false;
        } catch (const std::exception&) {
            return false;
        }
        if (comma == std::string::npos) break;
        at = comma + 1;
    }
    return !out->empty();
}

bool parse_irf(const std::string& spec, IrfSpec* out, std::string* err) {
    out->text = spec;
    if (spec == "delta") { out->kind = IrfSpec::Delta; return true; }

    // `gaussian` is the spelling this shipped with first and keeps working;
    // `gauss` is the short one, and `sgauss` its skewed sibling.
    struct { const char* prefix; IrfSpec::Kind kind; } kinds[] = {
        {"sgauss:", IrfSpec::SkewGaussian},
        {"skewed:", IrfSpec::SkewGaussian},
        {"gauss:", IrfSpec::Gaussian},
        {"gaussian:", IrfSpec::Gaussian},
    };
    for (const auto& k : kinds) {
        const std::string prefix = k.prefix;
        if (spec.rfind(prefix, 0) != 0) continue;
        const std::string name = prefix.substr(0, prefix.size() - 1);
        std::vector<double> v;
        if (!irf_numbers(spec.substr(prefix.size()), &v)) {
            *err = "--irf " + name + ": expects FWHM_NS[,T0_NS" +
                   (k.kind == IrfSpec::SkewGaussian ? "[,SKEW]" : "") +
                   "], got '" + spec.substr(prefix.size()) + "'";
            return false;
        }
        const std::size_t max_args = k.kind == IrfSpec::SkewGaussian ? 3u : 2u;
        if (v.size() > max_args) {
            *err = "--irf " + name + ": too many values";
            return false;
        }
        out->kind = k.kind;
        out->fwhm_ns = v[0];
        if (v.size() > 1) out->t0_ns = v[1];
        if (v.size() > 2) out->alpha = v[2];
        if (out->fwhm_ns <= 0.0) {
            *err = "--irf " + name + ": FWHM must be positive";
            return false;
        }
        return true;
    }

    std::ifstream probe(spec);
    if (!probe) {
        *err = "--irf: not 'delta', not 'gauss:FWHM[,T0]', not "
               "'sgauss:FWHM[,T0[,SKEW]]', and not a readable file: '" +
               spec + "'";
        return false;
    }
    out->kind = IrfSpec::File;
    out->path = spec;
    return true;
}

/// The instrument response over one polarization, `n_bins` long.
///
/// A file is read as one value per line (or the last column of a delimited
/// line), which is what a decay exported from anywhere looks like; it is padded
/// or truncated to `n_bins` rather than refused, because an IRF measured at a
/// different length is still that instrument's prompt.
std::vector<double> build_irf(const IrfSpec& spec, int n_bins, double dt_ns,
                              std::string* err) {
    std::vector<double> irf(static_cast<std::size_t>(n_bins), 0.0);
    switch (spec.kind) {
        case IrfSpec::Delta: {
            const int at = static_cast<int>(spec.t0_ns / dt_ns);
            irf[static_cast<std::size_t>(at >= 0 && at < n_bins ? at : 0)] = 1.0;
            return irf;
        }
        case IrfSpec::Gaussian:
        case IrfSpec::SkewGaussian: {
            // FWHM -> sigma for the *symmetric* Gaussian: 2*sqrt(2*ln2).
            const double sigma = spec.fwhm_ns / 2.354820045030949;
            // A skew-normal: the Gaussian times a smooth ramp. `t0` is the
            // location parameter, NOT the peak — with a non-zero skew the mode
            // sits a little past it, and the FWHM is the underlying Gaussian's
            // rather than the skewed curve's. Both are how the skew-normal is
            // parameterised everywhere, and a prompt fitted to a measured IRF
            // hands back these numbers, not the ones read off the plot.
            const double alpha =
                    spec.kind == IrfSpec::SkewGaussian ? spec.alpha : 0.0;
            for (int i = 0; i < n_bins; ++i) {
                const double z = ((i + 0.5) * dt_ns - spec.t0_ns) / sigma;
                double v = std::exp(-0.5 * z * z);
                if (alpha != 0.0)
                    v *= 1.0 + std::erf(alpha * z / 1.4142135623730951);
                irf[static_cast<std::size_t>(i)] = v;
            }
            return irf;
        }
        case IrfSpec::File: {
            std::ifstream ifs(spec.path);
            if (!ifs) { *err = "cannot read IRF " + spec.path; return {}; }
            std::vector<double> raw;
            std::string line;
            while (std::getline(ifs, line)) {
                if (line.empty() || line[0] == '#') continue;
                for (char& c : line)
                    if (c == ',' || c == ';' || c == '\t') c = ' ';
                std::istringstream is(line);
                double v = 0.0, last = 0.0;
                bool any = false;
                while (is >> v) { last = v; any = true; }
                if (any) raw.push_back(last);
            }
            if (raw.empty()) { *err = "IRF " + spec.path + " holds no numbers"; return {}; }
            for (int i = 0; i < n_bins; ++i)
                irf[static_cast<std::size_t>(i)] =
                        i < (int) raw.size() ? raw[static_cast<std::size_t>(i)] : 0.0;
            return irf;
        }
    }
    return irf;
}

/// Everything one detector's fit needs, built once for the whole measurement.
struct MleSetup {
    std::vector<int> par, perp;          ///< routing channels per arm
    std::vector<double> irf;             ///< 2 * n_bins, parallel then perpendicular
    std::vector<double> background;      ///< same layout, measured off-burst
    int n_bins = 0;
    double dt_ns = 1.0;
    /// Raw micro-time channels per fit bin.
    ///
    /// A burst holds ~100 photons and the TAC axis has 4096 channels, so a
    /// per-burst decay is almost all zeros and the convolution is 4096 long for
    /// no gain. Rebinning is what makes the fit both fast and conditioned; the
    /// factor is recorded in the settings so the axis a lifetime was fitted on
    /// is part of the run's identity.
    int rebin = 1;

    /// Background counts per second on this detector, measured off-burst.
    ///
    /// `fit23`'s `gamma` is the *fraction of the burst that is background* — the
    /// kernel renormalises the background pattern to the burst's own total, so
    /// only its shape comes from the pattern and its magnitude comes from here.
    /// That makes gamma a measured quantity per burst rather than a constant to
    /// guess: leaving it at 0 subtracts nothing and biases every lifetime down
    /// by roughly the background fraction.
    double bg_cps = 0.0;
};

/// One row of the fit output, in ChiSurf's order.
struct MleRow {
    std::int64_t n_par = 0, n_perp = 0;
    double two_istar = kNaN, tau = kNaN, gamma = kNaN, r0 = kNaN, rho = kNaN;
    double r_scatter = kNaN, r_experimental = kNaN;
};

// -- PTO tagging -------------------------------------------------------------
//
// **Every term written here comes from the MMFDB dictionary.** The profile
// defines no vocabulary of its own, so a word this invents is a word nothing can
// query — and ChiSurf's writer *refuses* to write one, while this one had been
// emitting four: `bva`, `kde_cde`, `mle_<detector>` and `companion_of`, none of
// which the dictionary declares. The operations are
// `burst_variance_analysis`, `burst_2cde` and `burst_lifetime_fitting`; a
// companion is `derived_from` its burst table, which is what it is (that the
// two share a grain is what `row_grain` says, not the edge). Pinned from the
// ChiSurf side, which is the only side that can read the dictionary, by
// `test/fio/test_container_cross_writer.py`.

using tttr::io::PtoFile;
using tttr::io::PtoTag;
using tttr::io::PtoType;

void tag_text(PtoFile& pto, std::uint64_t uid, const std::string& name,
              const std::string& text) {
    PtoTag t;
    t.target = uid;
    t.name = name;
    t.type = PtoType::Text;
    t.text = text;
    pto.set_tag(t);
}

void tag_uid(PtoFile& pto, std::uint64_t uid, const std::string& name,
             std::uint64_t value) {
    PtoTag t;
    t.target = uid;
    t.name = name;
    t.type = PtoType::UID;
    t.u = value;
    pto.set_tag(t);
}

std::string tag_of(const PtoFile& pto, std::uint64_t uid,
                   const std::string& name) {
    for (const PtoTag& t : pto.tags_for(uid))
        if (t.name == name) return t.text;
    return {};
}

/// Where a result goes: the object holding an earlier run of the same operation
/// with the same settings, or 0 for "add a new one".
///
/// This is what keeps a container from accumulating one artifact per re-run.

/// The run as a **pipeline document** -- the same schema
/// `tttrlib.Pipeline` writes and mmfdb's workflow reads (`version: 1`,
/// `sources` + `steps`). Stored in the container under
/// `_mmfdb_workflow.definition`, so the artifact carries the recipe that
/// produced it and `Pipeline.from_pto()` (or `mmfdb workflow run`) can replay
/// it. The per-artifact `_mmfdb_operation.*` tags say what each table IS; this
/// says how the whole run was invoked, which is the part a replay needs.
json pipeline_document(const std::string& input_path,
                       const std::string& input_format,
                       const json& search_settings,
                       const std::vector<json>& companion_steps) {
    const std::string version = TTTRLIB_VERSION_STRING;
    json software = {{"package", "tttrlib"}, {"version", version}};
    json steps = json::array();

    // A channel restriction is applied BEFORE the search, so it is its own
    // step. Hiding it in the search's parameters would make a replay run on
    // the whole stream and quietly find other bursts.
    std::string previous = "raw";
    if (search_settings.contains("channels")) {
        steps.push_back({{"id", "photon_selection"},
                         {"operation", "photon_selection"},
                         {"operation_type", "filtering"},
                         {"params", {{"channels", search_settings["channels"]}}},
                         {"inputs", {{"photons", "raw"}}},
                         {"outputs", json::object()},
                         {"python", "tttrlib.pipeline:run_step"},
                         {"software", software}});
        previous = "photon_selection";
    }

    // The step's parameters are the ones `burst_selection` DECLARES -- the
    // arguments of `TTTR.burst_search_by_name(algorithm, **params)` -- not the
    // .bur settings names used for the per-artifact `settings_json` tag. A
    // document is only replayable if its parameters are the call's.
    json search_params = json::object();
    search_params["algorithm"] = search_settings.value("method", std::string("sliding_window"));
    if (search_settings.contains("min_photons"))
        search_params["L"] = search_settings["min_photons"];
    if (search_settings.contains("rate_window"))
        search_params["m"] = search_settings["rate_window"];
    if (search_settings.contains("time_separation"))
        search_params["T"] = search_settings["time_separation"];
    steps.push_back({{"id", "burst_selection"},
                     {"operation", "burst_selection"},
                     {"operation_type", "burst_selection"},
                     {"params", search_params},
                     {"inputs", {{"photons", previous == "raw"
                                                 ? std::string("raw")
                                                 : previous + ".output"}}},
                     {"outputs", json::object()},
                     {"python", "tttrlib.pipeline:run_step"},
                     {"software", software}});
    for (const json& step : companion_steps) steps.push_back(step);

    return json{{"format", "tttrlib.pipeline"},
                {"format_version", 1},
                {"version", 1},                       // the mmfdb workflow schema
                {"name", "tttr-sm"},
                {"description", "tttr sm burst analysis"},
                {"software", software},
                {"sources", {{"raw", {{"path", input_path},
                                      {"kind", "raw_measurement"},
                                      {"metadata", {{"file_type", input_format}}}}}}},
                {"steps", steps}};
}

/// One companion step of the document, in the same shape.
json pipeline_step(const std::string& id, const std::string& operation,
                   const std::string& operation_type, const json& settings,
                   const std::string& after) {
    // The document's `params` are the step's ARGUMENTS. The run identity of
    // the burst list a companion was computed over is provenance, and it is
    // already stated as the step's input, so it does not belong here -- a
    // replay would pass a hash where a number goes.
    json params = settings;
    params.erase("bursts");
    return json{{"id", id},
                {"operation", operation},
                {"operation_type", operation_type},
                {"params", params},
                {"inputs", {{"bursts", after + ".output"}}},
                {"outputs", json::object()},
                {"python", "tttrlib.pipeline:run_step"},
                {"software", {{"package", "tttrlib"},
                              {"version", TTTRLIB_VERSION_STRING}}}};
}

std::uint64_t find_run(const PtoFile& pto, const std::string& operation,
                       const std::string& run_hash) {
    for (const auto& obj : pto.objects()) {
        if (tag_of(pto, obj.uid, "_mmfdb_operation.operation_type") != operation)
            continue;
        if (tag_of(pto, obj.uid, "_mmfdb_operation.settings_hash") != run_hash)
            continue;
        return obj.uid;
    }
    return 0;
}

/// Write a table into the container, replacing an earlier run of the same
/// operation with the same settings, and describe it the way the PTO.MFDB
/// profile does. Mirrors ChiSurf's `Measurement.put_table` + `_describe`.
std::uint64_t put_table(PtoFile& pto, const std::string& name,
                        DataStore& store, const std::string& operation,
                        const json& settings, const std::string& row_grain,
                        const std::vector<std::uint64_t>& derived_from,
                        const std::string& relationship,
                        const std::string& algorithm = std::string()) {
    describe_columns(store);
    const std::string run = settings_hash(settings);
    std::uint64_t uid = find_run(pto, operation, run);
    if (uid != 0) {
        if (!tttr::io::pto_update_store(pto, uid, store)) return 0;
    } else {
        uid = tttr::io::pto_add_store(pto, "burst_table", name, store);
        if (uid == 0) return 0;
    }

    // `dstore` is the encoding of every table in a container. The legacy
    // per-analysis extensions (bur, bg4, bv4, ...) name a FILE layout, and this
    // is not one; the operation_type says what the table is.
    tag_text(pto, uid, "_mmfdb_artifact.data_format", "dstore");
    tag_text(pto, uid, "_mmfdb_artifact.row_grain", row_grain);
    tag_text(pto, uid, "_mmfdb_operation.operation_type", operation);
    // `operation_type` is deliberately coarse because it is the join key --
    // every per-burst lifetime is `burst_lifetime_fitting`. Which *estimator*
    // produced it is a separate, controlled fact: an MLE lifetime, a phasor
    // lifetime and a moment-derived one have different bias and must not be
    // pooled. See _mmfdb_operation.algorithm in mmfdb.
    if (!algorithm.empty())
        tag_text(pto, uid, "_mmfdb_operation.algorithm", algorithm);
    tag_text(pto, uid, "_mmfdb_operation.settings_json", settings.dump());
    tag_text(pto, uid, "_mmfdb_operation.settings_hash", run);
    tag_text(pto, uid, "_mmfdb_operation.software_package", "tttrlib");
    for (std::uint64_t parent : derived_from)
        tag_uid(pto, uid, "_mmfdb_edge.source_node_id", parent);
    if (!derived_from.empty())
        tag_text(pto, uid, "_mmfdb_edge.relationship_type", relationship);
    return uid;
}

/// Build the per-detector fit setup: which arm is which, the response, and the
/// background measured from the photons no burst contains.
MleSetup build_mle_setup(const DetectorDef& d, const IrfSpec& irf_spec,
                         const signed char* rout, const unsigned short* micro,
                         std::size_t n_ph, const std::vector<unsigned char>& in_burst,
                         int n_micro_channels, double micro_ns, int fit_bins,
                         double duration_s, std::string* err) {
    MleSetup s;
    s.rebin = fit_bins > 0 ? std::max(1, n_micro_channels / fit_bins) : 1;
    s.n_bins = n_micro_channels / s.rebin;
    s.dt_ns = micro_ns * s.rebin;
    const int n_bins = s.n_bins;
    s.par = DetectorSetup::parallel_channels(d);
    s.perp = DetectorSetup::perpendicular_channels(d);

    const std::vector<double> one = build_irf(irf_spec, n_bins, s.dt_ns, err);
    if (one.empty()) return s;
    // The same prompt on both arms: a per-arm response needs a per-arm
    // measurement, which nothing here has, and asserting a difference would be
    // inventing one.
    s.irf.reserve(one.size() * 2);
    s.irf.insert(s.irf.end(), one.begin(), one.end());
    s.irf.insert(s.irf.end(), one.begin(), one.end());

    s.background.assign(static_cast<std::size_t>(n_bins) * 2, 0.0);
    std::int64_t off_burst = 0;
    for (std::size_t i = 0; i < n_ph; ++i) {
        if (in_burst[i]) continue;
        const int ch = rout != nullptr ? static_cast<int>(rout[i]) : -1;
        const int raw = micro != nullptr ? static_cast<int>(micro[i]) : -1;
        const int mt = raw < 0 ? -1 : raw / s.rebin;
        if (mt < 0 || mt >= n_bins) continue;
        if (std::find(s.par.begin(), s.par.end(), ch) != s.par.end()) {
            s.background[static_cast<std::size_t>(mt)] += 1.0;
            ++off_burst;
        } else if (std::find(s.perp.begin(), s.perp.end(), ch) != s.perp.end()) {
            s.background[static_cast<std::size_t>(n_bins + mt)] += 1.0;
            ++off_burst;
        }
    }
    // Photons no burst contains, over the measurement's own length: the
    // detector's background rate, measured rather than assumed.
    if (duration_s > 0.0) s.bg_cps = double(off_burst) / duration_s;
    // A background of all zeros is what `fit23` gets when every photon landed in
    // a burst. Flat and tiny is the honest stand-in: it says "no background was
    // measured" without dividing by zero.
    double total = 0.0;
    for (double v : s.background) total += v;
    if (total <= 0.0) {
        s.background.assign(s.background.size(), 1.0);
        total = double(s.background.size());
    }
    // Normalised to unit area: only its *shape* is used, because the kernel
    // rescales it to each burst's own total, and a pattern of a million raw
    // counts against a burst of a hundred only costs precision.
    for (double& v : s.background) v /= total;
    return s;
}

/// Fit every burst of one detector. Returns one row per burst, NaN where the
/// fit did not converge or the burst holds too few photons to try.
std::vector<MleRow> fit_bursts(
        const MleSetup& s, const DetectorSetup& setup,
        const std::vector<std::pair<long long, long long>>& bursts,
        const signed char* rout, const unsigned short* micro,
        const std::vector<double>& duration_ms,
        double period_ns, double tau_init, int min_photons, bool* reported) {
    std::vector<MleRow> rows(bursts.size());

    // `setup` here is the fit's, not the instrument's: [dt, period, g, l1, l2,
    // convolution_stop, soft_bifl, objective].
    const std::vector<double> fit_setup = {
        s.dt_ns, period_ns, setup.g_factor, setup.l1, setup.l2,
        double(s.n_bins - 1), 0.0, 0.0,
    };
    DecayFit2 model("fit23", fit_setup, s.irf);

    // **Only tau is fitted.** A burst of a hundred photons does not determine an
    // anisotropy, and asking for one turns a well-conditioned lifetime fit into
    // an ill-conditioned four-parameter one that lands wherever the optimiser
    // was pushed — measured here as a lifetime moving by a factor of two on a
    // start-value change, with a 2I* that still looked fine. r0 = 0 and rho held
    // is also the precondition for the kernel's own well-conditioned path.
    //
    // gamma is not fitted either, but it is not guessed: it is *measured* per
    // burst below.
    DecayFitConstraints constraints(std::vector<int>{0, -1, -1, -1});

    for (std::size_t b = 0; b < bursts.size(); ++b) {
        // Two channels of n_bins each — parallel then perpendicular. Getting
        // this the wrong way round makes `require_valid` reject every burst, and
        // a caught exception per burst reads as "0 fitted" rather than as an
        // error, which is why the failure below is reported rather than
        // swallowed.
        DecayFitProblem problem(2, s.n_bins, s.dt_ns);
        problem.irf = s.irf;
        problem.background = s.background;

        std::int64_t n_par = 0, n_perp = 0;
        for (long long k = bursts[b].first; k <= bursts[b].second; ++k) {
            const int ch = rout != nullptr ? static_cast<int>(rout[k]) : -1;
            const int raw = micro != nullptr ? static_cast<int>(micro[k]) : -1;
            const int mt = raw < 0 ? -1 : raw / s.rebin;
            if (mt < 0 || mt >= s.n_bins) continue;
            if (std::find(s.par.begin(), s.par.end(), ch) != s.par.end()) {
                problem.data[static_cast<std::size_t>(mt)] += 1.0;
                ++n_par;
            } else if (std::find(s.perp.begin(), s.perp.end(), ch) != s.perp.end()) {
                problem.data[static_cast<std::size_t>(s.n_bins + mt)] += 1.0;
                ++n_perp;
            }
        }
        rows[b].n_par = n_par;
        rows[b].n_perp = n_perp;
        if (n_par + n_perp < min_photons) continue;   // stays NaN

        // The expected background fraction of *this* burst: a rate times a
        // duration, over what the detector actually saw. The kernel renormalises
        // the background pattern to the burst's total, so this is the whole of
        // the background's magnitude.
        double gamma = 0.0;
        const double n_det = double(n_par + n_perp);
        if (s.bg_cps > 0.0 && n_det > 0.0 && b < duration_ms.size()) {
            gamma = s.bg_cps * (duration_ms[b] * 1e-3) / n_det;
            if (!(gamma > 0.0)) gamma = 0.0;
            if (gamma > 0.98) gamma = 0.98;
        }

        try {
            const DecayFitOutcome out = model.fit(
                    {tau_init, gamma, 0.0, 1.0}, constraints, problem);
            // A fit that did not converge reports it, and a reported failure
            // must not reach the table as a number.
            if (out.results.size() > 1 && out.results[1] == 0.0) continue;
            rows[b].tau = out.parameters[0];
            rows[b].gamma = out.parameters[1];
            rows[b].r0 = out.parameters[2];
            rows[b].rho = out.parameters[3];
            rows[b].two_istar = out.objective;
            if (out.results.size() > 3) rows[b].r_scatter = out.results[3];
            if (out.results.size() > 4) rows[b].r_experimental = out.results[4];
        } catch (const std::exception& e) {
            // Stays NaN — but a *systematic* failure must not read as "no burst
            // had enough photons". The first reason is reported once; a fit that
            // fails on one burst is ordinary, a fit that fails on all of them is
            // a bug in the caller and was invisible until this printed.
            if (!*reported) {
                std::cerr << "warning: lifetime fit failed: " << e.what()
                          << std::endl;
                *reported = true;
            }
        }
    }
    return rows;
}

/// The detector a stream flag names, by explicit choice or by convention.
///
/// BVA and 2CDE need to know which detector is the donor and which the
/// acceptor, and that is not derivable from a channel number. `--donor`/
/// `--acceptor` say it; failing that the conventional names are tried, so the
/// usual green/red setup needs no extra flags and an unconventional one is
/// simply not given the companions rather than given wrong ones.
const DetectorDef* pick_stream(const DetectorSetup& setup,
                               const std::string& explicit_name,
                               const std::vector<std::string>& conventional) {
    if (!explicit_name.empty()) return setup.find_detector(explicit_name);
    for (const std::string& n : conventional) {
        for (const DetectorDef& d : setup.detectors) {
            std::string lower = d.name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (lower == n) return &d;
        }
    }
    return nullptr;
}

}  // namespace

int tttrlib::cli::cmd_sm(int argc, char** argv) {
    cxxopts::Options opts(
            "tttr sm",
            "Single-molecule / burst processing from a TTTR file");
    opts.add_options()
        ("file", "input TTTR file, or - for stdin", cxxopts::value<std::string>())
        ("config", "JSON config with search parameters and output paths",
         cxxopts::value<std::string>())
        ("pipeline", "run the pipeline document in this file (.json, or a .pto "
                     "that carries one): its burst_selection parameters replace "
                     "the search options",
         cxxopts::value<std::string>())
        ("write-pipeline", "write the pipeline document this run WOULD execute "
                           "to this file and exit, without reading the data",
         cxxopts::value<std::string>())
        ("method", "search method: sliding_window, cusum_sprt, maxtree",
         cxxopts::value<std::string>())
        ("min-photons", "minimum photons in a burst (L)", cxxopts::value<int>())
        ("rate-window", "photons used to compute the local rate (m)", cxxopts::value<int>())
        ("time-separation", "max time separation of m photons, seconds (T)",
         cxxopts::value<double>())
        ("channels", "restrict to routing channels, e.g. \"0,1\"",
         cxxopts::value<std::string>())
        ("setup", "chiSurf detector_setups.json; its detectors name the columns",
         cxxopts::value<std::string>())
        ("setup-name", "setup in --setup (default: file's last_used/first)",
         cxxopts::value<std::string>())
        ("detector", "detector in the setup; without it the whole setup is used",
         cxxopts::value<std::string>())
        ("donor", "detector to use as the donor stream for BVA / 2CDE",
         cxxopts::value<std::string>())
        ("acceptor", "detector to use as the acceptor stream for BVA / 2CDE",
         cxxopts::value<std::string>())
        ("no-companions", "skip the BVA and FRET-2CDE companion tables")
        ("mle", "fit one lifetime per burst per detector (fit23); needs --irf")
        ("irf", "instrument response for --mle: \"delta\","
         " \"gauss:FWHM_NS[,T0_NS]\","
         " \"sgauss:FWHM_NS[,T0_NS[,SKEW]]\" (skew-normal, default skew 1.5,"
         " positive tailing to later times), or a file of one number per line."
         " There is no default: a prompt is a claim about the instrument, and a"
         " lifetime fitted against the wrong one is wrong by about its width"
         " with nothing in the output saying so.",
         cxxopts::value<std::string>())
        ("tau-init", "starting lifetime for --mle, ns (default 2.0)",
         cxxopts::value<double>())
        ("mle-min-photons", "skip the fit below this many photons in the detector"
         " (default 20)", cxxopts::value<int>())
        ("mle-bins", "micro-time bins the fit runs on, by rebinning the TAC axis"
         " (default 128). A burst is ~100 photons over 4096 raw channels, so the"
         " raw axis is almost all zeros.", cxxopts::value<int>())
        // `-o` as well as `--output`: `tttr sim` has taken `-o` since it
        // existed, and a chain that writes `-o` for one command and `--output`
        // for the next is a papercut in the one place this pipeline is meant to
        // be typed by hand.
        ("o,output", "output JSON, or a container when the name ends in .pto"
         " (write it as <stem>.mmfdb.pto: the container is a .pto, the .mmfdb"
         " says it carries the PTO.MFDB profile). Default: <stem>.burst.json",
         cxxopts::value<std::string>())
        ("csv", "also write the burst table there as a delimited file",
         cxxopts::value<std::string>())
        ("progress", "write JSONL progress events for a client"
         " (\"-\" = stderr, \"stdout\" = stdout)",
         cxxopts::value<std::string>())
        ("h,help", "print usage");
    opts.parse_positional({"file"});

    try {
        auto r = opts.parse(argc, argv);
        if (r.count("help")) {
            std::cout << opts.help() << std::endl;
            return 0;
        }
        if (!r.count("file")) {
            std::cerr << "error: sm needs an input file\n" << std::endl;
            std::cerr << opts.help() << std::endl;
            return 1;
        }
        std::string file = r["file"].as<std::string>();

        // defaults
        std::string method = "sliding_window";
        int L = 20;
        int m = 10;
        double T = 5e-4;
        std::string channels;
        std::string setup_path;
        std::string setup_name;
        std::string detector_name;
        std::string donor_name;
        std::string acceptor_name;
        std::string output;
        std::string csv;
        bool companions = !r.count("no-companions");
        bool mle = r.count("mle") != 0;
        std::string irf_arg;
        double tau_init = 2.0;
        int mle_min_photons = 20;
        int mle_bins = 128;

        // A pipeline document (`tttrlib.Pipeline`, or the one a previous run
        // wrote into its .pto) IS the configuration: its `burst_selection`
        // step carries the parameters, so re-running an analysis is
        // `tttr sm data.spc --pipeline previous.pto`. Read before --config and
        // the flags, both of which still override it.
        if (r.count("pipeline")) {
            std::string err;
            const json doc = load_pipeline_document(r["pipeline"].as<std::string>(), &err);
            if (!err.empty()) {
                std::cerr << "error: " << err << std::endl;
                return 1;
            }
            for (const auto& step : doc.value("steps", json::array())) {
                if (step.value("operation", std::string()) == "photon_selection") {
                    const json params = step.value("params", json::object());
                    if (params.contains("channels")) {
                        channels.clear();
                        for (const auto& c : params["channels"]) {
                            if (!channels.empty()) channels += ",";
                            channels += std::to_string(c.get<int>());
                        }
                    }
                    continue;
                }
                if (step.value("operation", std::string()) != "burst_selection") continue;
                const json params = step.value("params", json::object());
                // The declared names (`algorithm`, `L`, `m`, `T`) are what a
                // document written from 2026-08-18 carries; the `.bur` settings
                // names are what the earlier ones did, and both still run.
                if (params.contains("algorithm")) method = params["algorithm"].get<std::string>();
                else if (params.contains("method")) method = params["method"].get<std::string>();
                if (params.contains("L")) L = params["L"].get<int>();
                else if (params.contains("min_photons")) L = params["min_photons"].get<int>();
                if (params.contains("m")) m = params["m"].get<int>();
                else if (params.contains("rate_window")) m = params["rate_window"].get<int>();
                if (params.contains("T")) T = params["T"].get<double>();
                else if (params.contains("time_separation"))
                    T = params["time_separation"].get<double>();
                if (params.contains("channels")) {
                    channels.clear();
                    for (const auto& c : params["channels"]) {
                        if (!channels.empty()) channels += ",";
                        channels += std::to_string(c.get<int>());
                    }
                }
                break;
            }
        }

        if (r.count("config")) {
            std::string err;
            json cfg = load_config(r["config"].as<std::string>(), &err);
            if (!err.empty()) {
                std::cerr << "error: " << err << std::endl;
                return 1;
            }
            if (cfg.contains("method")) method = cfg["method"].get<std::string>();
            if (cfg.contains("min_photons")) L = cfg["min_photons"].get<int>();
            if (cfg.contains("rate_window")) m = cfg["rate_window"].get<int>();
            if (cfg.contains("time_separation")) T = cfg["time_separation"].get<double>();
            if (cfg.contains("channels")) {
                for (auto& c : cfg["channels"]) {
                    if (!channels.empty()) channels += ",";
                    channels += std::to_string(c.get<int>());
                }
            }
            if (cfg.contains("detector_setup")) setup_path = cfg["detector_setup"].get<std::string>();
            if (cfg.contains("setup_name")) setup_name = cfg["setup_name"].get<std::string>();
            if (cfg.contains("detector")) detector_name = cfg["detector"].get<std::string>();
            if (cfg.contains("donor")) donor_name = cfg["donor"].get<std::string>();
            if (cfg.contains("acceptor")) acceptor_name = cfg["acceptor"].get<std::string>();
            if (cfg.contains("output")) output = cfg["output"].get<std::string>();
            if (cfg.contains("csv")) csv = cfg["csv"].get<std::string>();
            if (cfg.contains("mle")) mle = cfg["mle"].get<bool>();
            if (cfg.contains("irf")) irf_arg = cfg["irf"].get<std::string>();
            if (cfg.contains("tau_init")) tau_init = cfg["tau_init"].get<double>();
            if (cfg.contains("mle_min_photons"))
                mle_min_photons = cfg["mle_min_photons"].get<int>();
            if (cfg.contains("mle_bins")) mle_bins = cfg["mle_bins"].get<int>();
        }

        // inline flags override the config
        if (r.count("method")) method = r["method"].as<std::string>();
        if (r.count("min-photons")) L = r["min-photons"].as<int>();
        if (r.count("rate-window")) m = r["rate-window"].as<int>();
        if (r.count("time-separation")) T = r["time-separation"].as<double>();
        if (r.count("channels")) channels = r["channels"].as<std::string>();
        if (r.count("setup")) setup_path = r["setup"].as<std::string>();
        if (r.count("setup-name")) setup_name = r["setup-name"].as<std::string>();
        if (r.count("detector")) detector_name = r["detector"].as<std::string>();
        if (r.count("donor")) donor_name = r["donor"].as<std::string>();
        if (r.count("acceptor")) acceptor_name = r["acceptor"].as<std::string>();
        if (r.count("output")) output = r["output"].as<std::string>();
        if (r.count("csv")) csv = r["csv"].as<std::string>();
        if (r.count("irf")) irf_arg = r["irf"].as<std::string>();
        if (r.count("tau-init")) tau_init = r["tau-init"].as<double>();
        if (r.count("mle-min-photons")) mle_min_photons = r["mle-min-photons"].as<int>();
        if (r.count("mle-bins")) mle_bins = r["mle-bins"].as<int>();

        IrfSpec irf_spec;
        if (mle) {
            if (irf_arg.empty()) {
                std::cerr << "error: --mle needs --irf. There is no default "
                             "instrument response: a prompt is a claim about the "
                             "instrument, and a lifetime fitted against the wrong "
                             "one is wrong by about its width with nothing in the "
                             "output saying so. Use --irf delta if that is really "
                             "what you mean." << std::endl;
                return 1;
            }
            std::string err;
            if (!parse_irf(irf_arg, &irf_spec, &err)) {
                std::cerr << "error: " << err << std::endl;
                return 1;
            }
        }

        // Named after the input, which for stdin has no name of its own.
        if (output.empty()) output = strip_ext(file == "-" ? std::string("stdin") : file)
                                     + ".burst.json";

        tttrlib::cli::Progress progress;
        progress.set_job("sm");
        progress.open(r.count("progress") ? r["progress"].as<std::string>() : "");
        progress.set_total(3);  // load, search, write
        progress.begin();

        std::cout << "Loading: " << file << std::endl;
        // --write-pipeline: emit the document this invocation would run and
        // stop. Nothing is read, so a recipe can be written, reviewed, edited
        // and version-controlled before it ever touches a measurement -- and
        // handed to `mmfdb workflow run` or to `tttrlib.Pipeline`.
        if (r.count("write-pipeline")) {
            json settings = {{"method", method},
                             {"min_photons", L},
                             {"rate_window", m},
                             {"time_separation", T}};
            if (!channels.empty()) {
                json ch = json::array();
                for (int c : parse_channels(channels)) ch.push_back(c);
                settings["channels"] = ch;
            }
            const std::string target = r["write-pipeline"].as<std::string>();
            std::ofstream ofs(target);
            if (!ofs) {
                std::cerr << "error: cannot write " << target << std::endl;
                return 1;
            }
            ofs << pipeline_document(file, tttr_format_term(file), settings, {}).dump(2)
                << std::endl;
            std::cout << "Wrote pipeline " << target << std::endl;
            return 0;
        }

        // `-` is stdin, so `tttr sim ... | tttr sm - ...` needs no
        // intermediate file from the user. It is spooled, not streamed -- see
        // InputPath.
        InputPath input;
        {
            std::string err;
            if (!input.resolve(file, &err)) {
                std::cerr << "error: " << err << std::endl;
                return 1;
            }
        }
        std::shared_ptr<TTTR> data = std::make_shared<TTTR>(input.path().c_str());
        progress.tick();  // load

        // -- the instrument ---------------------------------------------------
        DetectorSetup setup;
        bool have_setup = false;
        std::vector<signed char> used;
        if (!setup_path.empty()) {
            std::string err;
            if (!resolve_setup(setup_path, setup_name, detector_name, &setup, &err)) {
                std::cerr << "error: " << err << std::endl;
                return 1;
            }
            have_setup = true;
            std::cout << "Detector setup: " << setup_path << "  (" << setup.name << ")"
                      << std::endl;
            std::cout << "Detectors:";
            for (const DetectorDef& d : setup.detectors) std::cout << " " << d.name;
            std::cout << std::endl;
        }

        // An explicit --channels still wins: it is the "I know what I want"
        // escape hatch, and it selects photons without naming detectors.
        if (!channels.empty()) {
            used = parse_channels(channels);
            if (used.empty()) {
                std::cerr << "error: invalid channel list" << std::endl;
                return 1;
            }
        } else if (have_setup) {
            const std::vector<int> ch = setup.all_channels();
            if (ch.empty()) {
                std::cerr << "error: no channels in detector setup" << std::endl;
                return 1;
            }
            used.assign(ch.begin(), ch.end());
        }
        if (!used.empty()) {
            std::cout << "Channels: ";
            for (auto c : used) std::cout << " " << (int) c;
            std::cout << std::endl;
            data = data->get_tttr_by_channel(
                    const_cast<signed char*>(used.data()), (int) used.size());
        }

        const double macro_res = data->get_header()->get_macro_time_resolution();
        const double micro_res_s = data->get_header()->get_micro_time_resolution();
        // ChiSurf writes the mean micro time in nanoseconds, and writes the
        // sentinel rather than a number in unknown units when the header does
        // not say what a channel is worth.
        const double micro_ns = micro_res_s > 0.0 ? micro_res_s * 1e9 : 0.0;

        std::cout << "Burst search: " << method << " L=" << L
                  << " m=" << m << " T=" << T << "s" << std::endl;

        progress.set_phase("burst search");
        std::vector<long long> sel =
                data->burst_search(L, m, T, method, 0.05, 0.05);
        progress.tick();  // search

        const std::size_t n_ph = data->size();
        unsigned long long* macro_ptr = nullptr;
        unsigned short* micro_ptr = nullptr;
        signed char* rout_ptr = nullptr;
        int n_macro = 0, n_micro = 0, n_rout = 0;
        data->get_macro_times(&macro_ptr, &n_macro);
        data->get_micro_times(&micro_ptr, &n_micro);
        data->get_routing_channel(&rout_ptr, &n_rout);

        // Bursts a table cannot describe are dropped, not repaired: same rule
        // as ChiSurf, so the two agree on how many rows there are.
        std::vector<std::pair<long long, long long>> bursts;
        std::vector<std::size_t> burst_index;  // position in `sel`, for confidence
        for (std::size_t i = 0; i + 1 < sel.size(); i += 2) {
            const long long start = sel[i];
            const long long stop = sel[i + 1];
            if (stop <= start || stop >= (long long) n_ph || start < 0) continue;
            bursts.emplace_back(start, stop);
            burst_index.push_back(i / 2);
        }
        const std::size_t n_bursts = bursts.size();

        // How strongly the data supports each burst. Indexed by position in the
        // search's own output, so a dropped burst does not shift the rest.
        std::vector<double> confidence;
        try {
            confidence = data->burst_confidence(sel);
        } catch (const std::exception&) {
            confidence.clear();
        }

        // -- the table --------------------------------------------------------
        std::vector<std::int64_t> v_first(n_bursts), v_last(n_bursts), v_n(n_bursts);
        std::vector<double> v_dur(n_bursts), v_meanm(n_bursts), v_rate(n_bursts);
        std::vector<double> v_conf(n_bursts, 0.0);

        std::vector<DetectorMask> masks;
        if (have_setup)
            masks = detector_masks(setup, rout_ptr, micro_ptr, n_ph);

        const std::size_t n_det = masks.size();
        std::vector<std::vector<std::int64_t>> d_first(n_det, std::vector<std::int64_t>(n_bursts, -1));
        std::vector<std::vector<std::int64_t>> d_last(n_det, std::vector<std::int64_t>(n_bursts, -1));
        std::vector<std::vector<double>> d_dur(n_det, std::vector<double>(n_bursts, kSentinel));
        std::vector<std::vector<double>> d_meanm(n_det, std::vector<double>(n_bursts, kSentinel));
        std::vector<std::vector<std::int64_t>> d_n(n_det, std::vector<std::int64_t>(n_bursts, 0));
        std::vector<std::vector<double>> d_rate(n_det, std::vector<double>(n_bursts, kSentinel));
        std::vector<std::vector<double>> d_micro(n_det, std::vector<double>(n_bursts, kSentinel));

        // window x detector count rates, in the order ChiSurf emits them
        std::vector<std::string> win_names;
        std::vector<std::pair<int, int>> win_ranges;
        std::vector<std::vector<unsigned char>> win_masks;
        if (have_setup) {
            for (const auto& w : setup.windows) {
                win_names.push_back(w.name);
                win_ranges.emplace_back(w.lo, w.hi);
                win_masks.push_back(window_mask(micro_ptr, n_ph, w.lo, w.hi));
            }
        }
        std::vector<std::vector<double>> wd_rate(
                win_names.size() * n_det, std::vector<double>(n_bursts, kSentinel));

        for (std::size_t b = 0; b < n_bursts; ++b) {
            const long long start = bursts[b].first;
            const long long stop = bursts[b].second;

            const double t0 = macro_ptr != nullptr ? (double) macro_ptr[start] : 0.0;
            const double t1 = macro_ptr != nullptr ? (double) macro_ptr[stop] : 0.0;
            const double dur = (t1 - t0) * macro_res * 1e3;
            const std::int64_t npix = stop - start + 1;

            v_first[b] = start;
            v_last[b] = stop;
            v_dur[b] = dur;
            v_meanm[b] = ((t1 + t0) / 2.0) * macro_res * 1e3;
            v_n[b] = npix;
            // `dur` is milliseconds, so photons-per-`dur` is already kHz. Do
            // not scale again.
            v_rate[b] = dur > 0.0 ? (double) npix / dur : kNaN;
            if (burst_index[b] < confidence.size())
                v_conf[b] = confidence[burst_index[b]];

            for (std::size_t d = 0; d < n_det; ++d) {
                const unsigned char* in = masks[d].in.data();
                long long i0 = -1, i1 = -1, cnt = 0;
                double micro_sum = 0.0;
                for (long long k = start; k <= stop; ++k) {
                    if (in[k] == 0) continue;
                    if (i0 < 0) i0 = k;
                    i1 = k;
                    ++cnt;
                    if (micro_ptr != nullptr) micro_sum += (double) micro_ptr[k];
                }
                if (cnt == 0) continue;  // sentinels already in place

                const double m0 = macro_ptr != nullptr ? (double) macro_ptr[i0] : 0.0;
                const double m1 = macro_ptr != nullptr ? (double) macro_ptr[i1] : 0.0;
                const double d_ms = (m1 - m0) * macro_res * 1e3;
                d_first[d][b] = i0;
                d_last[d][b] = i1;
                d_dur[d][b] = d_ms;
                d_meanm[d][b] = ((m1 + m0) / 2.0) * macro_res * 1e3;
                d_n[d][b] = cnt;
                d_rate[d][b] = d_ms > 0.0 ? (double) cnt / d_ms : kNaN;
                if (micro_ns > 0.0)
                    d_micro[d][b] = (micro_sum / (double) cnt) * micro_ns;
            }

            for (std::size_t w = 0; w < win_names.size(); ++w) {
                const unsigned char* wm = win_masks[w].data();
                for (std::size_t d = 0; d < n_det; ++d) {
                    const unsigned char* in = masks[d].in.data();
                    long long i0 = -1, i1 = -1, cnt = 0;
                    for (long long k = start; k <= stop; ++k) {
                        if (in[k] == 0 || wm[k] == 0) continue;
                        if (i0 < 0) i0 = k;
                        i1 = k;
                        ++cnt;
                    }
                    if (cnt == 0) continue;
                    const double m0 = macro_ptr != nullptr ? (double) macro_ptr[i0] : 0.0;
                    const double m1 = macro_ptr != nullptr ? (double) macro_ptr[i1] : 0.0;
                    const double d_ms = (m1 - m0) * macro_res * 1e3;
                    wd_rate[w * n_det + d][b] = d_ms > 0.0 ? (double) cnt / d_ms : kNaN;
                }
            }
        }

        DataStore store("bursts");
        store.set_n_rows(n_bursts);
        add_i64(store, "First Photon", v_first);
        add_i64(store, "Last Photon", v_last);
        add_f64(store, "Duration (ms)", v_dur);
        add_f64(store, "Mean Macro Time (ms)", v_meanm);
        add_i64(store, "Number of Photons", v_n);
        add_f64(store, "Count Rate (KHz)", v_rate);
        add_f64(store, "Confidence (sigma)", v_conf);
        // `input.display_name()` rather than the raw argument: a piped input is
        // `-` on the command line, and `-` is not a name a reader can resolve
        // later -- neither in the "First File" column nor as the stem of the
        // container's objects.
        const std::string base = file_stem(input.display_name());
        const std::string leaf = file_name(input.display_name());
        add_str(store, "First File", leaf, n_bursts);
        add_str(store, "Last File", leaf, n_bursts);
        for (std::size_t d = 0; d < n_det; ++d) {
            const std::string& nm = masks[d].name;
            add_i64(store, "First Photon (" + nm + ")", d_first[d]);
            add_i64(store, "Last Photon (" + nm + ")", d_last[d]);
            add_f64(store, "Duration (" + nm + ") (ms)", d_dur[d]);
            add_f64(store, "Mean Macrotime (" + nm + ") (ms)", d_meanm[d]);
            add_i64(store, "Number of Photons (" + nm + ")", d_n[d]);
            add_f64(store, capitalize(nm) + " Count Rate (KHz)", d_rate[d]);
        }
        for (std::size_t w = 0; w < win_names.size(); ++w) {
            for (std::size_t d = 0; d < n_det; ++d) {
                add_f64(store,
                        "S " + win_names[w] + " " + masks[d].name + " (kHz) | " +
                                std::to_string(win_ranges[w].first) + "-" +
                                std::to_string(win_ranges[w].second),
                        wd_rate[w * n_det + d]);
            }
        }
        // Appended last, as in the .bur layout.
        for (std::size_t d = 0; d < n_det; ++d)
            add_f64(store, "Mean Microtime (" + masks[d].name + ") (ns)", d_micro[d]);

        json search_settings = {
            {"method", method},
            {"min_photons", L},
            {"rate_window", m},
            {"time_separation", T},
        };
        if (!used.empty()) {
            json ch = json::array();
            for (auto c : used) ch.push_back((int) c);
            search_settings["channels"] = ch;
        }
        if (have_setup) search_settings["setup"] = setup.name;

        // -- companions, computed rather than asserted -------------------------
        //
        // A donor and an acceptor are what BVA and FRET-2CDE are defined over,
        // and neither is derivable from a channel number. Without them the
        // tables are not written at all — a column of plausible constants is
        // worse than a missing column, because nothing downstream can tell it
        // from a measurement.
        const DetectorDef* donor = nullptr;
        const DetectorDef* acceptor = nullptr;
        if (have_setup && companions) {
            donor = pick_stream(setup, donor_name, {"green", "donor", "dd", "green_par"});
            acceptor = pick_stream(setup, acceptor_name, {"red", "acceptor", "da", "red_par"});
            if ((!donor_name.empty() && donor == nullptr) ||
                (!acceptor_name.empty() && acceptor == nullptr)) {
                std::cerr << "error: --donor/--acceptor names no detector in setup '"
                          << setup.name << "'" << std::endl;
                return 1;
            }
        }

        // -- per-burst lifetimes -----------------------------------------------
        std::vector<std::vector<MleRow>> mle_rows;   // one vector per detector
        if (mle && n_bursts > 0) {
            if (!have_setup) {
                std::cerr << "error: --mle needs --setup: a lifetime is fitted "
                             "per named detector, over its parallel and "
                             "perpendicular arms." << std::endl;
                return 1;
            }
            progress.set_phase("mle");
            const int n_micro_bins = data->get_number_of_micro_time_channels();
            // Which photons a burst holds, so the rest can measure the background.
            std::vector<unsigned char> in_burst(n_ph, 0);
            for (const auto& b : bursts)
                for (long long k = b.first; k <= b.second; ++k)
                    in_burst[static_cast<std::size_t>(k)] = 1;

            const double period_ns =
                    micro_ns > 0.0 ? micro_ns * n_micro_bins : 0.0;
            // The measurement's own length, for the background rate.
            const double measurement_s =
                    (macro_ptr != nullptr && n_ph > 0)
                            ? double(macro_ptr[n_ph - 1] - macro_ptr[0]) * macro_res
                            : 0.0;
            for (const DetectorDef& d : setup.detectors) {
                std::string err;
                MleSetup ms = build_mle_setup(d, irf_spec, rout_ptr, micro_ptr,
                                              n_ph, in_burst, n_micro_bins,
                                              micro_ns, mle_bins, measurement_s,
                                              &err);
                if (!err.empty()) {
                    std::cerr << "error: " << err << std::endl;
                    return 1;
                }
                bool reported = false;
                mle_rows.push_back(fit_bursts(ms, setup, bursts, rout_ptr,
                                              micro_ptr, v_dur, period_ns,
                                              tau_init, mle_min_photons,
                                              &reported));
                std::size_t ok = 0;
                for (const MleRow& row : mle_rows.back())
                    if (std::isfinite(row.tau)) ++ok;
                std::cout << "MLE " << d.name << ": " << ok << "/" << n_bursts
                          << " bursts fitted" << std::endl;
            }
        }

        std::vector<double> bva_mean, bva_std, fret_2cde;
        if (donor != nullptr && acceptor != nullptr && n_bursts > 0) {
            std::vector<long long> inclusive;
            inclusive.reserve(n_bursts * 2);
            for (const auto& b : bursts) {
                inclusive.push_back(b.first);
                inclusive.push_back(b.second);
            }
            progress.set_phase("companions");
            try {
                tttrlib::BVA bva(data);
                bva.set_donor(donor->channels, donor->micro_time_ranges);
                bva.set_acceptor(acceptor->channels, acceptor->micro_time_ranges);
                bva.compute(inclusive.data(), (int) n_bursts, 2, 5);
                bva_mean = bva.get_proximity_ratio_mean();
                bva_std = bva.get_proximity_ratio_std();
            } catch (const std::exception& e) {
                std::cerr << "warning: BVA skipped: " << e.what() << std::endl;
            }
            try {
                tttrlib::TwoCDE cde(data);
                cde.set_donor(donor->channels, donor->micro_time_ranges);
                cde.set_acceptor(acceptor->channels, acceptor->micro_time_ranges);
                cde.compute(inclusive.data(), (int) n_bursts, 2, 1e-4,
                            tttrlib::TwoCDE::FRET_2CDE);
                fret_2cde = cde.get_result();
            } catch (const std::exception& e) {
                std::cerr << "warning: 2CDE skipped: " << e.what() << std::endl;
            }
        }

        // -- write -------------------------------------------------------------
        progress.set_phase("write");
        const bool to_pto =
                output.size() >= 4 && output.substr(output.size() - 4) == ".pto";

        if (to_pto) {
            // The steps this run actually performed, for the pipeline
            // document written at the end.
            std::vector<json> pipeline_steps;

            PtoFile pto;
            const bool reopened = tttr::io::is_pto_file(output);
            if (reopened) {
                if (!pto.open(output, true)) {
                    std::cerr << "error: cannot open PTO file for writing: "
                              << pto.error() << std::endl;
                    return 1;
                }
            } else if (!pto.create(output, "Burst Selection Artifact")) {
                std::cerr << "error: cannot create PTO file: " << pto.error()
                          << std::endl;
                return 1;
            }

            tag_text(pto, 0, "_mmfdb_container.profile", "PTO.MFDB");
            tag_text(pto, 0, "_mmfdb_container.profile_version", "1.1");
            tag_text(pto, 0, "_mmfdb_container.profile_read_version", "1");

            // The instrument file goes in **verbatim**, so a container answers
            // for its own inputs and comes back byte-for-byte. Not `data`: by
            // this point that is the channel-filtered stream, and writing it as
            // a `.sm` would put a lossy re-encoding of the measurement in the
            // one place the profile promises is not one — the other channels
            // gone, the vendor header gone, and nothing in the file saying so.
            // An existing one is reused rather than added a second time — and
            // the match is on the **checksum**, not the name. A container this
            // did not create already holds its primary; adding a second copy
            // would give the graph two roots and leave this table hanging off
            // the one nothing else references, which reads as intact until
            // somebody walks the lineage of an artifact written by the other
            // tool. Falling back to the name covers a container whose primary
            // predates the checksum tag.
            // Of the bytes actually read: for a piped input that is the spool
            // file, and `-` is not openable at all.
            const std::string sum = tttrlib::util::sha256_file_hex(input.path());
            std::uint64_t stream_uid = 0;
            for (const auto& obj : pto.objects()) {
                if (obj.kind != "tttr_photon_stream") continue;
                const std::string other =
                        tag_of(pto, obj.uid, "_mmfdb_artifact.checksum");
                if ((!sum.empty() && other == sum) ||
                    (other.empty() && obj.name == leaf)) {
                    stream_uid = obj.uid;
                    break;
                }
            }
            if (stream_uid == 0) {
                const std::string enc = tttr_format_term(file);
                stream_uid = pto.add_file("tttr_photon_stream", enc, leaf, file);
                if (stream_uid != 0) {
                    tag_text(pto, stream_uid, "_mmfdb_artifact.data_format", enc);
                    tag_text(pto, stream_uid, "_mmfdb_artifact.file_path", file);
                    if (!sum.empty()) {
                        tag_text(pto, stream_uid, "_mmfdb_artifact.checksum", sum);
                        tag_text(pto, stream_uid,
                                 "_mmfdb_artifact.checksum_algorithm", "sha256");
                    }
                }
            }

            std::vector<std::uint64_t> from;
            if (stream_uid != 0) from.push_back(stream_uid);
            const std::uint64_t table_uid =
                    put_table(pto, "bi4_bur/" + base + ".bur", store,
                              "burst_selection", search_settings, "burst", from,
                              "derived_from", method);
            if (table_uid == 0) {
                std::cerr << "error: cannot write burst table: " << pto.error()
                          << std::endl;
                return 1;
            }

            // A companion's run identity has to include the burst list it was
            // computed over. Its rows are positional against that list and mean
            // nothing against another, so a companion keyed on its own settings
            // alone gets overwritten by the next search — silently, because the
            // row count still matches for as long as the two searches happen to
            // find the same number of bursts.
            const std::string parent_run =
                    tag_of(pto, table_uid, "_mmfdb_operation.settings_hash");

            // One table per detector, named the way ChiSurf names its `.bg4` /
            // `.br4`: `mle_<detector>`, and the columns in its historical order.
            for (std::size_t d = 0; d < mle_rows.size(); ++d) {
                const std::string nm = setup.detectors[d].name;
                const std::vector<MleRow>& rows = mle_rows[d];
                std::vector<std::int64_t> v_np(n_bursts), v_ns(n_bursts), v_nfit(n_bursts);
                std::vector<double> v_2i(n_bursts), v_tau(n_bursts), v_gamma(n_bursts);
                std::vector<double> v_r0(n_bursts), v_rho(n_bursts);
                std::vector<double> v_rsc(n_bursts), v_rex(n_bursts);
                std::vector<std::int64_t> v_bifl(n_bursts, 1), v_p2s(n_bursts, 0);
                for (std::size_t i = 0; i < n_bursts; ++i) {
                    v_np[i] = rows[i].n_par;
                    v_ns[i] = rows[i].n_perp;
                    v_nfit[i] = rows[i].n_par + rows[i].n_perp;
                    v_2i[i] = rows[i].two_istar;
                    v_tau[i] = rows[i].tau;
                    v_gamma[i] = rows[i].gamma;
                    v_r0[i] = rows[i].r0;
                    v_rho[i] = rows[i].rho;
                    v_rsc[i] = rows[i].r_scatter;
                    v_rex[i] = rows[i].r_experimental;
                }
                DataStore mle_store("mle_" + nm);
                mle_store.set_n_rows(n_bursts);
                add_i64(mle_store, "Ng-p-all", v_np);
                add_i64(mle_store, "Ng-s-all", v_ns);
                add_i64(mle_store, "Number of Photons (fit window) (" + nm + ")", v_nfit);
                // Two spaces after the star: historical, part of the `.b?4`
                // format, preserved exactly rather than tidied.
                add_f64(mle_store, "2I*  (" + nm + ")", v_2i);
                add_f64(mle_store, "Tau (" + nm + ")", v_tau);
                add_f64(mle_store, "gamma (" + nm + ")", v_gamma);
                add_f64(mle_store, "r0 (" + nm + ")", v_r0);
                add_f64(mle_store, "rho (" + nm + ")", v_rho);
                add_i64(mle_store, "BIFL scatter? (" + nm + ")", v_bifl);
                add_i64(mle_store, "2I*: P+2S? (" + nm + ")", v_p2s);
                add_f64(mle_store, "r Scatter (" + nm + ")", v_rsc);
                add_f64(mle_store, "r Experimental (" + nm + ")", v_rex);

                json s = {{"model", "fit23"},
                          {"detector", nm},
                          {"irf", irf_spec.text},
                          {"tau_init_ns", tau_init},
                          {"min_photons", mle_min_photons},
                          {"fit_bins", mle_bins},
                          {"g_factor", setup.g_factor},
                          {"l1", setup.l1},
                          {"l2", setup.l2},
                          {"fixed", {"gamma", "r0"}},
                          {"bursts", parent_run}};
                // Named after the detector, not after the legacy `bg4`/`br4`
                // extensions: those spell a *colour*, so a four-arm setup would
                // put `green_par` and `green_perp` in the same place.
                put_table(pto, "mle/" + base + "." + nm, mle_store,
                          "burst_lifetime_fitting", s, "burst",
                          {table_uid}, "derived_from", "mle");
                pipeline_steps.push_back(pipeline_step(
                        "mle_" + nm, "mle_green", "burst_lifetime_fitting", s,
                        "burst_selection"));
            }

            if (!bva_std.empty() && bva_std.size() == n_bursts) {
                DataStore bva_store("bva");
                bva_store.set_n_rows(n_bursts);
                add_f64(bva_store, "Proximity Ratio Mean", bva_mean);
                add_f64(bva_store, "Proximity Ratio Std", bva_std);
                json s = {{"donor", donor->name},
                          {"acceptor", acceptor->name},
                          {"photons_per_slice", 5},
                          {"bursts", parent_run}};
                put_table(pto, "bv4/" + base + ".bv4", bva_store,
                          "burst_variance_analysis", s, "burst",
                          {table_uid}, "derived_from", "variance");
                pipeline_steps.push_back(pipeline_step(
                        "bva", "bva", "burst_variance_analysis", s, "burst_selection"));
            }
            if (!fret_2cde.empty() && fret_2cde.size() == n_bursts) {
                DataStore cde_store("kde_cde");
                cde_store.set_n_rows(n_bursts);
                add_f64(cde_store, "FRET 2CDE", fret_2cde);
                json s = {{"donor", donor->name},
                          {"acceptor", acceptor->name},
                          {"tau_s", 1e-4},
                          {"variant", "fret_2cde"},
                          {"bursts", parent_run}};
                put_table(pto, "2c4/" + base + ".2c4", cde_store, "burst_2cde", s,
                          "burst", {table_uid}, "derived_from", "kernel_density");
                pipeline_steps.push_back(pipeline_step(
                        "kde_cde", "kde_cde", "burst_2cde", s, "burst_selection"));
            }

            // The run as a replayable pipeline document. `tttrlib.Pipeline`
            // and mmfdb read the same schema, so the container answers "how
            // was this made?" with something that can be run again rather
            // than only with per-table settings.
            {
                const json document = pipeline_document(
                        file, tttr_format_term(file), search_settings, pipeline_steps);
                tag_text(pto, 0, "_mmfdb_workflow.definition", document.dump());
                tag_text(pto, 0, "_mmfdb_workflow.name", "tttr-sm");
                tag_text(pto, 0, "_mmfdb_workflow.version", "1");
            }

            if (!pto.commit()) {
                std::cerr << "error: failed to commit PTO file: " << pto.error()
                          << std::endl;
                return 1;
            }
            pto.close();
            std::cout << "Wrote PTO container " << output << " (" << n_bursts
                      << " bursts, " << store.n_columns() << " columns)" << std::endl;
        } else {
            json out;
            out["file"] = file;
            out["n_events"] = data->get_n_valid_events();
            out["macro_time_resolution_s"] = macro_res;
            out["parameters"] = search_settings;
            out["parameters"]["detector_setup"] =
                    setup_path.empty() ? json(nullptr) : json(setup_path);
            out["parameters"]["detector"] =
                    detector_name.empty() ? json(nullptr) : json(detector_name);
            json arr = json::array();
            for (std::size_t b = 0; b < n_bursts; ++b) {
                arr.push_back({
                    {"start", v_first[b]},
                    {"stop", v_last[b]},
                    {"n_photons", v_n[b]},
                    {"duration_s", v_dur[b] / 1e3},
                    {"rate_khz", v_rate[b]},
                    {"confidence_sigma", v_conf[b]},
                });
            }
            out["n_bursts"] = (long long) n_bursts;
            out["bursts"] = arr;

            std::ofstream ofs(output);
            if (!ofs) {
                std::cerr << "error: cannot write " << output << std::endl;
                return 1;
            }
            ofs << out.dump(2) << std::endl;
            std::cout << "Wrote " << output << " (" << n_bursts << " bursts)"
                      << std::endl;
        }
        progress.tick();  // write
        progress.finish();

        if (!csv.empty()) {
            tttrlib::io::CsvWriteOptions o;
            o.delimiter = '\t';
            if (!tttrlib::io::write_csv(csv, store, o)) {
                std::cerr << "warning: cannot write " << csv << std::endl;
            } else {
                std::cout << "Wrote " << csv << std::endl;
            }
        }
        return 0;
    } catch (const cxxopts::exceptions::exception& e) {
        std::cerr << "error: " << e.what() << "\n" << std::endl;
        std::cerr << opts.help() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << std::endl;
        return 1;
    }
}

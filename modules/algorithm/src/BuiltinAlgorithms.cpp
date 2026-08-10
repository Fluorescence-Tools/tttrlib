// SPDX-License-Identifier: BSD-3-Clause
#include "AlgorithmRegistry.h"

#include <mutex>

/// PRD-027 Part 1/3 — the built-in algorithm families that had no registry
/// entry at all.
///
/// Burst searches and decay fits were already listed, because someone wrote a
/// JSON literal for each by hand. FCS, HMM and PDA were not, and the
/// consequence was not cosmetic: a UI could not offer them, the `.pto`
/// provenance system had no schema to validate an `operation_type` against or
/// to replay from, and a plugin had no name to register a competing
/// implementation under.
///
/// These declarations live next to the registry rather than inside each
/// algorithm module on purpose, for now: moving a declaration into its module
/// is a one-line change once that module is migrated onto `register_algorithm`
/// (PRD-032), and doing it here first means the families become visible without
/// touching five modules another agent may be editing.

namespace tttrlib {

namespace {

/// Citations transcribed from the reference lists the sources already carry
/// (`Correlator.h`, `HMM.h`). Where a source carried none -- PDA -- the entry
/// is marked so a maintainer can confirm it rather than inherit it silently.
const char* kFcsReferences = R"JSON([
  {"type": "journal",
   "authors": "Wahl, M., Gregor, I., Patting, M., Enderlein, J.",
   "title": "Fast calculation of fluorescence correlation data with asynchronous time-correlated single-photon counting",
   "journal": "Optics Express", "year": 2003, "volume": "11", "pages": "3383"},
  {"type": "journal",
   "authors": "Felekyan, S., Kuehnemuth, R., Kudryavtsev, V., Sandhagen, C., Becker, W., Seidel, C. A. M.",
   "title": "Full correlation from picoseconds to seconds by time-resolved and time-correlated single photon detection",
   "journal": "Review of Scientific Instruments", "year": 2005, "volume": "76", "pages": "083104"},
  {"type": "journal",
   "authors": "Laurence, T. A., Fore, S., Huser, T.",
   "title": "Fast, flexible algorithm for calculating photon correlations",
   "journal": "Optics Letters", "year": 2006, "volume": "31", "pages": "829-831"}
])JSON";

const char* kHmmReferences = R"JSON([
  {"type": "journal",
   "authors": "Pirchi, M., Tsukanov, R., Khamis, R., Tomov, T. E., Berger, Y., Khara, D. C., Volkov, H., Haran, G., Nir, E.",
   "title": "Photon-by-photon hidden Markov model analysis for microsecond single-molecule FRET kinetics",
   "journal": "The Journal of Physical Chemistry B", "year": 2016, "volume": "120", "pages": "13065"}
])JSON";

/// NOTE: PDA carries no reference in its own sources. This is the founding
/// paper of the method and should be confirmed against the implementation the
/// module actually follows before it is treated as authoritative.
const char* kPdaReferences = R"JSON([
  {"type": "journal",
   "authors": "Antonik, M., Felekyan, S., Gaiduk, A., Seidel, C. A. M.",
   "title": "Separating structural heterogeneities from stochastic variations in fluorescence resonance energy transfer distributions via photon distribution analysis",
   "journal": "The Journal of Physical Chemistry B", "year": 2006, "volume": "110",
   "note": "transcribed from the literature, not from the source; confirm before citing"}
])JSON";

void register_fcs() {
    AlgorithmDescriptor d;
    d.operation_type = "fcs_correlation";
    d.display_name = "Fluorescence correlation (multi-tau)";
    d.capability = "fcs";
    d.summary = "Multi-tau auto- or cross-correlation of a photon stream, on a "
                "logarithmically coarsening lag axis.";
    d.description =
        "Correlates one or two photon streams on the multi-tau lag axis: the lag "
        "spacing doubles every n_bins channels, so a curve spanning nanoseconds to "
        "seconds costs a few hundred points rather than a few billion.\n\n"
        "Three kernels compute the same curve and differ in how they get there. "
        "'wahl' coarsens the photon times themselves once per cascade and is the "
        "default. 'felekyan' is the time-resolved/time-correlated formulation, and "
        "is what filtered-FCS builds on. 'laurence' walks the two streams with a "
        "per-lag pointer and is fastest when the streams are very sparse.\n\n"
        "Correlating micro times as well as macro times ('make_fine') extends the "
        "curve below the macro-time resolution, at the cost of correlating a much "
        "longer effective time axis.\n\n"
        "Assumes a stationary signal over the correlated interval: the "
        "normalisation divides by the mean count rate of the whole record, so a "
        "photobleaching trend or a drifting focus shows up as an upturn at long "
        "lag rather than as an error.";
    d.references_json = kFcsReferences;
    d.row_grain = "curve_point";
    d.settings_schema = R"JSON({
      "type": "object",
      "properties": {
        "method": {"type": "string", "enum": ["wahl", "felekyan", "laurence"], "default": "wahl"},
        "n_casc": {"type": "integer", "default": 25, "minimum": 1,
                   "description": "Number of cascades; the lag axis doubles its spacing once per cascade."},
        "n_bins": {"type": "integer", "default": 17, "minimum": 2,
                   "description": "Linear channels per cascade."},
        "make_fine": {"type": "boolean", "default": false,
                      "description": "Correlate micro times as well, extending the curve below the macro-time resolution."}
      }
    })JSON";
    d.inputs_json = R"JSON({
      "required": ["tttr_photon_stream"],
      "optional": ["tttr_photon_stream_2", "weights"],
      "description": "One stream for an autocorrelation, two for a cross-correlation."
    })JSON";
    d.outputs_json = R"JSON({
      "columns": ["Lag time (s)", "G(tau)", "G(tau) normalized"]
    })JSON";
    d.can_replay = true;
    register_algorithm(d);
}

void register_hmm() {
    AlgorithmDescriptor d;
    d.operation_type = "photon_hmm";
    d.display_name = "Photon-by-photon hidden Markov model (H2MM)";
    d.capability = "hmm";
    d.summary = "Maximum-likelihood hidden Markov inference on individual photon "
                "arrivals, resolving kinetics faster than the burst duration.";
    d.description =
        "Fits a hidden Markov model whose observations are individual photons "
        "rather than binned intensities, so the accessible kinetics are limited by "
        "the photon rate rather than by a bin width. State assignment is available "
        "as a Viterbi path (one state per photon) or as a posterior distribution.\n\n"
        "Because the likelihood is over inter-photon times, the propagator "
        "A^(delta t) is needed for every observed gap; it is cached per unique gap, "
        "which is what makes the fit tractable on millions of photons.\n\n"
        "Model selection is the part that goes wrong quietly: the likelihood "
        "always improves with more states, so the state count has to be chosen "
        "with a criterion (BIC and its variants) rather than by fitting until the "
        "residual looks good.";
    d.references_json = kHmmReferences;
    d.row_grain = "photon";
    d.settings_schema = R"JSON({
      "type": "object",
      "properties": {
        "n_states": {"type": "integer", "default": 2, "minimum": 1},
        "max_iter": {"type": "integer", "default": 500, "minimum": 1},
        "tolerance": {"type": "number", "default": 1e-8, "minimum": 0.0},
        "decoder": {"type": "string", "enum": ["viterbi", "jitter", "ffbs"], "default": "viterbi"}
      }
    })JSON";
    d.inputs_json = R"JSON({
      "required": ["tttr_photon_stream"],
      "optional": ["burst_table"],
      "description": "A photon stream, optionally partitioned into bursts."
    })JSON";
    d.outputs_json = R"JSON({
      "columns": ["State", "Log likelihood", "Transition rate matrix", "Emission probabilities"]
    })JSON";
    d.can_replay = true;
    register_algorithm(d);
}

void register_pda() {
    {
        AlgorithmDescriptor d;
        d.operation_type = "pda_histogram_computation";
        d.display_name = "Photon distribution analysis (histogram)";
        d.capability = "pda";
        d.summary = "Predicts the photon-count distribution of a FRET mixture, so a "
                    "measured histogram can be fitted rather than merely described.";
        d.description =
            "Computes the distribution of photon counts across two channels expected "
            "from a given mixture of species, given the burst-size distribution of the "
            "measurement. Comparing that prediction with the measured histogram "
            "separates the width that is shot noise -- which is fixed by the number of "
            "photons and carries no information -- from the width that is genuine "
            "structural or dynamic heterogeneity.\n\n"
            "This is what makes a broad FRET peak interpretable: a single species "
            "observed with few photons produces a broad peak too, and only the "
            "predicted shot-noise width tells the two apart.\n\n"
            "Assumes the species do not interconvert within a burst. Exchange faster "
            "than the burst duration averages the species out and is a dynamic-PDA "
            "problem, not this one.";
        d.references_json = kPdaReferences;
        d.row_grain = "histogram_bin";
        d.settings_schema = R"JSON({
          "type": "object",
          "properties": {
            "n_bins": {"type": "integer", "default": 81, "minimum": 2},
            "x_min": {"type": "number", "default": 0.0},
            "x_max": {"type": "number", "default": 1.0},
            "log_x": {"type": "boolean", "default": false},
            "n_min": {"type": "integer", "default": 20, "minimum": 1,
                      "description": "Smallest burst size entering the histogram."},
            "skip_zero_photon": {"type": "boolean", "default": true},
            "background_ch1": {"type": "number", "default": 0.0, "unit": "kHz"},
            "background_ch2": {"type": "number", "default": 0.0, "unit": "kHz"}
          }
        })JSON";
        d.inputs_json = R"JSON({
          "required": ["burst_table"],
          "optional": ["probability_spectrum"],
          "description": "Burst-wise photon counts per channel, plus the species amplitudes and distances to predict from."
        })JSON";
        d.outputs_json = R"JSON({
          "columns": ["Bin", "Model probability", "Experimental probability"]
        })JSON";
        d.can_replay = true;
        register_algorithm(d);
    }
    {
        AlgorithmDescriptor d;
        d.operation_type = "pda_burst_likelihood";
        d.display_name = "PDA burst likelihood (K-channel)";
        d.capability = "pda";
        d.summary = "Burst-wise photon-partition maximum likelihood over K channels — "
                    "the likelihood a multi-colour PDA fit maximises.";
        d.description =
            "Evaluates the likelihood of the observed photon partition of each burst "
            "under a model of per-channel probabilities, for an arbitrary number of "
            "channels. Where the histogram path compares binned distributions, this "
            "path scores each burst individually, so no information is lost to "
            "binning and bursts of different sizes contribute according to how much "
            "they actually constrain the model.\n\n"
            "This is the hot path of a three-colour PDA fit; the enclosing model "
            "supplies the per-channel probabilities and this evaluates them against "
            "the data.";
        d.references_json = kPdaReferences;
        d.row_grain = "burst";
        d.settings_schema = R"JSON({
          "type": "object",
          "properties": {
            "n_channels": {"type": "integer", "default": 3, "minimum": 2},
            "background": {"type": "array", "items": {"type": "number"},
                           "description": "Per-channel background rate, kHz."}
          }
        })JSON";
        d.inputs_json = R"JSON({
          "required": ["burst_table", "channel_probabilities"],
          "description": "Per-burst photon counts per channel, and the model probabilities to score them against."
        })JSON";
        d.outputs_json = R"JSON({
          "columns": ["Log likelihood"]
        })JSON";
        d.can_replay = true;
        register_algorithm(d);
    }
}

} // namespace

void register_builtin_algorithms() {
    static std::once_flag once;
    std::call_once(once, [] {
        register_fcs();
        register_hmm();
        register_pda();
    });
}

} // namespace tttrlib

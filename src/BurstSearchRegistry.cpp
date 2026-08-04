// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file BurstSearchRegistry.cpp
 * \brief Machine-readable description of the available burst searches.
 *
 * tttrlib advertises its file containers through `TTTR::container_names`, so a
 * caller can discover what it is able to read instead of hard-coding a list. The
 * burst searches are advertised the same way, but a burst search needs more than
 * a name: anything offering one to a user has to know which parameters it takes,
 * their types, defaults and sensible ranges.
 *
 * Each entry therefore carries a JSON Schema of its parameters under
 * `params_schema`. The schema is deliberately the standard vocabulary (`type`,
 * `title`, `description`, `default`, `minimum`, `maximum`) rather than a bespoke
 * one, so that a consumer already able to render a JSON Schema can render a burst
 * search form with no tttrlib-specific code. Non-standard hints (`unit`, `scale`,
 * `advanced`) are extra keys, which a JSON Schema consumer ignores.
 *
 * The whole registry is JSON so that every language binding reads it from one
 * source rather than each re-declaring the parameter lists.
 *
 * Keep this in sync with the method signatures in TTTR.h — `method` is called by
 * name and each property name must match that method's keyword argument.
 */
#include <string>

#include "TTTR.h"

namespace {

const char* const kBurstSearchRegistry = R"JSON({
  "sliding_window": {
    "name": "sliding_window",
    "label": "Sliding window",
    "method": "burst_search_sliding_window",
    "summary": "Fixed rate threshold: a burst is where m consecutive photons fall inside a time T.",
    "description": "The classical single-molecule burst search. Fast and predictable, but it applies one global rate threshold (m/T) to the whole measurement, so it cannot separate a dim from a bright population, and it merges transits that overlap in time.",
    "params_schema": {
      "type": "object",
      "required": ["L", "m", "T"],
      "properties": {
        "L": {
          "type": "integer", "title": "Min photons (L)", "default": 20,
          "minimum": 1, "maximum": 100000,
          "description": "Bursts with fewer photons than this are discarded."
        },
        "m": {
          "type": "integer", "title": "Photons per window", "default": 10,
          "minimum": 2, "maximum": 1000,
          "description": "Number of consecutive photons used to estimate the local count rate."
        },
        "T": {
          "type": "number", "title": "Window duration (T)", "default": 5e-4,
          "minimum": 1e-9, "maximum": 1.0, "unit": "s", "scale": "log",
          "description": "Maximum time separation of m photons inside a burst. The rate threshold is m/T."
        }
      }
    }
  },
  "cusum_sprt": {
    "name": "cusum_sprt",
    "label": "Cumulative (CUSUM / SPRT)",
    "method": "burst_search_cusum_sprt",
    "summary": "Sequential likelihood-ratio test between a background rate and a signal rate.",
    "description": "Decides photon by photon between 'background' and 'burst' hypotheses on the local count rate, with controlled false-positive and false-negative rates. More selective than a hard threshold, but still one global contrast setting, so overlapping transits tend to be merged.",
    "params_schema": {
      "type": "object",
      "required": ["min_photons", "background_cps", "signal_to_background_ratio", "alpha", "beta"],
      "properties": {
        "min_photons": {
          "type": "integer", "title": "Min photons (L)", "default": 20,
          "minimum": 1, "maximum": 100000,
          "description": "Bursts with fewer photons than this are discarded."
        },
        "background_cps": {
          "type": "number", "title": "Background rate (cps)", "default": 0.0,
          "minimum": 0.0, "maximum": 1e9, "unit": "cps",
          "description": "Background count rate. 0 estimates it from the data (median of binned rates)."
        },
        "signal_to_background_ratio": {
          "type": "number", "title": "Signal/background ratio", "default": 4.0,
          "minimum": 0.0, "maximum": 1000.0,
          "description": "How many times brighter a burst is than background. 0 estimates it from the data."
        },
        "alpha": {
          "type": "number", "title": "False-positive rate", "default": 0.05,
          "minimum": 1e-6, "maximum": 0.5, "scale": "log",
          "description": "Probability of calling background a burst."
        },
        "beta": {
          "type": "number", "title": "False-negative rate", "default": 0.05,
          "minimum": 1e-6, "maximum": 0.5, "scale": "log",
          "description": "Probability of missing a real burst."
        }
      }
    }
  },
  "kalman": {
    "name": "kalman",
    "label": "Kalman (rate change)",
    "method": "burst_search_kalman",
    "summary": "Kalman-filtered count rate; a burst is a bin whose innovation is large against Poisson noise.",
    "description": "Bins the stream and tracks the count rate with a Kalman filter whose measurement noise is derived from Poisson statistics, marking bins where the measured rate departs from the predicted one by more than the filter's own uncertainty (a Mahalanobis distance). It therefore responds to a change in rate rather than to an absolute level, so a drifting background is tracked and ignored instead of detected. With more than one routing channel it tracks one rate per detector, scoring a simultaneous rise across detectors above an uncorrelated one. Its time resolution is limited by the bin width, unlike the photon-resolved searches.",
    "params_schema": {
      "type": "object",
      "required": ["L", "dt", "q", "r_scale", "z_thresh", "min_len", "merge_gap",
                   "per_channel"],
      "properties": {
        "L": {
          "type": "integer", "title": "Min photons (L)", "default": 20,
          "minimum": 1, "maximum": 100000,
          "description": "Bursts with fewer photons than this are discarded."
        },
        "dt": {
          "type": "number", "title": "Bin width (s)", "default": 1e-4,
          "minimum": 1e-9, "maximum": 1.0, "unit": "s", "scale": "log",
          "description": "Time bin width. Sets the resolution of the method: a burst shorter than a bin cannot be resolved."
        },
        "q": {
          "type": "number", "title": "Process noise (q)",
          "group": "Filter tuning", "default": 100.0,
          "minimum": 0.0, "maximum": 1e6, "scale": "log",
          "description": "How fast the filter believes the underlying rate itself changes, in (counts/s)^2. Larger makes the search less sensitive. Not dimensionless - it must be commensurate with the count rates in the data; far too small freezes the filter, and every bin then looks anomalous, giving one burst spanning the whole measurement."
        },
        "r_scale": {
          "type": "number", "title": "Measurement noise scale (r)",
          "group": "Filter tuning", "default": 0.1,
          "minimum": 1e-6, "maximum": 1000.0, "scale": "log",
          "description": "Scale on the Poisson measurement variance. 1.0 trusts shot noise exactly; larger tolerates extra technical noise."
        },
        "z_thresh": {
          "type": "number", "title": "Detection threshold (sigma)", "default": 3.0,
          "minimum": 0.0, "maximum": 100.0,
          "description": "Innovation distance above which a bin counts as part of a burst."
        },
        "min_len": {
          "type": "integer", "title": "Min consecutive bins",
          "group": "Advanced", "default": 2,
          "minimum": 1, "maximum": 10000,
          "description": "Minimum number of consecutive bins over threshold."
        },
        "merge_gap": {
          "type": "integer", "title": "Merge gap (bins)",
          "group": "Advanced", "default": 5,
          "minimum": 0, "maximum": 10000,
          "description": "Merge bursts separated by at most this many bins."
        },
        "per_channel": {
          "type": "boolean", "title": "Track channels separately", "default": true,
          "description": "Track one rate per routing channel, so a rise seen across detectors at once scores higher than an uncorrelated one. Ignored for single-channel data."
        }
      }
    }
  },
  "coincident": {
    "name": "coincident",
    "label": "Coincident (multi-detector)",
    "method": "burst_search_coincident",
    "summary": "Keeps bursts found independently in several detector groups at once.",
    "description": "A search over the pooled stream cannot distinguish a molecule carrying every label from one carrying a subset - both are simply bright. Requiring a burst to be found independently in several groups of detectors can, which is what rejects singly-labelled and photobleached molecules in ALEX/PIE. Any number of groups may be given and min_groups sets how many must agree, so '2 of 3' is expressible as well as 'all of 2'; the classical dual-channel burst search is the two-group case. This is a composition rather than a new algorithm: the search named by 'algorithm' runs once per group, so it works with every other entry here, including ones added later. Coincidence is decided in time - a burst marks the whole span of photons it covers - not per photon.",
    "params_schema": {
      "type": "object",
      "required": ["channel_groups", "algorithm", "min_groups", "L"],
      "properties": {
        "channel_groups": {
          "type": "array", "title": "Detector groups",
          "items": {"type": "array", "items": {"type": "integer"}},
          "description": "Routing-channel groups, e.g. [[0,1],[2,3]]. A group containing no photons is ignored rather than emptying the result."
        },
        "algorithm": {
          "type": "string", "title": "Per-group search", "default": "maxtree",
          "enum": ["sliding_window", "cusum_sprt", "kalman", "maxtree", "bayesian_blocks"],
          "description": "Which burst search to run inside each detector group."
        },
        "min_groups": {
          "type": "integer", "title": "Groups that must agree", "default": 0,
          "minimum": 0, "maximum": 64,
          "description": "How many groups must find the burst. 0 means all groups that contain photons."
        },
        "L": {
          "type": "integer", "title": "Min photons (L)", "default": 20,
          "minimum": 1, "maximum": 100000,
          "description": "Minimum photons in the coincident burst. The inner search applies its own L per group first."
        },
        "parameters": {
          "type": "object", "title": "Per-group search parameters",
          "parameters_of": {"category": "burst_search", "selector": "algorithm"},
          "description": "Parameters handed to the inner search; anything omitted takes that search's own defaults."
        }
      }
    }
  },
  "maxtree": {
    "name": "maxtree",
    "label": "Max-tree (threshold-free)",
    "method": "burst_search_maxtree",
    "summary": "Attribute filtering of the rate signal's component tree; each burst is found at its own level.",
    "description": "Builds the component tree of the local log count rate - every connected component at every level - and keeps the components that are maximally stable and whose photon count, duration, contrast and Poisson significance are plausible. Because no single level is chosen, dim and bright bursts in the same trace are both detected, and overlapping transits are deblended by the tree structure. Slower than the threshold searches; the parameters are dimensionless and transfer between instruments.",
    "params_schema": {
      "type": "object",
      "required": ["L", "m", "delta", "max_variation", "background_window",
                   "min_contrast", "min_duration", "max_duration", "n_levels",
                   "min_significance"],
      "properties": {
        "L": {
          "type": "integer", "title": "Min photons (L)", "default": 20,
          "minimum": 1, "maximum": 100000,
          "description": "Bursts with fewer photons than this are discarded."
        },
        "m": {
          "type": "integer", "title": "Photons per window", "default": 10,
          "minimum": 2, "maximum": 1000,
          "description": "Number of consecutive photons used to estimate the local count rate."
        },
        "delta": {
          "type": "number", "title": "Stability offset (log2 rate)", "default": 0.15,
          "minimum": 0.01, "maximum": 8.0, "unit": "log2 rate",
          "description": "Rate change over which a component's stability is judged. 0.15 probes a factor of about 1.11. Lower is the more permissive setting, leaning more on the contrast and significance filters, which measured markedly better than the 0.5 this once defaulted to."
        },
        "max_variation": {
          "type": "number", "title": "Max extent growth", "default": 0.5,
          "minimum": 0.01, "maximum": 10.0,
          "description": "Reject components whose extent grows more than this (relative) over the stability offset. Larger accepts less well-defined bursts."
        },
        "background_window": {
          "type": "number", "title": "Background window (s)",
          "group": "Background", "default": 0.05,
          "minimum": 0.0, "maximum": 100.0, "unit": "s", "scale": "log",
          "description": "Rolling-ball window for the baseline; must be much longer than a burst. 0 disables background estimation."
        },
        "min_contrast": {
          "type": "number", "title": "Min burst/background ratio",
          "group": "Background", "default": 2.0,
          "minimum": 0.0, "maximum": 1000.0,
          "description": "Minimum burst-to-background rate ratio. 0 disables the contrast filter."
        },
        "min_duration": {
          "type": "number", "title": "Min burst duration (s)",
          "group": "Advanced", "default": 0.0,
          "minimum": 0.0, "maximum": 10.0, "unit": "s",
          "description": "Discard bursts shorter than this. 0 disables the bound."
        },
        "max_duration": {
          "type": "number", "title": "Max burst duration (s)",
          "group": "Advanced", "default": 0.0,
          "minimum": 0.0, "maximum": 10.0, "unit": "s",
          "description": "Discard bursts longer than this. 0 disables the bound."
        },
        "n_levels": {
          "type": "integer", "title": "Quantization levels", "default": 1024,
          "minimum": 16, "maximum": 65536, "advanced": true,
          "description": "Levels used to quantize the log-rate signal. Rarely needs changing."
        },
        "min_significance": {
          "type": "number", "title": "Min significance (sigma)",
          "group": "Significance", "default": 4.0,
          "minimum": 0.0, "maximum": 50.0, "unit": "sigma",
          "description": "Minimum Poisson significance of the photon excess over the local background. This is what rejects shot-noise clumps; raising it to 6 trades a little recall for precision. 0 disables the test."
        },
        "significance_mode": {
          "type": "integer", "title": "Significance statistic",
          "group": "Significance", "default": 0,
          "minimum": 0, "maximum": 2, "advanced": true,
          "enum": [0, 1, 2],
          "enum_labels": ["Gaussian (k-mu)/sqrt(mu)", "Exact Poisson", "Li & Ma (1983)"],
          "description": "Which statistic computes the significance. Gaussian is the default so old results reproduce exactly, but it assumes the Poisson distribution is already normal, which is false at the counts here (~20 photons over ~2 expected). Li & Ma additionally accounts for the background being measured rather than known, and is the better choice for new work."
        },
        "max_false_alarm_rate": {
          "type": "number", "title": "Max false-alarm rate",
          "group": "Significance", "default": 0.0,
          "minimum": 0.0, "maximum": 1000.0, "unit": "1/s", "scale": "log",
          "advanced": true,
          "description": "Expected spurious bursts per second. When > 0 this replaces the sigma threshold with a post-trials one, so a single setting means the same thing on a 10 s and a 1 h acquisition. The trials correction is approximate; verify against a background-only measurement. 0 uses the sigma threshold."
        },
        "background_off_ratio": {
          "type": "number", "title": "Off/on exposure ratio",
          "group": "Background", "default": 0.0,
          "minimum": 0.0, "maximum": 10000.0, "advanced": true,
          "description": "t_off/t_on for the Li & Ma test. 0 derives it from the background window, which is normally what you want."
        }
      }
    }
  },
  "bayesian_blocks": {
    "name": "bayesian_blocks",
    "label": "Bayesian Blocks (optimal segmentation)",
    "method": "burst_search_bayesian_blocks",
    "summary": "Most probable partition of the photon stream into constant-rate intervals, behind a cheap trigger.",
    "description": "Scargle's Bayesian Blocks, developed for time-tagged photon events from BATSE and Fermi - the same data model as a TTTR file. Rather than asking whether the rate near each photon clears a threshold, it finds by dynamic programming the single most probable partition of the stream into intervals of constant rate. There is no binning, no window duration and no phase, so burst edges are placed optimally instead of being snapped to a window boundary, and the only detection parameter is p0, a false-alarm probability. The segmentation is O(N^2), so it runs behind a loose sliding-window trigger in the manner of Fermi GBM and Swift BAT: cheap pass proposes regions, exact segmentation refines inside them. Slower than the threshold searches and the most accurate on burst extent.",
    "params_schema": {
      "type": "object",
      "required": ["L", "m", "p0", "trigger_contrast", "pad_photons",
                   "max_region_photons", "min_significance",
                   "max_false_alarm_rate", "significance_mode", "trials_model"],
      "properties": {
        "L": {
          "type": "integer", "title": "Min photons (L)", "default": 20,
          "minimum": 1, "maximum": 100000,
          "description": "Bursts with fewer photons than this are discarded."
        },
        "m": {
          "type": "integer", "title": "Trigger window", "default": 10,
          "minimum": 2, "maximum": 1000,
          "description": "Photons per stage-1 trigger window. Affects only which regions get examined, never where the burst boundaries end up."
        },
        "p0": {
          "type": "number", "title": "Change-point p0", "default": 0.005,
          "minimum": 1e-6, "maximum": 0.5, "scale": "log",
          "description": "False-alarm probability for accepting a change point. Lower gives fewer, longer blocks. This replaces the rate threshold and, being dimensionless, transfers between instruments. The strict default measured better on purity, completeness and detection limit together than a looser one - splitting a burst into spurious blocks costs more here than missing a marginal change point."
        },
        "trigger_contrast": {
          "type": "number", "title": "Trigger contrast (x background)",
          "group": "Stage 1 (trigger)", "default": 2.5,
          "minimum": 1.0, "maximum": 100.0,
          "description": "Stage-1 trigger rate as a multiple of the measured background. The trigger is tuned for completeness, so this stays well below the contrast of a real burst. Below about 2 the segmentation ends up covering the whole stream and the trigger stops filtering anything; above about 3 the faintest bursts start to be lost, since a burst the trigger never fires on cannot be recovered later."
        },
        "pad_photons": {
          "type": "integer", "title": "Region padding", "default": 64,
          "minimum": 0, "maximum": 100000, "unit": "photons", "advanced": true,
          "description": "Background context added each side of a candidate run. Load-bearing rather than slack: given only in-burst photons the segmentation finds one block and refines nothing, so it needs flanks to place an edge against. This is the dominant cost knob - lowering it to 16 roughly 2.5x the speed for about 0.03 less completeness."
        },
        "max_region_photons": {
          "type": "integer", "title": "Max region size", "default": 4096,
          "minimum": 64, "maximum": 1000000, "unit": "photons", "advanced": true,
          "description": "Cap on the photons in one region, bounding the quadratic cost. Oversized regions are split at their sparsest interior point. Raising it improves boundary placement on long crowded stretches and costs quadratically."
        },
        "min_significance": {
          "type": "number", "title": "Min significance (sigma)", "default": 4.0,
          "minimum": 0.0, "maximum": 50.0, "unit": "sigma",
          "description": "Minimum significance of a block over the local background. Ignored when a false-alarm rate is set."
        },
        "max_false_alarm_rate": {
          "type": "number", "title": "Max false-alarm rate", "default": 0.0,
          "minimum": 0.0, "maximum": 1000.0, "unit": "1/s", "scale": "log",
          "description": "Expected spurious bursts per second. When > 0 this replaces the sigma threshold with a post-trials one, so a single setting means the same thing on a 10 s and a 1 h acquisition. The trials correction is approximate; verify against a background-only measurement. 0 uses the sigma threshold."
        },
        "significance_mode": {
          "type": "integer", "title": "Significance statistic", "default": 2,
          "minimum": 0, "maximum": 2, "advanced": true,
          "enum": [0, 1, 2],
          "enum_labels": ["Gaussian (k-mu)/sqrt(mu)", "Exact Poisson", "Li & Ma (1983)"],
          "description": "Which statistic tests a block against the local background. Defaults to Li & Ma because the background here is always measured from the region's own flanks rather than known a priori."
        },
        "trials_model": {
          "type": "integer", "title": "Trials model", "default": 0,
          "minimum": 0, "maximum": 1, "advanced": true,
          "enum": [0, 1],
          "enum_labels": ["Independent windows", "Tested components"],
          "description": "How the trials factor for the false-alarm rate is estimated. Independent windows (N/m) is the conservative default."
        }
      }
    }
  }
})JSON";

} // namespace

std::string TTTR::burst_search_algorithms_json() {
    return std::string(kBurstSearchRegistry);
}

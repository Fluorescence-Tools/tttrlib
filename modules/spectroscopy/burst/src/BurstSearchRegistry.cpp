// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file BurstSearchRegistry.cpp
 * \brief Every burst search declares itself here -- what it is, what parameters
 *        it takes, and the function that runs it.
 *
 * The built-in searches used to be a hand-authored JSON literal
 * (`kBurstSearchRegistry`) sitting beside a separate table of dispatch
 * functions. Two lists of the same seven algorithms, in two files, with nothing
 * keeping them in step -- and they did fall out of step: `bocpd` and
 * `coincident` were advertised with a `method` that the dispatcher had never
 * heard of, so calling them returned sliding-window bursts from the wrong
 * algorithm, silently.
 *
 * There is now one declaration per search, carrying the description and the
 * dispatch function together, through the same `register_algorithm` path a
 * plugin uses. A search that is added, renamed or removed cannot be half-done.
 *
 * Each entry carries a JSON Schema of its parameters under `settings_schema`,
 * emitted as both `settings_schema` and `params_schema` -- the latter is the
 * name this category has always used and what ChiSurf, ndX and the web UI read.
 * The schema is deliberately the standard vocabulary (`type`, `title`,
 * `description`, `default`, `minimum`, `maximum`) rather than a bespoke one, so
 * a consumer that can already render a JSON Schema can render a burst-search
 * form with no tttrlib-specific code. Non-standard hints (`unit`, `scale`,
 * `advanced`) are extra keys, which a JSON Schema consumer ignores.
 *
 * `dispatch_name` must match the `TTTR` method a caller invokes, and each schema
 * property name must match that method's keyword argument.
 */
#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "TTTR.h"
#include "TTTRHeader.h"
#include "PluginHost.h"
#include "Registry.h"
#include "BurstSearchDispatch.h"
#include "BurstSearchBayesianBlocks.h"
#include "BurstSearchKalman.h"
#include "BurstSearchMaxTree.h"

using tttrlib::AlgorithmDescriptor;
using tttrlib::register_burst_search;

namespace tttrlib {

void register_builtin_burst_searches() {
    // Explicit rather than a static initialiser: a static initialiser in a
    // translation unit nothing else references is dropped when the library is
    // linked as a static archive, and the search then silently does not exist.
    static std::once_flag once;
    std::call_once(once, [] {
    {
        AlgorithmDescriptor d;
        d.name           = "sliding_window";   // the registry key
        d.operation_type = "burst_selection";  // mmfdb term for what it does
        d.capability      = "burst_search";
        d.display_name    = R"L(Sliding window)L";
        d.dispatch_name   = "burst_search_sliding_window";
        d.summary         = R"S(Fixed rate threshold: a burst is where m consecutive photons fall inside a time T.)S";
        d.description     = R"D(The classical single-molecule burst search. Fast and predictable, but it applies one global rate threshold (m/T) to the whole measurement, so it cannot separate a dim from a bright population, and it merges transits that overlap in time.)D";
        d.row_grain       = "burst";
        d.settings_schema = R"SCHEMA({
          "type": "object",
          "required": [
            "L",
            "m",
            "T"
          ],
          "properties": {
            "L": {
              "type": "integer",
              "title": "Min photons (L)",
              "default": 20,
              "minimum": 1,
              "maximum": 100000,
              "description": "Bursts with fewer photons than this are discarded."
            },
            "m": {
              "type": "integer",
              "title": "Photons per window",
              "default": 10,
              "minimum": 2,
              "maximum": 1000,
              "description": "Number of consecutive photons used to estimate the local count rate."
            },
            "T": {
              "type": "number",
              "title": "Window duration (T)",
              "default": 0.0005,
              "minimum": 1e-09,
              "maximum": 1.0,
              "unit": "s",
              "scale": "log",
              "description": "Maximum time separation of m photons inside a burst. The rate threshold is m/T."
            }
          }
        })SCHEMA";
        d.extra_json = R"JSON({"api": ["TTTR.burst_search", "TTTR.burst_search_sliding_window", "StreamingBurstDetector"]})JSON";
        register_burst_search(d,
        [](TTTR& d, int L, int m, double T, double, double) {
            return d.burst_search_sliding_window(L, m, T);
        });
    }

    {
        AlgorithmDescriptor d;
        d.name           = "cusum_sprt";   // the registry key
        d.operation_type = "burst_selection";  // mmfdb term for what it does
        d.capability      = "burst_search";
        d.display_name    = R"L(Cumulative (CUSUM / SPRT))L";
        d.dispatch_name   = "burst_search_cusum_sprt";
        d.summary         = R"S(Sequential likelihood-ratio test between a background rate and a signal rate.)S";
        d.description     = R"D(Decides photon by photon between 'background' and 'burst' hypotheses on the local count rate, with controlled false-positive and false-negative rates. More selective than a hard threshold, but still one global contrast setting, so overlapping transits tend to be merged.)D";
        d.row_grain       = "burst";
        d.settings_schema = R"SCHEMA({
          "type": "object",
          "required": [
            "min_photons",
            "background_cps",
            "signal_to_background_ratio",
            "alpha",
            "beta"
          ],
          "properties": {
            "min_photons": {
              "type": "integer",
              "title": "Min photons (L)",
              "default": 20,
              "minimum": 1,
              "maximum": 100000,
              "description": "Bursts with fewer photons than this are discarded."
            },
            "background_cps": {
              "type": "number",
              "title": "Background rate (cps)",
              "default": 0.0,
              "minimum": 0.0,
              "maximum": 1000000000.0,
              "unit": "cps",
              "description": "Background count rate. 0 estimates it from the data (median of binned rates)."
            },
            "signal_to_background_ratio": {
              "type": "number",
              "title": "Signal/background ratio",
              "default": 4.0,
              "minimum": 0.0,
              "maximum": 1000.0,
              "description": "How many times brighter a burst is than background. 0 estimates it from the data."
            },
            "alpha": {
              "type": "number",
              "title": "False-positive rate",
              "default": 0.05,
              "minimum": 1e-06,
              "maximum": 0.5,
              "scale": "log",
              "description": "Probability of calling background a burst."
            },
            "beta": {
              "type": "number",
              "title": "False-negative rate",
              "default": 0.05,
              "minimum": 1e-06,
              "maximum": 0.5,
              "scale": "log",
              "description": "Probability of missing a real burst."
            }
          }
        })SCHEMA";
        d.extra_json = R"JSON({"api": ["TTTR.burst_search_cusum_sprt"]})JSON";
        register_burst_search(d,
        [](TTTR& d, int L, int m, double T, double alpha, double beta) {
            return d.burst_search_cusum_sprt(L, m, T, alpha, beta);
        });
    }

    // `T` is the bin width in seconds here; 0 keeps the default.
    {
        AlgorithmDescriptor d;
        d.name           = "kalman";   // the registry key
        d.operation_type = "burst_selection";  // mmfdb term for what it does
        d.capability      = "burst_search";
        d.display_name    = R"L(Kalman (rate change))L";
        d.dispatch_name   = "burst_search_kalman";
        d.summary         = R"S(Kalman-filtered count rate; a burst is a bin whose innovation is large against Poisson noise.)S";
        d.description     = R"D(Bins the stream and tracks the count rate with a Kalman filter whose measurement noise is derived from Poisson statistics, marking bins where the measured rate departs from the predicted one by more than the filter's own uncertainty (a Mahalanobis distance). It therefore responds to a change in rate rather than to an absolute level, so a drifting background is tracked and ignored instead of detected. With more than one routing channel it tracks one rate per detector, scoring a simultaneous rise across detectors above an uncorrelated one. Its time resolution is limited by the bin width, unlike the photon-resolved searches.)D";
        d.row_grain       = "burst";
        d.settings_schema = R"SCHEMA({
          "type": "object",
          "required": [
            "L",
            "dt",
            "q",
            "r_scale",
            "z_thresh",
            "min_len",
            "merge_gap",
            "per_channel"
          ],
          "properties": {
            "L": {
              "type": "integer",
              "title": "Min photons (L)",
              "default": 20,
              "minimum": 1,
              "maximum": 100000,
              "description": "Bursts with fewer photons than this are discarded."
            },
            "dt": {
              "type": "number",
              "title": "Bin width (s)",
              "default": 0.0001,
              "minimum": 1e-09,
              "maximum": 1.0,
              "unit": "s",
              "scale": "log",
              "description": "Time bin width. Sets the resolution of the method: a burst shorter than a bin cannot be resolved."
            },
            "q": {
              "type": "number",
              "title": "Process noise (q)",
              "group": "Filter tuning",
              "default": 100.0,
              "minimum": 0.0,
              "maximum": 1000000.0,
              "scale": "log",
              "description": "How fast the filter believes the underlying rate itself changes, in (counts/s)^2. Larger makes the search less sensitive. Not dimensionless - it must be commensurate with the count rates in the data; far too small freezes the filter, and every bin then looks anomalous, giving one burst spanning the whole measurement."
            },
            "r_scale": {
              "type": "number",
              "title": "Measurement noise scale (r)",
              "group": "Filter tuning",
              "default": 0.1,
              "minimum": 1e-06,
              "maximum": 1000.0,
              "scale": "log",
              "description": "Scale on the Poisson measurement variance. 1.0 trusts shot noise exactly; larger tolerates extra technical noise."
            },
            "z_thresh": {
              "type": "number",
              "title": "Detection threshold (sigma)",
              "default": 3.0,
              "minimum": 0.0,
              "maximum": 100.0,
              "description": "Innovation distance above which a bin counts as part of a burst."
            },
            "min_len": {
              "type": "integer",
              "title": "Min consecutive bins",
              "group": "Advanced",
              "default": 2,
              "minimum": 1,
              "maximum": 10000,
              "description": "Minimum number of consecutive bins over threshold."
            },
            "merge_gap": {
              "type": "integer",
              "title": "Merge gap (bins)",
              "group": "Advanced",
              "default": 5,
              "minimum": 0,
              "maximum": 10000,
              "description": "Merge bursts separated by at most this many bins."
            },
            "per_channel": {
              "type": "boolean",
              "title": "Track channels separately",
              "default": true,
              "description": "Track one rate per routing channel, so a rise seen across detectors at once scores higher than an uncorrelated one. Ignored for single-channel data."
            },
            "warmup_bins": {
              "type": "integer",
              "title": "Warm-up bins",
              "default": 0,
              "minimum": 0,
              "maximum": 1000000,
              "group": "Advanced",
              "description": "Seed the filter from the first N bins (x0 = their mean rate, P0 its Poisson variance) and report no burst inside them. 0 is the legacy zero start, which flags one or two spurious bursts at t = 0 because R is ~0 on the first update. The method takes this argument; it was missing from this schema until 2026-08-19, so burst_search_by_name refused it."
            }
          }
        })SCHEMA";
        d.extra_json = R"JSON({"api": ["TTTR.burst_search_kalman"]})JSON";
        register_burst_search(d,
        [](TTTR& d, int L, int, double T, double, double) {
            return d.burst_search_kalman(L, (T > 0.0) ? T : 1e-4);
        });
    }

    // `T` is the bin width in seconds; 0 keeps the default.
    {
        AlgorithmDescriptor d;
        d.name           = "bocpd";   // the registry key
        d.operation_type = "burst_selection";  // mmfdb term for what it does
        d.capability      = "burst_search";
        d.display_name    = R"L(Bayesian changepoint (BOCPD))L";
        d.dispatch_name   = "burst_search_bocpd";
        d.summary         = R"S(Bayesian Online Changepoint Detection: segments the trace by run-length posterior, not a rate threshold.)S";
        d.description     = R"D(Maintains a posterior over run lengths — how many bins the current segment has lasted since the last changepoint — with a Gamma-Poisson conjugate model. A changepoint is flagged when the posterior collapses to run length zero, so the method adapts to the local rate rather than applying one global threshold. Bursts are the segments between changepoints that contain enough photons. For multi-channel data each channel carries its own Gamma pair and the predictive is the joint log-likelihood, so a simultaneous rate change across channels scores stronger than a single-channel one.)D";
        d.row_grain       = "burst";
        d.settings_schema = R"SCHEMA({
          "type": "object",
          "required": [
            "L",
            "dt",
            "prior_count",
            "prior_duration",
            "changepoint_prob",
            "max_run"
          ],
          "properties": {
            "L": {
              "type": "integer",
              "title": "Min photons (L)",
              "default": 20,
              "minimum": 1,
              "maximum": 100000,
              "description": "Bursts with fewer photons than this are discarded."
            },
            "dt": {
              "type": "number",
              "title": "Bin width",
              "default": 0.001,
              "minimum": 1e-07,
              "maximum": 1.0,
              "unit": "s",
              "scale": "log",
              "description": "Time bin width. A burst shorter than a bin cannot be resolved."
            },
            "prior_count": {
              "type": "number",
              "title": "Prior count (alpha)",
              "default": 1.0,
              "minimum": 1e-06,
              "maximum": 1000.0,
              "scale": "log",
              "group": "Prior",
              "description": "Gamma shape prior \u2014 pseudo-count of photons the prior represents before any data."
            },
            "prior_duration": {
              "type": "number",
              "title": "Prior duration (beta)",
              "default": 1.0,
              "minimum": 1e-06,
              "maximum": 1000.0,
              "scale": "log",
              "group": "Prior",
              "description": "Gamma rate prior \u2014 how many bins-worth of data the prior represents."
            },
            "changepoint_prob": {
              "type": "number",
              "title": "Changepoint probability",
              "default": 0.1,
              "minimum": 1e-08,
              "maximum": 1.0,
              "scale": "log",
              "group": "Model",
              "description": "Hazard rate \u2014 probability of a changepoint in any bin. Larger detects more, shorter segments. It is not scale-free and has to be read together with the bin width: set it near 1/(bins you expect a burst to span), so a 200 us transit in 20 us bins wants about 0.1-0.2. Set well below that and the run length never resets, and the search returns the whole measurement as a single burst \u2014 a failure that looks like a detection rather than an error. Measured on a simulated dilute stream at dt = 20 us: 0.2 recovers 100% of the transits individually, 0.02 returns one burst spanning everything."
            },
            "max_run": {
              "type": "integer",
              "title": "Max run length",
              "default": 256,
              "minimum": 2,
              "maximum": 10000,
              "group": "Advanced",
              "description": "Maximum run length to track. Segments longer than this saturate."
            },
            "per_channel": {
              "type": "boolean",
              "title": "Track channels separately",
              "default": true,
              "description": "Track one Gamma pair per routing channel, so a simultaneous rate change across detectors scores higher. Ignored for single-channel data."
            }
          }
        })SCHEMA";
        d.extra_json = R"JSON({"api": ["TTTR.burst_search_bocpd"]})JSON";
        register_burst_search(d,
        [](TTTR& d, int L, int, double T, double, double) {
            return d.burst_search_bocpd(L, (T > 0.0) ? T : 1e-3);
        });
    }

    // `coincident` is registered precisely so that it FAILS through the narrow
    // door.
    //
    // Its essential input is the channel grouping, and the narrow (L, m, T)
    // signature has nowhere to put it -- so it cannot run this way. Before this
    // entry it did not say so: an unrecognised name falls back to the sliding
    // window, and `coincident` was unrecognised, so `burst_search("coincident",
    // ...)` returned sliding-window bursts while the registry advertised a
    // coincidence search. A caller got a plausible answer from the wrong
    // algorithm.
    //
    // A name the registry lists is not an unknown name, so failing here does not
    // touch the unknown-name fallback that callers rely on.
    {
        AlgorithmDescriptor d;
        d.name           = "coincident";   // the registry key
        d.operation_type = "burst_selection";  // mmfdb term for what it does
        d.capability      = "burst_search";
        d.display_name    = R"L(Coincident (multi-detector))L";
        d.dispatch_name   = "burst_search_coincident";
        d.summary         = R"S(Keeps bursts found independently in several detector groups at once.)S";
        d.description     = R"D(A search over the pooled stream cannot distinguish a molecule carrying every label from one carrying a subset - both are simply bright. Requiring a burst to be found independently in several groups of detectors can, which is what rejects singly-labelled and photobleached molecules in ALEX/PIE. Any number of groups may be given and min_groups sets how many must agree, so '2 of 3' is expressible as well as 'all of 2'; the classical dual-channel burst search is the two-group case. This is a composition rather than a new algorithm: the search named by 'algorithm' runs once per group, so it works with every other entry here, including ones added later. Coincidence is decided in time - a burst marks the whole span of photons it covers - not per photon.)D";
        d.row_grain       = "burst";
        d.settings_schema = R"SCHEMA({
          "type": "object",
          "required": [
            "channel_groups",
            "algorithm",
            "min_groups",
            "L"
          ],
          "properties": {
            "channel_groups": {
              "type": "array",
              "title": "Detector groups",
              "items": {
                "type": "array",
                "items": {
                  "type": "integer"
                }
              },
              "description": "Routing-channel groups, e.g. [[0,1],[2,3]]. A group containing no photons is ignored rather than emptying the result."
            },
            "algorithm": {
              "type": "string",
              "title": "Per-group search",
              "default": "maxtree",
              "enum": [
                "sliding_window",
                "cusum_sprt",
                "kalman",
                "bocpd",
                "maxtree",
                "bayesian_blocks"
              ],
              "description": "Which burst search to run inside each detector group."
            },
            "min_groups": {
              "type": "integer",
              "title": "Groups that must agree",
              "default": 0,
              "minimum": 0,
              "maximum": 64,
              "description": "How many groups must find the burst. 0 means all groups that contain photons."
            },
            "L": {
              "type": "integer",
              "title": "Min photons (L)",
              "default": 20,
              "minimum": 1,
              "maximum": 100000,
              "description": "Minimum photons in the coincident burst. The inner search applies its own L per group first."
            },
            "parameters": {
              "type": "object",
              "title": "Per-group search parameters",
              "parameters_of": {
                "category": "burst_search",
                "selector": "algorithm"
              },
              "description": "Parameters handed to the inner search; anything omitted takes that search's own defaults."
            }
          }
        })SCHEMA";
        d.extra_json = R"JSON({"api": ["TTTR.burst_search_coincident", "TTTR.burst_search_by_name", "TTTR.burst_search_plugin", "TTTR.burst_search_algorithms", "TTTR.burst_search_defaults"]})JSON";
        register_burst_search(d,
        [](TTTR&, int, int, double, double, double) -> std::vector<long long> {
            throw std::invalid_argument(
                "the 'coincident' burst search needs a channel grouping, "
                "which burst_search(L, m, T, mode) cannot carry -- call "
                "burst_search_coincident(channel_groups, ...) instead");
        });
    }

    // `T` is the rolling-ball background window in seconds; 0 falls back to the
    // default. The remaining max-tree parameters keep theirs -- reach them
    // through burst_search_maxtree() itself.
    //
    // The defaults come from the settings struct rather than being written out
    // again: a second copy of a default is a second thing to forget.
    {
        AlgorithmDescriptor d;
        d.name           = "maxtree";   // the registry key
        d.operation_type = "burst_selection";  // mmfdb term for what it does
        d.capability      = "burst_search";
        d.display_name    = R"L(Max-tree (threshold-free))L";
        d.dispatch_name   = "burst_search_maxtree";
        d.summary         = R"S(Attribute filtering of the rate signal's component tree; each burst is found at its own level.)S";
        d.description     = R"D(Builds the component tree of the local log count rate - every connected component at every level - and keeps the components that are maximally stable and whose photon count, duration, contrast and Poisson significance are plausible. Because no single level is chosen, dim and bright bursts in the same trace are both detected, and overlapping transits are deblended by the tree structure. Slower than the threshold searches; the parameters are dimensionless and transfer between instruments.)D";
        d.row_grain       = "burst";
        d.settings_schema = R"SCHEMA({
          "type": "object",
          "required": [
            "L",
            "m",
            "delta",
            "max_variation",
            "background_window",
            "min_contrast",
            "min_duration",
            "max_duration",
            "n_levels",
            "min_significance"
          ],
          "properties": {
            "L": {
              "type": "integer",
              "title": "Min photons (L)",
              "default": 20,
              "minimum": 1,
              "maximum": 100000,
              "description": "Bursts with fewer photons than this are discarded."
            },
            "m": {
              "type": "integer",
              "title": "Photons per window",
              "default": 10,
              "minimum": 2,
              "maximum": 1000,
              "description": "Number of consecutive photons used to estimate the local count rate."
            },
            "delta": {
              "type": "number",
              "title": "Stability offset (log2 rate)",
              "default": 0.15,
              "minimum": 0.01,
              "maximum": 8.0,
              "unit": "log2 rate",
              "description": "Rate change over which a component's stability is judged. 0.15 probes a factor of about 1.11. Lower is the more permissive setting, leaning more on the contrast and significance filters, which measured markedly better than the 0.5 this once defaulted to."
            },
            "max_variation": {
              "type": "number",
              "title": "Max extent growth",
              "default": 0.5,
              "minimum": 0.01,
              "maximum": 10.0,
              "description": "Reject components whose extent grows more than this (relative) over the stability offset. Larger accepts less well-defined bursts."
            },
            "background_window": {
              "type": "number",
              "title": "Background window (s)",
              "group": "Background",
              "default": 0.05,
              "minimum": 0.0,
              "maximum": 100.0,
              "unit": "s",
              "scale": "log",
              "description": "Rolling-ball window for the baseline; must be much longer than a burst. 0 disables background estimation."
            },
            "min_contrast": {
              "type": "number",
              "title": "Min burst/background ratio",
              "group": "Background",
              "default": 2.0,
              "minimum": 0.0,
              "maximum": 1000.0,
              "description": "Minimum burst-to-background rate ratio. 0 disables the contrast filter."
            },
            "min_duration": {
              "type": "number",
              "title": "Min burst duration (s)",
              "group": "Advanced",
              "default": 0.0,
              "minimum": 0.0,
              "maximum": 10.0,
              "unit": "s",
              "description": "Discard bursts shorter than this. 0 disables the bound."
            },
            "max_duration": {
              "type": "number",
              "title": "Max burst duration (s)",
              "group": "Advanced",
              "default": 0.0,
              "minimum": 0.0,
              "maximum": 10.0,
              "unit": "s",
              "description": "Discard bursts longer than this. 0 disables the bound."
            },
            "n_levels": {
              "type": "integer",
              "title": "Quantization levels",
              "default": 1024,
              "minimum": 16,
              "maximum": 65536,
              "advanced": true,
              "description": "Levels used to quantize the log-rate signal. Rarely needs changing."
            },
            "min_significance": {
              "type": "number",
              "title": "Min significance (sigma)",
              "group": "Significance",
              "default": 4.0,
              "minimum": 0.0,
              "maximum": 50.0,
              "unit": "sigma",
              "description": "Minimum Poisson significance of the photon excess over the local background. This is what rejects shot-noise clumps; raising it to 6 trades a little recall for precision. 0 disables the test."
            },
            "significance_mode": {
              "type": "integer",
              "title": "Significance statistic",
              "group": "Significance",
              "default": 0,
              "minimum": 0,
              "maximum": 2,
              "advanced": true,
              "enum": [
                0,
                1,
                2
              ],
              "enum_labels": [
                "Gaussian (k-mu)/sqrt(mu)",
                "Exact Poisson",
                "Li & Ma (1983)"
              ],
              "description": "Which statistic computes the significance. Gaussian is the default so old results reproduce exactly, but it assumes the Poisson distribution is already normal, which is false at the counts here (~20 photons over ~2 expected). Li & Ma additionally accounts for the background being measured rather than known, and is the better choice for new work."
            },
            "max_false_alarm_rate": {
              "type": "number",
              "title": "Max false-alarm rate",
              "group": "Significance",
              "default": 0.0,
              "minimum": 0.0,
              "maximum": 1000.0,
              "unit": "1/s",
              "scale": "log",
              "advanced": true,
              "description": "Expected spurious bursts per second. When > 0 this replaces the sigma threshold with a post-trials one, so a single setting means the same thing on a 10 s and a 1 h acquisition. The trials correction is approximate; verify against a background-only measurement. 0 uses the sigma threshold."
            },
            "background_off_ratio": {
              "type": "number",
              "title": "Off/on exposure ratio",
              "group": "Background",
              "default": 0.0,
              "minimum": 0.0,
              "maximum": 10000.0,
              "advanced": true,
              "description": "t_off/t_on for the Li & Ma test. 0 derives it from the background window, which is normally what you want."
            }
          }
        })SCHEMA";
        d.extra_json = R"JSON({"api": ["TTTR.burst_search_maxtree", "max_tree_1d"]})JSON";
        register_burst_search(d,
        [](TTTR& d, int L, int m, double T, double, double) {
            const MaxTreeBurstSettings mt;
            return d.burst_search_maxtree(L, m, mt.delta, mt.max_variation,
                                          (T > 0.0) ? T : mt.background_window,
                                          mt.min_contrast);
        });
    }

    // `T` is p0, the change-point false-alarm probability; 0 keeps the default.
    {
        AlgorithmDescriptor d;
        d.name           = "bayesian_blocks";   // the registry key
        d.operation_type = "burst_selection";  // mmfdb term for what it does
        d.capability      = "burst_search";
        d.display_name    = R"L(Bayesian Blocks (optimal segmentation))L";
        d.dispatch_name   = "burst_search_bayesian_blocks";
        d.summary         = R"S(Most probable partition of the photon stream into constant-rate intervals, behind a cheap trigger.)S";
        d.description     = R"D(Scargle's Bayesian Blocks, developed for time-tagged photon events from BATSE and Fermi - the same data model as a TTTR file. Rather than asking whether the rate near each photon clears a threshold, it finds by dynamic programming the single most probable partition of the stream into intervals of constant rate. There is no binning, no window duration and no phase, so burst edges are placed optimally instead of being snapped to a window boundary, and the only detection parameter is p0, a false-alarm probability. The segmentation is O(N^2), so it runs behind a loose sliding-window trigger in the manner of Fermi GBM and Swift BAT: cheap pass proposes regions, exact segmentation refines inside them. Slower than the threshold searches and the most accurate on burst extent.)D";
        d.row_grain       = "burst";
        d.settings_schema = R"SCHEMA({
          "type": "object",
          "required": [
            "L",
            "m",
            "p0",
            "trigger_contrast",
            "pad_photons",
            "max_region_photons",
            "min_significance",
            "max_false_alarm_rate",
            "significance_mode",
            "trials_model"
          ],
          "properties": {
            "L": {
              "type": "integer",
              "title": "Min photons (L)",
              "default": 20,
              "minimum": 1,
              "maximum": 100000,
              "description": "Bursts with fewer photons than this are discarded."
            },
            "m": {
              "type": "integer",
              "title": "Trigger window",
              "default": 10,
              "minimum": 2,
              "maximum": 1000,
              "description": "Photons per stage-1 trigger window. Affects only which regions get examined, never where the burst boundaries end up."
            },
            "p0": {
              "type": "number",
              "title": "Change-point p0",
              "default": 0.005,
              "minimum": 1e-06,
              "maximum": 0.5,
              "scale": "log",
              "description": "False-alarm probability for accepting a change point. Lower gives fewer, longer blocks. This replaces the rate threshold and, being dimensionless, transfers between instruments. The strict default measured better on purity, completeness and detection limit together than a looser one - splitting a burst into spurious blocks costs more here than missing a marginal change point."
            },
            "trigger_contrast": {
              "type": "number",
              "title": "Trigger contrast (x background)",
              "group": "Stage 1 (trigger)",
              "default": 2.5,
              "minimum": 1.0,
              "maximum": 100.0,
              "description": "Stage-1 trigger rate as a multiple of the measured background. The trigger is tuned for completeness, so this stays well below the contrast of a real burst. Below about 2 the segmentation ends up covering the whole stream and the trigger stops filtering anything; above about 3 the faintest bursts start to be lost, since a burst the trigger never fires on cannot be recovered later."
            },
            "pad_photons": {
              "type": "integer",
              "title": "Region padding",
              "default": 64,
              "minimum": 0,
              "maximum": 100000,
              "unit": "photons",
              "advanced": true,
              "description": "Background context added each side of a candidate run. Load-bearing rather than slack: given only in-burst photons the segmentation finds one block and refines nothing, so it needs flanks to place an edge against. This is the dominant cost knob - lowering it to 16 roughly 2.5x the speed for about 0.03 less completeness. It is bounded above by the data as well: padded regions that touch are merged, so a padding wider than the background between two transits joins them into one region and returns them as one burst. Keep it well under the photons you expect between bursts - on a stream carrying only 20-60 background photons between transits the default of 64 recovered 16 of 40 bursts, and 8 recovered 38."
            },
            "max_region_photons": {
              "type": "integer",
              "title": "Max region size",
              "default": 4096,
              "minimum": 64,
              "maximum": 1000000,
              "unit": "photons",
              "advanced": true,
              "description": "Cap on the photons in one region, bounding the quadratic cost. Oversized regions are split at their sparsest interior point. Raising it improves boundary placement on long crowded stretches and costs quadratically."
            },
            "min_significance": {
              "type": "number",
              "title": "Min significance (sigma)",
              "default": 4.0,
              "minimum": 0.0,
              "maximum": 50.0,
              "unit": "sigma",
              "description": "Minimum significance of a block over the local background. Ignored when a false-alarm rate is set."
            },
            "max_false_alarm_rate": {
              "type": "number",
              "title": "Max false-alarm rate",
              "default": 0.0,
              "minimum": 0.0,
              "maximum": 1000.0,
              "unit": "1/s",
              "scale": "log",
              "description": "Expected spurious bursts per second. When > 0 this replaces the sigma threshold with a post-trials one, so a single setting means the same thing on a 10 s and a 1 h acquisition. The trials correction is approximate; verify against a background-only measurement. 0 uses the sigma threshold."
            },
            "significance_mode": {
              "type": "integer",
              "title": "Significance statistic",
              "default": 2,
              "minimum": 0,
              "maximum": 2,
              "advanced": true,
              "enum": [
                0,
                1,
                2
              ],
              "enum_labels": [
                "Gaussian (k-mu)/sqrt(mu)",
                "Exact Poisson",
                "Li & Ma (1983)"
              ],
              "description": "Which statistic tests a block against the local background. Defaults to Li & Ma because the background here is always measured from the region's own flanks rather than known a priori."
            },
            "trials_model": {
              "type": "integer",
              "title": "Trials model",
              "default": 0,
              "minimum": 0,
              "maximum": 1,
              "advanced": true,
              "enum": [
                0,
                1
              ],
              "enum_labels": [
                "Independent windows",
                "Tested components"
              ],
              "description": "How the trials factor for the false-alarm rate is estimated. Independent windows (N/m) is the conservative default."
            }
          }
        })SCHEMA";
        d.extra_json = R"JSON({"api": ["TTTR.burst_search_bayesian_blocks"]})JSON";
        register_burst_search(d,
        [](TTTR& d, int L, int m, double T, double, double) {
            const BayesianBlocksBurstSettings bb;
            return d.burst_search_bayesian_blocks(L, m, (T > 0.0) ? T : bb.p0);
        });
    }
    });
}


// ---- registry("operation") entry ------------------------------------------
// The burst pipeline's first step: a search plus the primary burst table. The
// column names listed are those of the two-detector (green/red) default; a
// `--setup` with other detector names produces `Duration (<detector>) (ms)`
// etc. for each of its detectors (modules/cli/src/cmd_sm.cpp).
namespace {
const char* const kBurstSelectionEntry = R"JSON({
  "name": "burst_selection",
  "method": "burst_search_by_name",
  "api": [
    "TTTR.burst_search_by_name",
    "TTTR.burst_search",
    "BurstFilter"
  ],
  "label": "Burst search and selection",
  "summary": "Sliding-window / CUSUM / Kalman / Bayesian-blocks burst search on TTTR macro-times. Produces the primary .bur burst table.",
  "operation_type": "burst_selection",
  "data_format": "dstore",
  "row_grain": "burst",
  "kind": "burst_table",
  "inputs": {
    "required": [
      "tttr_photon_stream"
    ],
    "description": "Raw TTTR photon stream (macro + micro times, routing channels)."
  },
  "outputs": {
    "columns": [
      "First Photon",
      "Last Photon",
      "Duration (ms)",
      "Duration (green) (ms)",
      "Duration (red) (ms)",
      "Mean Macro Time (ms)",
      "Mean Macro Time (green) (ms)",
      "Mean Macro Time (red) (ms)",
      "Number of Photons",
      "Number of Photons (green)",
      "Number of Photons (red)",
      "Count Rate (KHz)",
      "Green Count Rate (KHz)",
      "Red Count Rate (KHz)",
      "Mean Microtime (green) (ns)",
      "Mean Microtime (red) (ns)",
      "Proximity Ratio"
    ]
  },
  "settings_schema": {
    "type": "object",
    "properties": {
      "algorithm": {
        "type": "string",
        "title": "Search",
        "default": "sliding_window",
        "description": "Name of a registered burst search (registry(\"burst_search\")); its own parameters follow."
      },
      "L": {
        "type": "integer",
        "title": "Minimum photons",
        "default": 20,
        "minimum": 1
      },
      "m": {
        "type": "integer",
        "title": "Photons for the local rate",
        "default": 10,
        "minimum": 1
      },
      "T": {
        "type": "number",
        "title": "Time separation of m photons",
        "default": 0.0005,
        "unit": "s"
      }
    },
    "required": [
      "algorithm"
    ]
  },
  "can_replay": true,
  "params_schema": {
    "type": "object",
    "properties": {
      "algorithm": {
        "type": "string",
        "title": "Search",
        "default": "sliding_window",
        "description": "Name of a registered burst search (registry(\"burst_search\")); its own parameters follow."
      },
      "L": {
        "type": "integer",
        "title": "Minimum photons",
        "default": 20,
        "minimum": 1
      },
      "m": {
        "type": "integer",
        "title": "Photons for the local rate",
        "default": 10,
        "minimum": 1
      },
      "T": {
        "type": "number",
        "title": "Time separation of m photons",
        "default": 0.0005,
        "unit": "s"
      }
    },
    "required": [
      "algorithm"
    ]
  },
  "positional": [
    "algorithm"
  ],
  "description": "Sliding-window / CUSUM / Kalman / Bayesian-blocks burst search on TTTR macro-times. Produces the primary .bur burst table.\n\nParameters are those of `TTTR.burst_search_by_name(algorithm, **params)`: the name of a registered search plus that search's own parameters (the `burst_search` category describes each). Until 2026-08-18 this entry declared `threshold_khz` / `l_min` / `m_min` / `t_window_ms`, which matched no callable in the library -- they were the hand-authored literal's names, so a caller building a call from them got a TypeError. Restricting the search to routing channels is a separate step (`photon_selection`), not a parameter here: the selection happens before the search, and a document that hid it inside the search would replay on the whole stream."
})JSON";
}  // namespace

/// Register this operation's registry entry. Idempotent (a duplicate key is refused).
void register_operation_burst_selection() {
    register_algorithm_json("operation", "burst_selection", kBurstSelectionEntry);
}

void register_operation_bva();
void register_operation_kde_cde();
void register_operation_burst_fusion();

void register_burst_operations() {
    static std::once_flag once;
    std::call_once(once, [] {
        register_operation_burst_selection();
        register_operation_bva();
        register_operation_kde_cde();
        register_operation_burst_fusion();
    });
}

// Registered when this library loads: the searches (description + dispatch)
// and the burst pipeline operations. See Registry.h on why a static consumer
// links the archive whole.
namespace {
const bool kBurstRegistered = (register_builtin_burst_searches(), register_burst_operations(), true);
}

} // namespace tttrlib

std::string TTTR::burst_search_algorithms_json() {
    // The `burst_search` category of the one registry: the seven built-ins
    // register themselves with their dispatch entry (register_builtin_burst_searches),
    // a plugin's search is registered by the plugin host when it loads. Both
    // have to be primed here -- `algorithms_json` only knows the *algorithm*
    // module's own built-ins, and without these calls the category came back
    // empty rather than failing.
    tttrlib::register_builtin_burst_searches();
    tttrlib::PluginHost::ensure_loaded();
    return tttrlib::algorithms_json("burst_search");
}

/*!
 * \brief Run a plugin's burst search over this object's photons.
 *
 * A burst search is nearly a pure function -- arrival times in, index ranges
 * out -- which is why this capability needed no registrar handed down from a
 * higher layer, unlike the fit models: the host can call it directly.
 *
 * The result buffer is the host's, and grows rather than truncating. A search
 * that finds more bursts than it was given room for reports the total it would
 * have written, and is called again with a buffer that fits; silently returning
 * the first N bursts of a measurement would be a wrong answer that looks like a
 * right one.
 */
std::vector<long long> TTTR::burst_search_plugin(
        const std::string& name, const std::string& parameters_json) {
    const tttrlib_burst_search_v1* s = tttrlib::PluginHost::burst_search(name);
    if (s == nullptr) {
        throw std::invalid_argument(
            "no plugin provides the burst search '" + name + "'");
    }

    const std::size_t n = get_n_valid_events();
    const double resolution = header != nullptr ? header->get_macro_time_resolution() : 0.0;
    const char* params = parameters_json.empty() ? nullptr : parameters_json.c_str();

    // Start at a burst per thousand photons, which is generous for real data,
    // and grow only if the search says it needs more.
    std::vector<long long> out(2 * std::max<std::size_t>(64, n / 1000 + 1), 0);
    for (int attempt = 0; attempt < 3; ++attempt) {
        uint64_t n_bursts = 0;
        int status = TTTRLIB_ERROR;
        try {
            status = s->search(s->ctx,
                               reinterpret_cast<const uint64_t*>(macro_times),
                               reinterpret_cast<const int8_t*>(routing_channels),
                               static_cast<uint64_t>(n), resolution, params,
                               reinterpret_cast<int64_t*>(out.data()),
                               static_cast<uint64_t>(out.size() / 2), &n_bursts);
        } catch (...) {
            status = TTTRLIB_ERROR;
        }
        if (status != TTTRLIB_OK) {
            const std::string detail = tttrlib::PluginHost::last_error();
            throw std::runtime_error(
                "plugin burst search '" + name + "' failed" +
                (detail.empty() ? std::string() : " (" + detail + ")"));
        }
        if (n_bursts * 2 <= out.size()) {
            out.resize(static_cast<std::size_t>(n_bursts) * 2);
            return out;
        }
        out.assign(static_cast<std::size_t>(n_bursts) * 2, 0);
    }
    throw std::runtime_error("plugin burst search '" + name +
                             "' kept asking for a larger result buffer");
}

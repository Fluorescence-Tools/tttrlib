// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file FitRegistry.cpp
 * \brief Machine-readable description of the available lifetime fit models.
 *
 * The burst searches advertise their parameters through the registry so a caller
 * can build a form without hard-coding the list (see BurstSearchRegistry.cpp).
 * The Fit2x maximum-likelihood lifetime models — ``Fit23``/``Fit24``/``Fit25``/
 * ``Fit26`` — are advertised here the same way, and for the same reason: a tool
 * offering one to a user (chisurf's burst-MLE wizard hard-codes exactly this list
 * of parameters, defaults and ranges today) needs to know which parameters each
 * takes, their meaning, units, ranges and which are fixed by default.
 *
 * Each ``fit`` entry describes its **optimisable model parameters** under
 * ``params_schema``, in the order they occupy the ``initial_values`` array the
 * fit is called with. Properties use the standard JSON Schema vocabulary
 * (``type``, ``title``, ``description``, ``default``, ``minimum``, ``maximum``)
 * plus two hints a plain schema consumer ignores: ``unit`` and ``fixed_default``
 * (whether the parameter is held fixed unless the user frees it — scatter and
 * anisotropy are not identifiable against an auto-extracted background, so they
 * default to fixed).
 *
 * The **construction / correction inputs** shared by a family of models (``dt``,
 * ``period``, ``g_factor``, ``l1``, ``l2``, ``convolution_stop``, the fitted
 * channel range and the choice of objective) are set once when the fit object is
 * built, not optimised.
 * They live once in the separate ``fit_setup`` category, and every fit entry
 * points at them with ``setup: {category, name}`` — the same declarative-link
 * idea as the burst-search ``parameters_of``, so the shared block is described in
 * one place instead of copied into four entries. A consumer that ignores the key
 * still has a complete model-parameter form.
 *
 * ``name`` is what a caller passes to ``make_decay_fit``. There is no separate
 * ``method``: dispatch goes through the model interface, so the registry key *is*
 * the callable's identity rather than the name of a class to look up.
 *
 * \par The flattening rule (normative)
 * Parameters, setup values and results each cross the C++ boundary as one flat
 * ``double`` array, and this registry is the only description of what each slot
 * means. The mapping is therefore a rule, not a convention, and both sides derive
 * it from the same JSON:
 *
 *   1. Properties are laid out in **declaration order** — the order they appear
 *      in the schema object, which is why the registry is built with
 *      ``nlohmann::ordered_json``.
 *   2. A scalar property occupies **one** slot. ``boolean`` is carried as 0.0 or
 *      1.0; ``integer`` as an exactly representable double.
 *   3. A ``string`` property with an ``enum`` occupies **one** slot holding the
 *      **index of its value within that ``enum``**. The schema stays idiomatic —
 *      a consumer renders a dropdown of names — while the wire stays numeric.
 *   4. An ``array`` property occupies **``count`` contiguous slots** in item
 *      order, where ``count`` comes from the ``count_from`` link described below.
 *   5. Nothing else occupies a slot. In particular a flat array never carries
 *      construction inputs or outputs alongside the optimised parameters — those
 *      are separate arrays with separate schemas.
 *
 * Rule 5 is not hypothetical tidiness. The previous arrangement passed an
 * 8-element vector to a 4-parameter model, with two setup flags and two outputs
 * sharing the array and only a comment to say so.
 *
 * \par Variable-length blocks: ``count_from``
 * Not every model has a fixed parameter count — a multi-exponential model's
 * length follows its number of components, a batched model's follows its number
 * of entries. Such a property is a standard JSON Schema ``array`` with an
 * ``items`` sub-schema plus ``"count_from": "<property>"``, naming a property of
 * the model's **setup** block that holds the entry count. Setup rather than
 * parameters, because the count is fixed when the model is built and is not
 * something the optimiser may vary. This is the same declarative-link idea as
 * ``setup`` and ``parameters_of``, and a consumer that ignores the key still
 * sees a valid JSON Schema array.
 *
 * \par ``results_schema``
 * Results are described exactly like parameters, so a batch result is a matrix
 * whose columns are named by JSON rather than by a comment on a C++ function.
 * Anything sized by the number of micro-time bins is deliberately absent: the
 * fitted curve stays in ``DecayFitProblem::model``, because carrying it per row
 * would make a batch result thousands of columns wide instead of a handful.
 *
 * \par Capability flags
 * ``supports_lnprob`` and ``supports_gradient`` say whether a model can be driven
 * by a sampler and whether it has an analytic gradient. A caller degrades
 * gracefully — falling back to finite differences, or declining to sample —
 * instead of discovering the gap through a runtime failure.
 */
#include <string>

#include "Registry.h"

namespace tttrlib {

namespace {

const char* const kFitRegistry = R"JSON({
  "fit23": {
    "name": "fit23",
    "n_patterns": 0,
    "label": "Single lifetime + anisotropy (Fit23)",
    "summary": "Poisson MLE of one fluorescence lifetime with time-resolved anisotropy.",
    "description": "The single-molecule burst-MLE workhorse. Fits one lifetime to a polarization-resolved (Jordi VV|VH) decay by maximum likelihood, jointly modelling the anisotropy decay so the parallel and perpendicular channels are described together. Scatter (gamma) and anisotropy (r0, rho) are barely identifiable from a short burst decay against an auto-extracted background, so only tau is free by default; free the others when a measured IRF/background makes them meaningful. tau, gamma, r0, rho map to initial_values in that order.",
    "setup": {"category": "fit_setup", "name": "fit2x"},
    "params_schema": {
      "type": "object",
      "required": ["tau", "gamma", "r0", "rho"],
      "properties": {
        "tau": {
          "type": "number", "title": "Lifetime tau (ns)", "default": 2.0,
          "minimum": 0.01, "maximum": 20.0, "unit": "ns", "fixed_default": false,
          "description": "Fluorescence lifetime. The one parameter fit by default."
        },
        "gamma": {
          "type": "number", "title": "Scatter fraction", "default": 0.1,
          "minimum": 0.0, "maximum": 1.0, "fixed_default": true,
          "description": "Fraction of the signal explained by the background/scatter pattern rather than the decay. Not identifiable against an auto-extracted (scatter-shaped) background, so fixed by default."
        },
        "r0": {
          "type": "number", "title": "Fundamental anisotropy r0", "default": 0.38,
          "minimum": -0.2, "maximum": 0.4, "fixed_default": true,
          "description": "Anisotropy at time zero, set by the dye's absorption/emission dipole angle (0.4 for parallel dipoles). A known photophysical constant, so fixed by default."
        },
        "rho": {
          "type": "number", "title": "Rotational correlation time rho (ns)",
          "default": 1.22, "minimum": 0.01, "maximum": 100.0, "unit": "ns",
          "fixed_default": true,
          "description": "Rotational correlation time of the anisotropy decay. Left free against a short burst decay it rails to zero, so fixed by default; free it with a measured IRF/background to fit anisotropy."
        }
      }
    },
    "supports_lnprob": true,
    "supports_gradient": false,
    "results_schema": {
      "type": "object",
      "properties": {
        "twoIstar": {
          "type": "number", "title": "2I*",
          "description": "Goodness of the optimised fit: -2 ln(L(C|M)/L(C|C)), the Poisson deviance against a perfectly fitting model. Around 1 for a good fit to counting data; large values mean the model cannot describe the decay."
        },
        "converged": {
          "type": "boolean", "title": "Converged",
          "description": "Whether the optimiser reached its tolerance rather than stopping on the iteration limit. Carried as 0.0 or 1.0 in the flat result vector."
        },
        "iterations": {
          "type": "integer", "title": "Iterations",
          "description": "Objective evaluations the optimiser needed. A row that ran to the limit is worth looking at even when its 2I* looks acceptable."
        },
        "r_scatter": {
          "type": "number", "title": "Anisotropy (scatter-corrected)",
          "description": "Steady-state anisotropy computed from the integrated signals after removing the background/scatter contribution."
        },
        "r_experimental": {
          "type": "number", "title": "Anisotropy (uncorrected)",
          "description": "Steady-state anisotropy computed from the raw integrated signals, without background subtraction."
        }
      }
    }
  },
  "fit24": {
    "name": "fit24",
    "n_patterns": 0,
    "label": "Bi-exponential (Fit24)",
    "summary": "Poisson MLE of two lifetimes with a mixing fraction, scatter and a constant offset.",
    "description": "Two-lifetime maximum-likelihood fit for a decay that a single exponential cannot describe — a mixture of two states (e.g. FRET and no-FRET donor populations) with distinct lifetimes. The second component's amplitude fraction A2, a scatter fraction gamma and a constant offset are fit alongside the two lifetimes. Parameters map to initial_values as [tau1, gamma, tau2, A2, offset].",
    "setup": {"category": "fit_setup", "name": "fit2x"},
    "params_schema": {
      "type": "object",
      "required": ["tau1", "gamma", "tau2", "A2", "offset"],
      "properties": {
        "tau1": {
          "type": "number", "title": "Lifetime 1 tau1 (ns)", "default": 1.0,
          "minimum": 0.01, "maximum": 20.0, "unit": "ns", "fixed_default": false,
          "description": "First (shorter) fluorescence lifetime."
        },
        "gamma": {
          "type": "number", "title": "Scatter fraction", "default": 0.0,
          "minimum": 0.0, "maximum": 1.0, "fixed_default": false,
          "description": "Fraction of the signal explained by the background/scatter pattern."
        },
        "tau2": {
          "type": "number", "title": "Lifetime 2 tau2 (ns)", "default": 4.0,
          "minimum": 0.01, "maximum": 20.0, "unit": "ns", "fixed_default": false,
          "description": "Second (longer) fluorescence lifetime."
        },
        "A2": {
          "type": "number", "title": "Fraction of tau2", "default": 0.5,
          "minimum": 0.0, "maximum": 1.0, "fixed_default": false,
          "description": "Amplitude fraction of the second lifetime component."
        },
        "offset": {
          "type": "number", "title": "Constant offset", "default": 0.0,
          "minimum": 0.0, "maximum": 1e6, "fixed_default": true,
          "description": "Flat baseline added to every channel (uncorrelated dark counts)."
        }
      }
    },
    "supports_lnprob": true,
    "supports_gradient": false,
    "results_schema": {
      "type": "object",
      "properties": {
        "twoIstar": {
          "type": "number", "title": "2I*",
          "description": "Poisson deviance of the optimised fit against a perfectly fitting model."
        },
        "converged": {
          "type": "boolean", "title": "Converged",
          "description": "Whether the optimiser reached its tolerance. Carried as 0.0 or 1.0."
        },
        "iterations": {
          "type": "integer", "title": "Iterations",
          "description": "Objective evaluations the optimiser needed."
        },
        "r_scatter": {
          "type": "number", "title": "Anisotropy (scatter-corrected)",
          "description": "Steady-state anisotropy from the integrated signals with the background/scatter contribution removed."
        },
        "r_experimental": {
          "type": "number", "title": "Anisotropy (uncorrected)",
          "description": "Steady-state anisotropy from the raw integrated signals."
        }
      }
    }
  },
  "fit25": {
    "name": "fit25",
    "n_patterns": 0,
    "label": "Best of four fixed lifetimes (Fit25)",
    "summary": "Selects which of four fixed lifetimes best describes the decay.",
    "description": "A discrete selection rather than a continuous fit: the four lifetimes are held fixed, each is scored against the data, and the one best describing the decay is returned (with its scatter fraction). Useful when the sample is known to occupy one of a few discrete states and the goal is to classify rather than to measure a continuous lifetime. Parameters map to initial_values as [tau1, tau2, tau3, tau4, gamma, r0]; the four lifetimes are always fixed.",
    "setup": {"category": "fit_setup", "name": "fit2x"},
    "params_schema": {
      "type": "object",
      "required": ["tau1", "tau2", "tau3", "tau4", "gamma", "r0"],
      "properties": {
        "tau1": {
          "type": "number", "title": "Candidate lifetime 1 (ns)", "default": 0.5,
          "minimum": 0.01, "maximum": 20.0, "unit": "ns", "fixed_default": true,
          "description": "First candidate lifetime (always fixed)."
        },
        "tau2": {
          "type": "number", "title": "Candidate lifetime 2 (ns)", "default": 1.5,
          "minimum": 0.01, "maximum": 20.0, "unit": "ns", "fixed_default": true,
          "description": "Second candidate lifetime (always fixed)."
        },
        "tau3": {
          "type": "number", "title": "Candidate lifetime 3 (ns)", "default": 2.5,
          "minimum": 0.01, "maximum": 20.0, "unit": "ns", "fixed_default": true,
          "description": "Third candidate lifetime (always fixed)."
        },
        "tau4": {
          "type": "number", "title": "Candidate lifetime 4 (ns)", "default": 4.0,
          "minimum": 0.01, "maximum": 20.0, "unit": "ns", "fixed_default": true,
          "description": "Fourth candidate lifetime (always fixed)."
        },
        "gamma": {
          "type": "number", "title": "Scatter fraction", "default": 0.0,
          "minimum": 0.0, "maximum": 1.0, "fixed_default": false,
          "description": "Fraction of the signal explained by the background/scatter pattern."
        },
        "r0": {
          "type": "number", "title": "Fundamental anisotropy r0", "default": 0.38,
          "minimum": 0.0, "maximum": 0.4, "fixed_default": true,
          "description": "Fundamental anisotropy (fixed input; part of the initial_values vector)."
        }
      }
    },
    "supports_lnprob": true,
    "supports_gradient": false,
    "results_schema": {
      "type": "object",
      "properties": {
        "twoIstar": {
          "type": "number", "title": "2I*",
          "description": "Poisson deviance of the winning candidate against a perfectly fitting model."
        },
        "converged": {
          "type": "boolean", "title": "Converged",
          "description": "Whether the scatter optimisation for the winning candidate reached its tolerance. Carried as 0.0 or 1.0."
        },
        "iterations": {
          "type": "integer", "title": "Iterations",
          "description": "Objective evaluations summed over the candidates scored."
        },
        "selected_index": {
          "type": "integer", "title": "Winning candidate",
          "description": "Which of the four candidate lifetimes best described the decay, as a 0-based index. This model classifies rather than measures, so the index is the answer; previously it could only be recovered by comparing the returned lifetime against the four inputs."
        },
        "r_scatter": {
          "type": "number", "title": "Anisotropy (scatter-corrected)",
          "description": "Steady-state anisotropy with the background/scatter contribution removed."
        },
        "r_experimental": {
          "type": "number", "title": "Anisotropy (uncorrected)",
          "description": "Steady-state anisotropy from the raw integrated signals."
        }
      }
    }
  },
  "fit26": {
    "name": "fit26",
    "n_patterns": 2,
    "label": "Two-pattern mixture (Fit26)",
    "summary": "Fits the mixing fraction between two fixed reference patterns.",
    "description": "A one-parameter mixture: the model is a linear combination of two fixed reference decay patterns and only their mixing fraction x1 is fit. Used for species fractioning when the pure-component decays are known (e.g. two conformational states measured separately). The single parameter maps to initial_values as [x1].",
    "setup": {"category": "fit_setup", "name": "fit2x"},
    "params_schema": {
      "type": "object",
      "required": ["x1"],
      "properties": {
        "x1": {
          "type": "number", "title": "Fraction of pattern 1", "default": 0.5,
          "minimum": 0.0, "maximum": 1.0, "fixed_default": false,
          "description": "Amplitude fraction of the first reference pattern; the second is 1 - x1."
        }
      }
    },
    "supports_lnprob": true,
    "supports_gradient": false,
    "results_schema": {
      "type": "object",
      "properties": {
        "twoIstar": {
          "type": "number", "title": "2I*",
          "description": "Poisson deviance of the optimised mixture against a perfectly fitting model."
        },
        "converged": {
          "type": "boolean", "title": "Converged",
          "description": "Whether the optimiser reached its tolerance. Carried as 0.0 or 1.0."
        },
        "iterations": {
          "type": "integer", "title": "Iterations",
          "description": "Objective evaluations the optimiser needed."
        }
      }
    }
  },
  "fit_nexp": {
    "name": "fit_nexp",
    "n_patterns": 0,
    "label": "Multi-exponential reconvolution (N-exp)",
    "summary": "Poisson MLE of any number of lifetimes, with amplitudes profiled by EM.",
    "description": "General one- or multi-exponential reconvolution fit. Lifetimes are found by deterministic coordinate-wise Brent minimisation over a log-spaced grid of starting points; at every lifetime trial the nonnegative amplitudes and the background fraction are profiled by expectation-maximisation, so they are never searched over directly. Data may be one channel or a polarisation-resolved pair sharing one temporal shape, in which case the channels are pooled as exact sufficient statistics while each keeps its own profiled total. Set 'tail_start' in the setup to fit the tail without reconvolution, which is the usual treatment of a sensitised-emission decay whose rise is not a simple instrument response.",
    "setup": {"category": "fit_setup", "name": "nexp"},
    "params_schema": {
      "type": "object",
      "required": ["lifetimes", "amplitudes"],
      "properties": {
        "lifetimes": {
          "type": "array", "title": "Lifetimes (ns)",
          "count_from": "n_exponentials",
          "items": {
            "type": "number", "default": 2.0, "minimum": 1e-3, "maximum": 100.0,
            "unit": "ns", "fixed_default": false,
            "description": "One fluorescence lifetime. Fix a lifetime to fit a known component's amplitude only."
          },
          "description": "The exponential lifetimes, as many as 'n_exponentials' in the setup."
        },
        "amplitudes": {
          "type": "array", "title": "Amplitudes",
          "count_from": "n_exponentials",
          "items": {
            "type": "number", "default": 1.0, "minimum": 0.0, "maximum": 1e12,
            "fixed_default": true,
            "description": "Starting amplitude of the matching lifetime. Profiled by EM rather than searched, so these are starting values, not free parameters in the usual sense."
          },
          "description": "Starting amplitudes, one per lifetime."
        }
      }
    },
    "supports_lnprob": true,
    "supports_gradient": false,
    "results_schema": {
      "type": "object",
      "properties": {
        "twoIstar": {
          "type": "number", "title": "2I*",
          "description": "Poisson deviance of the optimised fit against a perfectly fitting model."
        },
        "converged": {
          "type": "boolean", "title": "Converged",
          "description": "Whether the outer lifetime search met its tolerance. Carried as 0.0 or 1.0."
        },
        "iterations": {
          "type": "integer", "title": "Outer iterations",
          "description": "Coordinate sweeps over the lifetimes."
        },
        "negative_log_likelihood": {
          "type": "number", "title": "-ln L",
          "description": "Profile shape negative log likelihood. Terms depending only on the data are omitted, so it is comparable between fits of the same data and not otherwise."
        },
        "photon_count": {
          "type": "number", "title": "Photons",
          "description": "Total counts the fit was scored against."
        },
        "background_amplitude": {
          "type": "number", "title": "Background amplitude",
          "description": "Profiled amplitude of the background pattern."
        },
        "em_iterations": {
          "type": "integer", "title": "EM iterations",
          "description": "Amplitude-profiling iterations at the final lifetimes."
        }
      }
    }
  }
})JSON";

const char* const kFitSetupRegistry = R"JSON({
  "fit2x": {
    "name": "fit2x",
    "label": "Fit2x construction inputs",
    "summary": "Inputs set once when a Fit2x model is built (not optimised).",
    "description": "The instrument description and correction factors shared by every Fit2x model (Fit23/24/25/26). Supplied to the model constructor; the IRF and background are Jordi (VV|VH) histograms the same length as the data. Referenced from each fit entry's 'setup' link.",
    "params_schema": {
      "type": "object",
      "required": ["dt", "period", "g_factor", "l1", "l2",
                   "convolution_stop", "soft_bifl_scatter_flag",
                   "p2s_twoIstar_flag"],
      "properties": {
        "dt": {
          "type": "number", "title": "Micro-time bin width (ns)", "default": 0.032,
          "minimum": 1e-6, "maximum": 1e3, "unit": "ns",
          "description": "Time width of one micro-time channel. tau is reported in these units, so it must be in nanoseconds for the lifetime to be in nanoseconds."
        },
        "period": {
          "type": "number", "title": "Excitation period (ns)", "default": 13.5,
          "minimum": 1e-3, "maximum": 1e4, "unit": "ns",
          "description": "Time between excitation pulses. The decay is convolved over one period (wrap-around), so a lifetime longer than the period cannot be measured."
        },
        "g_factor": {
          "type": "number", "title": "G-factor", "default": 1.0,
          "minimum": 0.0, "maximum": 100.0,
          "description": "Detection-efficiency ratio between the parallel and perpendicular channels, used only for the anisotropy."
        },
        "l1": {
          "type": "number", "title": "Mixing l1", "default": 0.0,
          "minimum": 0.0, "maximum": 1.0, "advanced": true,
          "description": "Depolarisation/mixing correction between the parallel and perpendicular detection channels."
        },
        "l2": {
          "type": "number", "title": "Mixing l2", "default": 0.0,
          "minimum": 0.0, "maximum": 1.0, "advanced": true,
          "description": "Second depolarisation/mixing correction factor."
        },
        "convolution_stop": {
          "type": "integer", "title": "Convolution stop (channel)", "default": -1,
          "minimum": -1, "maximum": 1000000, "advanced": true,
          "description": "Last micro-time channel included in the convolution. -1 uses the full IRF length."
        },
        "soft_bifl_scatter_flag": {
          "type": "boolean", "title": "Discount background photons", "default": true,
          "description": "When true the reported score is reduced by the background photon contribution (background photons carry no lifetime information)."
        },
        "objective": {
          "type": "string", "title": "Objective", "default": "poisson_mle",
          "enum": ["poisson_mle", "p2s_mle", "neyman_lsq", "gehrels_lsq"],
          "entries_of": {"category": "objective"},
          "description": "Which statistic the fit minimises, by name from the 'objective' category. 'p2s_mle' scores the anisotropy-free sum P + 2S; the default scores the parallel and perpendicular channels individually in a global fit. Replaces the former p2s_twoIstar_flag, so that adding a statistic does not mean adding a flag to every model."
        },
        "fit_start": {
          "type": "integer", "title": "First fitted channel", "default": 0,
          "minimum": 0, "maximum": 1000000, "advanced": true,
          "description": "First micro-time channel included in the objective. Non-zero makes this a tail fit."
        },
        "fit_stop": {
          "type": "integer", "title": "Last fitted channel", "default": -1,
          "minimum": -1, "maximum": 1000000, "advanced": true,
          "description": "One past the last micro-time channel included in the objective; -1 fits to the end."
        }
      }
    }
  },
  "nexp": {
    "name": "nexp",
    "label": "Multi-exponential construction inputs",
    "summary": "Inputs set once when a multi-exponential model is built.",
    "description": "Instrument description, component count and search controls for the N-exponential reconvolution fit. 'n_exponentials' is what the model's variable-length lifetime and amplitude arrays take their length from, via their 'count_from' link.",
    "params_schema": {
      "type": "object",
      "required": ["dt", "n_exponentials"],
      "properties": {
        "dt": {
          "type": "number", "title": "Micro-time bin width (ns)", "default": 0.032,
          "minimum": 1e-6, "maximum": 1e3, "unit": "ns",
          "description": "Time width of one micro-time channel; lifetimes are reported in these units."
        },
        "n_exponentials": {
          "type": "integer", "title": "Number of components", "default": 1,
          "minimum": 1, "maximum": 32,
          "description": "How many exponential components the model has. The lifetime and amplitude parameter arrays each hold this many entries — this is the property their 'count_from' names."
        },
        "period": {
          "type": "number", "title": "Excitation period (ns)", "default": 0.0,
          "minimum": 0.0, "maximum": 1e4, "unit": "ns",
          "description": "Time between excitation pulses; 0 disables the periodic wrap-around."
        },
        "convolution_stop": {
          "type": "integer", "title": "Convolution stop (channel)", "default": -1,
          "minimum": -1, "maximum": 1000000, "advanced": true,
          "description": "Last channel included in the convolution; -1 uses the full IRF length."
        },
        "tail_start": {
          "type": "integer", "title": "Tail-fit start (channel)", "default": -1,
          "minimum": -1, "maximum": 1000000,
          "description": "When >= 0 the fit is a tail fit: each component is a pure decay from this channel with no IRF reconvolution, and earlier channels are excluded. The standard treatment of a sensitised-emission decay, whose rise is not a simple instrument response. -1 keeps the normal reconvolution fit."
        },
        "tau_min": {
          "type": "number", "title": "Shortest allowed lifetime (ns)", "default": 1e-3,
          "minimum": 1e-9, "maximum": 1e3, "unit": "ns", "advanced": true,
          "description": "Lower bound of the lifetime search."
        },
        "tau_max": {
          "type": "number", "title": "Longest allowed lifetime (ns)", "default": 100.0,
          "minimum": 1e-6, "maximum": 1e6, "unit": "ns", "advanced": true,
          "description": "Upper bound of the lifetime search."
        },
        "coordinate_grid_intervals": {
          "type": "integer", "title": "Search grid intervals", "default": 24,
          "minimum": 1, "maximum": 1024, "advanced": true,
          "description": "Log-spaced starting points per lifetime. Higher is more robust against local minima and linearly slower; a well-conditioned 1-2 component fit with a decent initial guess is usually fine at 8."
        },
        "max_outer_iterations": {
          "type": "integer", "title": "Max lifetime sweeps", "default": 20,
          "minimum": 1, "maximum": 10000, "advanced": true,
          "description": "Cap on coordinate sweeps over the lifetimes."
        },
        "max_em_iterations": {
          "type": "integer", "title": "Max EM iterations", "default": 500,
          "minimum": 1, "maximum": 100000, "advanced": true,
          "description": "Cap on amplitude-profiling iterations per lifetime trial."
        },
        "initial_background_fraction": {
          "type": "number", "title": "Initial background fraction", "default": 0.01,
          "minimum": 0.0, "maximum": 1.0, "advanced": true,
          "description": "Starting share of the counts attributed to the background pattern."
        },
        "fit_start": {
          "type": "integer", "title": "First fitted channel", "default": 0,
          "minimum": 0, "maximum": 1000000, "advanced": true,
          "description": "First micro-time channel included in the objective."
        },
        "fit_stop": {
          "type": "integer", "title": "Last fitted channel", "default": -1,
          "minimum": -1, "maximum": 1000000, "advanced": true,
          "description": "One past the last channel included in the objective; -1 fits to the end."
        }
      }
    }
  }
})JSON";

/*!
 * The selectable objectives, as a registry category.
 *
 * Which statistic a fit minimises used to be two boolean flags smuggled into the
 * parameter vector (`soft_bifl_scatter`, `p2s_twoIstar`), which meant a new
 * statistic could not be added without changing every model that might use it.
 * Naming them here makes the choice a setup value like any other: a model reads
 * one slot, and a caller picks from a list it can render.
 */
const char* const kObjectiveRegistry = R"JSON({
  "poisson_mle": {
    "name": "poisson_mle",
    "label": "Poisson maximum likelihood (2I*)",
    "summary": "The counting-statistics likelihood; the right default for photon data.",
    "description": "Minimises the Poisson deviance 2I* = -2 ln(L(C|M)/L(C|C)), which compares the model against a hypothetical perfectly fitting one. Correct at every count level including empty channels, where a chi-squared weighted by the data is undefined and one weighted by the model is biased. This is what a TCSPC decay should normally be fitted with."
  },
  "p2s_mle": {
    "name": "p2s_mle",
    "label": "Poisson MLE on the P+2S sum",
    "summary": "Scores the summed decay rather than the two channels separately.",
    "description": "Forms the anisotropy-free sum P + 2S from the parallel and perpendicular channels and applies the Poisson deviance to it. Removes the anisotropy from the objective entirely, which is what you want when the rotational correlation time is a nuisance rather than a measurement. The alternative is to score the two channels individually in a global fit."
  },
  "neyman_lsq": {
    "name": "neyman_lsq",
    "label": "Least squares, data-weighted (Neyman)",
    "summary": "Chi-squared weighted by the observed counts.",
    "description": "Weights each channel by 1/max(1, C). Fast and familiar, but biased low at small counts because a channel that happens to fluctuate down is given more weight. Use it for well-populated decays, or for comparison with historical fits; prefer the Poisson likelihood otherwise."
  },
  "gehrels_lsq": {
    "name": "gehrels_lsq",
    "label": "Least squares, Gehrels-weighted",
    "summary": "Chi-squared with a small-count correction to the variance.",
    "description": "Weights by an approximation to the Poisson confidence interval rather than by the raw count, which keeps a least-squares fit usable where the counts are low enough that Neyman weighting visibly biases the result. A pragmatic middle ground when a least-squares optimiser is required but the data are sparse."
  }
})JSON";

} // namespace

std::string fit_models_json() {
    return std::string(kFitRegistry);
}

std::string fit_setup_json() {
    return std::string(kFitSetupRegistry);
}

std::string fit_objectives_json() {
    return std::string(kObjectiveRegistry);
}

} // namespace tttrlib

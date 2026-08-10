// SPDX-License-Identifier: BSD-3-Clause

#include "Registry.h"

#include <nlohmann/json.hpp>

namespace tttrlib {

namespace {
using json = nlohmann::ordered_json;

/*!
 * \brief Pipeline operation catalog for .pto-mfdb provenance.
 *
 * Each entry describes one analysis step that can appear in a burst pipeline:
 * its operation_type (matching ``_mmfdb_operation.operation_type`` in PTO
 * tags), its inputs, its outputs (column names matching mmfdb.dic), its
 * data_format, row_grain, and a settings schema.
 *
 * ``data_format`` is **storage**, and for every entry here it is ``dstore``:
 * that is what a ``.pto`` artifact of this operation actually carries. It used
 * to name the legacy companion suffix -- ``bg4``, ``bv4``, ``2c4`` -- which is
 * wrong twice over. Those are not storage formats but statements about what a
 * table *is*, which is ``operation_type``'s job; and an object inside a
 * container has no suffix at all. mmfdb declares none of them, and its
 * dictionary is the authority (see test/python/test_vocabulary_matches_mmfdb.py).
 * Do not re-add them.
 *
 * This is the machine-readable contract between:
 *  - the compiled ``tttr`` CLI that writes .pto artifacts,
 *  - chiSurf plugins that read/produce the same artifacts,
 *  - ndx that consumes the output columns, and
 *  - the provenance reader that replays a pipeline from a .pto.
 *
 * A .pto container's provenance graph names operations by operation_type;
 * this registry tells a consumer what each operation consumes and produces,
 * so it can render a processing list, validate completeness, or jump to a
 * specific step for re-execution.
 */
const char* kOperationRegistry = R"JSON({
  "burst_selection": {
    "name": "burst_selection",
    "label": "Burst search and selection",
    "summary": "Sliding-window / CUSUM / Kalman / Bayesian-blocks burst search on TTTR macro-times. Produces the primary .bur burst table.",
    "operation_type": "burst_selection",
    "data_format": "dstore",
    "row_grain": "burst",
    "kind": "burst_table",
    "inputs": {
      "required": ["tttr_photon_stream"],
      "description": "Raw TTTR photon stream (macro + micro times, routing channels)."
    },
    "outputs": {
      "columns": [
        "First Photon", "Last Photon",
        "Duration (ms)", "Duration (green) (ms)", "Duration (red) (ms)",
        "Mean Macro Time (ms)", "Mean Macro Time (green) (ms)", "Mean Macro Time (red) (ms)",
        "Number of Photons", "Number of Photons (green)", "Number of Photons (red)",
        "Count Rate (KHz)", "Green Count Rate (KHz)", "Red Count Rate (KHz)",
        "Mean Microtime (green) (ns)", "Mean Microtime (red) (ns)",
        "Proximity Ratio"
      ]
    },
    "settings_schema": {
      "type": "object",
      "properties": {
        "threshold_khz": {"type": "number", "default": 30.0, "unit": "kHz"},
        "l_min": {"type": "integer", "default": 30, "minimum": 1},
        "m_min": {"type": "integer", "default": 5, "minimum": 1},
        "t_window_ms": {"type": "number", "default": 0.5, "unit": "ms"},
        "routing_channels": {"type": "array", "items": {"type": "integer"}, "default": [0, 1]},
        "microtime_ranges": {"type": "array", "items": {"type": "array", "items": {"type": "integer"}}, "default": [[0, 4096]]}
      }
    },
    "can_replay": true
  },

  "tcspc_calibration": {
    "name": "tcspc_calibration",
    "label": "IRF and background extraction",
    "summary": "Extracts the instrument response function (IRF) and background rate from non-burst photons per detector channel. The IRF curve is used by MLE lifetime fitting.",
    "operation_type": "calibration",
    "data_format": "dstore",
    "row_grain": "curve_point",
    "kind": "irf_curve",
    "inputs": {
      "required": ["tttr_photon_stream"],
      "optional": ["burst_selection"],
      "description": "Photon stream plus burst indices to mask out; non-burst photons form the IRF."
    },
    "outputs": {
      "columns": ["x_ns", "counts", "counts_raw"]
    },
    "settings_schema": {
      "type": "object",
      "properties": {
        "channel": {"type": "integer", "description": "Routing channel number"},
        "color": {"type": "string", "enum": ["green", "red"]},
        "source": {"type": "string", "default": "non_burst_photons", "enum": ["non_burst_photons", "synthetic", "scatter_species"]},
        "baseline_quantile": {"type": "number", "default": 0.2, "minimum": 0.0, "maximum": 1.0}
      }
    },
    "can_replay": true
  },

  "mle_green": {
    "name": "mle_green",
    "label": "MLE lifetime fitting (green channel)",
    "summary": "Burst-wise maximum-likelihood estimation of fluorescence lifetime and anisotropy on the green (donor) detector channel using Fit2x. Failed fits produce NaN in all result columns and MLE Fitted = 0.",
    "operation_type": "burst_lifetime_fitting",
    "data_format": "dstore",
    "row_grain": "burst",
    "kind": "burst_table",
    "inputs": {
      "required": ["tttr_photon_stream", "burst_selection"],
      "optional": ["tcspc_calibration"],
      "description": "Photon stream for micro-time histograms, burst indices for per-burst slicing, and the IRF curve for deconvolution."
    },
    "outputs": {
      "columns": [
        "Ng-p-all", "Ng-s-all",
        "Number of Photons (fit window) (green)",
        "2I*  (green)", "Tau (green)", "gamma (green)", "r0 (green)", "rho (green)",
        "r Scatter (green)", "r Experimental (green)",
        "BIFL scatter? (green)", "2I*: P+2S? (green)", "MLE Fitted (green)"
      ]
    },
    "settings_schema": {
      "type": "object",
      "properties": {
        "model": {"type": "string", "default": "fit23", "enum": ["fit23", "fit24", "fit25", "fit26", "fit_nexp"]},
        "channel": {"type": "integer", "default": 0},
        "color": {"type": "string", "default": "green"},
        "n_bins": {"type": "integer", "default": 4096},
        "period_ns": {"type": "number", "default": 32.0, "unit": "ns"},
        "g_factor": {"type": "number", "default": 1.0},
        "l1": {"type": "number", "default": 0.0},
        "l2": {"type": "number", "default": 0.0},
        "tau_init_ns": {"type": "number", "default": 3.8, "unit": "ns"},
        "gamma": {"type": "number", "default": 1.0},
        "r0": {"type": "number", "default": 0.38},
        "rho": {"type": "number", "default": 1.2, "unit": "ns"}
      }
    },
    "can_replay": true,
    "fit_model_link": {"category": "fit", "selector": "model"}
  },

  "mle_red": {
    "name": "mle_red",
    "label": "MLE lifetime fitting (red channel)",
    "summary": "Burst-wise Fit2x MLE on the red (acceptor) detector channel. Same contract as mle_green.",
    "operation_type": "burst_lifetime_fitting",
    "data_format": "dstore",
    "row_grain": "burst",
    "kind": "burst_table",
    "inputs": {
      "required": ["tttr_photon_stream", "burst_selection"],
      "optional": ["tcspc_calibration"],
      "description": "Photon stream, burst indices, and IRF curve for the red channel."
    },
    "outputs": {
      "columns": [
        "Ng-p-all", "Ng-s-all",
        "Number of Photons (fit window) (red)",
        "2I*  (red)", "Tau (red)", "gamma (red)", "r0 (red)", "rho (red)",
        "r Scatter (red)", "r Experimental (red)",
        "BIFL scatter? (red)", "2I*: P+2S? (red)", "MLE Fitted (red)"
      ]
    },
    "settings_schema": {
      "type": "object",
      "properties": {
        "model": {"type": "string", "default": "fit23"},
        "channel": {"type": "integer", "default": 1},
        "color": {"type": "string", "default": "red"},
        "n_bins": {"type": "integer", "default": 4096},
        "period_ns": {"type": "number", "default": 32.0, "unit": "ns"},
        "g_factor": {"type": "number", "default": 1.0},
        "l1": {"type": "number", "default": 0.0},
        "l2": {"type": "number", "default": 0.0},
        "tau_init_ns": {"type": "number", "default": 1.6, "unit": "ns"},
        "gamma": {"type": "number", "default": 1.0},
        "r0": {"type": "number", "default": 0.38},
        "rho": {"type": "number", "default": 1.2, "unit": "ns"}
      }
    },
    "can_replay": true,
    "fit_model_link": {"category": "fit", "selector": "model"}
  },

  "bva": {
    "name": "bva",
    "label": "Burst Variance Analysis",
    "summary": "BVA (Hoffmann et al.): slices each burst into time or photon windows, computes the proximity ratio per window, reports per-burst mean and standard deviation. Compares against the shot-noise static line.",
    "operation_type": "burst_variance_analysis",
    "data_format": "dstore",
    "row_grain": "burst",
    "kind": "burst_table",
    "inputs": {
      "required": ["tttr_photon_stream", "burst_selection"],
      "description": "Photon stream for per-window photon colours, burst indices for slicing."
    },
    "outputs": {
      "columns": ["Proximity Ratio Mean", "Proximity Ratio Std"]
    },
    "settings_schema": {
      "type": "object",
      "properties": {
        "win_size": {"type": "integer", "default": 5, "minimum": 1},
        "n_subbursts": {"type": "integer", "default": 10, "minimum": 1}
      }
    },
    "can_replay": true
  },

  "kde_cde": {
    "name": "kde_cde",
    "label": "KDE 2CDE / ALEX-2CDE",
    "summary": "Kernel-density estimate (Laplace or Gaussian) of donor/acceptor photon arrival times per burst (Tomov et al., 2012). A static burst scores ~100; dynamic bursts deviate.",
    "operation_type": "burst_2cde",
    "data_format": "dstore",
    "row_grain": "burst",
    "kind": "burst_table",
    "inputs": {
      "required": ["tttr_photon_stream", "burst_selection"],
      "description": "Photon stream for per-photon arrival times and colours."
    },
    "outputs": {
      "columns": ["FRET-2CDE", "ALEX-2CDE"]
    },
    "settings_schema": {
      "type": "object",
      "properties": {
        "kernel": {"type": "string", "default": "gaussian", "enum": ["gaussian", "laplace"]},
        "bandwidth": {"type": "number", "default": 0.05, "minimum": 0.001}
      }
    },
    "can_replay": true
  },

  "burst_fusion": {
    "name": "burst_fusion",
    "label": "Recurrence burst fusion",
    "summary": "Estimates same-molecule probability from inter-burst time gaps and fuses bursts from the same molecule passage (Hoffmann et al.).",
    "operation_type": "burst_fusion",
    "data_format": "dstore",
    "row_grain": "burst",
    "kind": "burst_table",
    "inputs": {
      "required": ["burst_selection"],
      "description": "Burst table with macro-time gaps."
    },
    "outputs": {
      "columns": ["Fused Bursts", "Fused Gap Photons"]
    },
    "settings_schema": {
      "type": "object",
      "properties": {
        "max_lag_ms": {"type": "number", "default": 10.0, "unit": "ms"}
      }
    },
    "can_replay": true
  },

  "burst_fcs": {
    "name": "burst_fcs",
    "label": "Burst-wise FCS",
    "summary": "Per-burst autocorrelation/cross-correlation curves fitted for diffusion time. Produces td4 companion.",
    "operation_type": "burst_correlation",
    "data_format": "dstore",
    "row_grain": "burst",
    "kind": "burst_table",
    "inputs": {
      "required": ["tttr_photon_stream", "burst_selection"],
      "description": "Photon stream for correlation, burst indices for per-burst correlation."
    },
    "outputs": {
      "columns": ["Burst Index", "td_mean", "td_peak"]
    },
    "settings_schema": {
      "type": "object",
      "properties": {
        "fit_model": {"type": "string", "default": "3d_gaussian", "enum": ["3d_gaussian", "maxent"]}
      }
    },
    "can_replay": true
  }
})JSON";

std::string operation_entries() {
    return std::string(kOperationRegistry);
}

} // namespace

std::string operation_registry_json() {
    return operation_entries();
}

} // namespace tttrlib

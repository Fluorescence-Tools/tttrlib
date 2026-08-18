// SPDX-License-Identifier: BSD-3-Clause
#include "DecayFitDescriptors.h"

#include "AlgorithmRegistry.h"

#include <mutex>

// Declared next to the entries they register (see DecayFitDescriptors.h).
void register_fit_descriptors_fit2x();
void register_fit_descriptors_nexp();
namespace tttrlib { void register_objective_descriptors(); }

namespace tttrlib {

namespace { void register_decay_operations(); }

void register_decay_descriptors() {
    static std::once_flag once;
    std::call_once(once, [] {
        register_fit_descriptors_fit2x();
        register_fit_descriptors_nexp();
        register_objective_descriptors();
        register_decay_operations();
    });
}

// ---- registry("operation") entries the decay module performs ---------------
//
// The burst pipeline's IRF/background extraction and per-detector burst-MLE
// steps (driven by `tttr sm`, modules/cli/src/cmd_sm.cpp). `mle_green` and
// `mle_red` are one operation type, `burst_lifetime_fitting`, applied to two
// detectors of the default two-colour setup; a setup with other detector
// names runs the same fit under those names.
namespace {
const char* const kTcspcCalibrationEntry = R"JSON({
  "name": "tcspc_calibration",
  "label": "IRF and background extraction",
  "summary": "Extracts the instrument response function (IRF) and background rate from non-burst photons per detector channel. The IRF curve is used by MLE lifetime fitting.",
  "operation_type": "calibration",
  "data_format": "dstore",
  "row_grain": "curve_point",
  "kind": "irf_curve",
  "inputs": {
    "required": [
      "tttr_photon_stream"
    ],
    "optional": [
      "burst_selection"
    ],
    "description": "Photon stream plus burst indices to mask out; non-burst photons form the IRF."
  },
  "outputs": {
    "columns": [
      "x_ns",
      "counts",
      "counts_raw"
    ]
  },
  "settings_schema": {
    "type": "object",
    "properties": {
      "channel": {
        "type": "integer",
        "description": "Routing channel number"
      },
      "color": {
        "type": "string",
        "enum": [
          "green",
          "red"
        ]
      },
      "source": {
        "type": "string",
        "default": "non_burst_photons",
        "enum": [
          "non_burst_photons",
          "synthetic",
          "scatter_species"
        ]
      },
      "baseline_quantile": {
        "type": "number",
        "default": 0.2,
        "minimum": 0.0,
        "maximum": 1.0
      }
    }
  },
  "can_replay": true
})JSON";
const char* const kMleGreenEntry = R"JSON({
  "name": "mle_green",
  "label": "MLE lifetime fitting (green channel)",
  "summary": "Burst-wise maximum-likelihood estimation of fluorescence lifetime and anisotropy on the green (donor) detector channel using Fit2x. Failed fits produce NaN in all result columns and MLE Fitted = 0.",
  "operation_type": "burst_lifetime_fitting",
  "data_format": "dstore",
  "row_grain": "burst",
  "kind": "burst_table",
  "inputs": {
    "required": [
      "tttr_photon_stream",
      "burst_selection"
    ],
    "optional": [
      "tcspc_calibration"
    ],
    "description": "Photon stream for micro-time histograms, burst indices for per-burst slicing, and the IRF curve for deconvolution."
  },
  "outputs": {
    "columns": [
      "Ng-p-all",
      "Ng-s-all",
      "Number of Photons (fit window) (green)",
      "2I*  (green)",
      "Tau (green)",
      "gamma (green)",
      "r0 (green)",
      "rho (green)",
      "r Scatter (green)",
      "r Experimental (green)",
      "BIFL scatter? (green)",
      "2I*: P+2S? (green)",
      "MLE Fitted (green)"
    ]
  },
  "settings_schema": {
    "type": "object",
    "properties": {
      "model": {
        "type": "string",
        "default": "fit23",
        "enum": [
          "fit23",
          "fit24",
          "fit25",
          "fit26",
          "fit_nexp"
        ]
      },
      "channel": {
        "type": "integer",
        "default": 0
      },
      "color": {
        "type": "string",
        "default": "green"
      },
      "n_bins": {
        "type": "integer",
        "default": 4096
      },
      "period_ns": {
        "type": "number",
        "default": 32.0,
        "unit": "ns"
      },
      "g_factor": {
        "type": "number",
        "default": 1.0
      },
      "l1": {
        "type": "number",
        "default": 0.0
      },
      "l2": {
        "type": "number",
        "default": 0.0
      },
      "tau_init_ns": {
        "type": "number",
        "default": 3.8,
        "unit": "ns"
      },
      "gamma": {
        "type": "number",
        "default": 1.0
      },
      "r0": {
        "type": "number",
        "default": 0.38
      },
      "rho": {
        "type": "number",
        "default": 1.2,
        "unit": "ns"
      }
    }
  },
  "can_replay": true,
  "fit_model_link": {
    "category": "fit",
    "selector": "model"
  }
})JSON";
const char* const kMleRedEntry = R"JSON({
  "name": "mle_red",
  "label": "MLE lifetime fitting (red channel)",
  "summary": "Burst-wise Fit2x MLE on the red (acceptor) detector channel. Same contract as mle_green.",
  "operation_type": "burst_lifetime_fitting",
  "data_format": "dstore",
  "row_grain": "burst",
  "kind": "burst_table",
  "inputs": {
    "required": [
      "tttr_photon_stream",
      "burst_selection"
    ],
    "optional": [
      "tcspc_calibration"
    ],
    "description": "Photon stream, burst indices, and IRF curve for the red channel."
  },
  "outputs": {
    "columns": [
      "Ng-p-all",
      "Ng-s-all",
      "Number of Photons (fit window) (red)",
      "2I*  (red)",
      "Tau (red)",
      "gamma (red)",
      "r0 (red)",
      "rho (red)",
      "r Scatter (red)",
      "r Experimental (red)",
      "BIFL scatter? (red)",
      "2I*: P+2S? (red)",
      "MLE Fitted (red)"
    ]
  },
  "settings_schema": {
    "type": "object",
    "properties": {
      "model": {
        "type": "string",
        "default": "fit23"
      },
      "channel": {
        "type": "integer",
        "default": 1
      },
      "color": {
        "type": "string",
        "default": "red"
      },
      "n_bins": {
        "type": "integer",
        "default": 4096
      },
      "period_ns": {
        "type": "number",
        "default": 32.0,
        "unit": "ns"
      },
      "g_factor": {
        "type": "number",
        "default": 1.0
      },
      "l1": {
        "type": "number",
        "default": 0.0
      },
      "l2": {
        "type": "number",
        "default": 0.0
      },
      "tau_init_ns": {
        "type": "number",
        "default": 1.6,
        "unit": "ns"
      },
      "gamma": {
        "type": "number",
        "default": 1.0
      },
      "r0": {
        "type": "number",
        "default": 0.38
      },
      "rho": {
        "type": "number",
        "default": 1.2,
        "unit": "ns"
      }
    }
  },
  "can_replay": true,
  "fit_model_link": {
    "category": "fit",
    "selector": "model"
  }
})JSON";
}  // namespace

namespace {
void register_decay_operations() {
        register_algorithm_json("operation", "tcspc_calibration", kTcspcCalibrationEntry);
        register_algorithm_json("operation", "mle_green", kMleGreenEntry);
        register_algorithm_json("operation", "mle_red", kMleRedEntry);
}
}  // namespace

}  // namespace tttrlib

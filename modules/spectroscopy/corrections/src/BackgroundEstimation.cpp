// SPDX-License-Identifier: BSD-3-Clause
#include "BackgroundEstimation.h"
#include "Registry.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace tttrlib {

double estimate_background_rate(
    const std::vector<double>& ipt_ms,
    double bin_size_ms,
    double tail_fraction
) {
    if (ipt_ms.empty()) return 0.0;

    // Sort to find tail threshold
    std::vector<double> sorted(ipt_ms);
    std::sort(sorted.begin(), sorted.end());
    // Remove NaNs
    sorted.erase(std::remove_if(sorted.begin(), sorted.end(),
        [](double v) { return !std::isfinite(v) || v <= 0.0; }), sorted.end());
    int n = static_cast<int>(sorted.size());
    if (n < 2) return 0.0;

    // Select tail: the largest tail_fraction of inter-photon times.
    // tail_fraction >= 1 is the whole sample, no truncation.
    int tail_start = (tail_fraction >= 1.0)
        ? 0 : static_cast<int>(n * (1.0 - tail_fraction));
    if (tail_start > n - 2) tail_start = n - 2;
    if (tail_start < 0) tail_start = 0;

    // Maximum likelihood on the left-truncated tail: the tail of an
    // exponential above t_thr is t_thr + Exp(lambda), so
    // lambda = N / sum(t_i - t_thr). Without the subtraction the estimate is
    // biased low by 1/(1 - ln f) -- 0.59x at f = 0.5 (found by the A/B vs
    // FRETBursts' expon_fit, 2026-08-17).
    const double t_thr = (tail_start > 0) ? sorted[tail_start] : 0.0;
    double sum_t = 0.0;
    int count = 0;
    for (int i = tail_start; i < n; ++i) {
        sum_t += sorted[i] - t_thr;
        count++;
    }
    if (count < 1 || sum_t <= 0.0) return 0.0;

    // Inter-photon times are in ms, so N / sum(t) is in 1/ms = kHz -- the
    // header's contract (typical single-molecule background: 0.2-3 kHz).
    // The old `* 1e3` returned Hz while the doc said kHz.
    return static_cast<double>(count) / sum_t;
}

} // namespace tttrlib

// ---- registry entries (Registry.h, core): declared next to the code, registered
// when this library loads; a static consumer links the archive whole.
namespace {
const char* const kBackgroundEstimationEntry = R"JSON({
  "name": "background_estimation",
  "label": "Background rate from inter-photon times",
  "summary": "Estimates the background count rate of a photon stream from the tail of its inter-photon time distribution.",
  "description": "Bins the inter-photon times and fits the exponential tail beyond the burst-dominated short times, whose rate is the background; the tail fraction and bin size are the two knobs. Model-free and cheap, so it is what the burst filters and `tcspc_calibration` use for a per-detector background.",
  "operation_type": "background_correction",
  "method": "estimate_background_rate",
  "params_schema": {
    "type": "object",
    "properties": {
      "bin_size_ms": {
        "type": "number",
        "title": "Bin size (ms)",
        "default": 0.1
      },
      "tail_fraction": {
        "type": "number",
        "title": "Tail fraction",
        "minimum": 0,
        "maximum": 1,
        "default": 0.5
      }
    }
  },
  "inputs": {
    "required": [
      "interphoton_times"
    ]
  },
  "outputs": {
    "columns": [
      "background_rate_khz"
    ]
  },
  "row_grain": "curve_point",
  "references": [
    {
      "type": "journal",
      "authors": "Eggeling, C., Berger, S., Brand, L., Fries, J. R., Schaffer, J., Volkmer, A., Seidel, C. A. M.",
      "title": "Data registration and selective single-molecule analysis using multi-parameter fluorescence detection",
      "journal": "J Biotechnol",
      "year": 2001,
      "volume": "86",
      "pages": "163-180"
    }
  ],
  "api": [
    "estimate_background_rate"
  ],
  "can_replay": true
})JSON";
bool register_backgroundestimation_entries() {
    tttrlib::register_algorithm_json("corrections", "background_estimation", kBackgroundEstimationEntry);
    return true;
}
const bool kBackgroundEstimationRegistered = register_backgroundestimation_entries();
}  // namespace

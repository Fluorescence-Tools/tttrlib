// SPDX-License-Identifier: BSD-3-Clause
#include "BurstConfidence.h"
#include "Registry.h"

#include <algorithm>
#include <cmath>

#include "TTTR.h"

namespace tttrlib {

std::vector<double> burst_confidence(
    const std::vector<int64_t>& macro_times,
    const std::vector<long long>& bursts,
    double macro_time_resolution,
    double background_window,
    SignificanceMode mode
) {
    const int64_t n_photons = static_cast<int64_t>(macro_times.size());
    const size_t n_bursts = bursts.size() / 2;
    std::vector<double> out(n_bursts, 0.0);
    if (n_bursts == 0 || n_photons < 2) return out;
    if (!(macro_time_resolution > 0.0)) return out;

    const double window = (background_window > 0.0) ? background_window : 0.05;
    const int64_t half_ticks =
        static_cast<int64_t>(0.5 * window / macro_time_resolution);

    for (size_t b = 0; b < n_bursts; ++b) {
        const int64_t start = static_cast<int64_t>(bursts[2 * b]);
        const int64_t stop = static_cast<int64_t>(bursts[2 * b + 1]);
        if (start < 0 || stop < start || stop >= n_photons) continue;

        const int64_t k = stop - start + 1;            // photons in the burst
        const double duration =
            static_cast<double>(macro_times[static_cast<size_t>(stop)] -
                                 macro_times[static_cast<size_t>(start)]) *
            macro_time_resolution;
        if (!(duration > 0.0)) continue;

        // Background from the photons flanking the burst. Walking outwards from
        // the burst edges rather than binning keeps this exact regardless of how
        // the rate varies, and costs only the photons actually looked at.
        const int64_t t_lo = macro_times[static_cast<size_t>(start)] - half_ticks;
        const int64_t t_hi = macro_times[static_cast<size_t>(stop)] + half_ticks;

        int64_t lo = start;
        while (lo > 0 && macro_times[static_cast<size_t>(lo - 1)] >= t_lo) --lo;
        int64_t hi = stop;
        while (hi + 1 < n_photons &&
               macro_times[static_cast<size_t>(hi + 1)] <= t_hi) {
            ++hi;
        }

        // The flanks are everything in the context window that is not the burst.
        const int64_t n_off = (start - lo) + (hi - stop);
        const double t_off =
            static_cast<double>(
                (macro_times[static_cast<size_t>(start)] -
                 macro_times[static_cast<size_t>(lo)]) +
                (macro_times[static_cast<size_t>(hi)] -
                 macro_times[static_cast<size_t>(stop)])) *
            macro_time_resolution;

        if (n_off <= 0 || !(t_off > 0.0)) {
            // No usable flank: the burst fills its own context window, which
            // happens in densely occupied traces. Reporting 0 rather than
            // guessing keeps a meaningless number out of the output.
            out[b] = 0.0;
            continue;
        }

        const double background_rate = static_cast<double>(n_off) / t_off;
        const double mu = background_rate * duration;

        switch (mode) {
            case SignificanceMode::kPoisson:
                out[b] = poisson_significance(k, mu);
                break;
            case SignificanceMode::kLiMa:
                // alpha is the on/off exposure ratio: how long we looked at the
                // burst versus how long we looked at its background.
                out[b] = li_ma_significance(static_cast<double>(k),
                                             static_cast<double>(n_off),
                                             duration / t_off);
                break;
            case SignificanceMode::kGaussian:
            default:
                out[b] = (mu > 0.0) ? (static_cast<double>(k) - mu) / std::sqrt(mu)
                                    : 0.0;
                break;
        }
        if (!std::isfinite(out[b])) out[b] = 0.0;
    }
    return out;
}

} // namespace tttrlib

// TTTR lives at global scope, so this definition sits outside `tttrlib`.
std::vector<double> TTTR::burst_confidence(
    const std::vector<long long>& bursts,
    double background_window,
    int significance_mode
) {
    const int64_t n = static_cast<int64_t>(size());
    std::vector<int64_t> times(static_cast<size_t>(n));
    for (int64_t i = 0; i < n; ++i) {
        times[static_cast<size_t>(i)] = static_cast<int64_t>(get_macro_time_at(i));
    }
    return tttrlib::burst_confidence(
        times, bursts, header->get_macro_time_resolution(), background_window,
        static_cast<tttrlib::SignificanceMode>(significance_mode));
}

// ---- registry entries (Registry.h, core): declared next to the code, registered
// when this library loads; a static consumer links the archive whole.
namespace {
const char* const kBurstSignificanceEntry = R"JSON({
  "name": "burst_significance",
  "label": "Burst significance (Li & Ma, Poisson tails, trials correction)",
  "summary": "How strongly the data supports each burst, in sigma, from its photons and the flanking background -- comparable across all burst searches.",
  "description": "A post-hoc statistic computed from the burst boundaries and the photon stream, so it applies to the output of any burst search and means the same thing for all of them: the Gaussian (k-mu)/sqrt(mu), the exact Poisson upper tail, or Li & Ma's likelihood-ratio significance which accounts for the background being measured rather than known. `sigma_for_false_alarm_rate` converts an expected number of spurious bursts per second into a post-trials threshold, so one setting means the same on a 10 s and a 1 h acquisition. Li & Ma is the right choice at the ~20-photon counts bursts have.",
  "operation_type": "validation",
  "method": "burst_confidence",
  "params_schema": {
    "type": "object",
    "properties": {
      "background_window": {
        "type": "number",
        "title": "Background window (s)",
        "minimum": 0,
        "default": 0.05
      },
      "significance_mode": {
        "type": "integer",
        "title": "Mode: 0 Gaussian, 1 Poisson, 2 Li&Ma",
        "minimum": 0,
        "maximum": 2,
        "default": 2
      }
    }
  },
  "inputs": {
    "required": [
      "tttr_photon_stream",
      "burst_selection"
    ]
  },
  "outputs": {
    "columns": [
      "Significance (sigma)"
    ]
  },
  "row_grain": "burst",
  "references": [
    {
      "type": "journal",
      "authors": "Li, T.-P., Ma, Y.-Q.",
      "title": "Analysis methods for results in gamma-ray astronomy",
      "journal": "Astrophys J",
      "year": 1983,
      "volume": "272",
      "pages": "317-324"
    }
  ],
  "api": [
    "TTTR.burst_confidence",
    "li_ma_significance",
    "poisson_significance",
    "log_poisson_upper_tail",
    "log_p_to_sigma",
    "sigma_for_false_alarm_rate",
    "estimate_n_trials",
    "log_gamma_p"
  ],
  "can_replay": true
})JSON";
bool register_burstconfidence_entries() {
    tttrlib::register_algorithm_json("burst", "burst_significance", kBurstSignificanceEntry);
    return true;
}
const bool kBurstConfidenceRegistered = register_burstconfidence_entries();
}  // namespace

// SPDX-License-Identifier: BSD-3-Clause
#include "BVA.h"
#include "BurstFilter.h"

#include "Registry.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace tttrlib {

std::function<double(int, int64_t, int64_t)> BVA::make_reducer(
    int number_of_photons_per_slice, double window_ticks
) {
    const std::vector<uint8_t>& is_d = membership(DONOR);
    const std::vector<uint8_t>& is_a = membership(ACCEPTOR);
    const std::vector<int64_t>& macro = macro_all();
    const bool by_time = number_of_photons_per_slice <= 0;

    return [&, by_time, number_of_photons_per_slice, window_ticks]
           (int b, int64_t s, int64_t e) -> double {
        const int64_t n_events = e - s + 1;
        if (n_events <= 0) return std::nan("");

        // Per-photon within-burst cumulative donor/acceptor counts (int32 is
        // ample: counts are bounded by the burst size).
        std::vector<int32_t> donor_cs(static_cast<size_t>(n_events));
        std::vector<int32_t> acceptor_cs(static_cast<size_t>(n_events));
        int32_t dcum = 0, acum = 0;
        for (int64_t k = 0; k < n_events; ++k) {
            const size_t idx = static_cast<size_t>(s + k);
            if (is_d[idx]) ++dcum;
            if (is_a[idx]) ++acum;
            donor_cs[static_cast<size_t>(k)] = dcum;
            acceptor_cs[static_cast<size_t>(k)] = acum;
        }

        auto count = [](const std::vector<int32_t>& cs, int64_t a, int64_t z) -> int64_t {
            return (a == 0) ? cs[z - 1] : cs[z - 1] - cs[a - 1];  // half-open [a, z)
        };

        // Welford mean/std over per-slice proximity ratios.
        int64_t n_slices = 0;
        double mean = 0.0, m2 = 0.0, size_sum = 0.0;
        auto add_slice = [&](int64_t a, int64_t z) {
            if (a >= z) return;
            const int64_t dc = count(donor_cs, a, z);
            const int64_t ac = count(acceptor_cs, a, z);
            const int64_t total = dc + ac;
            const double pr = (total > 0) ? (static_cast<double>(ac) / total) : 0.0;
            ++n_slices;
            const double delta = pr - mean;
            mean += delta / n_slices;
            m2 += delta * (pr - mean);
            size_sum += static_cast<double>(ac);
        };

        if (by_time) {
            int64_t start_idx = 0;
            while (start_idx < n_events) {
                const int64_t threshold =
                    macro[static_cast<size_t>(s + start_idx)] +
                    static_cast<int64_t>(window_ticks);
                // first index (within the burst) with macro > threshold
                int64_t end_idx = std::upper_bound(
                    macro.begin() + (s + start_idx), macro.begin() + (e + 1),
                    threshold) - (macro.begin() + s);
                if (end_idx <= start_idx) end_idx = start_idx + 1;
                add_slice(start_idx, end_idx);
                start_idx = end_idx;
            }
        } else {
            const int64_t chunk = number_of_photons_per_slice;
            for (int64_t a = 0; a < n_events; a += chunk) {
                add_slice(a, std::min(a + chunk, n_events));
            }
        }

        if (n_slices <= 0) return std::nan("");
        // Population standard deviation (matches numpy np.nanstd default).
        prox_std_[static_cast<size_t>(b)] = std::sqrt(m2 / n_slices);
        mean_slice_size_[static_cast<size_t>(b)] = size_sum / n_slices;
        return mean;
    };
}

void BVA::compute(
    long long* bursts, int n_bursts, int n_cols,
    int number_of_photons_per_slice, double minimum_window_length
) {
    build_streams();
    const size_t n = (bursts == nullptr || n_bursts < 1 || n_cols != 2)
        ? 0 : static_cast<size_t>(n_bursts);
    prox_std_.assign(n, std::nan(""));
    mean_slice_size_.assign(n, 0.0);
    const double window_ticks = seconds_to_macro_ticks(minimum_window_length);
    for_each_burst(bursts, n_bursts, n_cols,
                   make_reducer(number_of_photons_per_slice, window_ticks));
}

void BVA::compute(int number_of_photons_per_slice, double minimum_window_length) {
    if (!burst_filter_)
        throw std::runtime_error(
            "BVA::compute(): no BurstFilter — construct BVA(BurstFilter) or pass bursts explicitly");
    std::vector<long long> b(burst_filter_->get_burst_indices().begin(),
                             burst_filter_->get_burst_indices().end());
    compute(b.data(), static_cast<int>(b.size() / 2), 2,
            number_of_photons_per_slice, minimum_window_length);
}

std::pair<std::vector<double>, std::vector<double>> BVA::compute_static_bva_line(
    const std::vector<double>& prox_mean_bins,
    int number_of_photons_per_slice
) {
    std::vector<double> mean(prox_mean_bins.size());
    std::vector<double> stddev(prox_mean_bins.size());
    const double n = std::max(1, number_of_photons_per_slice);
    for (size_t i = 0; i < prox_mean_bins.size(); ++i) {
        const double p = prox_mean_bins[i];
        mean[i] = p;
        stddev[i] = std::sqrt(std::max(0.0, p * (1.0 - p) / n));
    }
    return {mean, stddev};
}


// ---- registry("operation") entry ------------------------------------------
// Burst variance analysis as a pipeline step, next to the class that computes it.
namespace {
const char* const kBvaEntry = R"JSON({
  "name": "bva",
  "api": ["BVA"],
  "label": "Burst Variance Analysis",
  "summary": "BVA (Hoffmann et al.): slices each burst into time or photon windows, computes the proximity ratio per window, reports per-burst mean and standard deviation. Compares against the shot-noise static line.",
  "operation_type": "burst_variance_analysis",
  "data_format": "dstore",
  "row_grain": "burst",
  "kind": "burst_table",
  "inputs": {
    "required": [
      "tttr_photon_stream",
      "burst_selection"
    ],
    "description": "Photon stream for per-window photon colours, burst indices for slicing."
  },
  "outputs": {
    "columns": [
      "Proximity Ratio Mean",
      "Proximity Ratio Std"
    ]
  },
  "settings_schema": {
    "type": "object",
    "properties": {
      "win_size": {
        "type": "integer",
        "default": 5,
        "minimum": 1
      },
      "n_subbursts": {
        "type": "integer",
        "default": 10,
        "minimum": 1
      }
    }
  },
  "can_replay": true
})JSON";
}  // namespace

/// Register this operation's registry entry. Idempotent (a duplicate key is refused).
void register_operation_bva() {
    register_algorithm_json("operation", "bva", kBvaEntry);
}

} // namespace tttrlib

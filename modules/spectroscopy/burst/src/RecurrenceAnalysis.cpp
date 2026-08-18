// SPDX-License-Identifier: BSD-3-Clause
#include "RecurrenceAnalysis.h"

#include "Registry.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace tttrlib {

namespace {
// Binary search: lower_bound / upper_bound on sorted array
int lower_bound(const double* arr, int n, double val) {
    int lo = 0, hi = n;
    while (lo < hi) { int mid = (lo + hi) / 2; if (arr[mid] < val) lo = mid + 1; else hi = mid; }
    return lo;
}
int upper_bound(const double* arr, int n, double val) {
    int lo = 0, hi = n;
    while (lo < hi) { int mid = (lo + hi) / 2; if (arr[mid] <= val) lo = mid + 1; else hi = mid; }
    return lo;
}
} // anonymous namespace

std::vector<double> pair_statistics(
    const std::vector<double>& burst_times_in,
    const std::vector<double>& edges,
    bool edge_correction
) {
    int n_bins = static_cast<int>(edges.size()) - 1;
    std::vector<double> counts(n_bins, 0.0), expected(n_bins, 0.0);

    // Sort and filter NaN
    std::vector<double> t(burst_times_in);
    std::sort(t.begin(), t.end());
    t.erase(std::remove_if(t.begin(), t.end(),
        [](double v) { return !std::isfinite(v); }), t.end());
    int n = static_cast<int>(t.size());
    if (n < 2) {
        std::vector<double> result(2 * n_bins, 0.0);
        return result;
    }

    double total_time = t[n - 1] - t[0];
    if (total_time <= 0) {
        std::vector<double> result(2 * n_bins, 0.0);
        return result;
    }
    double rate = static_cast<double>(n) / total_time;

    const double* tp = t.data();
    for (int k = 0; k < n_bins; ++k) {
        double a = edges[k], b = edges[k + 1];
        // For each photon i, count photons j > i with t[j] - t[i] in [a, b)
        double ck = 0.0;
        for (int i = 0; i < n; ++i) {
            int lo = lower_bound(tp, n, t[i] + a);
            int hi = lower_bound(tp, n, t[i] + b);
            // Subtract self-pair if t[i]+a <= t[i] < t[i]+b (when a <= 0)
            int cnt = hi - lo;
            if (lo <= i && i < hi) cnt--;
            ck += cnt;
        }
        counts[k] = ck;

        double d_tau = b - a;
        double span = edge_correction ? (total_time - 0.5 * (a + b)) : total_time;
        if (span < 0) span = 0;
        expected[k] = rate * rate * d_tau * span;
    }
    // Return flat [counts | expected]
    std::vector<double> result(2 * n_bins);
    for (int i = 0; i < n_bins; ++i) {
        result[i] = counts[i];
        result[n_bins + i] = expected[i];
    }
    return result;
}

std::vector<double> same_molecule_probability(
    const std::vector<double>& burst_times,
    double tau_min, double tau_max,
    int n_bins, bool edge_correction
) {
    // Log-spaced edges
    std::vector<double> edges(n_bins + 1);
    double lt_min = std::log10(tau_min), lt_max = std::log10(tau_max);
    for (int i = 0; i <= n_bins; ++i)
        edges[i] = std::pow(10.0, lt_min + (lt_max - lt_min) * i / n_bins);

    auto ps = pair_statistics(burst_times, edges, edge_correction);
    const double* counts = ps.data();
    const double* expected = ps.data() + n_bins;

    // Return flat: [tau_centers | p_same | g]
    std::vector<double> result(3 * n_bins);
    for (int i = 0; i < n_bins; ++i) {
        result[i] = std::sqrt(edges[i] * edges[i + 1]); // tau center
        double gi = (expected[i] > 0) ? counts[i] / expected[i]
                                      : std::numeric_limits<double>::quiet_NaN();
        result[n_bins + i] = (gi > 0) ? std::clamp(1.0 - 1.0 / gi, 0.0, 1.0) : 0.0;
        result[2 * n_bins + i] = gi;
    }
    return result;
}

std::vector<double> recurrence_efficiencies(
    const std::vector<double>& burst_times_in,
    const std::vector<double>& efficiencies_in,
    double e_min, double e_max,
    double dt_min, double dt_max
) {
    // Sort by time
    int n = static_cast<int>(burst_times_in.size());
    std::vector<int> order(n);
    for (int i = 0; i < n; ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return burst_times_in[a] < burst_times_in[b];
    });

    std::vector<double> t(n), e(n);
    for (int i = 0; i < n; ++i) { t[i] = burst_times_in[order[i]]; e[i] = efficiencies_in[order[i]]; }

    const double* tp = t.data();
    std::vector<double> out;

    for (int i = 0; i < n; ++i) {
        if (!std::isfinite(e[i]) || e[i] < e_min || e[i] > e_max) continue;
        int lo = lower_bound(tp, n, t[i] + dt_min);
        int hi = upper_bound(tp, n, t[i] + dt_max);
        for (int j = lo; j < hi; ++j) {
            if (j != i && std::isfinite(e[j])) out.push_back(e[j]);
        }
    }
    return out;
}


// ---- registry("operation") entry ------------------------------------------
// Same-molecule recurrence fusion as a pipeline step, next to the analysis it uses.
namespace {
const char* const kBurstFusionEntry = R"JSON({
  "name": "burst_fusion",
  "label": "Recurrence burst fusion",
  "summary": "Estimates same-molecule probability from inter-burst time gaps and fuses bursts from the same molecule passage (Hoffmann et al.).",
  "operation_type": "burst_fusion",
  "data_format": "dstore",
  "row_grain": "burst",
  "kind": "burst_table",
  "inputs": {
    "required": [
      "burst_selection"
    ],
    "description": "Burst table with macro-time gaps."
  },
  "outputs": {
    "columns": [
      "Fused Bursts",
      "Fused Gap Photons"
    ]
  },
  "settings_schema": {
    "type": "object",
    "properties": {
      "max_lag_ms": {
        "type": "number",
        "default": 10.0,
        "unit": "ms"
      }
    }
  },
  "can_replay": true
})JSON";
}  // namespace

/// Register this operation's registry entry. Idempotent (a duplicate key is refused).
void register_operation_burst_fusion() {
    register_algorithm_json("operation", "burst_fusion", kBurstFusionEntry);
}

} // namespace tttrlib

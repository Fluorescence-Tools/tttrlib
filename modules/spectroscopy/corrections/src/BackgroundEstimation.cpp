// SPDX-License-Identifier: BSD-3-Clause
#include "BackgroundEstimation.h"

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

    // Select tail: the largest tail_fraction of inter-photon times
    int tail_start = static_cast<int>(n * (1.0 - tail_fraction));
    if (tail_start >= n - 1) tail_start = n / 2;
    if (tail_start < 1) tail_start = 1;

    // Method of moments on the tail: lambda = N / sum(t_i)
    double sum_t = 0.0;
    int count = 0;
    for (int i = tail_start; i < n; ++i) {
        sum_t += sorted[i];
        count++;
    }
    if (count < 1 || sum_t <= 0.0) return 0.0;

    double lambda = static_cast<double>(count) / sum_t;
    return lambda * 1e3;  // convert 1/ms to kHz
}

} // namespace tttrlib

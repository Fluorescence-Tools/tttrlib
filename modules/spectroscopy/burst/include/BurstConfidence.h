// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file BurstConfidence.h
 * \brief How strongly the data supports each burst a search returned.
 *
 * Every burst search here computes some notion of confidence internally — the
 * max-tree its stability and Poisson significance, Bayesian Blocks its per-block
 * significance, Kalman its innovation distance — and then throws it away, because
 * the shared return convention is boundaries only. Downstream that hurts: a
 * burst list gives no way to tell a marginal detection from an unambiguous one,
 * so filtering has to fall back on photon count, which is not the same thing.
 *
 * This module computes the confidence *after the fact*, from the burst
 * boundaries and the photon stream alone. That choice matters:
 *
 *  - It applies to **every** search, including ones added later, and gives a
 *    number that means the same thing across all of them. An
 *    algorithm-internal score does not: an MSER variation and a Mahalanobis
 *    distance are not comparable, so bursts from different searches could not be
 *    ranked or thresholded together.
 *  - It leaves the searches untouched, so nothing about their tuning or output
 *    changes by adding it.
 *
 * The statistic is the significance of the burst's photon excess over the local
 * background, in sigma — the same quantity BurstSignificance.h provides to the
 * searches, applied to a finished burst. The background is measured from the
 * photons *around* each burst rather than assumed, so Li & Ma is the appropriate
 * default; see the discussion in BurstSignificance.h.
 *
 * The value is in units of sigma and is not capped, so very bright bursts score
 * in the hundreds. Treat it as a ranking, and remember the usual caveat about
 * sigma from a trials-heavy search: it is the significance of *this* burst, not
 * corrected for how many candidates were examined to find it.
 */
#ifndef TTTRLIB_BURSTCONFIDENCE_H
#define TTTRLIB_BURSTCONFIDENCE_H

// Validation: A/B-TESTED 2026-08-17 -- vs a NumPy transcription of the documented statistic (background from the
//   +-window/2 flanks, burst excluded; Li & Ma / Poisson / Gaussian modes), 1e-9. test/python/burstfilter/test_ab_burst_reference.py.
//   Register: okf/testing/algorithm-validation.md

#include <cstdint>
#include <vector>

#include "BurstSignificance.h"

namespace tttrlib {

/*!
 * \brief Per-burst significance over the locally measured background.
 *
 * \param macro_times sorted photon macro times, in ticks.
 * \param bursts interleaved `[start0, stop0, ...]` **inclusive** photon indices,
 *        as every `TTTR::burst_search*` returns.
 * \param macro_time_resolution seconds per macro-time tick.
 * \param background_window seconds of context around each burst used to measure
 *        the background. Must be several times a burst duration; the photons
 *        inside the burst itself are excluded from the estimate.
 * \param mode which statistic to apply. Li & Ma by default, because the
 *        background here is always measured rather than known.
 * \return one value per burst (half the length of \a bursts), in sigma.
 */
std::vector<double> burst_confidence(
    const std::vector<int64_t>& macro_times,
    const std::vector<long long>& bursts,
    double macro_time_resolution,
    double background_window = 0.05,
    SignificanceMode mode = SignificanceMode::kLiMa
);

} // namespace tttrlib

#endif // TTTRLIB_BURSTCONFIDENCE_H

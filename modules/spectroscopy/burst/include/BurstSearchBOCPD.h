// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file BurstSearchBOCPD.h
 * \brief Burst search by Bayesian Online Changepoint Detection.
 *
 * The threshold and Kalman searches test whether a local rate exceeds a
 * background. BOCPD does something different: it maintains a full posterior
 * over *run lengths* — how many bins the current segment has lasted since the
 * last changepoint — and flags a changepoint when that posterior collapses to
 * run length zero. A burst is then a segment between two changepoints that
 * contains enough photons.
 *
 * The conjugate prior for the per-bin photon count is Gamma-Poisson: each
 * channel's rate \f$\lambda\f$ has a Gamma(\f$\alpha, \beta\f$) prior. The
 * predictive used for a bin is the *plug-in* Poisson at the posterior mean
 * \f$\alpha/\beta\f$ (the same choice as the ChiSurf implementation this
 * ports and the A/B pins), not the marginal Negative-Binomial -- cheaper, and
 * indistinguishable once a segment holds more than a few bins. After
 * observing a count \f$k\f$ in a bin,
 * \f$\alpha \mathrel{+}= k\f$, \f$\beta \mathrel{+}= 1\f$, so the posterior
 * keeps updating as long as the segment continues, and resets at a changepoint.
 *
 * For multi-channel data each channel carries its own Gamma pair; the per-bin
 * predictive is the sum of per-channel Poisson log-likelihoods, which is the
 * natural generalisation of the two-channel (donor + acceptor) case. A
 * simultaneous rate change across channels therefore produces a stronger
 * predictive surprise than a single-channel one.
 *
 * The algorithm is Adams & MacKay, *Bayesian Online Changepoint Detection*,
 * arXiv:0710.3742 (2007). Ported from the BOCPD burst detector in chisurf
 * (`chisurf/core/fluorescence/burst/bocpd.py`), so every burst search
 * tttrlib's users need lives in tttrlib.
 */
#ifndef TTTRLIB_BURSTSEARCHBOCPD_H
#define TTTRLIB_BURSTSEARCHBOCPD_H

// Validation: A/B-TESTED 2026-08-19 -- (1) vs the Adams & MacKay 2007 run-length recursion transcribed
//   in NumPy from the paper, with a per-channel Gamma-Poisson model and the plug-in Poisson
//   predictive (`bocpd_reference` in the test): bursts identical, 1 and 2 channels, priors, run cap.
//   NB the predictive is the plug-in Poisson at alpha/beta, not the Negative-Binomial the file
//   comment names. ChiSurf is NOT a reference; this comment credited its numba code until 2026-08-19,
//   after the reference had already been rewritten from the paper.
//   (2) GROUND TRUTH on a dilute simulated measurement, 3 seeds: 44-46 detections for 40 known
//   transits, precision 98-100 %, recall 95-100 % at dt = 20 us and a hazard rate of 0.2. The hazard
//   rate is not scale-free -- see `changepoint_prob` in the registry schema for what a value too
//   small for the bin width does, which is to return the whole measurement as one burst.
//   test/python/burstfilter/test_ab_burst_reference.py, test_burst_search_ground_truth.py.
//   Register: okf/testing/algorithm-validation.md

#include <cstdint>
#include <vector>

namespace tttrlib {

/*!
 * \brief Parameters of the BOCPD burst search.
 */
struct BocpdBurstSettings {
    /// Bin width in seconds. Sets the time resolution: a burst shorter than a
    /// bin cannot be resolved, and \p max_run counts bins.
    double dt = 1e-3;
    /// Prior photon count before data (Gamma shape, \f$\alpha\f$).
    /// A pseudo-count the filter starts with; 1.0 is weakly informative.
    double prior_count = 1.0;
    /// Prior duration in bins (Gamma rate, \f$\beta\f$).
    /// How many bins-worth of data the prior represents; 1.0 means the prior
    /// is worth one bin.
    double prior_duration = 1.0;
    /// Probability of a changepoint in any bin (hazard rate). Larger detects
    /// more, shorter segments; smaller merges adjacent segments.
    double changepoint_prob = 0.1;
    /// Maximum run length to track. Truncates the run-length posterior;
    /// segments longer than this saturate at \p max_run. The cost per bin is
    /// O(max_run * n_channels).
    int max_run = 256;
    /// Minimum photons per burst, applied last (as L elsewhere).
    int L = 20;
    /// Track one Gamma pair per used routing channel rather than pooling all
    /// photons into a single rate. Off when the data has one channel.
    bool per_channel = true;
    /// Cap on state dimensions, so a file with many routing channels cannot
    /// turn the per-bin update into the dominant cost.
    int max_channels = 8;
};

/*!
 * \brief Run the BOCPD burst search on a macro-time array.
 *
 * \param macro_times sorted photon macro times, in macro-time ticks.
 * \param routing_channels per-photon routing channel; empty pools all photons
 *        into a single rate, as does \p per_channel = false.
 * \param macro_time_resolution seconds per macro-time tick.
 * \param settings see BocpdBurstSettings.
 * \return interleaved `[start0, stop0, ...]` **inclusive** photon indices,
 *         sorted and non-overlapping — the layout every `TTTR::burst_search*`
 *         returns.
 */
std::vector<long long> burst_search_bocpd(
    const std::vector<int64_t>& macro_times,
    const std::vector<signed char>& routing_channels,
    double macro_time_resolution,
    const BocpdBurstSettings& settings
);

} // namespace tttrlib

#endif // TTTRLIB_BURSTSEARCHBOCPD_H

// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file BurstSearchKalman.h
 * \brief Burst search by Kalman-filtered count rate with a Mahalanobis test.
 *
 * The other burst searches work on the photon stream directly. This one bins the
 * stream and tracks the count rate with a Kalman filter whose measurement noise
 * is derived from Poisson statistics — the variance of a rate estimated from a
 * bin is the rate divided by the bin width — so the filter knows how much of the
 * bin-to-bin scatter is shot noise and how much is not. A burst is then a run of
 * bins whose *innovation* (the gap between the measured rate and the rate the
 * filter predicted) is large compared with the filter's own uncertainty, measured
 * as a Mahalanobis distance.
 *
 * That framing gives it a property the threshold searches lack: it responds to a
 * *change* in rate rather than to an absolute level, so a slow drift in
 * background is tracked and ignored rather than being detected. The cost is a bin
 * width — unlike the sliding-window, CUSUM and max-tree searches, resolution is
 * limited by `dt` rather than by the photons themselves.
 *
 * Multi-channel is the interesting case. With one state dimension per detector,
 * the innovation covariance couples the channels, so a simultaneous rise across
 * detectors — what a molecule crossing the focus actually produces — is scored
 * more strongly than an uncorrelated fluctuation of the same size in one channel.
 *
 * Ported from the Kalman burst detector in chisurf
 * (`chisurf/core/fluorescence/burst/kalman.py`), so that every burst search
 * tttrlib's users need lives in tttrlib, with the same interface and burst-index
 * convention as the others.
 */
#ifndef TTTRLIB_BURSTSEARCHKALMAN_H
#define TTTRLIB_BURSTSEARCHKALMAN_H

#include <cstdint>
#include <vector>

namespace tttrlib {

/*!
 * \brief Parameters of the Kalman burst search.
 */
struct KalmanBurstSettings {
    /// Bin width in seconds. Sets the time resolution of the whole method: a
    /// burst shorter than a bin cannot be resolved, and `min_len` counts bins.
    double dt = 1e-4;
    /// Process-noise variance, in (counts/s)^2. How fast the filter is willing to
    /// believe the underlying rate itself changes; larger tracks bursts rather
    /// than flagging them, so raising it makes the search less sensitive.
    ///
    /// This is the parameter to be careful with, because it is **not
    /// dimensionless**: it has to be commensurate with the count rates in the
    /// data. Set far too small, the filter is effectively frozen, every bin then
    /// looks anomalous, and the search degenerates into one burst spanning the
    /// whole measurement — a failure that looks like a detection rather than an
    /// error. The default was chosen to be non-degenerate on both a realistic
    /// simulated single-molecule trace and a sparse synthetic one; on data with
    /// very different rates it may need scaling with them.
    double q = 100.0;
    /// Measurement-noise scale on the Poisson variance `rate / dt`. 1.0 trusts
    /// shot noise exactly; larger tolerates extra technical noise.
    double r_scale = 0.1;
    /// Mahalanobis distance above which a bin is considered part of a burst.
    double z_thresh = 3.0;
    /// Minimum number of consecutive bins over threshold.
    int min_len = 2;
    /// Merge bursts separated by at most this many bins.
    int merge_gap = 5;
    /// Minimum photons per burst, applied last (as `L` elsewhere).
    int L = 20;
    /// Track one state dimension per used routing channel rather than pooling
    /// all photons into a single rate. Off when the data has one channel.
    bool per_channel = true;
    /// Cap on state dimensions, so a file with many routing channels cannot turn
    /// the per-bin matrix inverse into the dominant cost.
    int max_channels = 8;
};

/*!
 * \brief Run the Kalman burst search on a macro-time array.
 *
 * \param macro_times sorted photon macro times, in macro-time ticks.
 * \param routing_channels per-photon routing channel; empty pools all photons
 *        into a single rate, as does `per_channel = false`.
 * \param macro_time_resolution seconds per macro-time tick.
 * \param settings see KalmanBurstSettings.
 * \return interleaved `[start0, stop0, ...]` **inclusive** photon indices, sorted
 *         and non-overlapping — the layout every `TTTR::burst_search*` returns.
 */
std::vector<long long> burst_search_kalman(
    const std::vector<int64_t>& macro_times,
    const std::vector<signed char>& routing_channels,
    double macro_time_resolution,
    const KalmanBurstSettings& settings
);

} // namespace tttrlib

#endif // TTTRLIB_BURSTSEARCHKALMAN_H

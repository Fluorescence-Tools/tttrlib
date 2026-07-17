// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_BVA_H
#define TTTRLIB_BVA_H

#include <vector>
#include <memory>
#include <utility>
#include <string>

#include "TTTR.h"

namespace tttrlib {

class BurstFilter;  // forward declaration (BurstFilter.h included in BVA.cpp)

/**
 * @brief Burst Variance Analysis (BVA).
 *
 * BVA (Torella et al., Biophys. J. 2011) probes sub-burst FRET dynamics.
 * Each burst is split into slices (either a fixed number of photons per slice
 * or fixed-duration time windows).  For every slice the proximity ratio
 * @f$ PR = n_A / (n_A + n_D) @f$ is computed from the donor/acceptor photon
 * counts.  The per-burst BVA statistics are the mean and the standard
 * deviation of @f$ PR @f$ across the burst's slices.  A dynamic (mixing)
 * population shows a standard deviation above the shot-noise-limited static
 * line @f$ \sigma = \sqrt{p(1-p)/n} @f$.
 *
 * This is a C++ port of the ChiSurf ``burst_bva`` plugin computation
 * (cumulative-sum slice counting), parallelised over bursts with OpenMP.
 *
 * Donor and acceptor photon streams are each defined by a set of routing
 * channels together with (inclusive) micro-time windows, so polarisation- and
 * spectrally-split detectors as well as PIE time gating are supported.
 */
class BVA {
public:
    /**
     * @param tttr TTTR photon stream the burst indices refer to.
     */
    explicit BVA(std::shared_ptr<TTTR> tttr);

    /**
     * @brief Construct from a BurstFilter, reusing its TTTR and detected
     *        bursts.  Call ::set_donor / ::set_acceptor to define the streams,
     *        then the no-argument-bursts ::compute overload.
     */
    explicit BVA(std::shared_ptr<BurstFilter> burst_filter);

    /**
     * @brief Define the donor photon stream.
     * @param channels Routing channels counted as donor.
     * @param micro_time_ranges Inclusive (start, stop) micro-time windows; an
     *        empty list accepts every micro time.
     */
    void set_donor(
        const std::vector<int>& channels,
        const std::vector<std::pair<int, int>>& micro_time_ranges = {}
    );

    /**
     * @brief Define the acceptor photon stream.
     */
    void set_acceptor(
        const std::vector<int>& channels,
        const std::vector<std::pair<int, int>>& micro_time_ranges = {}
    );

    /**
     * @brief Compute per-burst proximity-ratio mean and standard deviation.
     * @param bursts Interleaved half-open photon index ranges
     *        ``[s0, e0, s1, e1, ...]`` (as produced by BurstFilter).
     * @param number_of_photons_per_slice If > 0, slice each burst into
     *        consecutive chunks of this many photons.  If <= 0, slice into
     *        fixed-duration time windows of ``minimum_window_length`` seconds.
     * @param minimum_window_length Slice duration in seconds (time-window mode).
     *
     * The bursts are a pointer/length pair (spelled long long, not int64_t)
     * so the SWIG IN_ARRAY1 typemaps apply: Python passes a NumPy int array
     * directly (single buffer conversion, no per-element list boxing), R a
     * numeric vector, Java a long[].
     */
    void compute(
        long long* bursts, int n_bursts,
        int number_of_photons_per_slice = -1,
        double minimum_window_length = 0.01
    );

    /**
     * @brief Compute using the bursts of the BurstFilter this object was
     *        constructed from (throws if constructed from a bare TTTR).
     */
    void compute(
        int number_of_photons_per_slice = -1,
        double minimum_window_length = 0.01
    );

    /**
     * @brief Per-burst mean proximity ratio (one value per burst).
     */
    const std::vector<double>& get_proximity_ratio_mean() const { return prox_mean_; }

    /**
     * @brief Per-burst proximity-ratio standard deviation (one value per burst).
     */
    const std::vector<double>& get_proximity_ratio_std() const { return prox_std_; }

    /**
     * @brief Number of photons in the acceptor stream per slice, averaged over
     *        the slices of each burst (useful for weighting/plotting).
     */
    const std::vector<double>& get_mean_slice_size() const { return mean_slice_size_; }

    /**
     * @brief Shot-noise-limited static BVA line.
     *
     * For a static species at expected proximity ratio ``p`` observed with
     * ``n`` photons per slice, the slice proximity ratio is
     * @f$ \mathrm{Binomial}(n, p)/n @f$ with mean ``p`` and standard deviation
     * @f$ \sqrt{p(1-p)/n} @f$.  Returns ``(mean = p, std)`` for each input
     * bin, matching the ChiSurf ``compute_static_bva_line`` output in
     * expectation (analytic, deterministic — no Monte-Carlo sampling).
     *
     * @param prox_mean_bins Proximity-ratio values ``p`` to evaluate.
     * @param number_of_photons_per_slice Photons per slice ``n``.
     */
    static std::pair<std::vector<double>, std::vector<double>> compute_static_bva_line(
        const std::vector<double>& prox_mean_bins,
        int number_of_photons_per_slice = 4
    );

private:
    std::shared_ptr<TTTR> tttr_;
    std::shared_ptr<BurstFilter> burst_filter_;  // optional source of bursts
    std::vector<int> donor_channels_ = {0, 8};
    std::vector<int> acceptor_channels_ = {1, 9};
    std::vector<std::pair<int, int>> donor_micro_time_ranges_;
    std::vector<std::pair<int, int>> acceptor_micro_time_ranges_;

    std::vector<double> prox_mean_;
    std::vector<double> prox_std_;
    std::vector<double> mean_slice_size_;
};

} // namespace tttrlib

#endif // TTTRLIB_BVA_H

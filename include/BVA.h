// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_BVA_H
#define TTTRLIB_BVA_H

#include <vector>
#include <memory>
#include <utility>
#include <functional>

#include "BurstFeature.h"

namespace tttrlib {

/**
 * @brief Burst Variance Analysis (BVA) — a per-burst dynamics feature.
 *
 * BVA (Torella et al., Biophys. J. 2011) probes sub-burst FRET dynamics.
 * Each burst is split into slices (either a fixed number of photons per slice
 * or fixed-duration time windows).  For every slice the proximity ratio
 * @f$ PR = n_A / (n_A + n_D) @f$ is computed from the donor/acceptor photon
 * counts.  The per-burst BVA feature is the mean and the standard deviation of
 * @f$ PR @f$ across the burst's slices.  A dynamic (mixing) population shows a
 * standard deviation above the shot-noise-limited static line
 * @f$ \sigma = \sqrt{p(1-p)/n} @f$.
 *
 * This class only *computes* the feature; selecting/filtering bursts on the
 * returned values happens downstream.  It is a tttrlib::BurstFeature: donor and
 * acceptor photon streams are defined by routing channels + (inclusive)
 * micro-time windows (::set_donor / ::set_acceptor), so polarisation-/
 * spectrally-split detectors and PIE time gating are supported.  Bursts are
 * processed in parallel.
 */
class BVA : public BurstFeature {
public:
    explicit BVA(std::shared_ptr<TTTR> tttr) : BurstFeature(std::move(tttr)) {}
    explicit BVA(std::shared_ptr<BurstFilter> burst_filter)
        : BurstFeature(std::move(burst_filter)) {}

    /// Define the donor photon stream. Convenience wrapper over ::set_stream.
    void set_donor(
        const std::vector<int>& channels,
        const std::vector<std::pair<int, int>>& micro_time_ranges = {}
    ) { set_stream(DONOR, channels, micro_time_ranges); }

    /// Define the acceptor photon stream.
    void set_acceptor(
        const std::vector<int>& channels,
        const std::vector<std::pair<int, int>>& micro_time_ranges = {}
    ) { set_stream(ACCEPTOR, channels, micro_time_ranges); }

    /**
     * @brief Compute per-burst proximity-ratio mean and standard deviation.
     * @param bursts Interleaved **inclusive** photon index ranges
     *        ``[s0, e0, s1, e1, ...]`` as an (n_bursts, 2) array (as produced by
     *        BurstFilter and every ``TTTR::burst_search*`` method).
     * @param number_of_photons_per_slice If > 0, slice each burst into
     *        consecutive chunks of this many photons.  If <= 0, slice into
     *        fixed-duration time windows of ``minimum_window_length`` seconds.
     * @param minimum_window_length Slice duration in seconds (time-window mode).
     */
    void compute(
        long long* bursts, int n_bursts, int n_cols,
        int number_of_photons_per_slice = -1,
        double minimum_window_length = 0.01
    );

    /// Compute using the bursts of the source BurstFilter (throws if none).
    void compute(
        int number_of_photons_per_slice = -1,
        double minimum_window_length = 0.01
    );

    /// Per-burst mean proximity ratio (alias of ::get_result).
    const std::vector<double>& get_proximity_ratio_mean() const { return get_result(); }

    /// Per-burst proximity-ratio standard deviation (one value per burst).
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
     * @f$ \sqrt{p(1-p)/n} @f$.  Returns ``(mean = p, std)`` for each input bin
     * (analytic, deterministic — no Monte-Carlo sampling).
     *
     * @param prox_mean_bins Proximity-ratio values ``p`` to evaluate.
     * @param number_of_photons_per_slice Photons per slice ``n``.
     */
    static std::pair<std::vector<double>, std::vector<double>> compute_static_bva_line(
        const std::vector<double>& prox_mean_bins,
        int number_of_photons_per_slice = 4
    );

private:
    // Internal stream keys for the base's named-stream map (configured through
    // the setters above — no user-facing magic names, no hard-coded channels).
    static constexpr const char* DONOR = "donor";
    static constexpr const char* ACCEPTOR = "acceptor";

    std::function<double(int, int64_t, int64_t)> make_reducer(
        int number_of_photons_per_slice, double window_ticks);

    std::vector<double> prox_std_;
    std::vector<double> mean_slice_size_;
};

} // namespace tttrlib

#endif // TTTRLIB_BVA_H

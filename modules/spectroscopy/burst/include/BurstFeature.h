// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_BURSTFEATURE_H
#define TTTRLIB_BURSTFEATURE_H
// Validation: A/B-TESTED (via TwoCDE) 2026-08-17 -- build_kde is the one KDE in the library
//   (kde_eval: Laplace 5*tau / Gaussian 3*tau two-pointer window, the same rule as FRETBursts
//   phrates_numba); TwoCDE::compute calls it and its result agrees with FRETBursts' own
//   kde_laplace / kde_gaussian run live to 1e-9 -- test/python/bva/test_ab_bva_2cde_recurrence_reference.py.
//   The stream builder / for_each_burst reduction is plumbing (NumPy-transcribed in test_twocde.py).
//   Register: okf/testing/algorithm-validation.md

#include <vector>
#include <memory>
#include <utility>
#include <string>
#include <map>
#include <functional>

#include "TTTR.h"

namespace tttrlib {

class BurstFilter;  // forward declaration (BurstFilter.h included in the .cpp)

/**
 * @brief Base class for per-burst *features*.
 *
 * A burst feature reduces every burst of a burst list to one (or a few) scalar
 * quantities — e.g. Burst Variance Analysis (tttrlib::BVA) or the 2CDE dynamics
 * filters (tttrlib::TwoCDE).  This differs from tttrlib::BurstFilter, which
 * *detects and selects* bursts; a feature takes an existing burst list
 * (optionally from a BurstFilter) and computes a per-burst quantity, and from
 * tttrlib::BurstFeatureExtractor, which aggregates the standard size/rate/E
 * properties in one pass.
 *
 * The base provides the machinery shared by this family:
 *  - **named photon streams** defined by routing channels + (inclusive)
 *    micro-time windows (so polarisation-/spectrally-split detectors and PIE
 *    time gating are supported), via ::set_stream;
 *  - per-photon **stream membership** masks and the per-stream **timestamp
 *    subsets** (::build_streams);
 *  - an optional **kernel-density estimate** (KDE) of each stream evaluated at
 *    every photon, Laplace or Gaussian, computed with the bit-exact FRETBursts
 *    two-pointer sliding window (::build_kde) — only features that need it call
 *    it;
 *  - a parallel ::for_each_burst dispatcher that calls a subclass reducer and
 *    stores its primary result in ::get_result (subclasses may write further
 *    per-burst outputs of their own, keyed by the burst index passed in).
 *
 * Concrete features subclass this, declare the streams they need, call
 * ::build_streams (and ::build_kde if required), and implement their per-burst
 * reduction.
 */
class BurstFeature {
public:
    /// Kernel used by ::build_kde.
    enum Kernel {
        LAPLACE = 0,  ///< symmetric exponential exp(-|dt|/tau), 5*tau cutoff
        GAUSSIAN = 1  ///< Gaussian exp(-dt^2 / (2 tau^2)), 3*tau cutoff
    };

    explicit BurstFeature(std::shared_ptr<TTTR> tttr);
    explicit BurstFeature(std::shared_ptr<BurstFilter> burst_filter);
    virtual ~BurstFeature() = default;

    /// TTTR photon stream the burst indices refer to.
    std::shared_ptr<TTTR> get_tttr() const { return tttr_; }

    /**
     * @brief Define (or redefine) a named photon stream.
     * @param name Stream key referenced by subclasses.
     * @param channels Routing channels belonging to the stream.
     * @param micro_time_ranges Inclusive (start, stop) micro-time windows; an
     *        empty list accepts every micro time.
     */
    void set_stream(
        const std::string& name,
        const std::vector<int>& channels,
        const std::vector<std::pair<int, int>>& micro_time_ranges = {}
    );

    /// Primary per-burst feature result (one value per burst; NaN where undefined).
    const std::vector<double>& get_result() const { return result_; }

protected:
    /**
     * @brief Build the per-photon macro-time axis, and for every registered
     *        stream its membership mask and timestamp subset.  Does not compute
     *        KDEs (call ::build_kde separately if needed).
     */
    void build_streams();

    /**
     * @brief Compute the per-stream KDE evaluated at every photon.
     * @param tau_ticks Kernel time constant in macro-time ticks.
     * @param kernel ::LAPLACE or ::GAUSSIAN.
     * Requires ::build_streams to have run.
     */
    void build_kde(double tau_ticks, int kernel);

    /// Convert a time in seconds to macro-time ticks (header resolution).
    double seconds_to_macro_ticks(double seconds) const;

    /// Per-photon membership mask (0/1) of a registered stream.
    const std::vector<uint8_t>& membership(const std::string& name) const;

    /// Timestamps (macro times) of a registered stream, ascending.
    const std::vector<int64_t>& timestamps(const std::string& name) const;

    /// Per-photon KDE of a registered stream (valid after ::build_kde).
    const std::vector<double>& kde(const std::string& name) const;

    /// Macro time of every photon in the full stream (ascending).
    const std::vector<int64_t>& macro_all() const { return macro_all_; }

    /// Number of photons in the underlying TTTR stream.
    int64_t n_total() const { return static_cast<int64_t>(macro_all_.size()); }

    /**
     * @brief Run ``reduce`` over every burst in parallel, storing its return
     *        value in ::get_result.
     * @param bursts Interleaved inclusive index ranges ``[s0, e0, s1, e1, ...]``
     *        as an (n_bursts, 2) array; clamped to the valid photon range.
     * @param reduce Callback ``(burst_index, s, e) -> double`` returning the
     *        primary per-burst scalar (NaN when undefined).  The burst index is
     *        supplied so a subclass may also fill its own extra output arrays.
     *
     * ::get_result is sized to the number of bursts before the loop, so
     * subclasses can size their extra arrays to match.
     */
    void for_each_burst(
        long long* bursts, int n_bursts, int n_cols,
        const std::function<double(int b, int64_t s, int64_t e)>& reduce
    );

    /// Resolve the bursts of the source BurstFilter (throws if none) and dispatch.
    void for_each_burst_from_filter(
        const std::function<double(int b, int64_t s, int64_t e)>& reduce
    );

    std::shared_ptr<TTTR> tttr_;
    std::shared_ptr<BurstFilter> burst_filter_;  // optional source of bursts
    std::vector<double> result_;

private:
    struct StreamDef {
        std::vector<int> channels;
        std::vector<std::pair<int, int>> micro_time_ranges;
    };
    std::map<std::string, StreamDef> streams_;
    std::map<std::string, std::vector<uint8_t>> membership_;
    std::map<std::string, std::vector<int64_t>> timestamps_;
    std::map<std::string, std::vector<double>> kde_;
    std::vector<int64_t> macro_all_;
};

} // namespace tttrlib

#endif // TTTRLIB_BURSTFEATURE_H

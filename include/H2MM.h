// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_H2MM_H
#define TTTRLIB_H2MM_H

#include <vector>
#include <cstdint>
#include <cmath>
#include <limits>
#include <memory>

#include "TTTR.h"

namespace tttrlib {

class Channel;       // forward declarations (headers included in H2MM.cpp)
class BurstFilter;

namespace h2mm_detail { class ForkJoinPool; }  // persistent worker pool (H2MM.cpp)

/**
 * @brief An H2MM model (@f$ \lambda = \{\pi, A, B\} @f$) plus diagnostics.
 *
 * Row-major storage:
 *  - ``prior`` — length ``n_states``, initial-state probabilities.
 *  - ``trans`` — ``n_states * n_states``, one-base-tick row-stochastic
 *    transition matrix (``trans[i*n + j] = P(state j at t+1 | state i at t)``).
 *  - ``obs``   — ``n_states * n_streams``, row-stochastic emission matrix
 *    (``obs[i*p + k] = P(stream k | state i)``).
 */
struct H2mmModel {
    std::vector<double> prior;
    std::vector<double> trans;
    std::vector<double> obs;
    double loglik = -std::numeric_limits<double>::infinity();
    int n_iter = 0;
    long long n_phot = 0;
    bool converged = false;

    H2mmModel() = default;
    H2mmModel(
        std::vector<double> prior_,
        std::vector<double> trans_,
        std::vector<double> obs_
    ) : prior(std::move(prior_)), trans(std::move(trans_)), obs(std::move(obs_)) {}

    int n_states() const { return static_cast<int>(prior.size()); }
    int n_streams() const {
        int n = n_states();
        return n > 0 ? static_cast<int>(obs.size() / n) : 0;
    }
    /// Number of free parameters (used by BIC/ICL).
    int n_free() const {
        int n = n_states(), p = n_streams();
        return n * n + (p - 1) * n - 1;
    }
    /// Bayesian information criterion @f$ -2\log L + k\ln N_{phot} @f$.
    double bic() const {
        if (!std::isfinite(loglik) || n_phot <= 0)
            return std::numeric_limits<double>::infinity();
        return -2.0 * loglik + n_free() * std::log(static_cast<double>(n_phot));
    }
    /// Renormalise ``prior`` and the rows of ``trans`` and ``obs`` in place.
    void normalize();
};

/**
 * @brief Photon-by-photon Hidden Markov Model (H2MM) engine.
 *
 * A fast C++ port of the ChiSurf numba H2MM engine — itself a re-implementation
 * of Pirchi et al. (J. Phys. Chem. B 2016, 120, 13065) and Harris's ``H2MM_C``
 * reference.  It reproduces the two algorithmic wins that make the numba engine
 * outrun the reference C library:
 *
 *  1. **Sparse unique-@f$\Delta t@f$ cache.** @f$ A^{\Delta t} @f$ and the
 *     expected-transition tensor @f$ \rho(\Delta t) @f$ are built once per
 *     *observed* inter-photon gap (via the associative pair-power / binary
 *     exponentiation recursion), not densely for every tick up to the maximum
 *     gap.
 *  2. **Deferred @f$\rho@f$ contraction.** The forward/backward hot loop stays
 *     @f$ O(N n^2) @f$ by accumulating a per-slot transition weight ``W``; the
 *     @f$ O(n_{slots} n^4) @f$ contraction @f$ \xi = \sum W \cdot \rho @f$ is
 *     done once in the serial reduction rather than per photon.
 *
 * Bursts are processed in parallel with OpenMP; each thread keeps thread-local
 * Baum-Welch accumulators (no false sharing) that are reduced serially.  EM is
 * accelerated with SQUAREM (Varadhan & Roland 2008, scheme S3), reaching the
 * identical EM fixed point in far fewer maps.
 */
class H2MM {
public:
    H2MM() = default;

    /**
     * @brief Load per-burst photon streams into the engine's CSR layout.
     * @param times Per-burst monotonically non-decreasing integer macro-times.
     * @param streams Matching per-burst photon stream indices in ``[0, n_streams)``.
     * @param n_streams Number of photon streams (detector categories).
     */
    void set_bursts(
        const std::vector<std::vector<long long>>& times,
        const std::vector<std::vector<int>>& streams,
        int n_streams
    );

    /**
     * @brief Build the engine's photon-stream data directly from a TTTR and a
     *        set of bursts, classifying each photon into a stream.
     *
     * Each photon in a burst is assigned to the first ``stream_channels`` entry
     * (a tttrlib::Channel: routing channel(s) + inclusive micro-time windows)
     * that it matches; photons matching no stream are dropped.  Bursts with
     * fewer than ``min_photons`` matched photons are skipped.  Macro times are
     * optionally down-scaled by integer ``time_scale`` (>=1) to coarsen the
     * clock (fewer unique Δt, smaller caches).  Mirrors the ChiSurf
     * ``extract_burst_photons`` step so H2MM runs straight off a burst search.
     *
     * @param tttr Photon stream.
     * @param bursts Interleaved half-open index ranges ``[s0,e0,...]`` as a
     *        pointer/length pair (long long so the SWIG IN_ARRAY1 typemaps
     *        apply: NumPy int array from Python, numeric vector from R,
     *        long[] from Java).
     * @param n_bursts Length of the bursts array (2x the number of bursts).
     * @param stream_channels Stream definitions; the stream index is the entry
     *        position (``n_streams = stream_channels.size()``).
     * @param min_photons Minimum matched photons for a burst to be kept.
     * @param time_scale Integer macro-time down-scaling factor (>=1).
     */
    void set_bursts_from_tttr(
        std::shared_ptr<TTTR> tttr,
        long long* bursts, int n_bursts,
        const std::vector<std::shared_ptr<Channel>>& stream_channels,
        int min_photons = 3,
        long long time_scale = 1
    );

    /**
     * @brief Convenience: build photon-stream data from a BurstFilter's bursts.
     */
    void set_bursts_from_filter(
        std::shared_ptr<BurstFilter> burst_filter,
        const std::vector<std::shared_ptr<Channel>>& stream_channels,
        int min_photons = 3,
        long long time_scale = 1
    );

    int get_n_bursts() const { return static_cast<int>(offsets_.empty() ? 0 : offsets_.size() - 1); }
    long long get_n_photons() const { return static_cast<long long>(streams_.size()); }
    int get_n_streams() const { return n_streams_; }
    /// Sorted unique inter-photon gaps used to key the caches.
    std::vector<long long> get_unique_dt() const;

    /**
     * @brief Baum-Welch (EM) optimisation of an H2MM model.
     * @param init Initial model (not modified).
     * @param max_iter Maximum EM-map evaluations.
     * @param tol Convergence threshold on the log-likelihood increment.
     * @param min_trans Floor for off-diagonal transitions (keeps the chain irreducible).
     * @param accelerate Use SQUAREM extrapolation (identical fixed point, fewer maps).
     * @param single_precision Approximate fast mode: run the A^Δt/ρ caches and the
     *        forward-backward hot loop in float32 to roughly halve their memory
     *        bandwidth (model parameters and Baum-Welch reductions stay double).
     *        The log-likelihood then carries float32 round-off (~1e-2), so the
     *        convergence threshold is floored at 1e-3; use for exploratory fits on
     *        very large datasets, not for final numbers.
     * @return The optimised model with loglik / n_iter / n_phot / converged set.
     */
    H2mmModel optimize(
        const H2mmModel& init,
        int max_iter = 500,
        double tol = 1e-7,
        double min_trans = 1e-12,
        bool accelerate = true,
        bool single_precision = false
    );

    /**
     * @brief Most-likely hidden-state path per photon (Viterbi) plus ICL.
     * @param model A (usually optimised) model.
     * @param output Per-photon state index, length N (NumPy-owned).
     * @param n_output Length of output.
     * @param icl Integrated Complete Likelihood @f$ -2\log L_{path} + k\ln N @f$.
     */
    void viterbi(
        const H2mmModel& model,
        long long** output, int* n_output,
        double* icl
    );

    /**
     * @brief Build a reasonable initial model for EM (near-identity transitions,
     *        spread emission profiles).
     */
    static H2mmModel factory_model(
        int n_states, int n_streams, double trans_scale = 1e-4, int seed = -1
    );

    /**
     * @brief Fit an ``n_states`` model, keeping the best of ``n_restarts`` runs.
     */
    H2mmModel fit(
        int n_states, int n_restarts = 1,
        int max_iter = 500, double tol = 1e-7, int seed = 0,
        bool accelerate = true, bool single_precision = false
    );

    /**
     * @brief Monte-Carlo sample photon streams from a model along given time axes.
     *        The hidden chain is advanced tick-by-tick with the one-step ``trans``
     *        matrix (exercises the exact @f$ A^{\Delta t} @f$ propagation).
     */
    static std::vector<std::vector<int>> simulate_bursts(
        const H2mmModel& model,
        const std::vector<std::vector<long long>>& burst_times,
        int seed = -1
    );

private:
    // CSR photon data (see ChiSurf BurstPhotons).
    std::vector<int32_t> streams_;      // per-photon stream index
    std::vector<int32_t> gap_slot_;     // slot of Δt to next photon, -1 at burst end
    std::vector<int64_t> offsets_;      // CSR burst offsets, length n_bursts+1
    std::vector<int64_t> unique_dt_;    // sorted unique inter-photon Δt (>0)
    int n_streams_ = 0;

    // Build A^Δt and ρ(Δt) caches (pair-power binary exponentiation).
    void fill_caches(
        const std::vector<double>& A, int n,
        std::vector<double>& pow_cache, std::vector<double>& rho_cache,
        h2mm_detail::ForkJoinPool* pool = nullptr
    ) const;

    // Scaled forward-backward + Baum-Welch accumulation over all bursts.
    // Returns the total log-likelihood; fills xi/gamma_obs/prior accumulators.
    double estep(
        const std::vector<double>& prior,
        const std::vector<double>& obs,
        const std::vector<double>& pow_cache,
        const std::vector<double>& rho_cache,
        int n, int p,
        std::vector<double>& xi_acc,
        std::vector<double>& gamma_obs_acc,
        std::vector<double>& prior_acc,
        h2mm_detail::ForkJoinPool* pool = nullptr
    ) const;
};

} // namespace tttrlib

#endif // TTTRLIB_H2MM_H

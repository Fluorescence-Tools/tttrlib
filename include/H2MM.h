// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_H2MM_H
#define TTTRLIB_H2MM_H

#include <vector>
#include <cstdint>
#include <cmath>
#include <limits>
#include <memory>
#include <string>

#include "TTTR.h"

class TTTRMask;      // forward declaration (header included in H2MMState.cpp)

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

/// Sentinel state / stream index for photons no decoder assigned (outside every
/// burst, matching no stream, or in a burst the engine skipped).
constexpr uint8_t H2MM_UNASSIGNED = 255;

/// Largest routing-channel id a PTU record can hold (``unsigned channel :6``).
constexpr int H2MM_PTU_MAX_CHANNEL = 63;

/**
 * @brief Routing-channel ids allocated by a state split.
 *
 * Path A of persisting a decoded state assignment rewrites each photon's
 * routing channel so the state is visible to every tool that reads the file.
 *
 * **The whole id space is compacted, not just the new ids.** A source file's
 * channels are usually sparse — 1, 12 and 30 for three detectors is ordinary —
 * and those gaps are dead weight in a field only a few bits wide. So the used
 * source ids are first compressed to ``0..k-1`` in ascending order, and the
 * ``(stream, state)`` pairs are allocated immediately after, **densely, step
 * 1**, stream major / state minor:
 *
 * ```
 * source 1, 12, 30   ->  0, 1, 2          (compressed, in ascending order)
 * (stream 0, state 0) -> 3    (stream 1, state 0) -> 5
 * (stream 0, state 1) -> 4    (stream 1, state 1) -> 6
 * ```
 *
 * Every id in the output file is then in ``[0, k + n_streams*n_states)`` with no
 * holes, which is what decides whether the result still fits a narrow container:
 * left uncompressed, the example above would keep a photon on id 30 and need 5
 * bits to store a file that actually has 7 distinct channels.
 *
 * Both directions are recorded, so nothing is lost: ``used_channels[i]`` is the
 * original id and ``compressed_channels[i]`` the id it became. The map is a
 * published lookup table — written into the state sidecar — not something a
 * reader is expected to derive from arithmetic.
 */
struct H2mmChannelMap {
    int n_streams = 0;
    int n_states = 0;
    /// Allocated id per (stream, state), row-major ``[stream * n_states + state]``.
    std::vector<int> channels;
    /// Source routing-channel ids that were in use, ascending.
    std::vector<int> used_channels;
    /// Compressed id each ``used_channels`` entry was moved to (``0..k-1``).
    std::vector<int> compressed_channels;
    /// Largest id the target container's record field can hold.
    int max_channel = H2MM_PTU_MAX_CHANNEL;

    /// Allocated id for a (stream, state) pair, or ``-1`` if out of range.
    int channel_for(int stream, int state) const {
        if (stream < 0 || state < 0 || stream >= n_streams || state >= n_states)
            return -1;
        return channels[static_cast<size_t>(stream) * n_states + state];
    }
    /// Compressed id for an original source channel, or ``-1`` if unknown.
    int compressed_for(int source_channel) const {
        for (size_t i = 0; i < used_channels.size(); ++i)
            if (used_channels[i] == source_channel)
                return i < compressed_channels.size() ? compressed_channels[i] : -1;
        return -1;
    }
    /// Highest id the split will write; ``-1`` for an empty map.
    int highest_channel() const {
        int hi = -1;
        for (int c : compressed_channels) hi = std::max(hi, c);
        for (int c : channels) hi = std::max(hi, c);
        return hi;
    }
    std::string to_json() const;
    static H2mmChannelMap from_json(const std::string& payload);
};

/**
 * @brief A decoded per-photon state assignment, persisted as a msgpack sidecar.
 *
 * Path B of persisting a decoded assignment: the source file is left untouched
 * and the assignment travels beside it.  What is stored is the per-photon state
 * **array**, not N masks — an assignment is a partition, so one ``uint8`` per
 * photon holds everything N bitmasks would.  ``mask_for_state`` materialises a
 * ``TTTRMask`` on demand to feed the existing selection machinery.
 *
 * The payload is msgpack rather than JSON because the arrays are one entry per
 * photon: nlohmann's ``json::binary`` values survive the msgpack round trip as a
 * native ``bin`` field, so a 10 M-photon assignment is 10 MB of bytes instead of
 * ~20 MB of decimal text.
 */
struct H2mmStateSidecar {
    /// Per-photon state over the **source** photon range; ``H2MM_UNASSIGNED``
    /// for photons no burst/stream claimed.
    std::vector<uint8_t> states;
    /// Per-photon stream index over the same range, same sentinel.
    std::vector<uint8_t> streams;
    int n_states = 0;
    int n_streams = 0;
    /// ``"viterbi"``, ``"jitter"`` or ``"ffbs"`` — how ``states`` was produced.
    std::string decoder;
    /// Seed of the draw (meaningless for ``"viterbi"``).
    long long seed = 0;
    /// Model the decode ran under.
    H2mmModel model;
    H2mmChannelMap channel_map;
    bool has_channel_map = false;

    void write(const std::string& filename) const;
    static H2mmStateSidecar read(const std::string& filename);

    /// Number of photons assigned to ``state``.
    long long count_state(int state) const;
    /**
     * @brief Mask selecting exactly the photons in ``state``.
     *
     * ``TTTRMask`` bits mark **excluded** events, so every photon outside
     * ``state`` is masked and ``get_indices(true)`` returns the state's photons.
     */
    std::shared_ptr<TTTRMask> mask_for_state(int state) const;
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
     * @param bursts Interleaved **inclusive** index ranges ``[s0,e0,...]`` (both
     *        ends belong to the burst, so it spans ``e - s + 1`` photons) as a
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
        long long* bursts, int n_bursts, int n_cols,
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

    /// Per-photon stream index (CSR values), length ``get_n_photons()``.
    const std::vector<int32_t>& get_streams() const { return streams_; }
    /// Per-photon slot of @f$\Delta t@f$ to the next photon; ``-1`` at each burst end.
    const std::vector<int32_t>& get_gap_slot() const { return gap_slot_; }
    /// CSR burst offsets, length ``get_n_bursts() + 1``.
    const std::vector<int64_t>& get_offsets() const { return offsets_; }
    /**
     * @brief Index in the source TTTR of each photon in the CSR layout.
     *
     * Length ``get_n_photons()``, or empty when the bursts were supplied as
     * plain arrays through ``set_bursts`` (there is no source file to point
     * at).  Both persistence paths need it — Path A to rewrite the right
     * records, Path B to place the state at the right photon.
     */
    const std::vector<int64_t>& get_photon_index() const { return photon_index_; }
    /// Number of photons in the source TTTR (0 when set through ``set_bursts``).
    long long get_n_source_photons() const { return n_source_photons_; }

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
     * @brief Per-photon posterior state probabilities @f$\gamma@f$.
     *
     * @f$ \gamma_t(i) = P(s_t = i \mid \text{data}, \lambda) @f$ from the scaled
     * forward-backward recursion — the same quantity the reference ``H2MM_C``
     * calls ``gamma``, and the one the E-step forms and contracts away.  Rows
     * sum to 1.
     *
     * Unlike ``viterbi`` this is a *distribution*, not an assignment: it is what
     * to use when the question is "how do the photons distribute over states"
     * rather than "what is the single most likely sequence".
     *
     * @param model A (usually optimised) model.
     * @param output ``(N, n_states)`` row-major float32 matrix (NumPy-owned).
     * @param n_rows N, the photon count.
     * @param n_cols ``n_states``.
     * @param n_underflow Photons whose forward scale underflowed to zero; their
     *        rows carry no information and are returned uniform (``1/n``).
     *        Non-zero means the model assigns (near-)zero probability to part of
     *        the data — treat the decode with suspicion.
     */
    void posterior(
        const H2mmModel& model,
        float** gamma_out, int* gamma_rows, int* gamma_cols,
        long long* n_underflow
    );

    /**
     * @brief Draw each photon's state independently from its @f$\gamma@f$ row.
     *
     * The cheap faithful decoder.  Viterbi answers "most likely sequence" and
     * therefore reports the photon distribution winner-takes-all: photons at
     * @f$\gamma = (0.7, 0.3)@f$ all land in state 0 and the 30 % is erased.
     * Drawing from @f$\gamma@f$ reproduces the marginal by construction, so
     * well-separated states stop being inflated and ambiguous ones stop
     * vanishing.
     *
     * @warning The draws are **independent per photon**, so the sampled path
     * has none of @f$\gamma@f$'s temporal correlation: a solid state at
     * @f$\gamma = (0.9, 0.1)@f$ fragments into spurious one-photon dwells.
     * Use it for per-photon questions (occupancies, per-state decays, per-state
     * spectra); use ``sample_paths`` when dwell times, transition counts or
     * path-level error bars matter.
     *
     * @param model A (usually optimised) model.
     * @param seed Seed of the counter-based per-photon RNG.  Output is
     *        reproducible and **independent of the thread count**.
     * @param output Per-photon state index, length N (NumPy-owned).
     * @param n_output Length of output.
     * @param n_underflow Photons whose @f$\gamma@f$ row underflowed (drawn
     *        uniformly).
     */
    void sample_states(
        const H2mmModel& model, long long seed,
        long long** output, int* n_output,
        long long* n_underflow
    );

    /**
     * @brief Draw whole state trajectories from @f$P(\text{path} \mid \text{data})@f$ (FFBS).
     *
     * Forward filtering, backward sampling: the shared scaled forward pass, then
     * @f$ s_N \sim \alpha_N @f$ and
     * @f$ s_t \sim \alpha_t(i)\,A^{\Delta t}[i, s_{t+1}] @f$ backwards.  Each
     * draw is an exact sample from the joint posterior, so it keeps
     * @f$\gamma@f$'s temporal correlation: the per-photon marginal of many draws
     * converges to @f$\gamma@f$ *and* the dwell-time statistics are valid,
     * which independent per-photon draws (``sample_states``) cannot give.
     *
     * Averaging a quantity over ``n_samples`` draws is multiple imputation: the
     * spread across draws is the decoding uncertainty that a single Viterbi path
     * reports as zero.
     *
     * @param model A (usually optimised) model.
     * @param seed Seed of the counter-based RNG (thread-count independent).
     * @param n_samples Number of independent trajectories.
     * @param output ``(n_samples, N)`` row-major int64 matrix (NumPy-owned).
     * @param n_rows ``n_samples``.
     * @param n_cols N, the photon count.
     */
    void sample_paths(
        const H2mmModel& model, long long seed, int n_samples,
        long long** paths_out, int* path_rows, int* path_cols
    );

    // -----------------------------------------------------------------------
    // Persisting a decoded assignment
    // -----------------------------------------------------------------------

    /**
     * @brief Spread a CSR-ordered decode over the full source photon range.
     * @param path Per-photon state, length ``get_n_photons()`` (from any decoder).
     * @param n_path Length of path.
     * @param output Per-photon state over ``get_n_source_photons()`` photons,
     *        ``H2MM_UNASSIGNED`` where no burst/stream claimed the photon.
     * @param n_output Length of output.
     */
    void photon_states(
        const long long* path, int n_path,
        unsigned char** states_out, int* n_states_out
    ) const;

    /**
     * @brief Per-photon stream index over the full source photon range.
     *        ``H2MM_UNASSIGNED`` where no burst/stream claimed the photon.
     */
    void photon_stream_index(unsigned char** streams_out, int* n_streams_out) const;

    /**
     * @brief Allocate the routing-channel ids a state split will write.
     *
     * The source file's used ids are compressed to ``0..k-1`` and the
     * ``(stream, state)`` pairs allocated immediately after, densely, stream
     * major / state minor — see ``H2mmChannelMap``.  The output therefore uses
     * ``k + n_streams*n_states`` consecutive ids starting at 0, whatever the
     * source numbering looked like.
     *
     * @param src Source photon stream (its used channels are compressed).
     * @param n_states Number of hidden states.
     * @param max_channel Largest id the *target container's record field* can
     *        hold.  This is the real budget, not the in-memory ``signed char``:
     *        PTU HydraHarp T2/T3 store ``unsigned channel :6`` (0..63), which is
     *        why PTU is the assumed target.  Narrower containers truncate
     *        **silently** rather than failing — PicoHarp and SPC-130 keep 4 bits
     *        (an id of 40 reads back as 8), SPC-600/256 keeps 3 — so writing a
     *        split to one of those quietly merges states into each other.
     * @throws std::runtime_error naming the numbers when the ids do not fit;
     *         the mask path (``state_sidecar``) has no budget.
     */
    H2mmChannelMap build_channel_map(
        std::shared_ptr<TTTR> src, int n_states,
        int max_channel = H2MM_PTU_MAX_CHANNEL
    ) const;

    /**
     * @brief Copy ``src`` with each photon's routing channel replaced by the id
     *        ``map`` allocated for it.
     *
     * Every photon stays in the one file.  Photons a decoder assigned move to
     * their ``(stream, state)`` id; photons it did not — outside every burst, or
     * matching no stream — move to the **compressed** form of the channel they
     * were already on, so they stay distinguishable while the file's id space
     * stays dense.  Write the result with ``TTTR::write`` — per-state decays,
     * FCS and burst analyses are then ordinary channel selections.
     *
     * @throws std::runtime_error if a photon sits on a channel ``map`` does not
     *         know, which means the map was built from a different file.
     */
    std::shared_ptr<TTTR> split_routing_channels(
        std::shared_ptr<TTTR> src,
        const long long* path, int n_path,
        const H2mmChannelMap& map
    ) const;

    /**
     * @brief Bundle a decode into the sidecar object (Path B).
     * @param path Per-photon state from any decoder, length ``get_n_photons()``.
     * @param n_path Length of path.
     * @param model Model the decode ran under.
     * @param decoder ``"viterbi"``, ``"jitter"`` or ``"ffbs"``.
     * @param seed Seed of the draw (0 for Viterbi).
     * @param map Optional channel map to record alongside, when Path A also ran.
     */
    H2mmStateSidecar state_sidecar(
        const long long* path, int n_path,
        const H2mmModel& model,
        const std::string& decoder = "viterbi",
        long long seed = 0,
        const H2mmChannelMap* map = nullptr
    ) const;

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
    std::vector<int64_t> photon_index_; // index in the source TTTR, or empty
    long long n_source_photons_ = 0;    // photons in the source TTTR, or 0
    int n_streams_ = 0;

    // Shared by set_bursts and the TTTR-backed loaders; `indices` is null when
    // there is no source file to point back at.
    void set_bursts_impl(
        const std::vector<std::vector<long long>>& times,
        const std::vector<std::vector<int>>& streams,
        int n_streams,
        const std::vector<std::vector<int64_t>>* indices,
        long long n_source_photons
    );

    // Build A^Δt only (no ρ), for the decoders and Viterbi which never need the
    // expected-transition tensor.  Bit-identical to fill_caches' pow output.
    void fill_pow_cache(
        const std::vector<double>& A, int n, std::vector<double>& pow_cache
    ) const;

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

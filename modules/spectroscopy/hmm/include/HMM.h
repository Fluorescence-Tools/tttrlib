// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_HMM_H
#define TTTRLIB_HMM_H

// Validation: A/B-TESTED 2026-08-17 -- vs H2MM_C 2.2.1 (recorded fixture; same model, same bursts): total and
//   per-burst log-likelihood 1e-9 rel, gamma 2e-7 (float32), Viterbi paths identical, one
//   Baum-Welch step 1e-13; ICL term = exact complete-data path log-likelihood (NumPy).
//   test/python/hmm/test_ab_hmm_reference.py.
//   Register: okf/testing/algorithm-validation.md

#include <vector>
#include <cstdint>
#include <cmath>
#include <limits>
#include <memory>
#include <string>

#include "TTTR.h"
#include "HMMRestraints.h"
#include "HMMConstraints.h"
#include "HMMEmission.h"
#include "HMMBayes.h"

class TTTRMask;      // forward declaration (header included in HMMState.cpp)

namespace tttrlib {

class Channel;       // forward declarations (headers included in HMM.cpp)
class BurstFilter;

namespace hmm_detail { class ForkJoinPool; }  // persistent worker pool (HMM.cpp)

/**
 * @brief An HMM model (@f$ \lambda = \{\pi, A, B\} @f$) plus diagnostics.
 *
 * Row-major storage:
 *  - ``prior`` — length ``n_states``, initial-state probabilities.
 *  - ``trans`` — ``n_states * n_states``, one-base-tick row-stochastic
 *    transition matrix (``trans[i*n + j] = P(state j at t+1 | state i at t)``).
 *  - ``obs``   — ``n_states * n_symbols``, row-stochastic emission matrix
 *    (``obs[i*p + y] = P(symbol y | state i)``).
 *
 * A **symbol** is a detection stream when ``n_micro_bins == 1`` (the classic
 * case) and the product ``stream * n_micro_bins + micro_bin`` otherwise. The
 * recursions read ``obs[i*p + y]`` and never ask which it is, so the micro-time
 * axis costs the engine nothing — it is a wider table, not a second code path.
 */
struct HmmModel {
    std::vector<double> prior;
    std::vector<double> trans;
    std::vector<double> obs;
    double loglik = -std::numeric_limits<double>::infinity();
    /*!
     * \brief `loglik + log p(theta)` — the objective the fit actually climbed.
     *
     * Equal to `loglik` unless restraints were supplied, because **only
     * restraints are scored**: a constraint is imposed on the parameters and
     * contributes nothing here. That difference is the whole reason the two are
     * separate types, and this field is where it becomes observable.
     *
     * Report this, not `loglik`, when comparing two restrained fits — the
     * marginal likelihood alone ranks them by a criterion neither one was
     * optimising. Note `bic()` is deliberately defined on `loglik` and is valid
     * for the unrestrained (MLE) path only.
     */
    double logpost = -std::numeric_limits<double>::infinity();
    int n_iter = 0;
    long long n_phot = 0;
    bool converged = false;
    /*!
     * \brief Micro-time bins per stream; ``1`` for the stream-only alphabet.
     *
     * Carried explicitly because it cannot be recovered from ``obs``: a table
     * of 128 columns is 128 streams or 4 streams × 32 bins, and nothing in the
     * numbers says which. Only the *split* of the alphabet is ambiguous, never
     * its size, so this is the one thing the model has to be told.
     */
    int n_micro_bins = 1;

    HmmModel() = default;
    HmmModel(
        std::vector<double> prior_,
        std::vector<double> trans_,
        std::vector<double> obs_
    ) : prior(std::move(prior_)), trans(std::move(trans_)), obs(std::move(obs_)) {}

    int n_states() const { return static_cast<int>(prior.size()); }
    /// Alphabet size — the width of ``obs``, i.e. ``n_streams * n_micro_bins``.
    int n_symbols() const {
        int n = n_states();
        return n > 0 ? static_cast<int>(obs.size() / n) : 0;
    }
    /// Detection streams; equals ``n_symbols()`` unless a micro-time axis is set.
    int n_streams() const {
        int m = n_micro_bins > 0 ? n_micro_bins : 1;
        return n_symbols() / m;
    }
    /*!
     * \brief Number of free parameters (used by BIC/ICL).
     *
     * Counted over the **alphabet**, so a free categorical emission on a
     * product alphabet is charged for every column it actually estimates. A
     * *parameterised* emission has far fewer — a lifetime spectrum is a handful
     * of numbers whatever the binning — and this count does not know that, so
     * `bic()` is valid for the free-categorical path only.
     */
    int n_free() const {
        int n = n_states(), p = n_symbols();
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

/*!
 * \brief One E-step's sufficient statistics and score, for an external model.
 *
 * The E-step already computes every one of these and then throws them away at
 * the end of each EM map. Exposing them is what lets a host — a joint
 * structural model, say — drive its own optimiser or sampler over an HMM
 * submodel without ever touching photon data again: the statistics summarise
 * the whole dataset at the model's dimension, not the data's.
 *
 * The counts are **raw expected counts**, before any restraint or constraint.
 * That is deliberate: a consumer supplying its own prior needs the likelihood's
 * contribution alone, and anything else would double-count.
 */
struct HmmEval {
    /// Marginal log-likelihood of the model under the loaded data.
    double loglik = -std::numeric_limits<double>::infinity();
    /// Expected one-tick transition counts, ``n_states * n_states``.
    std::vector<double> xi;
    /// Expected per-(state, symbol) counts, ``n_states * n_symbols``.
    std::vector<double> gamma_obs;
    /// Expected initial-state counts, ``n_states`` (summed over bursts).
    std::vector<double> prior_counts;
    /*!
     * \brief @f$ \partial \log L / \partial \theta @f$, packed as
     *        ``[prior | trans | obs]``.
     *
     * By Fisher's identity the gradient of the *marginal* log-likelihood equals
     * the expectation of the complete-data score, which for a multinomial
     * parameter is just ``count / parameter``. So the same E-step that produced
     * the counts produces the gradient — no extra pass, and no finite
     * differences.
     *
     * \warning **Meaningful along the simplex, not off it.** For `prior` and
     * `obs` these match central finite differences entry by entry. For `trans`
     * only *differences within a row* do: the @f$A^{\Delta t}@f$ cache
     * row-normalises after every composition, which is a no-op for a
     * row-stochastic matrix but makes the likelihood invariant to scaling a
     * row, so the component of the gradient that leaves the simplex is an
     * artifact of that renormalisation. Since any consumer moves along the
     * simplex anyway — a transition row must keep summing to 1 — this costs
     * nothing in practice, but a finite-difference check must perturb
     * `(A_ij + h, A_ik - h)` rather than one entry alone, or it will appear to
     * disagree by a constant per row.
     */
    std::vector<double> score;
};

/// Sentinel state / stream index for photons no decoder assigned (outside every
/// burst, matching no stream, or in a burst the engine skipped).
constexpr uint8_t HMM_UNASSIGNED = 255;

/*!
 * \brief Largest routing-channel id a record field of @p bits can hold.
 *
 * The limit is a property of the *target container*, not of this engine, and it
 * differs per format:
 *
 *   | container                    | bits | max id |
 *   |------------------------------|------|--------|
 *   | PTU HydraHarp T2/T3          |  6   |   63   |
 *   | PicoHarp T3, Becker&Hickl SPC-130 |  4   |   15   |
 *   | SPC-600/256                  |  3   |    7   |
 *
 * Prefer `hmm_max_channel(bits)` over a literal, so the intent survives when a
 * new format is added: a narrower container does not error, it **truncates
 * silently**, which is the failure this constant exists to prevent.
 */
constexpr int hmm_max_channel(int bits) { return (1 << bits) - 1; }

/*!
 * \brief Default channel budget — the 6-bit field of PTU HydraHarp T2/T3.
 *
 * A default, not a universal limit. Writing to a narrower container requires
 * passing its own `hmm_max_channel(bits)`; 63 ids would otherwise be allocated
 * and then quietly truncated on write.
 */
constexpr int HMM_DEFAULT_MAX_CHANNEL = hmm_max_channel(6);

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
struct HmmChannelMap {
    int n_streams = 0;
    int n_states = 0;
    /// Allocated id per (stream, state), row-major ``[stream * n_states + state]``.
    std::vector<int> channels;
    /// Source routing-channel ids that were in use, ascending.
    std::vector<int> used_channels;
    /// Compressed id each ``used_channels`` entry was moved to (``0..k-1``).
    std::vector<int> compressed_channels;
    /// Largest id the target container's record field can hold.
    int max_channel = HMM_DEFAULT_MAX_CHANNEL;

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
    /**
     * @brief Build a map from a bare list of source channels.
     *
     * The allocation itself, without an ``HMM`` engine — for callers that
     * assembled their photon streams some other way (several source files, a
     * burst table, a nanotime-split stream set) and still want *this* id
     * layout rather than a second, subtly different one.
     * ``HMM::build_channel_map`` is a thin wrapper over it.
     *
     * @param used_channels Routing-channel ids present in the source data
     *        (duplicates and order do not matter).
     * @param n_streams Number of photon streams.
     * @param n_states Number of hidden states.
     * @param max_channel Largest id the target container's record field holds.
     * @throws std::runtime_error naming the numbers when the ids do not fit.
     */
    static HmmChannelMap allocate(
        const std::vector<int>& used_channels,
        int n_streams, int n_states,
        int max_channel = HMM_DEFAULT_MAX_CHANNEL
    );

    /// Highest id the split will write; ``-1`` for an empty map.
    int highest_channel() const {
        int hi = -1;
        for (int c : compressed_channels) hi = std::max(hi, c);
        for (int c : channels) hi = std::max(hi, c);
        return hi;
    }
    std::string to_json() const;
    static HmmChannelMap from_json(const std::string& payload);
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
struct HmmStateSidecar {
    /// Per-photon state over the **source** photon range; ``HMM_UNASSIGNED``
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
    HmmModel model;
    HmmChannelMap channel_map;
    bool has_channel_map = false;

    void write(const std::string& filename) const;
    static HmmStateSidecar read(const std::string& filename);

    /**
     * @brief Fill ``states`` and ``streams`` from plain arrays.
     *
     * ``HMM::state_sidecar`` is the usual way in; this is for callers that
     * derived the assignment themselves — from several source files, or from a
     * decode the engine did not perform — so that they still write *this*
     * format rather than inventing a parallel one.  The two arrays must be the
     * same length (one entry per photon of the source file).
     */
    void set_arrays(
        unsigned char* states_in, int n_states_in,
        unsigned char* streams_in, int n_streams_in
    );

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
 * @brief Photon-by-photon Hidden Markov Model engine.
 *
 * The class is named for the *model*, not for one way of fitting it: `fit` runs
 * the published **H2MM** maximum-likelihood algorithm, and `optimize` with
 * restraints runs MAP over the same recursions. Both share one core, which
 * neither knows nor cares what a symbol means — so further inference paths and
 * richer alphabets attach here without disturbing either.
 *
 * A fast C++ port of the ChiSurf numba HMM engine — itself a re-implementation
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
 *
 * \warning **One molecule per burst is assumed, and violating it manufactures
 * exactly the signal this class exists to detect.**
 *
 * A burst holding two molecules is a *superposition of two independent chains*,
 * not one chain, and a single-chain model has only one way to explain
 * interleaved photons from two sources: rapid switching. Measured on two
 * **static** species (E = 0.25 and 0.75, so the truth contains no dynamics at
 * all and every reported transition is an artifact), median over 8 datasets:
 *
 * | coincident bursts | apparent switching / tick | BIC picks 3 states |
 * |-------------------|---------------------------|--------------------|
 * | 0 %               | none (at the floor)       | 0 of 8             |
 * | **5 %**           | **7.5e-6**                | **8 of 8**         |
 * | 10 %              | 1.3e-5                    | 8 of 8             |
 * | 25 %              | 5.4e-5                    | 8 of 8             |
 * | 50 %              | 7.9e-5                    | 8 of 8             |
 *
 * So **5%** coincidence — ordinary at typical burst-analysis concentrations —
 * already invents kinetics *and* makes the BIC scan report a state that is not
 * there, in every dataset tried. That is a stronger confound than crosstalk or
 * background, which bias `E` by 0.03–0.05 but create neither states nor
 * dynamics.
 *
 * Two failure modes, and the intuitive one is not the dangerous one. With
 * *full* overlap the two species stop being resolved at all — a superposition
 * at every timescale has no time structure to separate, so the fit returns
 * something built from the mixture rather than the species, and the states are
 * destroyed rather than the kinetics invented. With *partial* overlap, which is
 * what diffusion actually produces, the burst contains a real change-point that
 * is not a conformational one, and that is what is read as a transition.
 *
 * Mitigation is upstream, in burst selection: coincident bursts are both longer
 * and brighter, so `BurstFilter`'s duration filter and the per-burst count rate
 * it exposes are the first line, with ALEX stoichiometry the other standard
 * route. Modelling it instead needs a product state space over the two
 * molecules — see the notes on the rate-aware likelihood, which needs the same
 * construction and would additionally make coincidence *detectable*, since two
 * molecules are roughly twice as bright and this likelihood is blind to rate.
 */
class HMM {
public:
    HMM() = default;

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

    /*!
     * \brief Load bursts on the **product alphabet**: stream × micro-time bin.
     *
     * Each photon contributes the symbol ``stream * n_micro_bins + bin``, so
     * the emission table becomes ``P(stream|state) * f_state(bin)`` and a state
     * is constrained by *when* its photons arrive as well as by *where*. That
     * is what separates a dark acceptor from real FRET: both move the intensity
     * ratio, only one moves the donor lifetime.
     *
     * @param times Per-burst monotonically non-decreasing macro-times.
     * @param streams Per-burst stream indices in ``[0, n_streams)``.
     * @param micro_bins Per-burst micro-time bin indices in ``[0, n_micro_bins)``;
     *        out-of-range values are clamped, since a photon at the edge of the
     *        axis is still a photon and dropping it would bias the fit.
     * @param n_streams Number of detection streams.
     * @param n_micro_bins Micro-time bins per stream (``1`` reproduces
     *        `set_bursts` exactly).
     * @param bin_width_ns Width of one micro-time bin, recorded for callers
     *        building an emission table on the same axis; 0 when unknown.
     */
    void set_bursts_micro(
        const std::vector<std::vector<long long>>& times,
        const std::vector<std::vector<int>>& streams,
        const std::vector<std::vector<int>>& micro_bins,
        int n_streams,
        int n_micro_bins,
        double bin_width_ns = 0.0
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
     * ``extract_burst_photons`` step so HMM runs straight off a burst search.
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
     * @param n_micro_bins Micro-time bins per stream. ``1`` (the default) keeps
     *        the classic stream-only alphabet; anything larger bins each
     *        photon's TAC channel over the file's full micro-time range and
     *        emits the product symbol ``stream * n_micro_bins + bin``.
     */
    void set_bursts_from_tttr(
        std::shared_ptr<TTTR> tttr,
        long long* bursts, int n_bursts, int n_cols,
        const std::vector<std::shared_ptr<Channel>>& stream_channels,
        int min_photons = 3,
        long long time_scale = 1,
        int n_micro_bins = 1
    );

    /**
     * @brief Convenience: build photon-stream data from a BurstFilter's bursts.
     */
    void set_bursts_from_filter(
        std::shared_ptr<BurstFilter> burst_filter,
        const std::vector<std::shared_ptr<Channel>>& stream_channels,
        int min_photons = 3,
        long long time_scale = 1,
        int n_micro_bins = 1
    );

    int get_n_bursts() const { return static_cast<int>(offsets_.empty() ? 0 : offsets_.size() - 1); }
    long long get_n_photons() const { return static_cast<long long>(streams_.size()); }
    int get_n_streams() const { return n_streams_; }
    /// Micro-time bins per stream; ``1`` for the stream-only alphabet.
    int get_n_micro_bins() const { return n_micro_bins_; }
    /// Alphabet size the emission table must have: ``n_streams * n_micro_bins``.
    int get_n_symbols() const { return n_streams_ * n_micro_bins_; }
    /*!
     * \brief Width of one micro-time bin in ns, or 0 when it was never supplied.
     *
     * Only bookkeeping — the engine scores bin *indices* and has no use for a
     * time unit. It is recorded so the caller building an emission table builds
     * it on the same axis the data were binned on; a spectrum evaluated on a
     * different grid is wrong in a way no shape check would catch.
     */
    double get_micro_time_bin_width_ns() const { return micro_bin_width_ns_; }
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
     * @brief Baum-Welch (EM) optimisation of an HMM model.
     * @param init Initial model (not modified).
     * @param max_iter Maximum EM-map evaluations.
     * @param tol Convergence threshold on the increment of the objective --
     *        the *penalised* one (`loglik + log p`) whenever restraints are
     *        supplied, since that is the quantity EM is climbing there.
     * @param min_trans Floor for off-diagonal transitions (keeps the chain irreducible).
     * @param accelerate Use SQUAREM extrapolation (identical fixed point, fewer maps).
     * @param single_precision Approximate fast mode: run the A^Δt/ρ caches and the
     *        forward-backward hot loop in float32 to roughly halve their memory
     *        bandwidth (model parameters and Baum-Welch reductions stay double).
     *        The log-likelihood then carries float32 round-off (~1e-2), so the
     *        convergence threshold is floored at 1e-3; use for exploratory fits on
     *        very large datasets, not for final numbers.
     * @param emission Optional **parameterised** emission. When supplied, the
     *        M-step re-fits the spec's decay parameters from the expected
     *        counts instead of freeing every column of `obs`, and the spec is
     *        updated in place so the fitted lifetimes can be read back.
     *        Required on a product alphabet — see the warning on `fit`.
     *        Restraints and constraints on `obs` do not apply, because the
     *        family is itself the constraint; SQUAREM is disabled, because an
     *        extrapolated table need not be reachable from any parameters.
     * @return The optimised model with loglik / logpost / n_iter / n_phot /
     *         converged set.  `logpost == loglik` unless restraints were given.
     */
    /*!
     * \brief EM to a fixed point.  With `restraints`, MAP instead of MLE.
     *
     * Both null is plain Baum-Welch — the classic H2MM algorithm, unchanged.
     *
     * **`restraints` are scored, `constraints` are imposed.**  Restraints add
     * Dirichlet pseudo-counts to the M-step and contribute `log p` to the
     * objective, so the fit trades them against the likelihood.  Constraints
     * pin entries exactly and never enter the objective at all.  Supplying
     * restraints switches **every convergence test to the penalised objective**
     * `logL + log p`.
     * That last part is not optional: under a prior the EM fixed point belongs
     * to the penalised map, so testing the marginal likelihood would stop in
     * the wrong place and would make a monotonicity check pass while the
     * algorithm climbed a different hill.
     *
     * The two paths share one loop rather than being duplicated, so "identical
     * when unconstrained" is true by construction instead of by testing.
     */
    HmmModel optimize(
        const HmmModel& init,
        int max_iter = 500,
        double tol = 1e-7,
        double min_trans = 1e-12,
        bool accelerate = true,
        bool single_precision = false,
        const HmmRestraints* restraints = nullptr,
        const HmmConstraints* constraints = nullptr,
        HmmEmissionSpec* emission = nullptr
    );

    /**
     * @brief Most-likely hidden-state path per photon (Viterbi) plus ICL.
     * @param model A (usually optimised) model.
     * @param output Per-photon state index, length N (NumPy-owned).
     * @param n_output Length of output.
     * @param icl Integrated Complete Likelihood @f$ -2\log L_{path} + k\ln N @f$.
     */
    void viterbi(
        const HmmModel& model,
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
        const HmmModel& model,
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
        const HmmModel& model, long long seed,
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
        const HmmModel& model, long long seed, int n_samples,
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
     *        ``HMM_UNASSIGNED`` where no burst/stream claimed the photon.
     * @param n_output Length of output.
     */
    void photon_states(
        const long long* path, int n_path,
        unsigned char** states_out, int* n_states_out
    ) const;

    /**
     * @brief Per-photon stream index over the full source photon range.
     *        ``HMM_UNASSIGNED`` where no burst/stream claimed the photon.
     */
    void photon_stream_index(unsigned char** streams_out, int* n_streams_out) const;

    /**
     * @brief Allocate the routing-channel ids a state split will write.
     *
     * The source file's used ids are compressed to ``0..k-1`` and the
     * ``(stream, state)`` pairs allocated immediately after, densely, stream
     * major / state minor — see ``HmmChannelMap``.  The output therefore uses
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
    HmmChannelMap build_channel_map(
        std::shared_ptr<TTTR> src, int n_states,
        int max_channel = HMM_DEFAULT_MAX_CHANNEL
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
        const HmmChannelMap& map
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
    HmmStateSidecar state_sidecar(
        const long long* path, int n_path,
        const HmmModel& model,
        const std::string& decoder = "viterbi",
        long long seed = 0,
        const HmmChannelMap* map = nullptr
    ) const;

    /*!
     * \brief One E-step: log-likelihood, sufficient statistics and score.
     *
     * The contract for an external consumer. Everything returned is already
     * computed inside an EM map and then discarded, so this costs exactly one
     * forward-backward pass and no more.
     *
     * The point is that the statistics summarise the dataset at the *model's*
     * dimension rather than the data's, so a host — a joint structural model,
     * say — can drive its own optimiser or sampler over an HMM submodel without
     * re-touching photon data. The score comes free from the same pass by
     * Fisher's identity, which is the difference between gradient-based
     * inference in a host being practical and being a finite-difference sketch.
     */
    HmmEval evaluate(const HmmModel& model) const;

    /*!
     * \brief Blocked Gibbs: draw from the posterior over @f$(\pi, A, B)@f$.
     *
     * EM returns one model; this returns a distribution over them, which is
     * what a credible interval needs and what no amount of post-processing can
     * recover from a point estimate.
     *
     * Every step is conjugate, so there is nothing to tune and no accept rate
     * to watch: sample the photon-level path (FFBS), sample the tick-level
     * bridge through each gap, count, then draw
     * @f$\theta \sim \mathrm{Dirichlet}(\text{counts} + \alpha)@f$.
     *
     * \param n_burnin Sweeps to discard before keeping any.
     * \param n_chains Independent chains; chains after the first start from a
     *        draw off the prior, because R-hat comparing identical starts
     *        measures nothing.
     * \param restraints Dirichlet concentrations; flat if null. Used **as-is**,
     *        not as `alpha - 1` — the MAP M-step wants the mode, a sampler
     *        wants the distribution, and conflating them is the classic
     *        off-by-one that yields a quietly biased "calibrated" sampler.
     * \param thin Keep one draw in `thin`. Set it from a measured ESS rather
     *        than by habit: autocorrelation depends strongly on how much data
     *        each burst carries, and a thinning factor carried over from a
     *        different dataset is how a posterior ends up over-confident.
     *
     * \warning `sample` is a *sampler*, so its cost is per sweep and a sweep is
     * not cheaper than an EM iteration — the bridge costs roughly what skipping
     * the rho cache saves. Budget accordingly.
     *
     * \param emission Optional **parameterised** emission, the sampling
     *        counterpart of `optimize`'s. Required on a product alphabet: left
     *        null, the emission is drawn as a free categorical and the sampler
     *        explores the degenerate family the parameterised M-step exists to
     *        avoid — measured on 64 micro-time bins, R-hat stalls at 1.04 after
     *        2000 burn-in sweeps, and the failure is in the *transitions*, not
     *        the emission, because a wandering emission corrupts the sampled
     *        paths the transition counts come from. Supplied, the stream split
     *        stays a conjugate Dirichlet draw and each lifetime gets one
     *        univariate slice update. The spec is updated in place.
     */
    HmmPosterior sample(
        const HmmModel& init, int n_draws, int n_burnin = 0, int n_chains = 1,
        long long seed = 0, const HmmRestraints* restraints = nullptr,
        int thin = 1, HmmEmissionSpec* emission = nullptr
    ) const;

    /**
     * @brief Build a reasonable initial model for EM (near-identity transitions,
     *        spread emission profiles).
     * @param n_symbols Alphabet size — the width of ``obs``. With a micro-time
     *        axis this is ``n_streams * n_micro_bins``, not the stream count.
     * @param n_micro_bins Recorded on the model so it can report its own stream
     *        count; it does not change the initial numbers.
     */
    static HmmModel factory_model(
        int n_states, int n_symbols, double trans_scale = 1e-4, int seed = -1,
        int n_micro_bins = 1
    );

    /*!
     * \brief Fit an ``n_states`` model, keeping the best of ``n_restarts`` runs.
     *
     * Free categorical emission — every column of `obs` re-estimated from the
     * E-step counts. That is the classic H2MM fit and it is the right thing on
     * the stream-only alphabet.
     *
     * \warning **Not on a product alphabet**, and this applies to `optimize`
     * too — anything that re-estimates the emission freely.
     *
     * The reason is structural, not a matter of resolution. Micro-time bins are
     * not free parameters: they are one smooth decay, of about two parameters,
     * sampled onto the TAC grid. Fitting them as independent columns discards
     * that and admits models no decay can produce — an exact zero in the
     * *interior* of an exponential, declaring a photon impossible at 3 ns while
     * allowing it at 2 and 4. EM finds those because they are in the family: a
     * state that calls a symbol impossible pays nothing for photons it never
     * has to explain, and cannot climb back out.
     *
     * So this is worse than a search failure. Started at the **exact generating
     * model**, free EM sometimes walks away from it — three of sixteen runs
     * over four macro-time seeds and four bin counts fell from ~0.78 per-photon
     * accuracy to ~0.51, and *not* monotonically in bins, which is exactly why
     * no bin count should be read as the safe one. More bins only widen a
     * family that was already wrong.
     *
     * **The remedy is `optimize(..., emission)`**, which re-fits the spec's
     * decay parameters instead of freeing the columns. The degenerate solution
     * is then unreachable — no lifetime spectrum can put a zero mid-decay — and
     * on the configuration where free EM falls from 0.775 to 0.509 it reaches
     * 0.776 *starting from a deliberately wrong seed*. Use `fit` for the
     * stream-only alphabet; use `optimize` with a spec for a product one.
     *
     * Note what this is *not*: the per-photon likelihood needs no correction for
     * counting statistics. No micro-time histogram is ever formed — conditioning
     * on the photon count, a Poisson likelihood factorises into `Poisson(N)`
     * times exactly the per-photon categorical this engine scores, so
     * resolution is a scoring-accuracy knob and free of statistical cost. The
     * sparse histogram appears only *here*, in estimating one number per bin.
     *
     * Nor is smoothing the fix. Dirichlet restraints on the emission remove
     * every zero — the `log(0)` hazard really does go away — and leave accuracy
     * near chance, because the obstacle is the parameter count, not the
     * sparsity. Build the table from a `HmmEmissionSpec` instead: a lifetime
     * spectrum is a handful of numbers at any binning, and its accuracy is flat
     * from 32 to 1024 bins.
     */
    HmmModel fit(
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
        const HmmModel& model,
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
    int n_micro_bins_ = 1;              // micro-time bins per stream
    double micro_bin_width_ns_ = 0.0;   // bookkeeping only (see accessor)

    // Shared by set_bursts and the TTTR-backed loaders; `indices` is null when
    // there is no source file to point back at, `micro_bins` when the alphabet
    // is stream-only.  `streams_` always holds the *symbol*, so every caller
    // below this point is alphabet-agnostic.
    void set_bursts_impl(
        const std::vector<std::vector<long long>>& times,
        const std::vector<std::vector<int>>& streams,
        int n_streams,
        const std::vector<std::vector<int64_t>>* indices,
        long long n_source_photons,
        const std::vector<std::vector<int>>* micro_bins = nullptr,
        int n_micro_bins = 1,
        double bin_width_ns = 0.0
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
        hmm_detail::ForkJoinPool* pool = nullptr
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
        hmm_detail::ForkJoinPool* pool = nullptr
    ) const;
};

} // namespace tttrlib

#endif // TTTRLIB_HMM_H

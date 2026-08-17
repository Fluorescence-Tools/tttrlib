/*!
 * \file Random.h
 * \brief Centralized counter-based RNG for tttrlib with pluggable engines.
 *
 * A single, global random-number source for all of tttrlib — simulation,
 * photon reassignment (eSRRF, SOFI, others), Monte-Carlo sampling, bootstrapping.
 *
 * Supports multiple engines via TTTR_RNG_ENGINE:
 *   - "philox" (default) — Philox 4×32-10 (Random123). Counter-based,
 *                      thread-safe for deterministic parallel draws.
 *   - "pcg"        — PCG-XSH-RR-64-32 (permuted). Counter-based, thread-safe.
 *   - "splitmix64" — SplitMix64. Counter-based, thread-safe.
 *   - "mt19937"   — Mersenne Twister 19937 (std::mt19937 = mt19937ar
 *                      init_genrand = numpy RandomState(int)). NOT counter-based:
 *                      the streaming Random draws use it, seek() discards
 *                      (O(n)); counter-based deterministic()
 *                      calls fall through to Philox when this is selected.
 *
 * Two interfaces:
 *   - **Streaming** (`Random` object): sequential draws via `next_u32()` /
 *     `random()` / `normal()`, positioned by `seed()`. NOT thread-safe unless
 *     each thread has its own instance.
 *   - **Counter-based** (static `deterministic()`): a pure function of
 *     (seed, index). Thread-safe, order-independent — the requirement for
 *     deterministic parallel Monte-Carlo. Two threads sampling index 7 with the
 *     same seed get the same value.
 *
 * Environment variables (read once, lazily, on first use):
 *   - TTTR_RNG_SEED         — master seed (default: fixed 20260731 for
 *                                reproducibility; "0" → time-derived non-deterministic)
 *   - TTTR_RNG_DETERMINISTIC — "0"/"false"/"off" → non-deterministic default seed
 *                                (convenience for exploratory runs that vary each time)
 *   - TTTR_RNG_ENGINE        — "philox" (default), "pcg", "splitmix64", "mt19937"
 *
 * This replaces ad-hoc RNG implementations scattered across modules. eSRRF is one
 * consumer; other super-resolution methods (SOFI, ISM variants) use the same
 * counter-based draws without rolling their own RNG.
 *
 * Header-only. Additive: existing code is unaffected until it opts in.
 */
#ifndef TTTRLIB_RANDOM_H
#define TTTRLIB_RANDOM_H

// Validation: A/B-TESTED 2026-08-17 -- Philox4x32-10 vs the Random123 known-answer vectors (bit-exact); PCG vs the
//   canonical pcg32 XSH-RR output (bit-exact -- the A/B found and fixed the
//   xorshift on 2026-08-17); SplitMix64 mixer canonical; MT19937 streaming engine == numpy
//   RandomState(seed) raw stream (implemented 2026-08-17; before, the name ran Philox);
//   deterministic() under mt19937 still falls through to Philox (not counter-based, pinned).
//   test/python/misc/test_math_ab_numerics.py.
//   Register: okf/testing/math-kernel-validation.md

#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <random>
#include <algorithm>
#include <cctype>
#include "info.h"  // cpu_features::safe_getenv, is_false_value

namespace tttrlib {

/*!
 * \brief RNG engine type, selectable via TTTR_RNG_ENGINE.
 */
enum class RNGEngine {
    PHILOX,     // Philox 4×32-10 (Random123) — counter-based, thread-safe [default]
    PCG,        // PCG-XSH-RR-64-32 — counter-based, thread-safe, fast
    SPLITMIX64, // SplitMix64 — counter-based, thread-safe
    MT19937     // Mersenne Twister 19937 — NOT counter-based; sequential only
};

/*!
 * \brief Centralized counter-based RNG with pluggable engines.
 *
 * The counter-based `deterministic(seed, index)` is a pure function: the same
 * arguments always return the same value, regardless of thread count or call
 * order. This is the property that makes deterministic parallel sampling
 * possible — no locks, no per-thread state divergence, reproducible bit-for-bit
 * across serial and OpenMP runs.
 *
 * The streaming interface (`next_u32` / `random` / `normal`) advances internal
 * state and is NOT thread-safe for shared instances; use one `Random` per
 * thread, or use the static `deterministic` draws in parallel regions.
 */
class Random {
public:
    Random() = default;

    // ------------------------------------------------------------------
    // Streaming interface (sequential draws; not thread-safe)
    // ------------------------------------------------------------------

    /*!
     * \brief Position the stream.
     * \param base_seed  Master seed (becomes key[0]).
     * \param stream_id  Sub-stream id (becomes key[1]); use to get independent
     *                   parallel streams from one master seed.
     */
    void seed(uint32_t base_seed, uint32_t stream_id = 0) {
        key_[0] = base_seed;
        key_[1] = stream_id;
        ctr_[0] = 0; ctr_[1] = 0; ctr_[2] = 0; ctr_[3] = 0;
        idx_ = 4;  // force refill on first draw
        // TTTR_RNG_ENGINE=mt19937 makes the *streaming* draws a Mersenne
        // Twister (std::mt19937 == mt19937ar init_genrand == numpy's legacy
        // RandomState(int) seeding), stream 0 seeded with base_seed itself so
        // it reproduces those; stream k > 0 folds k in. Until 2026-08-17 the
        // name was accepted and silently ran Philox.
        use_mt_ = (selected_engine() == RNGEngine::MT19937);
        if (use_mt_) mt_.seed(stream_id == 0 ? base_seed : (base_seed ^ (stream_id * 0x9E3779B9u)));
        mt_pos_ = 0;
    }

    /*!
     * \brief Jump to draw `draw_index` of the current stream, in O(1).
     *
     * Philox is a *counter-based* generator: its output is a pure function of
     * (key, counter), so an arbitrary position is reachable by setting the
     * counter rather than by generating everything up to it. This is the whole
     * reason to choose it for parallel work, and it is what makes a
     * per-molecule, per-window reseed affordable.
     *
     * Equivalent to `seed(...)` followed by `draw_index` calls to `next_u32()`,
     * exactly — not merely statistically. The state is reproduced bit for bit,
     * which is what lets `SimEngine` reseed mid-run without perturbing a
     * stream, and what the round-trip test pins.
     *
     * The two cases mirror `next_u32`'s own bookkeeping: it refills from `ctr_`
     * and *then* increments, so draw `n` lives in block `n/4` at offset `n%4`.
     * Landing on a block boundary means "refill on the next call" (`idx_ = 4`,
     * counter left *at* the block); landing mid-block means the refill has
     * already happened, so the buffer must be filled here and the counter left
     * one past it.
     */
    void seek(uint64_t draw_index) {
        if (use_mt_) {
            // Not counter-based: reach the position by discarding. Exact, O(n).
            if (draw_index < mt_pos_) { seed(key_[0], key_[1]); }
            mt_.discard(static_cast<unsigned long long>(draw_index - mt_pos_));
            mt_pos_ = draw_index;
            return;
        }
        const uint64_t block = draw_index / 4;
        const int rem = static_cast<int>(draw_index % 4);
        ctr_[0] = static_cast<uint32_t>(block);
        ctr_[1] = static_cast<uint32_t>(block >> 32);
        ctr_[2] = 0; ctr_[3] = 0;
        if (rem == 0) {
            idx_ = 4;                                  // refill block `block` on next draw
        } else {
            philox_refill();                           // buf_ = output of block `block`
            ++ctr_[0];
            if (ctr_[0] == 0) ++ctr_[1];
            idx_ = rem;
        }
    }

    /// Next raw 32-bit value (Philox streaming).
    inline uint32_t next_u32() {
        if (use_mt_) { ++mt_pos_; return static_cast<uint32_t>(mt_()); }
        if (idx_ >= 4) {
            philox_refill();
            ++ctr_[0];
            if (ctr_[0] == 0) ++ctr_[1];
            idx_ = 0;
        }
        return buf_[idx_++];
    }

    /// Uniform in [0, 1).
    inline double random() {
        return double(next_u32()) * kInv32;
    }

    /// Uniform in (0, 1) — excludes both endpoints.
    inline double random_open() {
        return (double(next_u32()) + 0.5) * kInv32;
    }

    /*!
     * \brief Symmetric standard normal (Leva ratio-of-uniforms).
     *
     * Same algorithm as the original SimCounterRandom — preserved so that
     * simulation results do not change when migrating to the shared core.
     */
    double normal() {
        const double ei = 0.27597, eo = 0.27846, a = 0.449871, b = 0.386595;
        const double sqrt2en = 3.994274348768903E-010;
        double u, v, q, x1, x2;
        for (;;) {
            u = random_open();
            v = double(int32_t(next_u32())) * sqrt2en;
            x1 = u - a; x2 = std::fabs(v) + b;
            q = x1 * x1 + (0.19600 * x2 - 0.25472 * x1) * x2;
            if (q < ei) break;
            if (q > eo) continue;
            if (v * v <= -4.0 * std::log(u) * u * u) break;
        }
        return v / u;
    }

    // ------------------------------------------------------------------
    // Engine selection (from TTTR_RNG_ENGINE env var)
    // ------------------------------------------------------------------

    /*!
     * \brief The engine selected by TTTR_RNG_ENGINE (or default).
     *
     * Read once lazily; subsequent calls return the cached value. If
     * TTTR_RNG_DETERMINISTIC is off and no engine is named, PCG is chosen
     * (faster, non-deterministic-friendly).
     */
    static inline RNGEngine selected_engine() {
        static const RNGEngine engine = []() -> RNGEngine {
            constexpr RNGEngine DEFAULT_ENGINE = RNGEngine::PHILOX;
            char buf[256];
            const char* env = cpu_features::safe_getenv("TTTR_RNG_ENGINE", buf, sizeof(buf));

            if (!env || env[0] == '\0') {
                if (!cpu_features::is_feature_enabled_by_env("TTTR_RNG_DETERMINISTIC", true)) {
                    return RNGEngine::PCG;
                }
                return DEFAULT_ENGINE;
            }

            std::string name(env);
            std::transform(name.begin(), name.end(), name.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            if (name == "philox" || name == "philox4x32") return RNGEngine::PHILOX;
            if (name == "pcg") return RNGEngine::PCG;
            if (name == "splitmix64" || name == "splitmix") return RNGEngine::SPLITMIX64;
            if (name == "mt19937" || name == "mt") return RNGEngine::MT19937;
            return DEFAULT_ENGINE;
        }();
        return engine;
    }

    // ------------------------------------------------------------------
    // Counter-based deterministic draws (static, thread-safe)
    // ------------------------------------------------------------------

    /*!
     * \brief Deterministic uniform [0, 1) from (seed, index).
     *
     * Pure function: two calls with the same arguments always return the same
     * value, regardless of thread count or call order. This is what parallel
     * code (photon reassignment, per-molecule simulation) must use instead of a
     * shared mutable generator.
     *
     * Dispatches on the engine selected by TTTR_RNG_ENGINE. For Philox, PCG,
     * and SplitMix64 the draw is truly counter-based. For MT19937 (which is not
     * counter-based) it falls through to Philox so parallel determinism is never
     * silently lost.
     *
     * \param seed   Key component 0.
     * \param index  Logical draw index (maps to the engine counter).
     * \return Uniform double in [0, 1).
     */
    static inline double deterministic(uint32_t seed, uint64_t index) {
        return double(deterministic_u32(seed, index)) * kInv32;
    }

    /*!
     * \brief Deterministic raw 32-bit value from (seed, index).
     *
     * Evaluates the selected engine at the position implied by (seed, index).
     */
    static inline uint32_t deterministic_u32(uint32_t seed, uint64_t index) {
        switch (selected_engine()) {
            case RNGEngine::PCG:
                return pcg_deterministic_u32(seed, index);
            case RNGEngine::SPLITMIX64:
                return static_cast<uint32_t>(splitmix64_deterministic(seed, index));
            case RNGEngine::MT19937:
                // MT19937 is not counter-based; use Philox for parallel determinism
            case RNGEngine::PHILOX:
            default:
                return philox_deterministic_u32(seed, index);
        }
    }

private:
    static constexpr double kInv32 = 1.0 / 4294967296.0;  // 1/2^32

    // ------------------------------------------------------------------
    // Streaming state (Philox)
    // ------------------------------------------------------------------
    uint32_t key_[2]{};
    uint32_t ctr_[4]{};
    uint32_t buf_[4]{};
    int idx_ = 4;
    // Streaming state (MT19937, only when TTTR_RNG_ENGINE=mt19937)
    bool use_mt_ = false;
    std::mt19937 mt_;
    uint64_t mt_pos_ = 0;

    static inline void mulhilo(uint32_t a, uint32_t b, uint32_t& hi, uint32_t& lo) {
        uint64_t p = uint64_t(a) * b;
        hi = uint32_t(p >> 32);
        lo = uint32_t(p);
    }

    /// Philox4x32-10: 10 rounds, output overwrites buf_.
    void philox_refill() {
        uint32_t c0 = ctr_[0], c1 = ctr_[1], c2 = ctr_[2], c3 = ctr_[3];
        uint32_t k0 = key_[0], k1 = key_[1];
        philox_rounds(c0, c1, c2, c3, k0, k1);
        buf_[0] = c0; buf_[1] = c1; buf_[2] = c2; buf_[3] = c3;
    }

    // ------------------------------------------------------------------
    // Philox 4×32-10 (counter-based: pure function of key + counter)
    // ------------------------------------------------------------------
    static inline uint32_t philox_deterministic_u32(uint32_t seed, uint64_t index) {
        uint32_t c0 = uint32_t(index);
        uint32_t c1 = uint32_t(index >> 32);
        uint32_t c2 = 0, c3 = 0;
        uint32_t k0 = seed, k1 = 0;
        philox_rounds(c0, c1, c2, c3, k0, k1);
        return c0;  // first output word
    }

    // ------------------------------------------------------------------
    // PCG-XSH-RR-64-32 (counter-based: the state transition is a pure
    // function of the 64-bit state; map (seed, index) → state → output)
    // ------------------------------------------------------------------
    static inline uint32_t pcg_deterministic_u32(uint32_t seed, uint64_t index) {
        // Mix seed and index into a 64-bit initial state
        uint64_t state = static_cast<uint64_t>(seed) * 6364136223846793005ULL
                       + static_cast<uint64_t>(index) * 1442695040888963407ULL
                       + 1;
        // PCG advance: one step of the LCG
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        // PCG output: XSH-RR (xorshift-high, random rotation)
        // Canonical XSH-RR: xorshift the state by 18, then take bits 27..58.
        // (Was `(state >> 18) ^ (state >> 27)` -- a 19-live-bit word whose
        // output bits were 1 with P ~ 0.22-0.37; caught by the pcg32 A/B.)
        uint64_t xorshifted = ((state >> 18u) ^ state) >> 27u;
        uint32_t rot = static_cast<uint32_t>(state >> 59u);
        uint32_t out = static_cast<uint32_t>(xorshifted);
        return (out >> rot) | (out << ((-rot) & 31));
    }

    // ------------------------------------------------------------------
    // SplitMix64 (counter-based: output is a pure function of the 64-bit state)
    // ------------------------------------------------------------------
    static inline uint64_t splitmix64_deterministic(uint32_t seed, uint64_t index) {
        uint64_t state = static_cast<uint64_t>(seed) * 6364136223846793005ULL
                       + static_cast<uint64_t>(index) * 1442695040888963407ULL
                       + 0x9E3779B97F4A7C15ULL;
        state += 0x9E3779B97F4A7C15ULL;
        uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    // ------------------------------------------------------------------
    // Shared Philox 10-round core (used by both streaming and counter paths)
    // ------------------------------------------------------------------
    static inline void philox_rounds(
        uint32_t& c0, uint32_t& c1, uint32_t& c2, uint32_t& c3,
        uint32_t& k0, uint32_t& k1
    ) {
        for (int r = 0; r < 10; ++r) {
            uint32_t hi0, lo0, hi1, lo1;
            mulhilo(0xD2511F53u, c0, hi0, lo0);
            mulhilo(0xCD9E8D57u, c2, hi1, lo1);
            uint32_t n0 = hi1 ^ c1 ^ k0;
            uint32_t n1 = lo1;
            uint32_t n2 = hi0 ^ c3 ^ k1;
            uint32_t n3 = lo0;
            c0 = n0; c1 = n1; c2 = n2; c3 = n3;
            k0 += 0x9E3779B9u;
            k1 += 0xBB67AE85u;
        }
    }
};

// ----------------------------------------------------------------------
// Global RNG instance — configurable from environment variables
// ----------------------------------------------------------------------

/*!
 * \brief The global master seed, read from TTTR_RNG_SEED.
 *
 * - If TTTR_RNG_SEED is set and non-zero, that value is used (reproducible).
 * - If TTTR_RNG_SEED is "0" (or unset and TTTR_RNG_DETERMINISTIC is off),
 *   a non-deterministic seed is derived from the clock.
 * - Default (no env vars): a fixed seed (20260731) for reproducible defaults.
 *
 * \return The master seed for the global RNG.
 */
inline uint32_t global_rng_seed() {
    static const uint32_t seed = []() -> uint32_t {
        constexpr uint32_t DEFAULT_SEED = 20260731u;
        char buf[256];
        const char* env = cpu_features::safe_getenv("TTTR_RNG_SEED", buf, sizeof(buf));
        if (env != nullptr && env[0] != '\0') {
            uint32_t parsed = 0;
            if (env[0] == '0' && (env[1] == 'x' || env[1] == 'X')) {
                parsed = static_cast<uint32_t>(std::strtoul(env + 2, nullptr, 16));
            } else {
                parsed = static_cast<uint32_t>(std::strtoul(env, nullptr, 10));
            }
            if (parsed != 0) return parsed;
            // seed == 0 requested → non-deterministic
            return static_cast<uint32_t>(std::time(nullptr));
        }
        if (!cpu_features::is_feature_enabled_by_env("TTTR_RNG_DETERMINISTIC", true)) {
            return static_cast<uint32_t>(std::time(nullptr));
        }
        return DEFAULT_SEED;
    }();
    return seed;
}

/*!
 * \brief The global RNG instance, seeded from TTTR_RNG_SEED on first use.
 *
 * Use for any code that wants "just give me random numbers" without managing
 * its own generator. For deterministic parallel sampling, use
 * Random::deterministic(seed, index) instead — the global instance is a
 * mutable sequential stream and is not safe to share across threads.
 *
 * \return Reference to the process-wide Random instance.
 */
inline Random& global_rng() {
    static Random instance = []() {
        Random r;
        r.seed(global_rng_seed());
        return r;
    }();
    return instance;
}

} // namespace tttrlib

#endif // TTTRLIB_RANDOM_H

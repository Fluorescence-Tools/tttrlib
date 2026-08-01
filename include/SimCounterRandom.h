/*!
 * \file SimCounterRandom.h
 * \brief Counter-based RNG for the parallel simulation engine (PRD-005).
 *
 * \deprecated This class is retained for backward compatibility with the
 * simulation engine. New code should use the centralized \ref tttrlib::Random
 * (include/Random.h), which is the single source of truth for all RNG in
 * tttrlib and is configurable via TTTRLIB_RNG_SEED. SimCounterRandom now
 * delegates to Random; the Philox 4×32-10 core lives in one place.
 *
 * A stateless, keyed, counter-based generator: the stream is a pure function of
 * (key, counter). Keying per molecule and positioning the counter per window gives
 * independent, lock-free, O(1)-memory streams whose output does **not** depend on
 * the number of threads or the order molecules are processed — the requirement for
 * deterministic parallel Monte-Carlo. Provides the same distribution helpers as
 * SimRandom (symmetric Gaussian). Header-only.
 */
#ifndef TTTRLIB_SIMCOUNTERRANDOM_H
#define TTTRLIB_SIMCOUNTERRANDOM_H

#include <cstdint>
#include <cmath>
#include "Random.h"  // shared Philox core + global_rng

namespace tttrlib {

class SimCounterRandom {
public:
    SimCounterRandom() = default;

    /// Position the stream: key = (base_seed, molecule_id); counter starts at `counter_start`.
    void reset(uint32_t base_seed, uint32_t molecule_id, uint64_t counter_start = 0) {
        rng_.seed(base_seed, molecule_id);
        // Advance the internal counter to counter_start (each philox() block = 4 draws)
        uint64_t blocks = counter_start / 4;
        for (uint64_t i = 0; i < blocks; ++i) {
            // Burn blocks to reach the desired position
            (void)rng_.next_u32(); (void)rng_.next_u32();
            (void)rng_.next_u32(); (void)rng_.next_u32();
        }
        // Burn the remainder within the block
        for (uint64_t i = 0; i < counter_start % 4; ++i) {
            (void)rng_.next_u32();
        }
    }

    inline uint32_t next_u32() {
        return rng_.next_u32();
    }

    inline double random0e1e() { return rng_.random_open(); }  // (0,1)
    inline double random0i1e() { return rng_.random(); }       // [0,1)

    /// Symmetric standard normal (Leva ratio-of-uniforms; v uses signed int32).
    double randomNorm() {
        return rng_.normal();
    }

private:
    Random rng_;  // delegates to the shared Philox 4×32-10 core
};

} // namespace tttrlib

#endif // TTTRLIB_SIMCOUNTERRANDOM_H

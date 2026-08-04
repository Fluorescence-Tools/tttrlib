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

    /*!
     * \brief Position the stream: key = (base_seed, molecule_id), at draw `counter_start`.
     *
     * O(1) in `counter_start`. It used to walk there by *burning* that many
     * draws, which is O(n) — and since `SimEngine` reseeds every molecule every
     * window at `window * kWindowStride`, the burn grew with the window index
     * and made a run quadratic in window count. At a few thousand molecules
     * that reached ~1e10 draws and simply never finished, which is why the
     * Philox and Mt19937 paths appeared to hang while Xoshiro and Pcg (whose
     * resets were already O(1)) ran the same workload in well under a second.
     *
     * Seeking rather than burning is not an optimisation of a counter-based
     * generator, it is the point of one: the output is a pure function of
     * (key, counter). `Random::seek` reproduces the burned state bit for bit,
     * so streams are unchanged.
     */
    void reset(uint32_t base_seed, uint32_t molecule_id, uint64_t counter_start = 0) {
        rng_.seed(base_seed, molecule_id);
        rng_.seek(counter_start);
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

/*!
 * \file SimXoshiroRandom.h
 * \brief xoshiro256++ PRNG — the fast, default RNG backend for the simulator.
 *
 * xoshiro256++ (Blackman & Vigna, public domain) is a small, very fast generator.
 * Seeding the 256-bit state from a base seed + stream id via splitmix64 is cheap
 * (a handful of ops), so — unlike a buffered stream generator — it can be reseeded
 * **per molecule per window**, giving a fast RNG that is still **thread-count-
 * independent** (a molecule's stream depends only on its id and the window, not on
 * which thread processes it). Presents the same interface as SimCounterRandom, so
 * the engine's per-molecule loop is generic over the RNG backend. Header-only.
 * Additive; does not modify any existing tttrlib class.
 */
#ifndef TTTRLIB_SIMXOSHIRORANDOM_H
#define TTTRLIB_SIMXOSHIRORANDOM_H

// Validation: A/B-TESTED 2026-08-17 -- bit-exact vs Blackman & Vigna's xoshiro256++ reference (transcription checked
//   on the state-{1,2,3,4} vector), seed hash + splitmix64 expansion included; randomNorm KS vs
//   N(0,1). test/python/simulation/test_ab_simulation_reference.py.
//   Register: okf/testing/algorithm-validation.md

#include <cstdint>
#include <cmath>

namespace tttrlib {

class SimXoshiroRandom {
public:
    SimXoshiroRandom() { reset(0, 0, 0); }

    /// Position the stream: state = splitmix64-expansion of hash(base, id, counter_start).
    void reset(uint32_t base, uint32_t id, uint64_t counter_start = 0) {
        uint64_t z = (uint64_t(base) << 32)
                   ^ (uint64_t(id) * 0x9E3779B97F4A7C15ull)
                   ^ (counter_start * 0xD1B54A32D192ED03ull) ^ 0x1;
        s_[0] = splitmix64(z); s_[1] = splitmix64(z);
        s_[2] = splitmix64(z); s_[3] = splitmix64(z);
    }

    inline uint64_t next_u64() {
        const uint64_t result = rotl(s_[0] + s_[3], 23) + s_[0];
        const uint64_t t = s_[1] << 17;
        s_[2] ^= s_[0]; s_[3] ^= s_[1]; s_[1] ^= s_[2]; s_[0] ^= s_[3];
        s_[2] ^= t; s_[3] = rotl(s_[3], 45);
        return result;
    }
    inline uint32_t next_u32() { return uint32_t(next_u64() >> 32); }

    inline double random0i1e() { return double(next_u64() >> 11) * kInv53; }          // [0,1)
    inline double random0e1e() { return (double(next_u64() >> 11) + 0.5) * kInv53; }  // (0,1)

    /// Symmetric standard normal (Leva ratio-of-uniforms; v uses signed int32).
    double randomNorm() {
        const double ei = 0.27597, eo = 0.27846, a = 0.449871, b = 0.386595;
        const double sqrt2en = 3.994274348768903E-010;
        double u, v, q, x1, x2;
        for (;;) {
            u = random0e1e();
            v = double(int32_t(next_u32())) * sqrt2en;
            x1 = u - a; x2 = std::fabs(v) + b;
            q = x1 * x1 + (0.19600 * x2 - 0.25472 * x1) * x2;
            if (q < ei) break;
            if (q > eo) continue;
            if (v * v <= -4.0 * std::log(u) * u * u) break;
        }
        return v / u;
    }

private:
    static inline uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    static inline uint64_t splitmix64(uint64_t& z) {
        z += 0x9E3779B97F4A7C15ull;
        uint64_t x = z;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        return x ^ (x >> 31);
    }
    static constexpr double kInv53 = 1.0 / 9007199254740992.0;  // 1/2^53
    uint64_t s_[4]{};
};

} // namespace tttrlib

#endif // TTTRLIB_SIMXOSHIRORANDOM_H

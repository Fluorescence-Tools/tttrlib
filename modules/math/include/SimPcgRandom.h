/*!
 * \file SimPcgRandom.h
 * \brief PCG (pcg32, O'Neill) PRNG — a selectable RNG backend.
 *
 * A compact inline implementation of PCG32 (pcg_setseq_64_xsh_rr_32, the pcg-cpp
 * default family). PCG has excellent statistical quality (passes TestU01 BigCrush),
 * cheap seeding, and built-in **stream selection** (the sequence constant), which
 * maps naturally onto per-molecule streams. Presents the engine's RNG interface
 * (reset / random0e1e / random0i1e / randomNorm). Implemented inline rather than
 * vendoring pcg-cpp. Header-only. Additive; existing tttrlib untouched.
 */
#ifndef TTTRLIB_SIMPCGRANDOM_H
#define TTTRLIB_SIMPCGRANDOM_H

// Validation: A/B-TESTED 2026-08-17 -- vs O'Neill's pcg32_srandom_r/pcg32_random_r (bit-exact, seeding included).
//   test/python/misc/test_math_ab_numerics.py.
//   Register: okf/testing/math-kernel-validation.md

#include <cstdint>
#include <cmath>

namespace tttrlib {

class SimPcgRandom {
public:
    SimPcgRandom() { reset(0, 0, 0); }

    /// Position the stream: sequence (stream id) = molecule id; seed folds base+window.
    void reset(uint32_t base, uint32_t id, uint64_t counter_start = 0) {
        uint64_t seed = (uint64_t(base) << 32) ^ (counter_start * 0xD1B54A32D192ED03ull) ^ 0x1;
        seed = (seed ^ (seed >> 30)) * 0xBF58476D1CE4E5B9ull;
        seed ^= seed >> 27;
        uint64_t seq = (uint64_t(id) * 0x9E3779B97F4A7C15ull) | 0x1ull;
        state_ = 0u; inc_ = (seq << 1) | 1u;
        next32(); state_ += seed; next32();
    }

    inline uint32_t next32() {
        uint64_t old = state_;
        state_ = old * 6364136223846793005ull + inc_;
        uint32_t xorshifted = uint32_t(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = uint32_t(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((-int(rot)) & 31));
    }
    inline uint32_t next_u32() { return next32(); }
    inline uint64_t next_u64() { return (uint64_t(next32()) << 32) | next32(); }

    inline double random0i1e() { return double(next_u64() >> 11) * kInv53; }
    inline double random0e1e() { return (double(next_u64() >> 11) + 0.5) * kInv53; }

    double randomNorm() {
        const double ei = 0.27597, eo = 0.27846, a = 0.449871, b = 0.386595;
        const double sqrt2en = 3.994274348768903E-010;
        double u, v, q, x1, x2;
        for (;;) {
            u = random0e1e();
            v = double(int32_t(next32())) * sqrt2en;
            x1 = u - a; x2 = std::fabs(v) + b;
            q = x1 * x1 + (0.19600 * x2 - 0.25472 * x1) * x2;
            if (q < ei) break;
            if (q > eo) continue;
            if (v * v <= -4.0 * std::log(u) * u * u) break;
        }
        return v / u;
    }

private:
    static constexpr double kInv53 = 1.0 / 9007199254740992.0;
    uint64_t state_ = 0, inc_ = 1;
};

} // namespace tttrlib

#endif // TTTRLIB_SIMPCGRANDOM_H

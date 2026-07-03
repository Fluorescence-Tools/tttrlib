/*!
 * \file SimCounterRandom.h
 * \brief Counter-based RNG (Philox 4×32-10) for the parallel simulation engine (PRD-005).
 *
 * A stateless, keyed, counter-based generator: the stream is a pure function of
 * (key, counter). Keying per molecule and positioning the counter per window gives
 * independent, lock-free, O(1)-memory streams whose output does **not** depend on
 * the number of threads or the order molecules are processed — the requirement for
 * deterministic parallel Monte-Carlo. Provides the same distribution helpers as
 * SimRandom (symmetric Gaussian). Header-only. Additive; existing tttrlib untouched.
 */
#ifndef TTTRLIB_SIMCOUNTERRANDOM_H
#define TTTRLIB_SIMCOUNTERRANDOM_H

#include <cstdint>
#include <cmath>

namespace tttrlib {

class SimCounterRandom {
public:
    SimCounterRandom() = default;

    /// Position the stream: key = (base_seed, molecule_id); counter starts at `counter_start`.
    void reset(uint32_t base_seed, uint32_t molecule_id, uint64_t counter_start = 0) {
        key_[0] = base_seed; key_[1] = molecule_id;
        ctr_[0] = uint32_t(counter_start);
        ctr_[1] = uint32_t(counter_start >> 32);
        ctr_[2] = 0; ctr_[3] = 0;
        idx_ = 4;  // force refill on first draw
    }

    inline uint32_t next_u32() {
        if (idx_ >= 4) { philox(); ++ctr_[0]; if (ctr_[0] == 0) ++ctr_[1]; idx_ = 0; }
        return buf_[idx_++];
    }

    inline double random0e1e() { return (double(next_u32()) + 0.5) * kInv32; }  // (0,1)
    inline double random0i1e() { return double(next_u32()) * kInv32; }          // [0,1)

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
    static constexpr double kInv32 = 1.0 / 4294967296.0;  // 1/2^32

    static inline void mulhilo(uint32_t a, uint32_t b, uint32_t& hi, uint32_t& lo) {
        uint64_t p = uint64_t(a) * b; hi = uint32_t(p >> 32); lo = uint32_t(p);
    }

    // Philox4x32-10: 10 rounds, output overwrites the counter block into buf_.
    void philox() {
        uint32_t c0 = ctr_[0], c1 = ctr_[1], c2 = ctr_[2], c3 = ctr_[3];
        uint32_t k0 = key_[0], k1 = key_[1];
        for (int r = 0; r < 10; ++r) {
            uint32_t hi0, lo0, hi1, lo1;
            mulhilo(0xD2511F53u, c0, hi0, lo0);
            mulhilo(0xCD9E8D57u, c2, hi1, lo1);
            uint32_t n0 = hi1 ^ c1 ^ k0;
            uint32_t n1 = lo1;
            uint32_t n2 = hi0 ^ c3 ^ k1;
            uint32_t n3 = lo0;
            c0 = n0; c1 = n1; c2 = n2; c3 = n3;
            k0 += 0x9E3779B9u; k1 += 0xBB67AE85u;
        }
        buf_[0] = c0; buf_[1] = c1; buf_[2] = c2; buf_[3] = c3;
    }

    uint32_t key_[2]{};
    uint32_t ctr_[4]{};
    uint32_t buf_[4]{};
    int idx_ = 4;
};

} // namespace tttrlib

#endif // TTTRLIB_SIMCOUNTERRANDOM_H

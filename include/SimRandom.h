/*!
 * \file SimRandom.h
 * \brief Mersenne-Twister (MT19937) RNG for the tttrlib photon simulator.
 *
 * Ported from the legacy "Burbulator" generator (Cokus/Bellew/Wada optimisation
 * of Matsumoto & Nishimura MT19937). The integer generators reproduce the legacy
 * stream bit-for-bit; the normal-variate helper `random4nrm` is **corrected** to
 * use an explicit `int32_t` reinterpretation so the Gaussian is symmetric on every
 * platform (the legacy `(long)` cast was positive-only where `sizeof(long)==8`,
 * biasing diffusion — see PRD-005). State is 32-bit for cross-platform determinism.
 *
 * Part of the additive photon-simulation subsystem (PRD-005). Does not modify any
 * existing tttrlib class.
 */
#ifndef TTTRLIB_SIMRANDOM_H
#define TTTRLIB_SIMRANDOM_H

#include <cstdint>
#include <cmath>
#include <vector>

namespace tttrlib {

/// Opaque, copyable MT19937 state (624 words + position) for save/restore.
///
/// `pos` (the offset of the next word to emit) is stored explicitly rather than
/// reconstructed from `left`: the decrement-then-refill-then-read ordering makes
/// `left` alone ambiguous by one word at a twist boundary, so a naive
/// `next = state + N - left` (as in the legacy code) does not round-trip.
struct SimRngState {
    std::vector<uint32_t> state;  ///< 624 state words
    int left = 1;                 ///< words remaining before the next twist
    int pos = 0;                  ///< offset of the next word to emit (next - state)
};

/*!
 * \brief MT19937 generator matching the legacy simulator stream (integers) with a
 *        corrected symmetric normal variate.
 */
class SimRandom {
public:
    static constexpr int N = 624;

    SimRandom() { seed(5489UL); }                 ///< deterministic default seed
    explicit SimRandom(uint32_t s) { seed(s); }

    void seed(uint32_t s);                         ///< (re)initialise from a seed
    void init_by_array(const uint32_t* key, int key_length);

    /// Uniform per-molecule RNG interface: seed from hash(base, id, counter_start).
    /// Note: MT seeding runs a 624-word init loop — expensive when reseeded per draw batch.
    void reset(uint32_t base, uint32_t id, uint64_t counter_start = 0) {
        uint64_t z = (uint64_t(base) << 32)
                   ^ (uint64_t(id) * 0x9E3779B97F4A7C15ull)
                   ^ (counter_start * 0xD1B54A32D192ED03ull);
        z = (z ^ (z >> 33)) * 0xFF51AFD7ED558CCDull;
        seed(uint32_t(z >> 32) ^ uint32_t(z));
    }

    SimRngState getState() const;                  ///< snapshot for resume
    void setState(const SimRngState& st);          ///< restore a snapshot

    /// Raw 32-bit output on [0, 0xffffffff].
    inline uint32_t randomUInt() {
        uint32_t y;
        if (--left_ == 0) next_state();
        y = *next_++;
        y ^= (y >> 11);
        y ^= (y << 7) & 0x9d2c5680UL;
        y ^= (y << 15) & 0xefc60000UL;
        return y ^ (y >> 18);
    }

    /// (0,1) open interval — used for `-log(u)` exponential sampling.
    inline double random0e1e() { return (double(randomUInt()) + 0.5) * kF1; }
    /// [0,1) half-open interval.
    inline double random0i1e() { return double(randomUInt()) * kF1; }
    /// [0,1] closed interval.
    inline double random0i1i() { return double(randomUInt()) * kF2; }

    /*!
     * \brief Symmetric raw variate in (-sqrt(2/e), sqrt(2/e)).
     *
     * Corrected vs legacy: the 32-bit output is reinterpreted as a **signed**
     * int32 (portable), so the value is symmetric about 0 on all platforms.
     */
    inline double random4nrm() {
        return double(int32_t(randomUInt())) * kSqrt2en;
    }

    /// 53-bit resolution real on [0,1).
    double random_res53();

    /// Standard normal via Leva's ratio-of-uniforms (now symmetric — see random4nrm).
    double randomNorm();

private:
    void next_state();

    // 1/2^32 style scaling constants (match the legacy generator).
    static constexpr double kF1     = 2.3283064365387E-010;
    static constexpr double kF2     = 2.3283064370808E-010;
    static constexpr double kSqrt2en = 3.994274348768903E-010;

    uint32_t state_[N];
    uint32_t* next_ = state_;
    int left_ = 1;
};

} // namespace tttrlib

#endif // TTTRLIB_SIMRANDOM_H

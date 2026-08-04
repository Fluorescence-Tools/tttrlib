/*!
 * \file SimRandom.cpp
 * \brief MT19937 implementation for the photon simulator (see SimRandom.h, PRD-005).
 *
 * Seed/twist/tempering are the standard MT19937 (Matsumoto & Nishimura 2002 init,
 * Cokus twist). Reproduces the legacy integer stream bit-for-bit; the normal
 * variate uses the corrected symmetric `random4nrm`.
 */
#include "SimRandom.h"

namespace tttrlib {

namespace {
    constexpr uint32_t MATRIX_A = 0x9908b0dfUL;
    constexpr uint32_t UMASK    = 0x80000000UL;  // most significant w-r bits
    constexpr uint32_t LMASK    = 0x7fffffffUL;  // least significant r bits
    inline uint32_t mixbits(uint32_t u, uint32_t v) { return (u & UMASK) | (v & LMASK); }
    inline uint32_t twist(uint32_t u, uint32_t v) {
        return (mixbits(u, v) >> 1) ^ ((v & 1UL) ? MATRIX_A : 0UL);
    }
}

void SimRandom::seed(uint32_t s) {
    state_[0] = s & 0xffffffffUL;
    for (int j = 1; j < N; ++j) {
        // Knuth TAOCP Vol.2 3rd ed. p.106 multiplier (Matsumoto 2002/01/09).
        state_[j] = (1812433253UL * (state_[j - 1] ^ (state_[j - 1] >> 30)) + j);
        state_[j] &= 0xffffffffUL;
    }
    left_ = 1;
    next_ = state_;
}

void SimRandom::init_by_array(const uint32_t* key, int key_length) {
    seed(19650218UL);
    int i = 1, j = 0;
    int k = (N > key_length ? N : key_length);
    for (; k; --k) {
        state_[i] = (state_[i] ^ ((state_[i - 1] ^ (state_[i - 1] >> 30)) * 1664525UL))
                    + key[j] + j;
        state_[i] &= 0xffffffffUL;
        if (++i >= N) { state_[0] = state_[N - 1]; i = 1; }
        if (++j >= key_length) j = 0;
    }
    for (k = N - 1; k; --k) {
        state_[i] = (state_[i] ^ ((state_[i - 1] ^ (state_[i - 1] >> 30)) * 1566083941UL))
                    - i;
        state_[i] &= 0xffffffffUL;
        if (++i >= N) { state_[0] = state_[N - 1]; i = 1; }
    }
    state_[0] = 0x80000000UL;  // assure a non-zero initial array
    left_ = 1;
    next_ = state_;
}

void SimRandom::next_state() {
    uint32_t* p = state_;
    left_ = N;
    next_ = state_;
    for (int j = N - 397 + 1; --j; ++p) *p = p[397] ^ twist(p[0], p[1]);
    for (int j = 397; --j; ++p) *p = p[397 - N] ^ twist(p[0], p[1]);
    *p = p[397 - N] ^ twist(p[0], state_[0]);
}

SimRngState SimRandom::getState() const {
    SimRngState st;
    st.state.assign(state_, state_ + N);
    st.left = left_;
    st.pos = int(next_ - state_);
    return st;
}

void SimRandom::setState(const SimRngState& st) {
    for (int j = 0; j < N; ++j) state_[j] = st.state[j];
    left_ = st.left;
    next_ = state_ + st.pos;
}

double SimRandom::random_res53() {
    uint32_t a = randomUInt() >> 5, b = randomUInt() >> 6;
    return (a * 67108864.0 + b) * (1.0 / 9007199254740992.0);
}

double SimRandom::randomNorm() {
    // Leva (1992) ratio-of-uniforms. v is now symmetric (corrected random4nrm).
    const double ei = 0.27597, eo = 0.27846;
    const double a = 0.449871, b = 0.386595;
    double u, v, q, x1, x2;
    for (;;) {
        u = random0i1e();
        v = random4nrm();
        x1 = u - a;
        x2 = std::fabs(v) + b;
        q = x1 * x1 + (0.19600 * x2 - 0.25472 * x1) * x2;
        if (q < ei) break;
        if (q > eo) continue;
        if (v * v <= -4.0 * std::log(u) * u * u) break;
    }
    return v / u;
}

} // namespace tttrlib

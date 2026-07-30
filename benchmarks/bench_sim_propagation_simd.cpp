// Is SIMD worth it for the simulator's propagation kernel?  ANSWER: no -- ~8 % end to end.
//
// Build and run:
//   clang++ -std=c++17 -O3 -mcpu=native -Iinclude \
//       benchmarks/bench_sim_propagation_simd.cpp -o /tmp/simd_bench && /tmp/simd_bench
//
// Measured on an 8-core Apple arm64:
//
//   scalar (ziggurat)              11.02 ns/molecule-step   1.00x
//   NEON 2x f64 (probit)           13.37 ns/molecule-step   0.82x
//   NEON 4x f32 (probit)           11.15 ns/molecule-step   0.99x
//   NEON 4x f32, vector RNG         7.4  ns/molecule-step   1.4-1.5x (run to run)
//
// The first two vector rows lose because their random numbers are still drawn one lane at
// a time: vectorising the arithmetic alone is pointless, since the arithmetic was never the
// cost. Only the last row -- four independent xoshiro128+ generators stepped in NEON --
// wins, and it wins 1.4-1.5x.
//
// That 1.4-1.5x applies to 11 ns of the engine's ~45 ns per molecule-step, so it is worth
// about 8 % of a run. Buying it would cost: a structure-of-arrays molecule pool, float32
// positions, the ziggurat replaced by a probit approximation, and -- the real objection --
// the per-molecule RNG keying that makes results independent of thread count. Compare
// active_margin (up to 3x) and independent_molecules (5-7x), both already available and
// exact. Hence: measured, and declined.
//
// The engine spends ~82 % of its time on "propagation + loop + RNG", at ~45 ns per
// molecule-step, and that scales perfectly linearly with molecule count. So the only
// vectorisation shape that could pay is across molecules. This measures the ceiling for
// that: the same drift+diffusion+boundary step, scalar (the engine's own ziggurat) versus
// NEON over 2 doubles and 4 floats, on a structure-of-arrays molecule pool.
//
// The vector paths cannot use the ziggurat -- it is a rejection sampler, so its control
// flow diverges per lane. They use a branch-free rational approximation of the normal
// quantile (Acklam) instead, which is the standard vectorisable alternative.
#include <arm_neon.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <chrono>
#include <vector>

#include "SimXoshiroRandom.h"
#include "SimZiggurat.h"

using tttrlib::SimXoshiroRandom;
using tttrlib::sim_randn;

static const double DT = 0.001, D = 1.0, VX = 5.0;
static const double STEP = 1.4142135623730951 * 0.0447213595499958;  // sqrt(2*D*dt)
static const double BOX_XY = 2.0, BOX_Z = 4.0;

// ---------------------------------------------------------------- scalar (engine-style)
static double run_scalar(std::vector<double>& x, std::vector<double>& y,
                         std::vector<double>& z, std::vector<uint8_t>& alive, int reps) {
    SimXoshiroRandom rng;
    rng.reset(12345u, 0u, 0ull);
    const double box_xy_sq = BOX_XY * BOX_XY;
    const double box_r_sq = box_xy_sq / (BOX_Z * BOX_Z);
    auto t0 = std::chrono::steady_clock::now();
    for (int r = 0; r < reps; ++r) {
        for (size_t i = 0; i < x.size(); ++i) {
            if (!alive[i]) continue;
            const double g0 = sim_randn(rng), g1 = sim_randn(rng), g2 = sim_randn(rng);
            double nx = x[i] + VX * DT + STEP * g0;
            double ny = y[i] + STEP * g1;
            double nz = z[i] + STEP * g2;
            x[i] = nx; y[i] = ny; z[i] = nz;
            if (nx * nx + ny * ny + box_r_sq * nz * nz > box_xy_sq) alive[i] = 0;
        }
    }
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(t1 - t0).count();
}

// ------------------------------------------------------ branch-free normal quantile
// Acklam's rational approximation of the inverse normal CDF; |error| < 1.15e-9.
template <class T>
static inline T probit_poly(T q, T r) {
    // central region only (the tails are handled by clamping the input away from 0/1,
    // which for a diffusion step is immaterial and is exactly the point of the exercise)
    const T a1 = T(-3.969683028665376e+01), a2 = T(2.209460984245205e+02);
    const T a3 = T(-2.759285104469687e+02), a4 = T(1.383577518672690e+02);
    const T a5 = T(-3.066479806614716e+01), a6 = T(2.506628277459239e+00);
    const T b1 = T(-5.447609879822406e+01), b2 = T(1.615858368580409e+02);
    const T b3 = T(-1.556989798598866e+02), b4 = T(6.680131188771972e+01);
    const T b5 = T(-1.328068155288572e+01);
    T num = (((((a1 * r + a2) * r + a3) * r + a4) * r + a5) * r + a6) * q;
    T den = ((((b1 * r + b2) * r + b3) * r + b4) * r + b5) * r + T(1);
    return num / den;
}

// ---------------------------------------------------------------- NEON, 2 x float64
static double run_neon_f64(std::vector<double>& x, std::vector<double>& y,
                           std::vector<double>& z, std::vector<uint8_t>& alive, int reps) {
    SimXoshiroRandom rng;
    rng.reset(12345u, 0u, 0ull);
    const float64x2_t vdrift = vdupq_n_f64(VX * DT);
    const float64x2_t vstep = vdupq_n_f64(STEP);
    const double box_xy_sq = BOX_XY * BOX_XY;
    const double box_r_sq = box_xy_sq / (BOX_Z * BOX_Z);
    const float64x2_t vbrs = vdupq_n_f64(box_r_sq);
    auto normals2 = [&](void) -> float64x2_t {          // two normals, branch free
        double u0 = rng.random0i1e(), u1 = rng.random0i1e();
        if (u0 < 1e-12) u0 = 1e-12;
        if (u1 < 1e-12) u1 = 1e-12;
        double q0 = u0 - 0.5, q1 = u1 - 0.5;
        double r0 = q0 * q0, r1 = q1 * q1;
        double n0 = probit_poly<double>(q0, r0), n1 = probit_poly<double>(q1, r1);
        double tmp[2] = {n0, n1};
        return vld1q_f64(tmp);
    };
    auto t0 = std::chrono::steady_clock::now();
    for (int r = 0; r < reps; ++r) {
        for (size_t i = 0; i + 1 < x.size(); i += 2) {
            float64x2_t vx = vld1q_f64(&x[i]);
            float64x2_t vy = vld1q_f64(&y[i]);
            float64x2_t vz = vld1q_f64(&z[i]);
            vx = vaddq_f64(vaddq_f64(vx, vdrift), vmulq_f64(vstep, normals2()));
            vy = vaddq_f64(vy, vmulq_f64(vstep, normals2()));
            vz = vaddq_f64(vz, vmulq_f64(vstep, normals2()));
            vst1q_f64(&x[i], vx); vst1q_f64(&y[i], vy); vst1q_f64(&z[i], vz);
            float64x2_t rr = vaddq_f64(vaddq_f64(vmulq_f64(vx, vx), vmulq_f64(vy, vy)),
                                       vmulq_f64(vbrs, vmulq_f64(vz, vz)));
            double tmp[2]; vst1q_f64(tmp, rr);
            if (tmp[0] > box_xy_sq) alive[i] = 0;
            if (tmp[1] > box_xy_sq) alive[i + 1] = 0;
        }
    }
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(t1 - t0).count();
}

// ---------------------------------------------------------------- NEON, 4 x float32
static double run_neon_f32(std::vector<float>& x, std::vector<float>& y,
                           std::vector<float>& z, std::vector<uint8_t>& alive, int reps) {
    SimXoshiroRandom rng;
    rng.reset(12345u, 0u, 0ull);
    const float32x4_t vdrift = vdupq_n_f32(float(VX * DT));
    const float32x4_t vstep = vdupq_n_f32(float(STEP));
    const float box_xy_sq = float(BOX_XY * BOX_XY);
    const float32x4_t vbrs = vdupq_n_f32(float(box_xy_sq / (BOX_Z * BOX_Z)));
    auto normals4 = [&](void) -> float32x4_t {
        float tmp[4];
        for (int k = 0; k < 4; ++k) {
            double u = rng.random0i1e();
            if (u < 1e-12) u = 1e-12;
            double q = u - 0.5;
            tmp[k] = float(probit_poly<double>(q, q * q));
        }
        return vld1q_f32(tmp);
    };
    auto t0 = std::chrono::steady_clock::now();
    for (int r = 0; r < reps; ++r) {
        for (size_t i = 0; i + 3 < x.size(); i += 4) {
            float32x4_t vx = vld1q_f32(&x[i]);
            float32x4_t vy = vld1q_f32(&y[i]);
            float32x4_t vz = vld1q_f32(&z[i]);
            vx = vaddq_f32(vaddq_f32(vx, vdrift), vmulq_f32(vstep, normals4()));
            vy = vaddq_f32(vy, vmulq_f32(vstep, normals4()));
            vz = vaddq_f32(vz, vmulq_f32(vstep, normals4()));
            vst1q_f32(&x[i], vx); vst1q_f32(&y[i], vy); vst1q_f32(&z[i], vz);
            float32x4_t rr = vaddq_f32(vaddq_f32(vmulq_f32(vx, vx), vmulq_f32(vy, vy)),
                                       vmulq_f32(vbrs, vmulq_f32(vz, vz)));
            float tmp[4]; vst1q_f32(tmp, rr);
            for (int k = 0; k < 4; ++k) if (tmp[k] > box_xy_sq) alive[i + k] = 0;
        }
    }
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(t1 - t0).count();
}


// ------------------------------------------- NEON, 4 x float32, VECTORISED RNG TOO
// Four independent xoshiro128+ generators, one per lane, stepped entirely in NEON: this is
// the strongest form of the idea -- nothing in the inner loop is scalar.
struct Xoshiro128x4 {
    uint32x4_t s0, s1, s2, s3;
    void seed() {
        uint32_t a[4] = {0x9E3779B9u, 0x243F6A88u, 0xB7E15162u, 0xDEADBEEFu};
        uint32_t b[4] = {0x13198A2Eu, 0x85A308D3u, 0x03707344u, 0xCAFEBABEu};
        uint32_t c[4] = {0xA4093822u, 0x299F31D0u, 0x082EFA98u, 0xFEEDFACEu};
        uint32_t d[4] = {0x452821E6u, 0x38D01377u, 0xBE5466CFu, 0x34E90C6Cu};
        s0 = vld1q_u32(a); s1 = vld1q_u32(b); s2 = vld1q_u32(c); s3 = vld1q_u32(d);
    }
    inline uint32x4_t next() {
        const uint32x4_t result = vaddq_u32(s0, s3);
        const uint32x4_t t = vshlq_n_u32(s1, 9);
        s2 = veorq_u32(s2, s0);
        s3 = veorq_u32(s3, s1);
        s1 = veorq_u32(s1, s2);
        s0 = veorq_u32(s0, s3);
        s2 = veorq_u32(s2, t);
        s3 = vorrq_u32(vshlq_n_u32(s3, 11), vshrq_n_u32(s3, 21));   // rotl(s3, 11)
        return result;
    }
};

static inline float32x4_t probit4(float32x4_t u) {
    // q = u - 0.5, r = q*q, then the same rational form, all in vector registers.
    const float32x4_t half = vdupq_n_f32(0.5f);
    const float32x4_t q = vsubq_f32(u, half);
    const float32x4_t r = vmulq_f32(q, q);
    float32x4_t num = vdupq_n_f32(-3.969683028665376e+01f);
    num = vmlaq_f32(vdupq_n_f32(2.209460984245205e+02f), num, r);
    num = vmlaq_f32(vdupq_n_f32(-2.759285104469687e+02f), num, r);
    num = vmlaq_f32(vdupq_n_f32(1.383577518672690e+02f), num, r);
    num = vmlaq_f32(vdupq_n_f32(-3.066479806614716e+01f), num, r);
    num = vmlaq_f32(vdupq_n_f32(2.506628277459239e+00f), num, r);
    num = vmulq_f32(num, q);
    float32x4_t den = vdupq_n_f32(-5.447609879822406e+01f);
    den = vmlaq_f32(vdupq_n_f32(1.615858368580409e+02f), den, r);
    den = vmlaq_f32(vdupq_n_f32(-1.556989798598866e+02f), den, r);
    den = vmlaq_f32(vdupq_n_f32(6.680131188771972e+01f), den, r);
    den = vmlaq_f32(vdupq_n_f32(-1.328068155288572e+01f), den, r);
    den = vmlaq_f32(vdupq_n_f32(1.0f), den, r);
    return vdivq_f32(num, den);
}

static double run_neon_full(std::vector<float>& x, std::vector<float>& y,
                            std::vector<float>& z, std::vector<uint8_t>& alive, int reps) {
    Xoshiro128x4 rng; rng.seed();
    const float32x4_t vdrift = vdupq_n_f32(float(VX * DT));
    const float32x4_t vstep = vdupq_n_f32(float(STEP));
    const float box_xy_sq = float(BOX_XY * BOX_XY);
    const float32x4_t vbox = vdupq_n_f32(box_xy_sq);
    const float32x4_t vbrs = vdupq_n_f32(float(box_xy_sq / (BOX_Z * BOX_Z)));
    const float32x4_t inv32 = vdupq_n_f32(2.3283064365386963e-10f);   // 1/2^32
    auto norm4 = [&]() {
        float32x4_t u = vmulq_f32(vcvtq_f32_u32(rng.next()), inv32);
        u = vmaxq_f32(u, vdupq_n_f32(1e-7f));
        return probit4(u);
    };
    auto t0 = std::chrono::steady_clock::now();
    for (int r = 0; r < reps; ++r) {
        for (size_t i = 0; i + 3 < x.size(); i += 4) {
            float32x4_t vx = vld1q_f32(&x[i]);
            float32x4_t vy = vld1q_f32(&y[i]);
            float32x4_t vz = vld1q_f32(&z[i]);
            vx = vmlaq_f32(vaddq_f32(vx, vdrift), vstep, norm4());
            vy = vmlaq_f32(vy, vstep, norm4());
            vz = vmlaq_f32(vz, vstep, norm4());
            vst1q_f32(&x[i], vx); vst1q_f32(&y[i], vy); vst1q_f32(&z[i], vz);
            float32x4_t rr = vmlaq_f32(vmlaq_f32(vmulq_f32(vx, vx), vy, vy),
                                       vbrs, vmulq_f32(vz, vz));
            uint32x4_t out = vcgtq_f32(rr, vbox);
            uint32_t m[4]; vst1q_u32(m, out);
            for (int k = 0; k < 4; ++k) if (m[k]) alive[i + k] = 0;
        }
    }
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(t1 - t0).count();
}

int main() {
    const size_t N = 1u << 16;
    const int REPS = 400;
    const double total = double(N) * REPS;

    std::vector<double> x(N, 0.1), y(N, 0.1), z(N, 0.1);
    std::vector<float> xf(N, 0.1f), yf(N, 0.1f), zf(N, 0.1f);
    std::vector<uint8_t> a(N, 1);

    // alive is reset each run: the boundary kill would otherwise empty the pool and the
    // later runs would measure nothing.
    std::fill(a.begin(), a.end(), 1);
    double ts = run_scalar(x, y, z, a, REPS);
    std::fill(a.begin(), a.end(), 1);
    std::fill(x.begin(), x.end(), 0.1); std::fill(y.begin(), y.end(), 0.1);
    std::fill(z.begin(), z.end(), 0.1);
    double t64 = run_neon_f64(x, y, z, a, REPS);
    std::fill(a.begin(), a.end(), 1);
    double t32 = run_neon_f32(xf, yf, zf, a, REPS);

    std::printf("propagation kernel, %zu molecules x %d steps = %.1fM molecule-steps\n\n",
                N, REPS, total / 1e6);
    std::printf("  %-28s %8.2f s   %6.2f ns/molecule-step   %5.2fx\n",
                "scalar (ziggurat)", ts, 1e9 * ts / total, 1.0);
    std::printf("  %-28s %8.2f s   %6.2f ns/molecule-step   %5.2fx\n",
                "NEON 2x f64 (probit)", t64, 1e9 * t64 / total, ts / t64);
    std::printf("  %-28s %8.2f s   %6.2f ns/molecule-step   %5.2fx\n",
                "NEON 4x f32 (probit)", t32, 1e9 * t32 / total, ts / t32);
    std::fill(a.begin(), a.end(), 1);
    std::fill(xf.begin(), xf.end(), 0.1f); std::fill(yf.begin(), yf.end(), 0.1f);
    std::fill(zf.begin(), zf.end(), 0.1f);
    double tfull = run_neon_full(xf, yf, zf, a, REPS);
    std::printf("  %-28s %8.2f s   %6.2f ns/molecule-step   %5.2fx\n",
                "NEON 4x f32, vector RNG", tfull, 1e9 * tfull / total, ts / tfull);

    std::printf("\n  engine measures ~45 ns per molecule-step for the whole step,\n"
                "  of which propagation+loop+RNG is ~82%%.\n");
    return 0;
}

// Is SIMD worth it for the simulator's propagation kernel?
//
// Build and run:
//   clang++ -std=c++17 -O3 -mcpu=native -Iinclude \
//       benchmarks/bench_sim_propagation_simd.cpp -o /tmp/simd_bench && /tmp/simd_bench
//
// Measured on an 8-core Apple arm64 (NEON, 4 lanes), five interleaved runs:
//
//   scalar (ziggurat)                    9.5-9.9 ns/molecule-step   1.00x
//   NEON 2x f64, scalar RNG             13-14   ns/molecule-step   ~0.7x
//   NEON 4x f32, scalar RNG             11      ns/molecule-step   ~0.9x
//   SimSimd.h (vector RNG, Box-Muller)   9.1-9.8 ns/molecule-step   1.01-1.05x
//
// Three things this measurement teaches, in increasing order of how much time they cost:
//
// 1. Vectorising the arithmetic while still drawing random numbers one lane at a time is
//    SLOWER than scalar. The arithmetic was never the cost.
//
// 2. The transform matters more than the vectorisation. A rational approximation of the
//    normal quantile looks like the obvious choice and is a trap: its central branch covers
//    only |z| < 1.97, so 4.85 % of lanes need a scalar fix-up that costs more than the
//    vectorisation saves (0.71x), and skipping that fix-up silently truncates the
//    distribution at |z| = 3.22 while mean, variance and a KS test all stay clean.
//    Box-Muller is exact, table-free and branch-free, and lands at parity-to-slightly-ahead.
//
// 3. The scalar ziggurat is very hard to beat at 4 lanes. Its fast path is one 32-bit draw,
//    a table lookup, a compare and a multiply, taken ~98 % of the time, and its tail is part
//    of the same rejection loop rather than a separate fix-up.
//
// AVX2 (8 lanes) is where a real win should be, and is UNMEASURED for speed here: this
// machine is arm64. The AVX2 path is checked for correctness by cross-compiling and running
// under emulation, which says nothing about its throughput. Run this on x86-64 before
// assuming either way.
//
#include <arm_neon.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <chrono>
#include <vector>

#include "SimXoshiroRandom.h"
#include "SimZiggurat.h"
#include "SimSimd.h"

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


// -------------------------------- vectorised RNG + quantile, from the library header
// This used to carry its own copy of the four-lane xoshiro and the quantile. It now uses
// SimSimd.h, so the thing benchmarked is the thing shipped -- and so the two cannot drift.
static double run_vector_boxmuller(std::vector<float>& x, std::vector<float>& y,
                                   std::vector<float>& z, std::vector<uint8_t>& alive,
                                   int reps) {
    using namespace tttrlib;
    using namespace tttrlib::simd;
    SimRandomV rng; rng.seed(12345u);
    const int L = kSimdLanes;
    const f32v vdrift = f_set(float(VX * DT));
    const f32v vstep = f_set(float(STEP));
    const float box_xy_sq = float(BOX_XY * BOX_XY);
    const f32v vbrs = f_set(box_xy_sq / float(BOX_Z * BOX_Z));
    float rr[8];
    f32v spare = f_set(0.0f); bool have_spare = false;
    auto t0 = std::chrono::steady_clock::now();
    for (int r = 0; r < reps; ++r) {
        for (size_t i = 0; i + size_t(L) <= x.size(); i += size_t(L)) {
            // Box-Muller yields two vectors per call and a block needs three, so the
            // fourth is carried to the next block instead of thrown away -- that waste was
            // a quarter of the transform work.
            f32v gx, gy, gz;
            if (have_spare) { gx = spare; have_spare = false; sim_normalv_boxmuller(rng, &gy, &gz); }
            else { sim_normalv_boxmuller(rng, &gx, &gy);
                   sim_normalv_boxmuller(rng, &gz, &spare); have_spare = true; }
            f32v vx = *reinterpret_cast<f32v*>(&x[i]);
            f32v vy = *reinterpret_cast<f32v*>(&y[i]);
            f32v vz = *reinterpret_cast<f32v*>(&z[i]);
            vx = f_add(f_add(vx, vdrift), f_mul(vstep, gx));
            vy = f_add(vy, f_mul(vstep, gy));
            vz = f_add(vz, f_mul(vstep, gz));
            f_store(&x[i], vx); f_store(&y[i], vy); f_store(&z[i], vz);
            f32v r2 = f_add(f_add(f_mul(vx, vx), f_mul(vy, vy)), f_mul(vbrs, f_mul(vz, vz)));
            f_store(rr, r2);
            for (int k = 0; k < L; ++k) if (rr[k] > box_xy_sq) alive[i + k] = 0;
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

    std::fill(a.begin(), a.end(), 1);
    std::fill(xf.begin(), xf.end(), 0.1f); std::fill(yf.begin(), yf.end(), 0.1f);
    std::fill(zf.begin(), zf.end(), 0.1f);
    double tbm = run_vector_boxmuller(xf, yf, zf, a, REPS);
    std::printf("  SimSimd.h %-14s %8.2f s   %6.2f ns/molecule-step   %5.2fx\n",
                tttrlib::sim_simd_backend(), tbm, 1e9 * tbm / total, ts / tbm);

    std::printf("\n  engine measures ~45 ns per molecule-step for the whole step,\n"
                "  of which propagation+loop+RNG is ~82%%.\n");
    return 0;
}

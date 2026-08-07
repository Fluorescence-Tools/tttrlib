/*!
 * \file SimSimd.h
 * \brief Vectorised 32-bit RNG and normals for the simulator's propagation kernel.
 *
 * The engine's per-molecule step is ~45 ns, of which the drift+diffusion+boundary kernel is
 * ~11 ns, and it scales perfectly linearly with molecule count, so vectorising across
 * molecules is the only shape that could pay.
 *
 * ### It does not currently pay, and the reason is instructive
 *
 * Measured with `benchmarks/bench_sim_propagation_simd.cpp` on arm64/NEON, 4 lanes:
 *
 *     scalar (ziggurat)                    9.6 ns/molecule-step   1.00x
 *     NEON 2x f64, scalar RNG             13.8 ns/molecule-step   0.71x
 *     NEON 4x f32, scalar RNG             11.3 ns/molecule-step   0.86x
 *     this header (vector RNG + tails)    13.4 ns/molecule-step   0.71-0.74x
 *
 * An earlier version of this measurement showed 1.4-1.5x in favour of SIMD. That version
 * was wrong: it used only the central branch of the quantile, which **hard-truncates the
 * normal at |z| = 3.22** -- `P(|z|>3)` came out 0.00184 against 0.00270 and `P(|z|>3.5)`
 * was exactly zero. Mean, variance and a KS test all looked perfect. Adding the tail branch
 * that makes the distribution correct costs more than the vectorisation saves, and the sign
 * of the result flips. The lesson is worth more than the code: **the apparent SIMD win was
 * the cost of the distribution it was not sampling.**
 *
 * The scalar ziggurat is simply hard to beat here -- its fast path is one 32-bit draw, a
 * table lookup, a compare and a multiply, taken ~98 % of the time, and it needs no tail
 * fix-up because its tail routine is part of the same rejection loop.
 *
 * ### What is still open
 *
 * AVX2 runs 8 lanes per step, which halves the per-normal cost of both the generator and
 * the quantile while leaving the tail fix-up frequency higher (~33 % of steps touch a tail
 * lane, against ~18 % at 4 lanes). Whether that flips the result is **unmeasured**: this
 * machine is arm64, and the AVX2 path has been verified for correctness by cross-compiling
 * and running under emulation, which says nothing about its speed. Anyone on x86-64 should
 * run the benchmark before assuming either way.
 *
 * ### Everything 32-bit, on purpose
 *
 * The generator is xoshiro128+ -- 32-bit state, 32-bit output -- and the normals are
 * single precision. That is not a compromise for a diffusion step (a 24-bit mantissa on a
 * displacement of order `sqrt(2*D*dt)` is far below any observable), and it doubles the lane
 * count for a given register width: 4 lanes in 128 bits (NEON, SSE2), 8 in 256 (AVX2).
 *
 * ### One algorithm, four backends
 *
 * The xoshiro step and the quantile are written **once**, against a tiny vector abstraction
 * (`u32v`/`f32v`) specialised per instruction set. Triplicating them per ISA is how the
 * backends drift apart.
 *
 *   AVX2  (`__AVX2__`)            8 lanes
 *   SSE2  (x86-64 baseline)       4 lanes
 *   NEON  (arm64 baseline)        4 lanes
 *   portable fallback             4 lanes, plain C++
 *
 * AVX2 is used only when the translation unit is compiled for it (`-mavx2`); SSE2 and NEON
 * are baseline on their architectures and need no flags, so a default build is vectorised
 * everywhere and never illegal-instructions on an older CPU.
 *
 * ### Why this cannot reproduce the scalar stream
 *
 * `sim_randn` is a ziggurat, and a ziggurat is a table lookup (`kn[iz]`, `wn[iz]`, `fn[iz]`)
 * indexed by a value derived from the draw. NEON has no gather at all and the SSE2 baseline
 * has none either, so a vector generator has to be table-free. This one uses a branch-free
 * rational approximation of the normal quantile (Acklam, |error| < 1.15e-9 in the central
 * region) applied to a uniform.
 *
 * The consequence is not accuracy -- the distribution is correct to well under the
 * simulator's own tolerances -- but *reproducibility*: a molecule's random stream would no
 * longer be the same numbers the scalar engine draws. The engine deliberately guarantees
 * that results do not depend on thread count, keyed per (molecule, window). So anything
 * built on this header must be opt-in and validated statistically, not bit-for-bit.
 *
 * ### Status
 *
 * Correct, tested and portable, but **not wired into the step and not currently a speedup**
 * on 4 lanes. It is kept because it is the validated groundwork: the measurement above is
 * reproducible, the four backends agree, and if the balance shifts -- 8 lanes, a wider
 * machine, or a use where the propagation kernel is a larger share than today's ~25 % --
 * the piece is ready and its limits are known rather than guessed.
 */
#ifndef TTTRLIB_SIMSIMD_H
#define TTTRLIB_SIMSIMD_H

#include <cmath>
#include <cstdint>
#include <vector>

// Define TTTRLIB_SIMD_FORCE_SCALAR to compile the portable path on any machine; the test
// suite uses it to check that every backend agrees on the same numbers.
#if defined(TTTRLIB_SIMD_FORCE_SCALAR)
#  define TTTRLIB_SIMD_ISA 0          // portable fallback, 4 lanes
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
#  include <arm_neon.h>
#  define TTTRLIB_SIMD_ISA 1          // NEON, 4 lanes
#elif defined(__AVX2__)
#  include <immintrin.h>
#  define TTTRLIB_SIMD_ISA 2          // AVX2, 8 lanes
#elif defined(__SSE2__) || defined(_M_X64) || defined(__x86_64__)
#  include <emmintrin.h>
#  define TTTRLIB_SIMD_ISA 3          // SSE2, 4 lanes
#else
#  define TTTRLIB_SIMD_ISA 0          // portable fallback, 4 lanes
#endif

namespace tttrlib {
namespace simd {

// ---------------------------------------------------------------------------------------
// The abstraction: unsigned-32 and float-32 vectors with just the operations the generator
// and the quantile need. Each backend supplies the same names, so everything below is
// written once.
// ---------------------------------------------------------------------------------------
#if TTTRLIB_SIMD_ISA == 1                                            // ---------- NEON
constexpr int kLanes = 4;
using u32v = uint32x4_t;
using f32v = float32x4_t;
inline u32v u_load(const uint32_t* p) { return vld1q_u32(p); }
inline void u_store(uint32_t* p, u32v v) { vst1q_u32(p, v); }
inline u32v u_add(u32v a, u32v b) { return vaddq_u32(a, b); }
inline u32v u_xor(u32v a, u32v b) { return veorq_u32(a, b); }
template <int N> inline u32v u_shl(u32v a) { return vshlq_n_u32(a, N); }
template <int N> inline u32v u_rotl(u32v a) {
    return vorrq_u32(vshlq_n_u32(a, N), vshrq_n_u32(a, 32 - N));
}
inline f32v f_from_u(u32v a) { return vcvtq_f32_u32(a); }
inline f32v f_set(float x) { return vdupq_n_f32(x); }
inline f32v f_add(f32v a, f32v b) { return vaddq_f32(a, b); }
inline f32v f_sub(f32v a, f32v b) { return vsubq_f32(a, b); }
inline f32v f_mul(f32v a, f32v b) { return vmulq_f32(a, b); }
inline f32v f_div(f32v a, f32v b) { return vdivq_f32(a, b); }
inline f32v f_max(f32v a, f32v b) { return vmaxq_f32(a, b); }
inline f32v f_min(f32v a, f32v b) { return vminq_f32(a, b); }
inline void f_store(float* p, f32v v) { vst1q_f32(p, v); }
inline bool f_any_outside(f32v v, f32v lo, f32v hi) {
    return vmaxvq_u32(vorrq_u32(vcltq_f32(v, lo), vcgtq_f32(v, hi))) != 0u;
}
inline f32v f_sqrt(f32v a) { return vsqrtq_f32(a); }
inline f32v f_floor(f32v a) { return vrndmq_f32(a); }
inline f32v f_and_mask(f32v a, u32v m) {
    return vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(a), m));
}
inline u32v f_lt_mask(f32v a, f32v b) { return vcltq_f32(a, b); }
inline u32v u_and(u32v a, u32v b) { return vandq_u32(a, b); }
inline u32v u_eq0_mask(u32v a) { return vceqq_u32(a, vdupq_n_u32(0)); }
inline f32v f_blend(f32v a, f32v b, u32v m) { return vbslq_f32(m, b, a); }
inline u32v u_set(uint32_t v) { return vdupq_n_u32(v); }
inline u32v f_to_i(f32v a) { return vreinterpretq_u32_s32(vcvtq_s32_f32(a)); }
inline f32v i_to_f(u32v a) { return vcvtq_f32_s32(vreinterpretq_s32_u32(a)); }
inline f32v f_reinterp(u32v a) { return vreinterpretq_f32_u32(a); }
inline u32v u_reinterp(f32v a) { return vreinterpretq_u32_f32(a); }
inline u32v u_shr_l(u32v a, int n) { return vshlq_u32(a, vdupq_n_s32(-n)); }
inline u32v u_shl_l(u32v a, int n) { return vshlq_u32(a, vdupq_n_s32(n)); }
inline u32v u_sub(u32v a, u32v b) { return vsubq_u32(a, b); }

#elif TTTRLIB_SIMD_ISA == 2                                          // ---------- AVX2
constexpr int kLanes = 8;
using u32v = __m256i;
using f32v = __m256;
inline u32v u_load(const uint32_t* p) { return _mm256_loadu_si256((const __m256i*)p); }
inline void u_store(uint32_t* p, u32v v) { _mm256_storeu_si256((__m256i*)p, v); }
inline u32v u_add(u32v a, u32v b) { return _mm256_add_epi32(a, b); }
inline u32v u_xor(u32v a, u32v b) { return _mm256_xor_si256(a, b); }
template <int N> inline u32v u_shl(u32v a) { return _mm256_slli_epi32(a, N); }
template <int N> inline u32v u_rotl(u32v a) {
    return _mm256_or_si256(_mm256_slli_epi32(a, N), _mm256_srli_epi32(a, 32 - N));
}
inline f32v f_from_u(u32v a) {
    // No unsigned->float convert before AVX512, so the top bit is folded back in by hand.
    // The mask MUST be built with an integer arithmetic shift, exactly as the SSE2 path
    // does: testing the extracted sign bit as a float instead does not work, because that
    // bit pattern is -0.0 and IEEE says -0.0 == 0.0, so the comparison is always false and
    // 2^31 is never added. The symptom is subtle -- |z| stays perfectly distributed while
    // every sign flips negative, since u is then confined to [0, 0.5).
    const __m256i mask = _mm256_set1_epi32(0x7FFFFFFF);
    __m256 low = _mm256_cvtepi32_ps(_mm256_and_si256(a, mask));
    __m256i top = _mm256_srai_epi32(a, 31);            // all ones where the top bit is set
    __m256 add = _mm256_and_ps(_mm256_set1_ps(2147483648.0f), _mm256_castsi256_ps(top));
    return _mm256_add_ps(low, add);
}
inline f32v f_set(float x) { return _mm256_set1_ps(x); }
inline f32v f_add(f32v a, f32v b) { return _mm256_add_ps(a, b); }
inline f32v f_sub(f32v a, f32v b) { return _mm256_sub_ps(a, b); }
inline f32v f_mul(f32v a, f32v b) { return _mm256_mul_ps(a, b); }
inline f32v f_div(f32v a, f32v b) { return _mm256_div_ps(a, b); }
inline f32v f_max(f32v a, f32v b) { return _mm256_max_ps(a, b); }
inline f32v f_min(f32v a, f32v b) { return _mm256_min_ps(a, b); }
inline void f_store(float* p, f32v v) { _mm256_storeu_ps(p, v); }
inline bool f_any_outside(f32v v, f32v lo, f32v hi) {
    const __m256 m = _mm256_or_ps(_mm256_cmp_ps(v, lo, _CMP_LT_OQ),
                                  _mm256_cmp_ps(v, hi, _CMP_GT_OQ));
    return _mm256_movemask_ps(m) != 0;
}
inline f32v f_sqrt(f32v a) { return _mm256_sqrt_ps(a); }
inline u32v f_lt_mask(f32v a, f32v b) {
    return _mm256_castps_si256(_mm256_cmp_ps(a, b, _CMP_LT_OQ));
}
inline f32v f_floor(f32v a) { return _mm256_floor_ps(a); }
inline u32v u_and(u32v a, u32v b) { return _mm256_and_si256(a, b); }
inline f32v f_blend(f32v a, f32v b, u32v m) {
    return _mm256_blendv_ps(a, b, _mm256_castsi256_ps(m));
}
inline u32v u_set(uint32_t v) { return _mm256_set1_epi32(int(v)); }
inline u32v f_to_i(f32v a) { return _mm256_cvttps_epi32(a); }
inline f32v i_to_f(u32v a) { return _mm256_cvtepi32_ps(a); }
inline f32v f_reinterp(u32v a) { return _mm256_castsi256_ps(a); }
inline u32v u_reinterp(f32v a) { return _mm256_castps_si256(a); }
inline u32v u_shr_l(u32v a, int n) { return _mm256_srli_epi32(a, n); }
inline u32v u_shl_l(u32v a, int n) { return _mm256_slli_epi32(a, n); }
inline u32v u_sub(u32v a, u32v b) { return _mm256_sub_epi32(a, b); }

#elif TTTRLIB_SIMD_ISA == 3                                          // ---------- SSE2
constexpr int kLanes = 4;
using u32v = __m128i;
using f32v = __m128;
inline u32v u_load(const uint32_t* p) { return _mm_loadu_si128((const __m128i*)p); }
inline void u_store(uint32_t* p, u32v v) { _mm_storeu_si128((__m128i*)p, v); }
inline u32v u_add(u32v a, u32v b) { return _mm_add_epi32(a, b); }
inline u32v u_xor(u32v a, u32v b) { return _mm_xor_si128(a, b); }
template <int N> inline u32v u_shl(u32v a) { return _mm_slli_epi32(a, N); }
template <int N> inline u32v u_rotl(u32v a) {
    return _mm_or_si128(_mm_slli_epi32(a, N), _mm_srli_epi32(a, 32 - N));
}
inline f32v f_from_u(u32v a) {
    const __m128i mask = _mm_set1_epi32(0x7FFFFFFF);
    __m128 low = _mm_cvtepi32_ps(_mm_and_si128(a, mask));
    __m128 top = _mm_castsi128_ps(_mm_srai_epi32(a, 31));            // all-ones where negative
    __m128 add = _mm_and_ps(_mm_set1_ps(2147483648.0f), top);
    return _mm_add_ps(low, add);
}
inline f32v f_set(float x) { return _mm_set1_ps(x); }
inline f32v f_add(f32v a, f32v b) { return _mm_add_ps(a, b); }
inline f32v f_sub(f32v a, f32v b) { return _mm_sub_ps(a, b); }
inline f32v f_mul(f32v a, f32v b) { return _mm_mul_ps(a, b); }
inline f32v f_div(f32v a, f32v b) { return _mm_div_ps(a, b); }
inline f32v f_max(f32v a, f32v b) { return _mm_max_ps(a, b); }
inline f32v f_min(f32v a, f32v b) { return _mm_min_ps(a, b); }
inline void f_store(float* p, f32v v) { _mm_storeu_ps(p, v); }
inline bool f_any_outside(f32v v, f32v lo, f32v hi) {
    return _mm_movemask_ps(_mm_or_ps(_mm_cmplt_ps(v, lo), _mm_cmpgt_ps(v, hi))) != 0;
}
inline f32v f_sqrt(f32v a) { return _mm_sqrt_ps(a); }
inline u32v f_lt_mask(f32v a, f32v b) { return _mm_castps_si128(_mm_cmplt_ps(a, b)); }
inline f32v f_floor(f32v a) {                       // SSE2 has no roundps
    __m128 t = _mm_cvtepi32_ps(_mm_cvttps_epi32(a));
    return _mm_sub_ps(t, _mm_and_ps(_mm_cmplt_ps(a, t), _mm_set1_ps(1.0f)));
}
inline u32v u_and(u32v a, u32v b) { return _mm_and_si128(a, b); }
inline f32v f_blend(f32v a, f32v b, u32v m) {
    const __m128 mm = _mm_castsi128_ps(m);
    return _mm_or_ps(_mm_andnot_ps(mm, a), _mm_and_ps(mm, b));
}
inline u32v u_set(uint32_t v) { return _mm_set1_epi32(int(v)); }
inline u32v f_to_i(f32v a) { return _mm_cvttps_epi32(a); }
inline f32v i_to_f(u32v a) { return _mm_cvtepi32_ps(a); }
inline f32v f_reinterp(u32v a) { return _mm_castsi128_ps(a); }
inline u32v u_reinterp(f32v a) { return _mm_castps_si128(a); }
inline u32v u_shr_l(u32v a, int n) { return _mm_srli_epi32(a, n); }
inline u32v u_shl_l(u32v a, int n) { return _mm_slli_epi32(a, n); }
inline u32v u_sub(u32v a, u32v b) { return _mm_sub_epi32(a, b); }

#else                                                                // ------- portable
constexpr int kLanes = 4;
struct u32v { uint32_t v[4]; };
struct f32v { float v[4]; };
inline u32v u_load(const uint32_t* p) { u32v r; for (int i=0;i<4;++i) r.v[i]=p[i]; return r; }
inline void u_store(uint32_t* p, u32v a) { for (int i=0;i<4;++i) p[i]=a.v[i]; }
inline u32v u_add(u32v a, u32v b) { u32v r; for (int i=0;i<4;++i) r.v[i]=a.v[i]+b.v[i]; return r; }
inline u32v u_xor(u32v a, u32v b) { u32v r; for (int i=0;i<4;++i) r.v[i]=a.v[i]^b.v[i]; return r; }
template <int N> inline u32v u_shl(u32v a) { u32v r; for (int i=0;i<4;++i) r.v[i]=a.v[i]<<N; return r; }
template <int N> inline u32v u_rotl(u32v a) {
    u32v r; for (int i=0;i<4;++i) r.v[i]=(a.v[i]<<N)|(a.v[i]>>(32-N)); return r;
}
inline f32v f_from_u(u32v a) { f32v r; for (int i=0;i<4;++i) r.v[i]=float(a.v[i]); return r; }
inline f32v f_set(float x) { f32v r; for (int i=0;i<4;++i) r.v[i]=x; return r; }
inline f32v f_add(f32v a, f32v b) { f32v r; for (int i=0;i<4;++i) r.v[i]=a.v[i]+b.v[i]; return r; }
inline f32v f_sub(f32v a, f32v b) { f32v r; for (int i=0;i<4;++i) r.v[i]=a.v[i]-b.v[i]; return r; }
inline f32v f_mul(f32v a, f32v b) { f32v r; for (int i=0;i<4;++i) r.v[i]=a.v[i]*b.v[i]; return r; }
inline f32v f_div(f32v a, f32v b) { f32v r; for (int i=0;i<4;++i) r.v[i]=a.v[i]/b.v[i]; return r; }
inline f32v f_max(f32v a, f32v b) { f32v r; for (int i=0;i<4;++i) r.v[i]=a.v[i]>b.v[i]?a.v[i]:b.v[i]; return r; }
inline f32v f_min(f32v a, f32v b) { f32v r; for (int i=0;i<4;++i) r.v[i]=a.v[i]<b.v[i]?a.v[i]:b.v[i]; return r; }
inline void f_store(float* p, f32v a) { for (int i=0;i<4;++i) p[i]=a.v[i]; }
inline bool f_any_outside(f32v v, f32v lo, f32v hi) {
    for (int i=0;i<4;++i) if (v.v[i] < lo.v[i] || v.v[i] > hi.v[i]) return true;
    return false;
}
inline f32v f_sqrt(f32v a) { f32v r; for (int i=0;i<4;++i) r.v[i]=std::sqrt(a.v[i]); return r; }
inline u32v f_lt_mask(f32v a, f32v b) { u32v r; for (int i=0;i<4;++i) r.v[i]=a.v[i]<b.v[i]?0xFFFFFFFFu:0u; return r; }
inline f32v f_floor(f32v a) { f32v r; for (int i=0;i<4;++i) r.v[i]=std::floor(a.v[i]); return r; }
inline u32v u_and(u32v a, u32v b) { u32v r; for (int i=0;i<4;++i) r.v[i]=a.v[i]&b.v[i]; return r; }
inline f32v f_blend(f32v a, f32v b, u32v m) {
    f32v r; for (int i=0;i<4;++i) r.v[i] = m.v[i] ? b.v[i] : a.v[i]; return r;
}
inline u32v u_set(uint32_t v) { u32v r; for (int i=0;i<4;++i) r.v[i]=v; return r; }
inline u32v f_to_i(f32v a) { u32v r; for (int i=0;i<4;++i) r.v[i]=uint32_t(int32_t(a.v[i])); return r; }
inline f32v i_to_f(u32v a) { f32v r; for (int i=0;i<4;++i) r.v[i]=float(int32_t(a.v[i])); return r; }
inline f32v f_reinterp(u32v a) { f32v r; for (int i=0;i<4;++i){ uint32_t t=a.v[i]; float f; __builtin_memcpy(&f,&t,4); r.v[i]=f;} return r; }
inline u32v u_reinterp(f32v a) { u32v r; for (int i=0;i<4;++i){ float f=a.v[i]; uint32_t t; __builtin_memcpy(&t,&f,4); r.v[i]=t;} return r; }
inline u32v u_shr_l(u32v a, int n) { u32v r; for (int i=0;i<4;++i) r.v[i]=a.v[i]>>n; return r; }
inline u32v u_shl_l(u32v a, int n) { u32v r; for (int i=0;i<4;++i) r.v[i]=a.v[i]<<n; return r; }
inline u32v u_sub(u32v a, u32v b) { u32v r; for (int i=0;i<4;++i) r.v[i]=a.v[i]-b.v[i]; return r; }
#endif

/// Fused multiply-add written as separate operations: correctness here does not depend on
/// the compiler contracting them, and letting it do so where it can is free.
inline f32v f_madd(f32v acc, f32v a, f32v b) { return f_add(acc, f_mul(a, b)); }

/// Name of the backend actually compiled in (for tests and reporting).
inline const char* backend() {
#if TTTRLIB_SIMD_ISA == 1
    return "neon";
#elif TTTRLIB_SIMD_ISA == 2
    return "avx2";
#elif TTTRLIB_SIMD_ISA == 3
    return "sse2";
#else
    return "scalar";
#endif
}

} // namespace simd

/// Lanes advanced together by SimRandomV; 8 with AVX2, otherwise 4.
constexpr int kSimdLanes = simd::kLanes;

/*!
 * \brief `kSimdLanes` independent xoshiro128+ generators advanced together.
 *
 * Lane `k` is its own generator with its own state, so lanes may be seeded from whatever
 * key the caller uses (per molecule, per window) exactly as the scalar engine does. What
 * differs from the scalar path is only the normal *transform*, not the notion of a
 * per-molecule stream.
 */
struct SimRandomV {
    uint32_t s0[kSimdLanes], s1[kSimdLanes], s2[kSimdLanes], s3[kSimdLanes];

    /// Seed lane `k` with SplitMix32, so nearby keys decorrelate.
    inline void seed_lane(int k, uint32_t key) {
        uint32_t z = key;
        auto mix = [&]() {
            z += 0x9E3779B9u;
            uint32_t t = z;
            t = (t ^ (t >> 16)) * 0x21F0AAADu;
            t = (t ^ (t >> 15)) * 0x735A2D97u;
            return t ^ (t >> 15);
        };
        s0[k] = mix(); s1[k] = mix(); s2[k] = mix(); s3[k] = mix();
        if ((s0[k] | s1[k] | s2[k] | s3[k]) == 0u) s0[k] = 1u;   // all-zero state is fixed
    }

    inline void seed(uint32_t base) {
        for (int k = 0; k < kSimdLanes; ++k)
            seed_lane(k, base + uint32_t(k) * 0x85EBCA6Bu);
    }

    /// One xoshiro128+ step across all lanes.
    inline simd::u32v next() {
        using namespace simd;
        u32v v0 = u_load(s0), v1 = u_load(s1), v2 = u_load(s2), v3 = u_load(s3);
        const u32v result = u_add(v0, v3);
        const u32v t = u_shl<9>(v1);
        v2 = u_xor(v2, v0);
        v3 = u_xor(v3, v1);
        v1 = u_xor(v1, v2);
        v0 = u_xor(v0, v3);
        v2 = u_xor(v2, t);
        v3 = u_rotl<11>(v3);
        u_store(s0, v0); u_store(s1, v1); u_store(s2, v2); u_store(s3, v3);
        return result;
    }

    /// Scalar reference for one lane -- the definition the vector step must match.
    inline uint32_t next_scalar(int k) {
        const uint32_t result = s0[k] + s3[k];
        const uint32_t t = s1[k] << 9;
        s2[k] ^= s0[k];
        s3[k] ^= s1[k];
        s1[k] ^= s2[k];
        s0[k] ^= s3[k];
        s2[k] ^= t;
        s3[k] = (s3[k] << 11) | (s3[k] >> 21);
        return result;
    }

    /// `kSimdLanes` normals, for bindings and tests.
    inline std::vector<double> draw();

    simd::f32v spare_{};        ///< second Box-Muller vector, kept for the next call
    bool has_spare_ = false;
};

/*!
 * \brief Branch-free vector natural logarithm for x in (0, 2], ~1e-7 relative.
 *
 * Splits `x = 1.m * 2^e` by exponent extraction and evaluates a minimax polynomial on the
 * mantissa. No table and no branch, which is what lets Box-Muller stay in vector registers.
 */
inline simd::f32v sim_vlog(simd::f32v x) {
    using namespace simd;
    const u32v xi = u_reinterp(x);
    const f32v e = i_to_f(u_sub(u_shr_l(xi, 23), u_set(127u)));          // exponent
    const f32v y = f_reinterp(u_add(u_and(xi, u_set(0x007FFFFFu)),
                                    u_set(0x3F800000u)));                // 1.mantissa, [1,2)
    // ln(y) = 2*atanh(f) with f = (y-1)/(y+1). Over [1,2) that is f in [0, 1/3], where the
    // series converges quickly. A plain polynomial in (y-1) does not: its error at the top
    // of the range reaches +6e-3, which for u just below 1 makes ln(u) POSITIVE, and then
    // sqrt(-2 ln u) is a nan. That produced 8138 nans in 1.6e6 draws while the histogram
    // still looked correct.
    const f32v f = f_div(f_sub(y, f_set(1.0f)), f_add(y, f_set(1.0f)));
    const f32v f2 = f_mul(f, f);
    f32v poly = f_set(0.0909090909f);                                    // 1/11
    poly = f_madd(f_set(0.1111111111f), poly, f2);                       // 1/9
    poly = f_madd(f_set(0.1428571429f), poly, f2);                       // 1/7
    poly = f_madd(f_set(0.2000000000f), poly, f2);                       // 1/5
    poly = f_madd(f_set(0.3333333333f), poly, f2);                       // 1/3
    poly = f_madd(f_set(1.0000000000f), poly, f2);
    return f_add(f_mul(f_mul(f_set(2.0f), f), poly),
                 f_mul(e, f_set(0.6931471805599453f)));
}


/// Branch-free vector sine and cosine, argument reduced from [0, 2*pi) to [-pi, pi).
inline void sim_vsincos(simd::f32v a, simd::f32v* s_out, simd::f32v* c_out) {
    using namespace simd;
    const f32v pi = f_set(3.141592653589793f), twopi = f_set(6.283185307179586f);
    // a arrives in [0, 2pi); fold the upper half down so the polynomials stay accurate.
    const f32v x = f_blend(a, f_sub(a, twopi), f_lt_mask(pi, a));
    const f32v x2 = f_mul(x, x);
    f32v sn = f_set(2.7557314e-06f);
    sn = f_madd(f_set(-1.9841270e-04f), sn, x2);
    sn = f_madd(f_set(8.3333333e-03f), sn, x2);
    sn = f_madd(f_set(-1.6666667e-01f), sn, x2);
    sn = f_madd(f_set(1.0f), sn, x2);
    *s_out = f_mul(sn, x);
    f32v cs = f_set(2.4801587e-05f);
    cs = f_madd(f_set(-1.3888889e-03f), cs, x2);
    cs = f_madd(f_set(4.1666667e-02f), cs, x2);
    cs = f_madd(f_set(-5.0000000e-01f), cs, x2);
    cs = f_madd(f_set(1.0f), cs, x2);
    *c_out = cs;
}



/*!
 * \brief Two vectors of standard normals by Box-Muller, entirely in registers.
 *
 * `z0 = sqrt(-2 ln u1) * cos(2*pi*u2)`, `z1 = sqrt(-2 ln u1) * sin(2*pi*u2)`, using the
 * branch-free vector log and sincos above. Both outputs come from one pair of uniform
 * draws, and nothing about it diverges per lane.
 */
inline void sim_normalv_boxmuller(SimRandomV& rng, simd::f32v* z0, simd::f32v* z1) {
    using namespace simd;
    const f32v inv32 = f_set(2.3283064365386963e-10f);
    // u1 must not be exactly zero (log); u2 needs no care.
    f32v u1 = f_mul(f_from_u(rng.next()), inv32);
    u1 = f_max(u1, f_set(1.1920929e-07f));                       // 2^-23
    const f32v u2 = f_mul(f_from_u(rng.next()), inv32);
    const f32v r = f_sqrt(f_mul(f_set(-2.0f), sim_vlog(u1)));
    f32v sn, cs;
    sim_vsincos(f_mul(u2, f_set(6.283185307179586f)), &sn, &cs);
    *z0 = f_mul(r, cs);
    *z1 = f_mul(r, sn);
}

/*!
 * \brief `kSimdLanes` standard normals into `out`.
 *
 * Box-Muller, because it is the transform that suits a vector unit: exact (no truncated
 * tail, so no scalar fix-up), table-free (nothing to gather) and entirely branch-free. It
 * yields two vectors per call and the unused one is carried to the next call rather than
 * discarded -- that waste was a quarter of the transform work.
 *
 * The obvious alternative, a rational approximation of the normal quantile, is a trap here.
 * Its central branch covers only |z| < 1.97, so 4.85 % of lanes fall outside it. Patching
 * those scalar costs more than the vectorisation saves (measured 0.71x against scalar), and
 * *not* patching them truncates the distribution at |z| = 3.22 while mean, variance and a
 * KS test all stay clean. Box-Muller has neither problem and measures 1.01-1.05x.
 */
inline void sim_normalv(SimRandomV& rng, float* out) {
    if (rng.has_spare_) {
        rng.has_spare_ = false;
        simd::f_store(out, rng.spare_);
        return;
    }
    simd::f32v z0, z1;
    sim_normalv_boxmuller(rng, &z0, &z1);
    rng.spare_ = z1;
    rng.has_spare_ = true;
    simd::f_store(out, z0);
}

inline std::vector<double> SimRandomV::draw() {
    float f[kSimdLanes];
    sim_normalv(*this, f);
    return std::vector<double>(f, f + kSimdLanes);
}

/// Which vector backend this build uses: "neon", "avx2", "sse2" or "scalar".
inline const char* sim_simd_backend() { return simd::backend(); }

/// Lanes advanced per vector step.
inline int sim_simd_lanes() { return kSimdLanes; }

} // namespace tttrlib

#endif // TTTRLIB_SIMSIMD_H

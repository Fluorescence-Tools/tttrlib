// SPDX-License-Identifier: BSD-3-Clause
#include "DecayConvolution.h"
#include "Verbose.h"
#include "info.h"

/* rescaling -- old version. sum(fit)->sum(decay) */
void rescale(double *fit, double *decay, double *scale, int start, int stop) {
    /* scaling */
    if (*scale == 0.) {
        double sumfit = 0., sumcurve = 0.;
        for (int i = start; i < stop; i++) {
            sumfit += fit[i];
            sumcurve += decay[i];
        }
        if (sumfit != 0.) *scale = sumcurve / sumfit;
    }
    for (int i = start; i < stop; i++)
        fit[i] *= *scale;
}

/* rescaling -- new version. scale = sum(fit*decay/w^2)/sum(fit^2/w^2) */
void rescale_w(double *fit, double *decay, double *w_sq, double *scale, int start, int stop) {
    /* scaling */
    if (*scale == 0.) {
        double sumnom = 0., sumdenom = 0.;
        for (int i = start; i < stop; i++) {
            if (decay[i] != 0.) {
                sumnom += fit[i] * decay[i] / w_sq[i];
                sumdenom += fit[i] * fit[i] / w_sq[i];
            }
        }
        if (sumdenom != 0.) *scale = sumnom / sumdenom;
    }
    for (int i = start; i < stop; i++)
        fit[i] *= *scale;

}

/* rescaling -- new version + background. scale = sum(fit*decay/w^2)/sum(fit^2/w^2) */
void rescale_w_bg(double *fit, double *decay, double *e_sq, double bg, double *scale, int start, int stop) {
    double sumnom = 0., sumdenom = 0.;
    for (int i = start; i < stop; i++) {
        if(decay[i] > 0){
            double iwsq = (e_sq[i]*e_sq[i]+1e-12);
            sumnom += fit[i] * (decay[i] - bg) * iwsq;
            sumdenom += fit[i] * fit[i] * iwsq;
        }
    }
    if (sumdenom != 0.) *scale = sumnom / sumdenom;
    for (int i = start; i < stop; i++)
        fit[i] *= *scale;
if (is_verbose()) {
    std::clog << "RESCALE_W_BG" << std::endl;
    std::clog << "w_sq [start:stop]: "; for(int i=start; i<stop; i++) std::clog << e_sq[i] << " "; std::clog << std::endl;
    std::clog << "decay [start:stop]: "; for(int i=start; i<stop; i++) std::clog << decay[i] << " "; std::clog << std::endl;
    std::clog << "fit [start:stop]: "; for(int i=start; i<stop; i++) std::clog << fit[i] << " "; std::clog << std::endl;
    std::clog << "-- sumnom: " << sumnom << std::endl;
    std::clog << "-- sumdenom: " << sumdenom << std::endl;
    std::clog << "-- final scale: " << *scale << std::endl;
}
}


// fast convolution - scalar reference implementation.
static void fconv_scalar(double *fit, double *x, double *lamp, int numexp, int start, int stop, double dt) {
    std::vector<double> l2(stop);
    start = std::max(1, start);
    for (int i = 0; i < stop; i++) l2[i] = dt * 0.5 * lamp[i];
    /* convolution */
    for (int ne = 0; ne < numexp; ne++) {
        double expcurr = exp(-dt / x[2 * ne + 1]);
        double a = x[2 * ne];
        double fitcurr = 0.0;
        fit[0] += l2[0] * a;
        for (int i = start; i < stop; i++) {
            fitcurr = (fitcurr + l2[i - 1]) * expcurr + l2[i];
            fit[i] += fitcurr * a;
        }
    }
}


#if TTTRLIB_COMPILE_AVX
// AVX+FMA kernel for fconv(). Only called after a runtime CPUID check confirms
// the host supports AVX and FMA (see fconv_simd() dispatcher below); the target
// attribute lets it use AVX/FMA even when the TU is built without -mavx.
TTTRLIB_TARGET_AVX_FMA
static void fconv_avx_impl(double *fit, double *x, double *lamp, int numexp, int start, int stop, double dt) {
    int start1 = std::max(1, start);

    // make sure that there are always multiple of 4 in the lifetimes
    const int chunk_size = 4; // the number of lifetimes per AVX register
    int n_chunks = (int) std::ceil((double) numexp / chunk_size);
    int n_ele = n_chunks * chunk_size;

    // copy the interleaved lifetime spectrum to vectors
    auto *p = (double *) _mm_malloc(n_ele * sizeof(double), 32);
    std::fill(p, p + n_ele, 0.0);
    for (int i = 0; i < numexp; i++) p[i] = x[2 * i + 0];

    auto *ex = (double *) _mm_malloc(n_ele * sizeof(double), 32);
    std::fill(ex, ex + n_ele, 0.0);
    for (int i = 0; i < numexp; i++) ex[i] = exp(-dt / x[2 * i + 1]);

    // precompute have lamp steps in units of dt
    auto l2 = (double *) malloc(stop * sizeof(double));
    for (int i = 0; i < stop; i++) l2[i] = dt * 0.5 * lamp[i];

    std::fill(fit, fit + stop, 0.0);
    __m256d e, a, fitcurr, l2p, l2c, tmp;
    double tmp_vals[4];
    for (int ne = 0; ne < numexp; ne += chunk_size) {
        // expcurr = exp(-dt / x[2 * ne + 1]);
        e = _mm256_load_pd(&ex[ne]);
        // amplitudes
        a = _mm256_load_pd(&p[ne]);
        // take care of first channel
        // fit[0] += l2[0] * a;
        l2c = _mm256_set1_pd(l2[0]);
        tmp = _mm256_mul_pd(l2c, a);
        _mm256_storeu_pd(tmp_vals, tmp);
        fit[0] += tmp_vals[0] + tmp_vals[1] + tmp_vals[2] + tmp_vals[3];
        fitcurr = _mm256_set1_pd(0.0);
        // convolution
        for (int i = start1; i < stop; i++) {
            l2p = _mm256_set1_pd(l2[i - 1]);
            l2c = _mm256_set1_pd(l2[i]);
            //fitcurr = (fitcurr + l2[i - 1]) * expcurr + l2[i];
            fitcurr = _mm256_add_pd(fitcurr, l2p);
            fitcurr = _mm256_fmadd_pd(fitcurr, e, l2c);
            // fit[i] += fitcurr * a;
            tmp = _mm256_mul_pd(fitcurr, a);
            _mm256_storeu_pd(tmp_vals, tmp);
            fit[i] += tmp_vals[0] + tmp_vals[1] + tmp_vals[2] + tmp_vals[3];
        }
    }
    free(l2);
    _mm_free(ex); _mm_free(p);
}
#endif // TTTRLIB_COMPILE_AVX

#if TTTRLIB_COMPILE_NEON
// NEON kernel for fconv(): processes 2 lifetimes per float64x2_t. The per-
// lifetime recurrence cannot be autovectorized, so this manual 2-wide version
// wins (~1.75x on Apple M1). NEON is baseline on AArch64 - no CPUID needed.
static void fconv_neon_impl(double *fit, double *x, double *lamp, int numexp, int start, int stop, double dt) {
    int start1 = std::max(1, start);
    const int chunk_size = 2; // lifetimes per NEON register (float64x2_t)
    int n_ele = ((numexp + chunk_size - 1) / chunk_size) * chunk_size;

    std::vector<double> p(n_ele, 0.0), ex(n_ele, 0.0), l2(stop);
    for (int i = 0; i < numexp; i++) { p[i] = x[2 * i]; ex[i] = exp(-dt / x[2 * i + 1]); }
    for (int i = 0; i < stop; i++) l2[i] = dt * 0.5 * lamp[i];

    std::fill(fit, fit + stop, 0.0);
    for (int ne = 0; ne < numexp; ne += chunk_size) {
        float64x2_t e = vld1q_f64(&ex[ne]);
        float64x2_t a = vld1q_f64(&p[ne]);
        float64x2_t tmp = vmulq_f64(vdupq_n_f64(l2[0]), a);
        fit[0] += vgetq_lane_f64(tmp, 0) + vgetq_lane_f64(tmp, 1);
        float64x2_t fitcurr = vdupq_n_f64(0.0);
        for (int i = start1; i < stop; i++) {
            fitcurr = vaddq_f64(fitcurr, vdupq_n_f64(l2[i - 1]));
            // fitcurr = fitcurr * e + l2[i]
            fitcurr = vfmaq_f64(vdupq_n_f64(l2[i]), fitcurr, e);
            tmp = vmulq_f64(fitcurr, a);
            fit[i] += vgetq_lane_f64(tmp, 0) + vgetq_lane_f64(tmp, 1);
        }
    }
}
#endif // TTTRLIB_COMPILE_NEON

// Below this many lifetimes the SIMD kernels do not pay off: they vectorise
// ACROSS lifetimes and zero-pad to the register width, so a single-exponential
// spectrum does the same work with extra setup. Measured on AArch64/NEON
// (n=1024): numexp=1 is 1.02x for fconv and 0.89x -- i.e. a REGRESSION -- for
// fconv_per, while numexp>=2 is a consistent 1.65-1.85x win. Selecting on CPU
// features alone would therefore make single-exponential fits slower.
static const int kSimdMinNumexp = 2;

static inline bool simd_convolution_available(int numexp) {
    if (numexp < kSimdMinNumexp) return false;
#if TTTRLIB_COMPILE_AVX
    return tttrlib::cpu_features::get_avx_enabled() && tttrlib::cpu_features::get_fma_enabled();
#elif TTTRLIB_COMPILE_NEON
    return tttrlib::cpu_features::get_neon_enabled();
#else
    return false;
#endif
}

// fast convolution - picks the best available kernel automatically (AVX/FMA on
// x86_64, NEON on AArch64, scalar otherwise), based on both the host CPU and
// the problem size. Callers do not need to know which kernels exist.
void fconv(double *fit, double *x, double *lamp, int numexp, int start, int stop, double dt) {
    if (simd_convolution_available(numexp)) {
#if TTTRLIB_COMPILE_AVX
        fconv_avx_impl(fit, x, lamp, numexp, start, stop, dt);
        return;
#elif TTTRLIB_COMPILE_NEON
        fconv_neon_impl(fit, x, lamp, numexp, start, stop, dt);
        return;
#endif
    }
    fconv_scalar(fit, x, lamp, numexp, start, stop, dt);
}

/// Explicit name for the same automatic selection; kept because callers use it.
void fconv_simd(double *fit, double *x, double *lamp, int numexp, int start, int stop, double dt) {
    fconv(fit, x, lamp, numexp, start, stop, dt);
}



/* fast convolution, high repetition rate - scalar reference implementation. */
static void fconv_per_scalar(double *fit, double *x, double *lamp, int numexp, int start, int stop,
               int n_points, double period, double dt)
{
    stop = (stop < 0) ? n_points: stop;
    int period_n = (int)ceil(period/dt-0.5);

    int lamp_start = 0;
    while (lamp_start < stop && lamp[lamp_start++] == 0);

    int start1 = std::max(1, start);
    int stop1 = std::min(period_n+lamp_start, n_points);

    if (is_verbose()) {
    std::clog << "FCONV_PER" << std::endl;
    std::clog << "-- numexp:" << numexp << std::endl;
    std::clog << "-- start:" << start << std::endl;
    std::clog << "-- stop:" << stop << std::endl;
    std::clog << "-- n_points:" << n_points << std::endl;
    std::clog << "-- period:" << period << std::endl;
    std::clog << "-- dt:" << dt << std::endl;
}

    // Precompute everything needed for the convolution
    // lamp * dt * 0.5
    auto l2 = (double *) malloc(stop * sizeof(double));
    for (int i = 0; i < stop; i++) l2[i] = dt * 0.5 * lamp[i];

    /* convolution */
    for (int ne=0; ne<numexp; ne++) {
        double expcurr = exp(-dt/x[2*ne+1]);
        double tail_a = 1./(1.-exp(-period/x[2*ne+1]));
        double fitcurr = 0;
        fit[0] += (fitcurr + l2[0])*x[2*ne];
        for (int i=start1; i<stop1; i++){
            fitcurr=(fitcurr + l2[i - 1])*expcurr + l2[i];
            fit[i] += fitcurr*x[2*ne];
        }
        fitcurr *= exp(-(period_n - stop1 + start)*dt/x[2*ne+1]);
        for (int i=start; i<stop; i++){
            fitcurr *= expcurr;
            fit[i] += fitcurr*x[2*ne]*tail_a;
        }
    }
    free(l2);
}


#if TTTRLIB_COMPILE_AVX
// AVX+FMA kernel for fconv_per(); dispatched only on AVX+FMA capable CPUs.
TTTRLIB_TARGET_AVX_FMA
static void fconv_per_avx_impl(double *fit, double *x, double *lamp, int numexp, int start, int stop,
                   int n_points, double period, double dt) {
if (is_verbose()) {
    std::clog << "FCONV_PER_AVX" << std::endl;
    std::clog << "-- numexp: " << numexp << std::endl;
    std::clog << "-- start: " << start << std::endl;
    std::clog << "-- stop: " << stop << std::endl;
    std::clog << "-- n_points: " << n_points << std::endl;
    std::clog << "-- period: " << period << std::endl;
    std::clog << "-- dt: " << dt << std::endl;
}
    int start1 = std::max(1, start);
    stop = (stop < 0) ? n_points: stop;
    // make sure that there are always multiple of the AVX register size
    const int chunk_size = 4; // the number of lifetimes per AVX register
    int n_chunks = (int) std::ceil((double) numexp / chunk_size);
    int n_ele = n_chunks * chunk_size;

    // Number of time channels in period
    int period_n = (int)ceil(period/dt-0.5);

    // Check if the window is larger than the decay histogram.
    // If it is larger only convolve till the end of the decay. Otherwise,
    // convolve till end of period. The period starts at the
    // excitation pulse.
    // Find the position where the IRF starts
    int lamp_start = 0;
    while (lamp_start < stop && lamp[lamp_start++] == 0);
    int stop1 = std::min(period_n+lamp_start, n_points);

    // Precompute everything needed for the convolution
    // lamp * dt * 0.5
    auto l2 = (double *) malloc(stop * sizeof(double));
    for (int i = 0; i < stop; i++) l2[i] = dt * 0.5 * lamp[i];

    // exponential
    auto ex = (double *) _mm_malloc(n_ele * sizeof(double), 32);
    std::fill(ex, ex + n_ele, 0.0);
    for (int i = 0; i < numexp; i++) ex[i] = exp(-dt / x[2 * i + 1]);

    // amplitudes
    auto p = (double *) _mm_malloc(n_ele * sizeof(double), 32);
    std::fill(p, p + n_ele, 0.0);
    for (int i = 0; i < numexp; i++) p[i] = x[2 * i];

    // scale of decay relative to tail
    auto scale = (double *) _mm_malloc(n_ele * sizeof(double), 32);
    std::fill(scale, scale + n_ele, 0.0);
    for (int i = 0; i < numexp; i++) scale[i] = exp(-(period_n - stop1 + start) * dt / x[2 * i + 1]);

    // tails wrapping to next period
    auto tails = (double *) _mm_malloc(n_ele * sizeof(double), 32);
    std::fill(tails, tails + n_ele, 0.0);
    for (int i = 0; i < numexp; i++) tails[i] = 1. / (1. - exp(-period / x[2 * i + 1]));

    // CONVOLUTION
    std::fill(fit, fit + n_points, 0.0);
    __m256d fitcurr, l2p, l2c, a, e, s, t, tmp;
    double tmp_vals[4];
    for (int ne = 0; ne < numexp; ne += chunk_size) {
        e = _mm256_load_pd(&ex[ne]);     // expcurr = exp(-dt / x[2 * ne + 1]);
        a = _mm256_load_pd(&p[ne]);      // amplitudes
        s = _mm256_load_pd(&scale[ne]);  // scales
        t = _mm256_load_pd(&tails[ne]);  // tail

        // take care of first channel
        // fit[0] += l2[0] * a;
        l2c = _mm256_set1_pd(l2[0]);
        tmp = _mm256_mul_pd(l2c, a);
        _mm256_storeu_pd(tmp_vals, tmp);
        fit[0] += tmp_vals[0] + tmp_vals[1] + tmp_vals[2] + tmp_vals[3];
        fitcurr = _mm256_set1_pd(0.0);
        for (int i = start1; i < stop1; i++) {
            //fitcurr = (fitcurr + l2[i - 1]) * expcurr + l2[i];
            int pre = std::max(0, i - 1);

            l2p = _mm256_set1_pd(l2[pre]);
            l2c = _mm256_set1_pd(l2[i]);
            fitcurr = _mm256_add_pd(fitcurr, l2p);
            fitcurr = _mm256_fmadd_pd(fitcurr, e, l2c);
            // fit[i] += fitcurr * a;
            tmp = _mm256_mul_pd(fitcurr, a);
            _mm256_storeu_pd(tmp_vals, tmp);
            fit[i] += tmp_vals[0] + tmp_vals[1] + tmp_vals[2] + tmp_vals[3];
        }
        // fitcurr *= scale[ne];
        fitcurr = _mm256_mul_pd(fitcurr, s);
        // tail
        for (int i = start; i < stop; i++) {
            //fitcurr *= e[ne];
            fitcurr = _mm256_mul_pd(fitcurr, e);
            //fit[i] += fitcurr * a[ne] * tails[ne];
            tmp = _mm256_mul_pd(fitcurr, a);
            tmp = _mm256_mul_pd(tmp, t);
            _mm256_storeu_pd(tmp_vals, tmp);
            fit[i] += tmp_vals[0] + tmp_vals[1] + tmp_vals[2] + tmp_vals[3];
        }
    }
    free(l2); _mm_free(p); _mm_free(ex); _mm_free(scale); _mm_free(tails);
}
#endif // TTTRLIB_COMPILE_AVX

#if TTTRLIB_COMPILE_NEON
// NEON kernel for fconv_per(): 2 lifetimes per float64x2_t (mirror of the AVX
// kernel). Wins on AArch64 because the per-lifetime recurrence cannot be
// autovectorized.
static void fconv_per_neon_impl(double *fit, double *x, double *lamp, int numexp, int start, int stop,
                   int n_points, double period, double dt) {
if (is_verbose()) {
    std::clog << "FCONV_PER_NEON" << std::endl;
}
    int start1 = std::max(1, start);
    stop = (stop < 0) ? n_points: stop;
    const int chunk_size = 2; // lifetimes per NEON register
    int n_ele = ((numexp + chunk_size - 1) / chunk_size) * chunk_size;

    int period_n = (int)ceil(period/dt-0.5);
    int lamp_start = 0;
    while (lamp_start < stop && lamp[lamp_start++] == 0);
    int stop1 = std::min(period_n+lamp_start, n_points);

    std::vector<double> l2(stop), ex(n_ele, 0.0), p(n_ele, 0.0), scale(n_ele, 0.0), tails(n_ele, 0.0);
    for (int i = 0; i < stop; i++) l2[i] = dt * 0.5 * lamp[i];
    for (int i = 0; i < numexp; i++) {
        ex[i] = exp(-dt / x[2 * i + 1]);
        p[i] = x[2 * i];
        scale[i] = exp(-(period_n - stop1 + start) * dt / x[2 * i + 1]);
        tails[i] = 1. / (1. - exp(-period / x[2 * i + 1]));
    }

    std::fill(fit, fit + n_points, 0.0);
    for (int ne = 0; ne < numexp; ne += chunk_size) {
        float64x2_t e = vld1q_f64(&ex[ne]);
        float64x2_t a = vld1q_f64(&p[ne]);
        float64x2_t s = vld1q_f64(&scale[ne]);
        float64x2_t t = vld1q_f64(&tails[ne]);

        float64x2_t tmp = vmulq_f64(vdupq_n_f64(l2[0]), a);
        fit[0] += vgetq_lane_f64(tmp, 0) + vgetq_lane_f64(tmp, 1);
        float64x2_t fitcurr = vdupq_n_f64(0.0);
        for (int i = start1; i < stop1; i++) {
            int pre = std::max(0, i - 1);
            fitcurr = vaddq_f64(fitcurr, vdupq_n_f64(l2[pre]));
            fitcurr = vfmaq_f64(vdupq_n_f64(l2[i]), fitcurr, e);
            tmp = vmulq_f64(fitcurr, a);
            fit[i] += vgetq_lane_f64(tmp, 0) + vgetq_lane_f64(tmp, 1);
        }
        fitcurr = vmulq_f64(fitcurr, s);
        for (int i = start; i < stop; i++) {
            fitcurr = vmulq_f64(fitcurr, e);
            tmp = vmulq_f64(vmulq_f64(fitcurr, a), t);
            fit[i] += vgetq_lane_f64(tmp, 0) + vgetq_lane_f64(tmp, 1);
        }
    }
}
#endif // TTTRLIB_COMPILE_NEON

// fast convolution, high repetition rate - picks the best available kernel
// automatically; see fconv() for why the choice depends on numexp as well as
// on the host CPU.
void fconv_per(double *fit, double *x, double *lamp, int numexp, int start, int stop,
               int n_points, double period, double dt) {
    if (simd_convolution_available(numexp)) {
#if TTTRLIB_COMPILE_AVX
        fconv_per_avx_impl(fit, x, lamp, numexp, start, stop, n_points, period, dt);
        return;
#elif TTTRLIB_COMPILE_NEON
        fconv_per_neon_impl(fit, x, lamp, numexp, start, stop, n_points, period, dt);
        return;
#endif
    }
    fconv_per_scalar(fit, x, lamp, numexp, start, stop, n_points, period, dt);
}

/// Explicit name for the same automatic selection; kept because callers use it.
void fconv_per_simd(double *fit, double *x, double *lamp, int numexp, int start, int stop,
                   int n_points, double period, double dt) {
    fconv_per(fit, x, lamp, numexp, start, stop, n_points, period, dt);
}


#if TTTRLIB_COMPILE_NEON
// Periodic convolution WITH a convolution stop, vectorised over lifetimes
// (lane 0 and lane 1 carry two different lifetimes of the same spectrum).
// Mirrors fconv_per_cs()'s scalar recurrences exactly; padding lanes are given
// a zero amplitude and a zero decay factor so they contribute nothing.
static void fconv_per_cs_neon_impl(double *fit, double *x, double *lamp, int numexp, int stop,
                                   int n_points, double period, int conv_stop, double dt)
{
    const int chunk = 2;                       // lifetimes per float64x2 register
    const int n_ele = ((numexp + chunk - 1) / chunk) * chunk;
    const int period_n = (int)ceil(period / dt - 0.5);
    const int stop1 = (period_n > n_points - 1) ? n_points - 1 : period_n;
    const double deltathalf = dt * 0.5;

    std::vector<double> ex(n_ele, 0.0), amp(n_ele, 0.0), tail(n_ele, 0.0), post(n_ele, 0.0);
    for (int i = 0; i < numexp; i++) {
        ex[i]   = exp(-dt / x[2 * i + 1]);
        amp[i]  = x[2 * i];
        tail[i] = 1. / (1. - exp(-period / x[2 * i + 1]));
        post[i] = exp(-(period_n - stop1) * dt / x[2 * i + 1]);
    }

    for (int i = 0; i <= stop; i++) fit[i] = 0.0;

    for (int ne = 0; ne < numexp; ne += chunk) {
        const float64x2_t e = vld1q_f64(&ex[ne]);
        const float64x2_t a = vld1q_f64(&amp[ne]);
        const float64x2_t t = vld1q_f64(&tail[ne]);
        const float64x2_t s = vld1q_f64(&post[ne]);

        // fit[0] += deltathalf*lamp[0]*(expcurr + 1.)*x[2*ne]
        float64x2_t tmp = vmulq_f64(vmulq_f64(vdupq_n_f64(deltathalf * lamp[0]),
                                              vaddq_f64(e, vdupq_n_f64(1.0))), a);
        fit[0] += vgetq_lane_f64(tmp, 0) + vgetq_lane_f64(tmp, 1);

        float64x2_t fitcurr = vdupq_n_f64(0.0);
        int i = 1;
        for (; i <= conv_stop; i++) {
            // fitcurr = (fitcurr + deltathalf*lamp[i-1])*expcurr + deltathalf*lamp[i]
            fitcurr = vaddq_f64(fitcurr, vdupq_n_f64(deltathalf * lamp[i - 1]));
            fitcurr = vfmaq_f64(vdupq_n_f64(deltathalf * lamp[i]), fitcurr, e);
            tmp = vmulq_f64(fitcurr, a);
            fit[i] += vgetq_lane_f64(tmp, 0) + vgetq_lane_f64(tmp, 1);
        }
        for (; i <= stop1; i++) {
            fitcurr = vmulq_f64(fitcurr, e);
            tmp = vmulq_f64(fitcurr, a);
            fit[i] += vgetq_lane_f64(tmp, 0) + vgetq_lane_f64(tmp, 1);
        }
        fitcurr = vmulq_f64(fitcurr, s);
        // The wrap-around tail. fitcurr now holds the continuation at bin
        // period_n, which *is* bin 0 of the next period — so bin 0 takes it as
        // it stands and the decay step comes after, not before. Stepping first
        // would place the value belonging to bin period_n+1 into bin 0 and shift
        // the whole tail one bin early. (fconv_per() gets this right by ending
        // its main loop one bin earlier, which is why only the _cs variants
        // carried the error.) It is invisible whenever the decay completes
        // within the period, and grows as it does not: 5.8e-5 of the peak at a
        // lifetime of a fifth of the period, and larger for longer lifetimes.
        // With this order the recursion matches an exact circular convolution to
        // 1.3e-15; test_dfa_kernel.py pins that against the spectral backend.
        for (i = 0; i <= stop; i++) {
            tmp = vmulq_f64(vmulq_f64(fitcurr, a), t);
            fit[i] += vgetq_lane_f64(tmp, 0) + vgetq_lane_f64(tmp, 1);
            fitcurr = vmulq_f64(fitcurr, e);
        }
    }
}
#endif // TTTRLIB_COMPILE_NEON

/* fast convolution, high repetition rate, with convolution stop for Paris */
static void fconv_per_cs_scalar(double *fit, double *x, double *lamp, int numexp, int stop,
                  int n_points, double period, int conv_stop, double dt)
{
    int ne, i,
            stop1, period_n = (int)ceil(period/dt-0.5);
    double fitcurr, expcurr, tail_a, deltathalf = dt*0.5;

    for (i=0; i<=stop; i++) fit[i]=0;
    stop1 = (period_n > n_points-1) ? n_points-1 : period_n;

    /* convolution */
    for (ne=0; ne<numexp; ne++) {
        expcurr = exp(-dt/x[2*ne+1]);
        tail_a = 1./(1.-exp(-period/x[2*ne+1]));
        fitcurr = 0.;
        fit[0] += deltathalf*lamp[0]*(expcurr + 1.)*x[2*ne];
        for (i=1; i<=conv_stop; i++) {
            fitcurr=(fitcurr + deltathalf*lamp[i-1])*expcurr + deltathalf*lamp[i];
            fit[i] += fitcurr*x[2*ne];
        }
        for (; i<=stop1; i++) {
            fitcurr=fitcurr*expcurr;
            fit[i] += fitcurr*x[2*ne];
        }
        fitcurr *= exp(-(period_n - stop1)*dt/x[2*ne+1]);
        // The wrap-around tail. fitcurr now holds the continuation at bin
        // period_n, which *is* bin 0 of the next period — so bin 0 takes it as
        // it stands and the decay step comes after, not before. Stepping first
        // would place the value belonging to bin period_n+1 into bin 0 and shift
        // the whole tail one bin early. (fconv_per() gets this right by ending
        // its main loop one bin earlier, which is why only the _cs variants
        // carried the error.) It is invisible whenever the decay completes
        // within the period, and grows as it does not: 5.8e-5 of the peak at a
        // lifetime of a fifth of the period, and larger for longer lifetimes.
        // With this order the recursion matches an exact circular convolution to
        // 1.3e-15; test_dfa_kernel.py pins that against the spectral backend.
        for (i=0; i<=stop; i++) {
            fit[i] += fitcurr*x[2*ne]*tail_a;
            fitcurr *= expcurr;
        }
    }
}

// Periodic convolution with a convolution stop - picks the best available
// kernel automatically, on the same CPU-and-size rule as fconv()/fconv_per().
// No AVX kernel exists for this variant yet, so x86 takes the scalar path.
void fconv_per_cs(double *fit, double *x, double *lamp, int numexp, int stop,
                  int n_points, double period, int conv_stop, double dt)
{
#if TTTRLIB_COMPILE_NEON
    if (numexp >= kSimdMinNumexp && tttrlib::cpu_features::get_neon_enabled()) {
        fconv_per_cs_neon_impl(fit, x, lamp, numexp, stop, n_points, period, conv_stop, dt);
        return;
    }
#endif
    fconv_per_cs_scalar(fit, x, lamp, numexp, stop, n_points, period, conv_stop, dt);
}


#if TTTRLIB_COMPILE_NEON
// Two channels (lane 0 = channel 0, lane 1 = channel 1) of the periodic
// convolution in NEON float64x2. The channels share the lifetimes (so expcurr
// is a broadcast scalar); only the amplitudes and IRF differ per lane. FMA is
// used (as in fconv_neon_impl), so results match the scalar path to rounding.
static void fconv_per_cs_2ch_neon(
        double *fit0, double *fit1,
        const double *x0, const double *x1,
        const double *lamp0, const double *lamp1,
        int numexp, int stop, int n_points,
        double period, int conv_stop, double dt) {
    const int period_n = (int)ceil(period / dt - 0.5);
    const double dh = dt * 0.5;
    for (int i = 0; i <= stop; i++) { fit0[i] = 0.0; fit1[i] = 0.0; }
    const int stop1 = (period_n > n_points - 1) ? n_points - 1 : period_n;
    const float64x2_t vdh = vdupq_n_f64(dh);
    const float64x2_t vone = vdupq_n_f64(1.0);
    for (int ne = 0; ne < numexp; ne++) {
        const double lifetime = x0[2 * ne + 1];  // shared with x1[2*ne+1]
        const double expcurr = exp(-dt / lifetime);
        const double tail_a = 1.0 / (1.0 - exp(-period / lifetime));
        const float64x2_t ve = vdupq_n_f64(expcurr);
        const float64x2_t vamp = float64x2_t{x0[2 * ne], x1[2 * ne]};
        // fit[0] += dh*lamp[0]*(expcurr + 1)*amp
        float64x2_t vf = float64x2_t{fit0[0], fit1[0]};
        float64x2_t vl = float64x2_t{lamp0[0], lamp1[0]};
        vf = vfmaq_f64(vf, vmulq_f64(vmulq_f64(vdh, vl), vaddq_f64(ve, vone)), vamp);
        fit0[0] = vgetq_lane_f64(vf, 0); fit1[0] = vgetq_lane_f64(vf, 1);
        float64x2_t vfc = vdupq_n_f64(0.0);
        int i;
        for (i = 1; i <= conv_stop; i++) {
            const float64x2_t vlm1 = float64x2_t{lamp0[i - 1], lamp1[i - 1]};
            const float64x2_t vli = float64x2_t{lamp0[i], lamp1[i]};
            vfc = vaddq_f64(vfc, vmulq_f64(vdh, vlm1));   // fitcurr + dh*lamp[i-1]
            vfc = vfmaq_f64(vmulq_f64(vdh, vli), vfc, ve); // *expcurr + dh*lamp[i]
            vf = float64x2_t{fit0[i], fit1[i]};
            vf = vfmaq_f64(vf, vfc, vamp);
            fit0[i] = vgetq_lane_f64(vf, 0); fit1[i] = vgetq_lane_f64(vf, 1);
        }
        for (; i <= stop1; i++) {
            vfc = vmulq_f64(vfc, ve);
            vf = float64x2_t{fit0[i], fit1[i]};
            vf = vfmaq_f64(vf, vfc, vamp);
            fit0[i] = vgetq_lane_f64(vf, 0); fit1[i] = vgetq_lane_f64(vf, 1);
        }
        vfc = vmulq_f64(vfc, vdupq_n_f64(exp(-(period_n - stop1) * dt / lifetime)));
        const float64x2_t vtail = vdupq_n_f64(tail_a);
        // The wrap-around tail. fitcurr now holds the continuation at bin
        // period_n, which *is* bin 0 of the next period — so bin 0 takes it as
        // it stands and the decay step comes after, not before. Stepping first
        // would place the value belonging to bin period_n+1 into bin 0 and shift
        // the whole tail one bin early. (fconv_per() gets this right by ending
        // its main loop one bin earlier, which is why only the _cs variants
        // carried the error.) It is invisible whenever the decay completes
        // within the period, and grows as it does not: 5.8e-5 of the peak at a
        // lifetime of a fifth of the period, and larger for longer lifetimes.
        // With this order the recursion matches an exact circular convolution to
        // 1.3e-15; test_dfa_kernel.py pins that against the spectral backend.
        for (i = 0; i <= stop; i++) {
            vf = float64x2_t{fit0[i], fit1[i]};
            vf = vfmaq_f64(vf, vmulq_f64(vfc, vamp), vtail);
            fit0[i] = vgetq_lane_f64(vf, 0); fit1[i] = vgetq_lane_f64(vf, 1);
            vfc = vmulq_f64(vfc, ve);
        }
    }
}
#endif // TTTRLIB_COMPILE_NEON


void fconv_per_cs_2ch(double *fit0, double *fit1,
                      const double *x0, const double *x1,
                      const double *lamp0, const double *lamp1,
                      int numexp, int stop, int n_points,
                      double period, int conv_stop, double dt) {
#if TTTRLIB_COMPILE_NEON
    if (tttrlib::cpu_features::get_neon_enabled()) {
        fconv_per_cs_2ch_neon(fit0, fit1, x0, x1, lamp0, lamp1,
                              numexp, stop, n_points, period, conv_stop, dt);
        return;
    }
#endif
    // Scalar fallback: two independent single-channel convolutions.
    fconv_per_cs(fit0, const_cast<double *>(x0), const_cast<double *>(lamp0),
                 numexp, stop, n_points, period, conv_stop, dt);
    fconv_per_cs(fit1, const_cast<double *>(x1), const_cast<double *>(lamp1),
                 numexp, stop, n_points, period, conv_stop, dt);
}


/* fast convolution with reference compound decay */
void fconv_ref(double *fit, double *x, double *lamp, int numexp, int start, int stop, double tauref, double dt) {
    double deltathalf = dt * 0.5, sum_a = 0;
    for (int i = 0; i < stop; i++) fit[i] = 0;
    /* convolution */
    for (int ne = 0; ne < numexp; ne++) {
        double expcurr = exp(-dt / x[2 * ne + 1]);
        double correct_a = x[2 * ne] * (1 / tauref - 1 / x[2 * ne + 1]);
        sum_a += x[2 * ne];
        double fitcurr = 0;
        for (int i = 1; i < stop; i++) {
            fitcurr = (fitcurr + deltathalf * lamp[i - 1]) * expcurr + deltathalf * lamp[i];
            fit[i] += fitcurr * correct_a;
        }
    }
    for (int i = 1; i < stop; i++) fit[i] += lamp[i] * sum_a;
}

/* slow convolution */
void sconv(double *fit, double *p, double *lamp, int start, int stop) {
    int i, j;
    /* convolution */
    for (i = start; i < stop; i++) {
        fit[i] = 0.5 * lamp[0] * p[i];
        for (j = 1; j < i; j++) fit[i] += lamp[j] * p[i - j];
        fit[i] += 0.5 * lamp[i] * p[0];
        fit[i] = fit[i];
    }
    fit[0] = 0;
}


/* shifting lamp */
void shift_lamp(double *lampsh, double *lamp, double ts, int n_points, double out_value) {
    int tsint = (int) (floor(ts));
    double tsdbl = ts - (double) tsint;
    int out_left = 0, out_right = 0, j;

    if (tsint < 0) out_left = -tsint;
    if (tsint + 1 > 0) out_right = tsint + 1;

    for (j = 0; j < out_left; j++) lampsh[j] = out_value;
    for (j = out_left; j < (n_points - out_right); j++)
        lampsh[j] = lamp[j + tsint] * (1 - tsdbl) + lamp[j + tsint + 1] * (tsdbl);
    for (j = (n_points - out_right); j < n_points; j++) lampsh[j] = out_value;

}


void add_pile_up_to_model(
        double* model, int n_model,
        double* data, int n_data,
        double repetition_rate,
        double instrument_dead_time,
        double measurement_time,
        std::string pile_up_model,
        int start,
        int stop
){
    stop = stop < 0 ? n_data : std::min(n_data, stop);
    start = start < 0 ? 0 : std::min(n_data, start);
    stop = std::min(n_data, n_model);
if (is_verbose()) {
    std::clog << "ADD PILE-UP" << std::endl;
    std::clog << "-- Repetition_rate [MHz]: " << repetition_rate << std::endl;
    std::clog << "-- Dead_time [ns]: " << instrument_dead_time << std::endl;
    std::clog << "-- Measurement_time [s]: " << measurement_time << std::endl;
    std::clog << "-- n_data: " << n_data << std::endl;
    std::clog << "-- n_model: " << n_model << std::endl;
    std::clog << "-- start: " << start << std::endl;
    std::clog << "-- stop: " << stop << std::endl;
}
    if(strcmp(pile_up_model.c_str(), "coates") == 0){
if (is_verbose()) {
        std::clog << "-- pile_up_model: " << pile_up_model << std::endl;
}
        repetition_rate *= 1e6;
        instrument_dead_time *= 1e-9;
        std::vector<double> cum_sum(n_data);
        std::partial_sum(data, data + n_data, cum_sum.begin(), std::plus<double>());
        long n_pulse_detected = (long) cum_sum[cum_sum.size() - 1];
        double total_dead_time = n_pulse_detected * instrument_dead_time;
        double live_time = measurement_time - total_dead_time;
        double n_excitation_pulses = std::max(live_time * repetition_rate, (double) n_pulse_detected);
if (is_verbose()) {
        std::clog << "-- live_time [s]: " << live_time << std::endl;
        std::clog << "-- total_dead_time [s]: " << total_dead_time << std::endl;
        std::clog << "-- n_pulse_detected [#]: " << n_pulse_detected << std::endl;
        std::clog << "-- n_excitation_pulses [#]: " << n_excitation_pulses << std::endl;
}
        // Coates, 1968, eq. 2 & 4
        std::vector<double> rescaled_data(n_data);

        for(int i = start; i < stop; i++)
            rescaled_data[i] = -std::log(1.0 - data[i] / (n_excitation_pulses - cum_sum[i]));
        for(int i = start; i < stop; i++)
            rescaled_data[i] = (rescaled_data[i] == 0) ? 1.0 : rescaled_data[i];
        // rescale model function to preserve data counting statistics
        std::vector<double> sf(n_data);
        for(int i = start; i < stop; i++)
            sf[i] = data[i] / rescaled_data[i];
        double s = std::accumulate(sf.begin(),sf.end(),0.0);
        for(int i = start; i < stop; i++)
            model[i] = model[i] * (sf[i] / s * n_data);
    }
}


void discriminate_small_amplitudes(
        double* lifetime_spectrum, int n_lifetime_spectrum,
        double amplitude_threshold
){
    int number_of_exponentials = n_lifetime_spectrum / 2;
if (is_verbose()) {
    std::clog << "APPLY_AMPLITUDE_THRESHOLD" << std::endl;
    std::clog << "-- amplitude_threshold spectrum: " << amplitude_threshold << std::endl;
    std::clog << "-- lifetime spectrum before: ";
    for (int i=0; i < number_of_exponentials * 2; i++){
        std::clog << lifetime_spectrum[i] << ' ';
    }
    std::clog << std::endl;
}
    for(int ne = 0; ne<number_of_exponentials; ne++){
        double amplitude = lifetime_spectrum[2 * ne];
        if(std::abs(amplitude) < amplitude_threshold){
            lifetime_spectrum[2 * ne] = 0.0;
        }
    }
if (is_verbose()) {
    std::clog << "-- lifetime spectrum after: ";
    for (int i=0; i < number_of_exponentials * 2; i++){
        std::clog << lifetime_spectrum[i] << ' ';
    }
    std::clog << std::endl;
}
}


/* fast convolution, high repetition rate, with time axis */
void fconv_per_cs_time_axis(
        double* model, int n_model,
        double* time_axis, int n_time_axis,
        double *irf, int n_irf,
        double* lifetime_spectrum, int n_lifetime_spectrum,
        int convolution_start,
        int convolution_stop,
        double period
){
    double dt = time_axis[1] - time_axis[0];
    // fconv_per_simd() dispatches to a SIMD kernel when the CPU supports one and
    // falls back to the scalar fconv_per() otherwise.
    fconv_per_simd(
            model, lifetime_spectrum, irf, (int) n_lifetime_spectrum / 2,
            convolution_start, convolution_stop, n_model, period, dt
    );
}


/* fast convolution, high repetition rate, with time axis */
void fconv_cs_time_axis(
        double* output, int n_output,
        double* time_axis, int n_time_axis,
        double *irf, int n_irf,
        double* lifetime_spectrum, int n_lifetime_spectrum,
        int convolution_start,
        int convolution_stop
){
    double dt = time_axis[1] - time_axis[0];
    // fconv_simd() dispatches to a SIMD kernel when the CPU supports one and
    // falls back to the scalar fconv() otherwise.
    fconv_simd(
            output,
            lifetime_spectrum,
            irf,
            (int) n_lifetime_spectrum / 2,
            convolution_start, convolution_stop, dt
    );
}


void fconv_cs_time_axis_old(
        double* output, int n_output,
        double* time_axis, int n_time_axis,
        double *irf, int n_irf,
        double* lifetime_spectrum, int n_lifetime_spectrum,
        int convolution_start,
        int convolution_stop
){
    int number_of_exponentials = n_lifetime_spectrum / 2;
if (is_verbose()) {
    std::clog << "convolve_lifetime_spectrum... " << std::endl;
    std::clog << "-- number_of_exponentials: " << number_of_exponentials << std::endl;
    std::clog << "-- convolution_start: " << convolution_start << std::endl;
    std::clog << "-- convolution_stop: " << convolution_stop << std::endl;
}
    for(int ne=0; ne<number_of_exponentials; ne++){
        double a = lifetime_spectrum[2 * ne];
        double current_lifetime = (lifetime_spectrum[2 * ne + 1]);
        if((a == 0.0) || (current_lifetime == 0.0)) continue;
        double current_model_value = 0.0;
        for(int i = convolution_start; i < convolution_stop; i++){
            double dt;
            int pre = std::max(0, i - 1);
            if(i < convolution_stop - 1){
                dt = (time_axis[i + 1] - time_axis[i]);
            } else{
                dt = (time_axis[i] - time_axis[i - 1]);
            }
            double dt_2 = dt / 2.0;
            double current_exponential = std::exp(-dt / current_lifetime);
            current_model_value = (current_model_value + dt_2 * irf[pre]) *
                                  current_exponential + dt_2 * irf[i];
            output[i] += current_model_value * a;
        }
    }
}

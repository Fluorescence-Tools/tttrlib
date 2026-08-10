// SPDX-License-Identifier: BSD-3-Clause
#include "Pda.h"
#include "Verbose.h"
#include "info.h"   // AVX/FMA intrinsics + runtime dispatch macros
#ifdef _OPENMP
#include <omp.h>
#endif
#include "pocketfft/pocketfft_hdronly.h"
#include <complex>
#include <cmath>
#include <algorithm>
#include <cstring>
#include "HistogramAxis.h"   // HistogramBinning -- one binning rule, not two

// Runtime AVX/FMA control (CPU detection + TTTRLIB_USE_AVX/FMA env overrides).
static bool g_pda_use_avx = tttrlib::cpu_features::get_avx_enabled();
static bool g_pda_use_fma = tttrlib::cpu_features::get_fma_enabled();
#if TTTRLIB_COMPILE_NEON
static bool g_pda_use_neon = tttrlib::cpu_features::get_neon_enabled();
#endif

#if TTTRLIB_COMPILE_AVX
// AVX+FMA kernel for the S1S2_pF probability-propagation recurrence:
//   cur[col+1] = pre[col]*(1-p) + pre[col+1]*p,  col = 0 .. row-1
// The current and previous matrix rows never overlap, so the row can be
// vectorized. Only entered when the CPU supports AVX and FMA at runtime.
TTTRLIB_TARGET_AVX_FMA
static void pda_propagate_row_avx(double* cur, const double* pre, size_t row, double p) {
    const double one_minus_p = 1.0 - p;
    __m256d v_p  = _mm256_set1_pd(p);
    __m256d v_1p = _mm256_set1_pd(one_minus_p);
    const size_t n_vec = (row / 4) * 4;
    size_t col = 0;
    for (; col < n_vec; col += 4) {
        __m256d v_pre0 = _mm256_loadu_pd(&pre[col]);      // pre[col]
        __m256d v_pre1 = _mm256_loadu_pd(&pre[col + 1]);  // pre[col+1]
        // cur[col+1] = pre[col]*(1-p) + pre[col+1]*p
        __m256d v_res = _mm256_fmadd_pd(v_pre1, v_p, _mm256_mul_pd(v_pre0, v_1p));
        _mm256_storeu_pd(&cur[col + 1], v_res);
    }
    for (; col < row; ++col) {
        cur[col + 1] = pre[col] * one_minus_p + pre[col + 1] * p;
    }
}

// AVX+FMA dot product for the conv_pF background convolution.
TTTRLIB_TARGET_AVX_FMA
static double pda_dot_avx(const double* a, const double* b, size_t n) {
    __m256d acc = _mm256_setzero_pd();
    const size_t n_vec = (n / 4) * 4;
    size_t i = 0;
    for (; i < n_vec; i += 4) {
        acc = _mm256_fmadd_pd(_mm256_loadu_pd(&a[i]), _mm256_loadu_pd(&b[i]), acc);
    }
    __m128d lo = _mm256_castpd256_pd128(acc);
    __m128d hi = _mm256_extractf128_pd(acc, 1);
    lo = _mm_add_pd(lo, hi);
    double s = _mm_cvtsd_f64(_mm_add_sd(lo, _mm_unpackhi_pd(lo, lo)));
    for (; i < n; ++i) s += a[i] * b[i];
    return s;
}
#endif // TTTRLIB_COMPILE_AVX

#if TTTRLIB_COMPILE_NEON
// NEON dot product for the conv_pF background convolution (2 accumulators to
// hide the FMA latency chain).
static double pda_dot_neon(const double* a, const double* b, size_t n) {
    float64x2_t acc0 = vdupq_n_f64(0.0), acc1 = vdupq_n_f64(0.0);
    const size_t n_vec = (n / 4) * 4;
    size_t i = 0;
    for (; i < n_vec; i += 4) {
        acc0 = vfmaq_f64(acc0, vld1q_f64(&a[i]), vld1q_f64(&b[i]));
        acc1 = vfmaq_f64(acc1, vld1q_f64(&a[i + 2]), vld1q_f64(&b[i + 2]));
    }
    double s = vaddvq_f64(vaddq_f64(acc0, acc1));
    for (; i < n; ++i) s += a[i] * b[i];
    return s;
}
#endif // TTTRLIB_COMPILE_NEON

// Runtime-dispatched dot product (AVX+FMA on x86, NEON on AArch64, scalar
// fallback everywhere).
//
// conv_pF runs this per output cell rather than sweeping kernel-tap-outermost
// over whole rows. Tap-outer was measured and is ~1.25x SLOWER: it stores every
// output element once per tap, where the dot keeps the accumulator in a
// register and stores once. Do not "optimize" it back.
static inline double pda_dot(const double* a, const double* b, size_t n) {
#if TTTRLIB_COMPILE_AVX
    if (g_pda_use_avx && g_pda_use_fma) return pda_dot_avx(a, b, n);
#elif TTTRLIB_COMPILE_NEON
    if (g_pda_use_neon) return pda_dot_neon(a, b, n);
#endif
    double s = 0.0;
    for (size_t i = 0; i < n; ++i) s += a[i] * b[i];
    return s;
}

// Runtime-dispatched row propagation shared by S1S2_pF and S1S2_pF_optimized.
// No NEON kernel: benchmarked slower than the autovectorized scalar loop.
static inline void pda_propagate_row(double* cur, const double* pre, size_t row, double p) {
#if TTTRLIB_COMPILE_AVX
    if (g_pda_use_avx && g_pda_use_fma) {
        pda_propagate_row_avx(cur, pre, row, p);
        return;
    }
#endif
    const double one_minus_p = 1.0 - p;
    for (size_t col = 0; col < row; ++col) {
        cur[col + 1] = pre[col] * one_minus_p + pre[col + 1] * p;
    }
}

// One species' contribution to FgFr: binomial split of pF, scaled by a.
// tmp is (Nmax+1)^2 scratch, reused across calls -- see the zeroing note below.
// Layout everywhere here: M[ch1 * (Nmax+1) + ch2], row is channel 1.
static void pda_accumulate_species(
        std::vector<double>& FgFr,
        std::vector<double>& tmp,
        double p, double a,
        const std::vector<double>& pF,
        unsigned int Nmax
) {
    if (a == 0.0) return;   // contributes nothing
    // Row r holds entries 0..r, but propagating row r+1 reads pre[r+1] -- one
    // past the diagonal, which must read as zero. Zero that single slot per
    // row instead of re-zeroing the whole (Nmax+1)^2 buffer.
    tmp[0] = 1.;
    if (Nmax >= 1) tmp[1] = 0.;
    for (size_t row = 1; row <= Nmax; row++) {
        // marks beginning of current and previous matrix row
        size_t row_offset_cur = (row + 0) * (Nmax + 1);
        size_t row_offset_pre = (row - 1) * (Nmax + 1);
        tmp[row_offset_cur + 0] = tmp[row_offset_pre + 0] * p;
        pda_propagate_row(&tmp[row_offset_cur], &tmp[row_offset_pre], row, p);
        if (row < Nmax) tmp[row_offset_cur + row + 1] = 0.;
    }
    // Scatter each binomial row onto the anti-diagonal ch1 + ch2 == row.
    // Both bounds inclusive -- red < Nmax drops the all-red corner.
    for (size_t row = 0; row <= Nmax; row++) {
        const double w = a * pF[row];
        if (w == 0.0) continue;   // empty photon-count bin
        const double* src = &tmp[row * (Nmax + 1)];
        for (size_t red = 0; red <= row; red++)
            FgFr[(row - red) * (Nmax + 1) + red] += src[red] * w;
    }
}

static int good_fft_size(int Nmax);

// FFT-based row convolution for conv_pF, used when the Poisson kernel support
// is too wide for the truncated direct method (large backgrounds).
// For every row r of `in` computes conv(in[r,:], kernel) and writes result k
// to out[(Nmax+1)*k + r] (transposed, matching conv_pF's access pattern),
// restricted to the triangle k + r <= Nmax.
static void pda_conv_fft(
        std::vector<double>& out,
        const std::vector<double>& in,
        const std::vector<double>& kernel,
        unsigned int Nmax
) {
    const size_t n = Nmax + 1;
    const size_t M = (size_t) good_fft_size((int) Nmax); // power of 2 >= 2(Nmax+1), no circular wrap
    pocketfft::shape_t shape{M};
    pocketfft::stride_t stride{sizeof(std::complex<double>)};
    pocketfft::shape_t axes{0};
    std::vector<std::complex<double>> K(M, std::complex<double>(0.0, 0.0));
    for (size_t i = 0; i < n; i++) K[i] = kernel[i];
    pocketfft::c2c(shape, stride, stride, axes, true, K.data(), K.data(), 1.0);
    std::vector<std::complex<double>> buf(M);
    for (size_t row = 0; row < n; row++) {
        std::fill(buf.begin(), buf.end(), std::complex<double>(0.0, 0.0));
        const double* src = &in[n * row];
        // Only i + row <= Nmax is populated; the rest of the row is scratch.
        // An FFT smears a stray NaN there across every output bin.
        for (size_t i = 0; i + row <= Nmax; i++) buf[i] = src[i];
        pocketfft::c2c(shape, stride, stride, axes, true, buf.data(), buf.data(), 1.0);
        for (size_t i = 0; i < M; i++) buf[i] *= K[i];
        pocketfft::c2c(shape, stride, stride, axes, false, buf.data(), buf.data(), 1.0 / M);
        for (size_t k = 0; k + row <= Nmax; k++) {
            // clamp spectral round-off: probabilities must stay non-negative
            // (downstream MLE takes log of these values)
            const double v = buf[k].real();
            out[n * k + row] = v < 0.0 ? 0.0 : v;
        }
    }
}

void Pda::get_1dhistogram(
        double **histogram_x, int *n_histogram_x,
        double **histogram_y, int *n_histogram_y,
        double x_max, double x_min, int n_bins, bool log_x,
        std::vector<double> s1s2,
        int n_min, bool skip_zero_photon,
        std::vector<double> species_amplitudes,
        std::vector<double> probabilities_ch1
) {
if (is_verbose()) {
    std::clog << "-- RUN: get_1dhistogram..." << std::endl;
    std::clog << "-- x_min: " << x_min << std::endl;
    std::clog << "-- x_max: " << x_max << std::endl;
    std::clog << "-- n_bins: " << n_bins << std::endl;
    std::clog << "-- log_x: " << log_x << std::endl;
}
    if(
        !species_amplitudes.empty() && !probabilities_ch1.empty()
    ){
        if(species_amplitudes.size() == probabilities_ch1.size()){
            set_amplitudes(species_amplitudes.data(), static_cast<int>(species_amplitudes.size()));
            set_probabilities_ch1(probabilities_ch1.data(), static_cast<int>(probabilities_ch1.size()));
            evaluate();
        } else{
            std::cerr << "WARNING: species_amplitudes and probabilities_ch1"
                         "differ in size - did not update S1S2!";
        }
    }
    // Bind, don't copy -- 723 kB at Nmax=300, once per fit iteration.
    if(s1s2.empty() && !_is_valid_sgsr) evaluate();
    const std::vector<double>& src = s1s2.empty() ? _S1S2 : s1s2;
    if (is_verbose()) {
        std::clog << (s1s2.empty() ? "-- Using model s1s2 matrix! "
                                   : "-- Using input s1s2 matrix! ") << std::endl;
    }
    int n_max = (int) std::sqrt((double) src.size()) - 1;

    (*n_histogram_x) = n_bins;
    (*n_histogram_y) = n_bins;
    *histogram_x = (double*) calloc(sizeof(double), n_bins);
    *histogram_y = (double*) calloc(sizeof(double), n_bins);
    n_min = n_min < 0 ? (int) _n_2d_min : n_min;
    int first_photon = skip_zero_photon;

    // build histogram
    //
    // The binning is hist's, not a second copy of it. PDA's bins are CENTRED on
    // the x values it reports back -- shifting them by half a bin would move
    // every point in a published PDA plot -- which is why HistogramBinning has
    // that convention rather than PDA having its own log/linear arithmetic.
    const HistogramBinning<double> axis =
            HistogramBinning<double>::centered(x_min, x_max, n_bins, log_x);
    const double bin_width = log_x ?
                       (log(x_max) - log(x_min)) / ((double) n_bins - 1) :
                       (x_max - x_min) / ((double) n_bins - 1.);
if (is_verbose()) {
    std::clog << "-- n_max: " << n_max << std::endl;
    std::clog << "-- n_min: " << n_min << std::endl;
    std::clog << "-- bin_width: " << bin_width << std::endl;
}

    // histogram X
    for (int bin = 0; bin < n_bins; bin++)
        (*histogram_x)[bin] = log_x ?
                              exp(log(x_min) + bin_width * (double) bin) :
                              x_min + bin_width * (double) bin;
    build_hist1d_cache(axis, n_max, n_min, first_photon, x_max, x_min,
                       n_bins, log_x, skip_zero_photon);
    project_s1s2(src, n_max, n_min, first_photon, n_bins, *histogram_y);
}


void Pda::build_hist1d_cache(
        const HistogramBinning<double>& axis,
        int n_max, int n_min, int first_photon,
        double x_max, double x_min, int n_bins, bool log_x,
        bool skip_zero_photon
) {
    // The callback value of a cell (ch1, ch2) is independent of the model
    // amplitudes/probabilities, so the target bin of every visited cell is
    // cached across calls (fit iterations). set_callback and any change of
    // the binning parameters invalidate the cache.
    if (_hist1d_valid
            && _hist1d_xmax == x_max && _hist1d_xmin == x_min
            && _hist1d_nbins == n_bins && _hist1d_logx == log_x
            && _hist1d_nmax == n_max && _hist1d_nmin == n_min
            && _hist1d_skip == skip_zero_photon) return;
    // Visit cells with n_min <= ch1 + ch2 <= n_max, both channels non-zero if
    // skipping. max() so ch1 == n_min obeys skip_zero_photon too.
    //
    // COMPACT: one entry per visited cell in row-major visit order, not one
    // per matrix cell. Off-axis cells get bin n_bins, a trash slot, so the
    // projection has no branch and both its streams are contiguous.
    _hist1d_bins.clear();
    _hist1d_bins.reserve((size_t)(n_max + 1) * (n_max + 2) / 2);
    for (int ch1 = first_photon; ch1 <= n_max; ch1++) {
        for (int ch2 = std::max(first_photon, n_min - ch1); ch2 <= n_max - ch1; ch2++) {
            const int bin_idx = axis.bin_of(_histogram_function->run(ch1, ch2));
            _hist1d_bins.push_back(bin_idx >= 0 ? bin_idx : n_bins);
        }
    }
    _hist1d_xmax = x_max; _hist1d_xmin = x_min;
    _hist1d_nbins = n_bins; _hist1d_logx = log_x;
    _hist1d_nmax = n_max; _hist1d_nmin = n_min;
    _hist1d_skip = skip_zero_photon;
    _hist1d_valid = true;
}


void Pda::project_s1s2(
        const std::vector<double>& src,
        int n_max, int n_min, int first_photon, int n_bins,
        double* out
) const {
    // Row is ch1, as the model matrix is built. Reading this transposed
    // mirrors the projected axis (E <-> 1 - E).
    std::vector<double> acc(n_bins + 1, 0.0);   // last slot is the trash bin
    const int* bins = _hist1d_bins.data();
    for (int ch1 = first_photon; ch1 <= n_max; ch1++) {
        const int first_ch2 = std::max(first_photon, n_min - ch1);
        const int len = n_max - ch1 - first_ch2 + 1;
        if (len <= 0) continue;
        const double* s = &src[(size_t) ch1 * (n_max + 1) + first_ch2];
        for (int k = 0; k < len; k++) acc[bins[k]] += s[k];
        bins += len;
    }
    std::memcpy(out, acc.data(), (size_t) n_bins * sizeof(double));
}


void Pda::get_1dhistogram_per_species(
        double **histogram_x, int *n_histogram_x,
        double **output, int *n_output1, int *n_output2,
        double x_max, double x_min, int n_bins, bool log_x,
        int n_min, bool skip_zero_photon
) {
    const int n_species = (int) _probability_ch1.size();
    const unsigned int Nmax = get_max_number_of_photons();
    const int n_max = (int) Nmax;
    if(pF.size() < Nmax + 1) pF.resize(Nmax + 1, 0.0);
    n_min = n_min < 0 ? (int) _n_2d_min : n_min;
    const int first_photon = skip_zero_photon;

    const HistogramBinning<double> axis =
            HistogramBinning<double>::centered(x_min, x_max, n_bins, log_x);
    const double bin_width = log_x ?
                       (log(x_max) - log(x_min)) / ((double) n_bins - 1) :
                       (x_max - x_min) / ((double) n_bins - 1.);
    *n_histogram_x = n_bins;
    *histogram_x = (double*) calloc(sizeof(double), n_bins);
    for (int bin = 0; bin < n_bins; bin++)
        (*histogram_x)[bin] = log_x ?
                              exp(log(x_min) + bin_width * (double) bin) :
                              x_min + bin_width * (double) bin;

    build_hist1d_cache(axis, n_max, n_min, first_photon, x_max, x_min,
                       n_bins, log_x, skip_zero_photon);

    *n_output1 = n_species;
    *n_output2 = n_bins;
    auto* rows = (double*) calloc((size_t) std::max(1, n_species) * n_bins, sizeof(double));
    std::vector<double> one_s1s2((size_t)(Nmax + 1) * (Nmax + 1));
    std::vector<double> one_amp(1, 1.0), one_p(1, 0.0);
    for (int i = 0; i < n_species; i++) {
        one_p[0] = _probability_ch1[i];
        std::fill(one_s1s2.begin(), one_s1s2.end(), 0.0);
        S1S2_pF(one_s1s2, pF, Nmax, _bg_ch1, _bg_ch2, one_p, one_amp);
        project_s1s2(one_s1s2, n_max, n_min, first_photon, n_bins,
                     rows + (size_t) i * n_bins);
    }
    *output = rows;
}


void Pda::evaluate() {
if (is_verbose()) {
    std::clog << "-- evaluate PDA..." << std::endl;
    std::clog << "-- making sure array sizes match" << std::endl;
}
    std::fill(_S1S2.begin(), _S1S2.end(), 0.0);
    auto Nmax = get_max_number_of_photons();
    // Short inputs are zero-padded, not rejected. Warn on cerr, not cout.
    if(pF.size() < Nmax + 1){
        std::cerr << "WARNING: Pda pF array shorter than hist2d_nmax + 1. "
                     "Appending zeros." << std::endl;
        pF.resize(Nmax + 1, 0.0);
    }
    if(_probability_ch1.size() < _amplitudes.size()){
        std::cerr << "WARNING: Pda probability array shorter than the amplitude "
                     "array. Appending zeros." << std::endl;
        _probability_ch1.resize(_amplitudes.size(), 0.0);
    }
    if(_amplitudes.size() < _probability_ch1.size()){
        std::cerr << "WARNING: Pda amplitude array shorter than the probability "
                     "array. Appending zeros." << std::endl;
        _amplitudes.resize(_probability_ch1.size(), 0.0);
    }
if (is_verbose()) {
    std::clog << "-- Computing S1S2 matrix" << std::endl;
}
    double bg_ch1 = get_ch1_background();
    double bg_ch2 = get_ch2_background();
    
    // Dispatch to the appropriate implementation
    if (_implementation == PdaImplementation::PDA_OPTIMIZED) {
        S1S2_pF_optimized(_S1S2, pF, Nmax, bg_ch1, bg_ch2,
                          _probability_ch1,
                          _amplitudes
        );
    } else {
        S1S2_pF(_S1S2, pF, Nmax, bg_ch1, bg_ch2,
                          _probability_ch1,
                          _amplitudes
        );
    }
    _is_valid_sgsr = true;
}



//
void Pda::conv_pF(
        std::vector<double> &S1S2,
        const std::vector<double> &F1F2,
        unsigned int Nmax,
        double background_ch1,
        double background_ch2
) {
    const size_t n = Nmax + 1;
    // Both passes only ever touch the triangle green + red <= Nmax, and each
    // cell is written by exactly one omp iteration before pass 2 reads it, so
    // one SHARED buffer is race-free. thread_local crashed here: inside the
    // parallel regions each worker rebinds to its own, never-sized TLS copy,
    // and pass 2 then reads a buffer the workers never wrote.
    std::vector<double> tmp(n * n);
    // n, not Nmax: poisson_0toN writes return_dim entries, and 0..Nmax is
    // Nmax+1 of them. Passing Nmax zeroes the last tap of the kernel.
    std::vector<double> bg(n, 0.0);
    poisson_0toN(bg, 0, background_ch1, (int) n);
    std::vector<double> br(n, 0.0);
    poisson_0toN(br, 0, background_ch2, (int) n);

    // Effective support of the Poisson kernels: terms beyond T are < 1e-15
    // and numerically irrelevant, so the convolutions are truncated there
    // (O(Nmax^2 T) instead of O(Nmax^3)). The tail is monotone past the mode.
    // Safe here because the operand is a probability <= 1; it would NOT be for
    // a likelihood ratio, where the discarded terms can dominate.
    size_t Tr = Nmax; while (Tr > 0 && br[Tr] < 1e-15) Tr--;
    size_t Tg = Nmax; while (Tg > 0 && bg[Tg] < 1e-15) Tg--;

    // For very wide kernels (large backgrounds) the FFT convolution,
    // O(Nmax^2 log Nmax), beats the truncated direct method.
    const size_t fft_crossover = std::max<size_t>(64, Nmax / 5);

#ifdef _OPENMP
    bool use_omp = tttrlib::cpu_features::get_openmp_enabled() && Nmax >= 64;
    int num_threads = tttrlib::cpu_features::get_openmp_num_threads();
#endif

    // Reversed kernels so both dot-product operands ascend contiguously:
    // br[red - i] == br_rev[(Nmax - red) + i]
    std::vector<double> br_rev(n), bg_rev(n);
    for (size_t i = 0; i < n; i++) {
        br_rev[i] = br[Nmax - i];
        bg_rev[i] = bg[Nmax - i];
    }

    // pass 1: convolve each F1F2 row (fixed green) with the ch2 background,
    // writing transposed into tmp
    if (Tr > fft_crossover) {
        pda_conv_fft(tmp, F1F2, br, Nmax);
    } else {
#ifdef _OPENMP
        #pragma omp parallel for schedule(static, 1) num_threads(num_threads) if(use_omp)
#endif
        for (long long red_ll = 0; red_ll <= (long long) Nmax; red_ll++) {
            const size_t red = (size_t) red_ll;
            const size_t i_start = red > Tr ? red - Tr : 0;
            const size_t len = red - i_start + 1;
            const double* krn = &br_rev[Nmax - red + i_start];
            for (size_t green = 0; green <= Nmax - red; green++) {
                tmp[n * red + green] = pda_dot(&F1F2[n * green + i_start], krn, len);
            }
        }
    }
    // pass 2: convolve each tmp row (fixed red) with the ch1 background
    if (Tg > fft_crossover) {
        pda_conv_fft(S1S2, tmp, bg, Nmax);
    } else {
#ifdef _OPENMP
        #pragma omp parallel for schedule(static, 1) num_threads(num_threads) if(use_omp)
#endif
        for (long long green_ll = 0; green_ll <= (long long) Nmax; green_ll++) {
            const size_t green = (size_t) green_ll;
            const size_t i_start = green > Tg ? green - Tg : 0;
            const size_t len = green - i_start + 1;
            const double* krn = &bg_rev[Nmax - green + i_start];
            for (size_t red = 0; red <= Nmax - green; red++) {
                S1S2[n * green + red] = pda_dot(&tmp[n * red + i_start], krn, len);
            }
        }
    }
}


void Pda::S1S2_pF(
        std::vector<double> &S1S2,        // see sgsr_pN
        std::vector<double> &pF,          // input: p(F)
        unsigned int Nmax,
        double background_ch1,
        double background_ch2,
        std::vector<double> &p_ch1,
        std::vector<double> &amplitudes // corresponding amplitudes
){
    /*** F1F2: matrix, F1F2(i,j) = p(F1 = i, F2 = j) ***/
    size_t matrix_elements = (size_t)(Nmax + 1) * (Nmax + 1);
    // Scratch kept between calls -- 1.4 MB of malloc traffic per call at
    // Nmax=300 otherwise. Only FgFr needs re-zeroing.
    static thread_local std::vector<double> FgFr, tmp;
    FgFr.assign(matrix_elements, 0.0);
    if (tmp.size() < matrix_elements) tmp.resize(matrix_elements);
    for(size_t pg_idx = 0; pg_idx < p_ch1.size(); pg_idx++) {
        auto p = p_ch1[pg_idx];
        auto a = amplitudes[pg_idx];
if (is_verbose()) {
        std::clog << "-- Computing S1S2 for species (amplitude, p(ch1)): " << a << ", " << p << std::endl;
}
        pda_accumulate_species(FgFr, tmp, p, a, pF, Nmax);
    }
    /*** S1S2: matrix, S1S2(i,j) = p(S1 = i, S2 = j) ***/
    conv_pF(S1S2, FgFr, Nmax, background_ch1, background_ch2);
}


/// \brief Find optimal FFT size (power of 2) for efficiency.
///
/// Calculates the smallest power-of-2 that is at least 2*(Nmax+1),
/// which is optimal for FFT computation.
///
/// \param Nmax Maximum number of photons
/// \return Optimal FFT size (power of 2)
static int good_fft_size(int Nmax) {
    int size = 2 * (Nmax + 1);
    int fft_size = 1;
    while (fft_size < size) fft_size *= 2;
    return fft_size;
}

/// \brief Determine Poisson threshold where probability drops below 1e-15.
///
/// Uses polynomial approximation for lambda < 100 and linear approximation
/// for lambda >= 100 to determine where Poisson probability becomes negligible.
/// This is used to optimize FFT size and skip negligible tail values.
///
/// Polynomial coefficients (lambda < 100):
///   threshold = -1.4040e-6*λ⁴ + 3.5397e-4*λ³ - 0.0337*λ² + 2.9359*λ + 22.7281
///
/// Linear approximation (lambda >= 100):
///   threshold = 1.32*λ + 63
///
/// \param lambda Poisson distribution parameter (mean number of photons)
/// \return Threshold index where Poisson probability < 1e-15
/// \note Reduces FFT size by 30-50% for typical backgrounds
static unsigned int poisson_threshold15(double lambda) {
    if (lambda < 100.) {
        return (unsigned int)std::ceil(
            -1.4040e-6*lambda*lambda*lambda*lambda + 
            3.5397e-4*lambda*lambda*lambda - 
            0.0337*lambda*lambda + 
            2.9359*lambda + 22.7281
        );
    } else {
        return (unsigned int)std::ceil(1.32*lambda + 63.);
    }
}

/// \brief Generate multiple Poisson distributions simultaneously.
///
/// Efficiently computes M different Poisson distributions with different
/// lambda parameters. Uses iterative computation: p[i] = p[i-1] * lambda / i
/// which is more numerically stable than computing factorials.
///
/// \param return_p[out] Output array of size (N+1)*M containing M Poisson distributions
/// \param lambda[in] Vector of M lambda parameters (mean photon counts)
/// \param N Maximum number of photons
/// \note Useful for multi-channel background correction
static void poisson_0toN_multi(std::vector<double>& return_p, 
                               const std::vector<double>& lambda, 
                               unsigned int N) {
    for (size_t j = 0; j < lambda.size(); j++) {
        size_t offset = (N + 1) * j;
        return_p[offset] = std::exp(-lambda[j]);
        for (unsigned int i = 1; i <= N; i++) {
            return_p[offset + i] = return_p[offset + i - 1] * lambda[j] / i;
        }
    }
}

/// \brief 1D FFT-based deconvolution to compute p^(1/order).
///
/// Uses FFT to efficiently compute the fractional power of a probability
/// distribution. This is used for multi-molecule event correction by
/// computing the single-molecule distribution from the observed multi-molecule
/// distribution.
///
/// Algorithm:
///   1. Forward FFT of input distribution
///   2. Point-by-point power(1/order) in Fourier space
///   3. Inverse FFT to get result
///
/// \param pf1[out] Output probability distribution p^(1/order)
/// \param pf_exp[in] Input probability distribution
/// \param Nmax Maximum number of photons
/// \param order Convolution order (typically 2-50 for multi-molecule correction)
/// \note Uses pocketfft for complex FFT operations
static void fftdc1D(std::vector<double>& pf1, const std::vector<double>& pf_exp, 
                    int Nmax, int order) {
    int fft_size = good_fft_size(Nmax);
    std::vector<std::complex<double>> dft_a(fft_size, std::complex<double>(0.0, 0.0));
    
    // Copy input to complex array
    for (int i = 0; i <= Nmax; i++) {
        dft_a[i] = std::complex<double>(pf_exp[i], 0.0);
    }
    
    // Forward FFT
    pocketfft::shape_t shape{(size_t)fft_size};
    pocketfft::stride_t stride{sizeof(std::complex<double>)};
    pocketfft::shape_t axes{0};
    pocketfft::c2c(shape, stride, stride, axes, false, dft_a.data(), dft_a.data(), 1.0);
    
    // Point-by-point power(1/order)
    double pw = 1.0 / order;
    for (int i = 0; i < fft_size; i++) {
        double r = std::pow(std::abs(dft_a[i]), pw);
        double phi = pw * std::arg(dft_a[i]);
        dft_a[i] = std::polar(r, phi);
    }
    
    // Inverse FFT
    pocketfft::c2c(shape, stride, stride, axes, true, dft_a.data(), dft_a.data(), 1.0 / fft_size);
    
    // Copy back to output
    pf1.resize(Nmax + 1);
    for (int i = 0; i <= Nmax; i++) {
        pf1[i] = dft_a[i].real();
    }
}

/// \brief 2D FFT-based convolution to compute A^order.
///
/// Uses 2D FFT to efficiently compute the power of a 2D probability matrix.
/// This is used for multi-molecule event correction by computing the
/// multi-molecule distribution from the single-molecule distribution.
///
/// Algorithm:
///   1. Forward 2D FFT of input matrix
///   2. Point-by-point power(order) in Fourier space
///   3. Inverse 2D FFT to get result
///
/// \param AxA[out] Output 2D probability matrix A^order
/// \param A[in] Input 2D probability matrix (size (Nmax+1)×(Nmax+1))
/// \param Nmax Maximum number of photons
/// \param order Convolution order (typically 2-50 for multi-molecule correction)
/// \note Uses pocketfft for 2D complex FFT operations
/// \note Result is normalized to maintain probability distribution properties
static void fftc2D_AA(std::vector<double>& AxA, const std::vector<double>& A, 
                      int Nmax, int order) {
    int fft_size = good_fft_size(Nmax);
    std::vector<std::complex<double>> dft_A(fft_size * fft_size, std::complex<double>(0.0, 0.0));
    
    // Copy A to complex array
    for (int i = 0; i <= Nmax; i++) {
        for (int j = 0; j <= Nmax; j++) {
            dft_A[fft_size * i + j] = std::complex<double>(A[(Nmax + 1) * i + j], 0.0);
        }
    }
    
    // Forward 2D FFT
    pocketfft::shape_t shape{(size_t)fft_size, (size_t)fft_size};
    pocketfft::stride_t stride{(ptrdiff_t)(fft_size * sizeof(std::complex<double>)), 
                               (ptrdiff_t)sizeof(std::complex<double>)};
    pocketfft::shape_t axes{0, 1};
    pocketfft::c2c(shape, stride, stride, axes, false, dft_A.data(), dft_A.data(), 1.0);
    
    // Point-by-point power(order)
    for (int i = 0; i < fft_size * fft_size; i++) {
        dft_A[i] = std::pow(dft_A[i], (double)order);
    }
    
    // Inverse 2D FFT
    pocketfft::c2c(shape, stride, stride, axes, true, dft_A.data(), dft_A.data(), 1.0 / (fft_size * fft_size));
    
    // Copy back to output
    AxA.resize((Nmax + 1) * (Nmax + 1));
    for (int i = 0; i <= Nmax; i++) {
        for (int j = 0; j <= Nmax; j++) {
            AxA[(Nmax + 1) * i + j] = dft_A[fft_size * i + j].real();
        }
    }
}

/// \brief Determine convolution order for multi-molecule correction.
///
/// Calculates how many times to convolve the distribution with itself
/// to achieve the desired probability threshold. Based on the formula:
///   order = ceil(log(p0) / log(p0_wanted))
///
/// \param p0 Current probability of zero photons (pF[0])
/// \param p0_wanted Target probability threshold (typically 1e-15)
/// \return Convolution order (1 = no correction, >1 = correction needed)
/// \note order=1 means no correction is needed
/// \note The ratio exceeds 1 only when p0 < p0_wanted, so with the usual
///       p0_wanted = 1e-15 a correction is almost never requested.
/// \note p0 outside (0, 1) is rejected: log(0) is -inf and casting the
///       resulting infinity to int is undefined behaviour.
static int pF_conv_order(double p0, double p0_wanted) {
    if (!(p0 > 0.0) || !(p0 < 1.0)) return 1;   // also catches NaN
    const double order = std::ceil(std::log(p0) / std::log(p0_wanted));
    if (!std::isfinite(order) || order < 1.0) return 1;
    return (int) std::min(order, 1024.0);       // cap the FFT power
}

/// \brief Optimized S1S2 computation with OpenMP parallelization and FFT correction.
///
/// This is the optimized implementation of S1S2_pF that includes:
///   - OpenMP parallelization over species
///   - Thread-local accumulators for efficiency
///   - Automatic FFT-based multi-molecule correction
///   - Poisson threshold optimization
///
/// The computation proceeds in two stages:
///   1. Parallel computation of FgFr matrix (green/red photon distribution)
///   2. Optional FFT-based multi-molecule correction if needed
///   3. Convolution with background to get final S1S2 matrix
///
/// \param S1S2[out] Output S1S2 matrix (size (Nmax+1)×(Nmax+1))
/// \param pF[in] Probability distribution of total photon counts
/// \param Nmax Maximum number of photons
/// \param background_ch1 Background level in channel 1 (green)
/// \param background_ch2 Background level in channel 2 (red)
/// \param p_ch1[in] Vector of green detection probabilities for each species
/// \param amplitudes[in] Vector of amplitudes (fractions) for each species
///
/// \note The multi-molecule correction is NOT an optimization: it changes the
///       model. It engages only when pF[0] < 1e-15 -- i.e. when the chance of
///       observing zero fluorescence photons is negligible -- and for every
///       ordinary pF this path is dead and the result is bit-comparable to
///       S1S2_pF. Do not rely on it silently; it is kept for compatibility.
/// \note Thread-safe with OpenMP critical sections
/// \note Falls back to single-threaded when OpenMP unavailable
void Pda::S1S2_pF_optimized(
        std::vector<double> &S1S2,        // see sgsr_pN
        std::vector<double> &pF,          // input: p(F)
        unsigned int Nmax,
        double background_ch1,
        double background_ch2,
        std::vector<double> &p_ch1,
        std::vector<double> &amplitudes // corresponding amplitudes
){
    /*** F1F2: matrix, F1F2(i,j) = p(F1 = i, F2 = j) ***/
    size_t matrix_elements = (size_t)(Nmax + 1) * (Nmax + 1);
    std::vector<double> FgFr(matrix_elements, 0.0);

#ifdef _OPENMP
    // One species means nothing to distribute, and fork/join plus the
    // (Nmax+1)^2 reduction costs more than it saves.
    const bool use_omp = tttrlib::cpu_features::get_openmp_enabled() &&
                         p_ch1.size() > 1;
    #pragma omp parallel if(use_omp) num_threads(tttrlib::cpu_features::get_openmp_num_threads())
    {
        // Allocated once per worker thread, not once per call.
        static thread_local std::vector<double> tmp, FgFr_local;
        if (tmp.size() < matrix_elements) tmp.resize(matrix_elements);
        FgFr_local.assign(matrix_elements, 0.0);

        #pragma omp for
        for(int pg_idx = 0; pg_idx < (int)p_ch1.size(); pg_idx++) {
            auto p = p_ch1[pg_idx];
            auto a = amplitudes[pg_idx];
            if (is_verbose()) {
                std::clog << "-- Computing S1S2 for species (amplitude, p(ch1)): " << a << ", " << p << std::endl;
            }
            pda_accumulate_species(FgFr_local, tmp, p, a, pF, Nmax);
        }

        #pragma omp critical
        {
            for(size_t i = 0; i < matrix_elements; i++){
                FgFr[i] += FgFr_local[i];
            }
        }
    }
#else
    {
        static thread_local std::vector<double> tmp;
        if (tmp.size() < matrix_elements) tmp.resize(matrix_elements);
        for(int pg_idx = 0; pg_idx < (int)p_ch1.size(); pg_idx++) {
            auto p = p_ch1[pg_idx];
            auto a = amplitudes[pg_idx];
            if (is_verbose()) {
                std::clog << "-- Computing S1S2 for species (amplitude, p(ch1)): " << a << ", " << p << std::endl;
            }
            pda_accumulate_species(FgFr, tmp, p, a, pF, Nmax);
        }
    }
#endif

    /*** Multi-molecule correction using FFT ***/
    // Determine convolution order based on pF[0] (probability of detecting 0 photons)
    // We want to correct for multi-molecule events
    double p0 = pF[0];
    double p0_wanted = 1e-15;  // Target threshold
    int conv_order = pF_conv_order(p0, p0_wanted);
    
    if (conv_order > 1) {
        if (is_verbose()) {
            std::clog << "-- Applying multi-molecule correction with order: " << conv_order << std::endl;
        }
        
        // Apply FFT-based 2D convolution for multi-molecule correction
        std::vector<double> FgFr_corrected;
        fftc2D_AA(FgFr_corrected, FgFr, Nmax, conv_order);
        
        // Normalize the corrected result
        double s = (1.0 - pF[0]) / (1.0 - FgFr_corrected[0]);
        FgFr_corrected[0] = pF[0];
        for (size_t j = 1; j < matrix_elements; j++) {
            FgFr_corrected[j] *= s;
        }
        
        FgFr = FgFr_corrected;
    }

    /*** S1S2: matrix, S1S2(i,j) = p(S1 = i, S2 = j) ***/
    conv_pF(S1S2, FgFr, Nmax, background_ch1, background_ch2);
}


void Pda::poisson_0toN(
        std::vector<double> &return_p,
        int start_idx,
        double lam,
        int return_dim
) {
    int i;
    return_p[start_idx] = exp(-lam);
    for (i = start_idx + 1; i < start_idx + return_dim; i++) {
        return_p[i] = return_p[i - 1] * lam / (double) i;
    }
}


void Pda::compute_experimental_histograms(
        TTTR* tttr_data,
        double** s1s2, int* dim1, int* dim2,
        double** ps, int* dim_ps,
        int** tttr_indices, int* n_tttr_indices,
        std::vector<int> channels_1,
        std::vector<int> channels_2,
        int maximum_number_of_photons,
        int minimum_number_of_photons,
        double minimum_time_window_length
){
if (is_verbose()) {
    std::clog << "-- Make S1S2 matrix... " << std::endl;
    std::clog << "-- minimum_time_window_length: " << minimum_time_window_length << std::endl;
    std::clog << "-- minimum_number_of_photons_in_time_window: " << minimum_number_of_photons << std::endl;
}
    const int n_dim = maximum_number_of_photons + 1;
    // calloc zeroes -- the explicit re-zeroing loops here were dead work.
    auto tmp_s1s2 = (double*) calloc((size_t) n_dim * n_dim, sizeof(double));
    auto tmp_ps = (double*) calloc((size_t) n_dim, sizeof(double));
    std::vector<int> tmp_tttr_indices;
    int *tws = nullptr; int n_tw = 0;
    tttr_data->get_time_window_ranges(
            &tws, &n_tw,
            minimum_time_window_length,
            minimum_number_of_photons
    );
    // tws is interleaved [start_0, stop_0, start_1, ...], so window w is the
    // pair at 2w/2w+1. tws[w], tws[w+1] reads a stop as the next start.
    const size_t n_windows = (size_t) (n_tw / 2);
if (is_verbose()) {
    std::clog << "-- Number of time windows: " << n_windows << std::endl;
    std::clog << "-- Counting photons... " << std::endl;
}
    for(size_t w = 0; w < n_windows; w++){
        const size_t start = (size_t) tws[2 * w + 0];
        const size_t stop  = (size_t) tws[2 * w + 1];
        int n_ch1 = 0;
        int n_ch2 = 0;
        for(size_t j = start; j < stop; j++){
            // Direct read -- get_routing_channel mallocs a copy of the file.
            const signed char channel = tttr_data->get_routing_channel_at(j);
            for(auto &c: channels_1) n_ch1 += (c == channel);
            for(auto &c: channels_2) n_ch2 += (c == channel);
        }
        const int n_photons = n_ch1 + n_ch2;
        if(
                n_photons < minimum_number_of_photons ||
                n_photons > maximum_number_of_photons
        ) continue;
        // start/stop pairs, as documented. The old code pushed the leftover
        // scan variable (the stop), which alone identifies no photons.
        tmp_tttr_indices.emplace_back(static_cast<int>(start));
        tmp_tttr_indices.emplace_back(static_cast<int>(stop));
        tmp_s1s2[(size_t) n_ch1 * n_dim + n_ch2] += 1.0;   // row is ch1
        tmp_ps[n_photons] += 1.0;
    }
    free(tws);
    *tttr_indices = (int*) malloc(std::max<size_t>(1, tmp_tttr_indices.size()) * sizeof(int));
    memcpy(*tttr_indices, tmp_tttr_indices.data(), tmp_tttr_indices.size() * sizeof(int));
    *n_tttr_indices = static_cast<int>(tmp_tttr_indices.size());
    *ps = tmp_ps;
    *dim_ps = n_dim;
    *s1s2 = tmp_s1s2;
    *dim1 = n_dim;
    *dim2 = n_dim;
}

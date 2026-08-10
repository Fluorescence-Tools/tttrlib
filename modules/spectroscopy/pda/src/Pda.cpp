// SPDX-License-Identifier: BSD-3-Clause
#include "Pda.h"
#include "Verbose.h"
#include "info.h"   // AVX/FMA intrinsics + runtime dispatch macros
#include "Mat.h"    // simd_dot, simd_scale, simd_add
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
TTTRLIB_TARGET_AVX_FMA
static void pda_propagate_row_avx(double* cur, const double* pre, size_t row, double p) {
    const double one_minus_p = 1.0 - p;
    __m256d v_p  = _mm256_set1_pd(p);
    __m256d v_1p = _mm256_set1_pd(one_minus_p);
    const size_t n_vec = (row / 4) * 4;
    size_t col = 0;
    for (; col < n_vec; col += 4) {
        __m256d v_pre0 = _mm256_loadu_pd(&pre[col]);
        __m256d v_pre1 = _mm256_loadu_pd(&pre[col + 1]);
        __m256d v_res = _mm256_fmadd_pd(v_pre1, v_p, _mm256_mul_pd(v_pre0, v_1p));
        _mm256_storeu_pd(&cur[col + 1], v_res);
    }
    for (; col < row; ++col) {
        cur[col + 1] = pre[col] * one_minus_p + pre[col + 1] * p;
    }
}
#endif // TTTRLIB_COMPILE_AVX

// pda_dot now delegates to the shared simd_dot from Mat.h, which already has
// NEON/SSE2/AVX backends with dual accumulators and proper tail handling.
static inline double pda_dot(const double* a, const double* b, size_t n) {
    return tttrlib::simd_dot(a, b, static_cast<int>(n));
}

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

static void pda_accumulate_species(
        std::vector<double>& FgFr,
        std::vector<double>& tmp,
        double p, double a,
        const std::vector<double>& pF,
        unsigned int Nmax
) {
    if (a == 0.0) return;
    tmp[0] = 1.;
    if (Nmax >= 1) tmp[1] = 0.;
    for (size_t row = 1; row <= Nmax; row++) {
        size_t row_offset_cur = (row + 0) * (Nmax + 1);
        size_t row_offset_pre = (row - 1) * (Nmax + 1);
        tmp[row_offset_cur + 0] = tmp[row_offset_pre + 0] * p;
        pda_propagate_row(&tmp[row_offset_cur], &tmp[row_offset_pre], row, p);
        if (row < Nmax) tmp[row_offset_cur + row + 1] = 0.;
    }
    for (size_t row = 0; row <= Nmax; row++) {
        const double w = a * pF[row];
        if (w == 0.0) continue;
        const double* src = &tmp[row * (Nmax + 1)];
        for (size_t red = 0; red <= row; red++)
            FgFr[(row - red) * (Nmax + 1) + red] += src[red] * w;
    }
}

static int good_fft_size(int Nmax);

static void pda_conv_fft(
        std::vector<double>& out,
        const std::vector<double>& in,
        const std::vector<double>& kernel,
        unsigned int Nmax
) {
    const size_t n = Nmax + 1;
    const size_t M = (size_t) good_fft_size((int) Nmax);
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
        for (size_t i = 0; i + row <= Nmax; i++) buf[i] = src[i];
        pocketfft::c2c(shape, stride, stride, axes, true, buf.data(), buf.data(), 1.0);
        for (size_t i = 0; i < M; i++) buf[i] *= K[i];
        pocketfft::c2c(shape, stride, stride, axes, false, buf.data(), buf.data(), 1.0 / M);
        for (size_t k = 0; k + row <= Nmax; k++) {
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
    if (_hist1d_valid
            && _hist1d_xmax == x_max && _hist1d_xmin == x_min
            && _hist1d_nbins == n_bins && _hist1d_logx == log_x
            && _hist1d_nmax == n_max && _hist1d_nmin == n_min
            && _hist1d_skip == skip_zero_photon) return;
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
    std::vector<double> acc(n_bins + 1, 0.0);
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

void Pda::conv_pF(
        std::vector<double> &S1S2,
        const std::vector<double> &F1F2,
        unsigned int Nmax,
        double background_ch1,
        double background_ch2
) {
    const size_t n = Nmax + 1;
    // One SHARED buffer: each cell is written by exactly one omp iteration
    // before pass 2 reads it. thread_local crashed here -- inside the parallel
    // regions each worker rebinds to its own, never-sized TLS copy.
    std::vector<double> tmp(n * n);
    std::vector<double> bg(n, 0.0);
    poisson_0toN(bg, 0, background_ch1, (int) n);
    std::vector<double> br(n, 0.0);
    poisson_0toN(br, 0, background_ch2, (int) n);

    size_t Tr = Nmax; while (Tr > 0 && br[Tr] < 1e-15) Tr--;
    size_t Tg = Nmax; while (Tg > 0 && bg[Tg] < 1e-15) Tg--;

    const size_t fft_crossover = std::max<size_t>(64, Nmax / 5);

#ifdef _OPENMP
    bool use_omp = tttrlib::cpu_features::get_openmp_enabled() && Nmax >= 64;
    int num_threads = tttrlib::cpu_features::get_openmp_num_threads();
#endif

    std::vector<double> br_rev(n), bg_rev(n);
    for (size_t i = 0; i < n; i++) {
        br_rev[i] = br[Nmax - i];
        bg_rev[i] = bg[Nmax - i];
    }

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
        std::vector<double> &S1S2,
        std::vector<double> &pF,
        unsigned int Nmax,
        double background_ch1,
        double background_ch2,
        std::vector<double> &p_ch1,
        std::vector<double> &amplitudes
){
    size_t matrix_elements = (size_t)(Nmax + 1) * (Nmax + 1);
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
    conv_pF(S1S2, FgFr, Nmax, background_ch1, background_ch2);
}

static int good_fft_size(int Nmax) {
    int size = 2 * (Nmax + 1);
    int fft_size = 1;
    while (fft_size < size) fft_size *= 2;
    return fft_size;
}

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

static void fftdc1D(std::vector<double>& pf1, const std::vector<double>& pf_exp, 
                    int Nmax, int order) {
    int fft_size = good_fft_size(Nmax);
    std::vector<std::complex<double>> dft_a(fft_size, std::complex<double>(0.0, 0.0));
    
    for (int i = 0; i <= Nmax; i++) {
        dft_a[i] = std::complex<double>(pf_exp[i], 0.0);
    }
    
    pocketfft::shape_t shape{(size_t)fft_size};
    pocketfft::stride_t stride{sizeof(std::complex<double>)};
    pocketfft::shape_t axes{0};
    pocketfft::c2c(shape, stride, stride, axes, false, dft_a.data(), dft_a.data(), 1.0);
    
    double pw = 1.0 / order;
    for (int i = 0; i < fft_size; i++) {
        double r = std::pow(std::abs(dft_a[i]), pw);
        double phi = pw * std::arg(dft_a[i]);
        dft_a[i] = std::polar(r, phi);
    }
    
    pocketfft::c2c(shape, stride, stride, axes, true, dft_a.data(), dft_a.data(), 1.0 / fft_size);
    
    pf1.resize(Nmax + 1);
    for (int i = 0; i <= Nmax; i++) {
        pf1[i] = dft_a[i].real();
    }
}

static void fftc2D_AA(std::vector<double>& AxA, const std::vector<double>& A, 
                      int Nmax, int order) {
    int fft_size = good_fft_size(Nmax);
    std::vector<std::complex<double>> dft_A(fft_size * fft_size, std::complex<double>(0.0, 0.0));
    
    for (int i = 0; i <= Nmax; i++) {
        for (int j = 0; j <= Nmax; j++) {
            dft_A[fft_size * i + j] = std::complex<double>(A[(Nmax + 1) * i + j], 0.0);
        }
    }
    
    pocketfft::shape_t shape{(size_t)fft_size, (size_t)fft_size};
    pocketfft::stride_t stride{(ptrdiff_t)(fft_size * sizeof(std::complex<double>)), 
                               (ptrdiff_t)sizeof(std::complex<double>)};
    pocketfft::shape_t axes{0, 1};
    pocketfft::c2c(shape, stride, stride, axes, false, dft_A.data(), dft_A.data(), 1.0);
    
    for (int i = 0; i < fft_size * fft_size; i++) {
        dft_A[i] = std::pow(dft_A[i], (double)order);
    }
    
    pocketfft::c2c(shape, stride, stride, axes, true, dft_A.data(), dft_A.data(), 1.0 / (fft_size * fft_size));
    
    AxA.resize((Nmax + 1) * (Nmax + 1));
    for (int i = 0; i <= Nmax; i++) {
        for (int j = 0; j <= Nmax; j++) {
            AxA[(Nmax + 1) * i + j] = dft_A[fft_size * i + j].real();
        }
    }
}

static int pF_conv_order(double p0, double p0_wanted) {
    if (!(p0 > 0.0) || !(p0 < 1.0)) return 1;
    const double order = std::ceil(std::log(p0) / std::log(p0_wanted));
    if (!std::isfinite(order) || order < 1.0) return 1;
    return (int) std::min(order, 1024.0);
}

void Pda::S1S2_pF_optimized(
        std::vector<double> &S1S2,
        std::vector<double> &pF,
        unsigned int Nmax,
        double background_ch1,
        double background_ch2,
        std::vector<double> &p_ch1,
        std::vector<double> &amplitudes
){
    size_t matrix_elements = (size_t)(Nmax + 1) * (Nmax + 1);
    std::vector<double> FgFr(matrix_elements, 0.0);

#ifdef _OPENMP
    const bool use_omp = tttrlib::cpu_features::get_openmp_enabled() &&
                         p_ch1.size() > 1;
    #pragma omp parallel if(use_omp) num_threads(tttrlib::cpu_features::get_openmp_num_threads())
    {
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

    double p0 = pF[0];
    double p0_wanted = 1e-15;
    int conv_order = pF_conv_order(p0, p0_wanted);
    
    if (conv_order > 1) {
        if (is_verbose()) {
            std::clog << "-- Applying multi-molecule correction with order: " << conv_order << std::endl;
        }
        
        std::vector<double> FgFr_corrected;
        fftc2D_AA(FgFr_corrected, FgFr, Nmax, conv_order);
        
        double s = (1.0 - pF[0]) / (1.0 - FgFr_corrected[0]);
        FgFr_corrected[0] = pF[0];
        for (size_t j = 1; j < matrix_elements; j++) {
            FgFr_corrected[j] *= s;
        }
        
        FgFr = FgFr_corrected;
    }

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
    auto tmp_s1s2 = (double*) calloc((size_t) n_dim * n_dim, sizeof(double));
    auto tmp_ps = (double*) calloc((size_t) n_dim, sizeof(double));
    std::vector<int> tmp_tttr_indices;
    int *tws = nullptr; int n_tw = 0;
    tttr_data->get_time_window_ranges(
            &tws, &n_tw,
            minimum_time_window_length,
            minimum_number_of_photons
    );
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
            const signed char channel = tttr_data->get_routing_channel_at(j);
            for(auto &c: channels_1) n_ch1 += (c == channel);
            for(auto &c: channels_2) n_ch2 += (c == channel);
        }
        const int n_photons = n_ch1 + n_ch2;
        if(
                n_photons < minimum_number_of_photons ||
                n_photons > maximum_number_of_photons
        ) continue;
        tmp_tttr_indices.emplace_back(static_cast<int>(start));
        tmp_tttr_indices.emplace_back(static_cast<int>(stop));
        tmp_s1s2[(size_t) n_ch1 * n_dim + n_ch2] += 1.0;
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

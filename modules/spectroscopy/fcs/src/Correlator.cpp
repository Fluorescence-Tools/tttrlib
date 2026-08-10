// SPDX-License-Identifier: BSD-3-Clause
#include "Correlator.h"
#include "Verbose.h"
#include "info.h"

// OpenMP for parallel processing
#ifdef _OPENMP
#include <omp.h>
#endif

// AVX intrinsics and runtime-dispatch macros come from info.h.

// Runtime AVX control using CPU feature detection from info.h
// Set TTTRLIB_USE_AVX=0 to disable AVX optimizations at runtime
static bool g_use_avx = tttrlib::cpu_features::get_avx_enabled();

#include <thread>
#include <cstdlib>
#include <algorithm>

// Worker-thread count for the std::thread-parallel correlation (used on all
// platforms, no OpenMP needed). TTTRLIB_NUM_THREADS / OMP_NUM_THREADS
// override; TTTRLIB_USE_OPENMP=0 keeps working as the parallelism kill
// switch. Defaults to the hardware thread count.
static unsigned int correlator_num_threads() {
    char buf[32];
    const char* off = tttrlib::cpu_features::safe_getenv("TTTRLIB_USE_OPENMP", buf, sizeof(buf));
    if (off != nullptr && tttrlib::cpu_features::is_false_value(off)) return 1;
    for (const char* name : {"TTTRLIB_NUM_THREADS", "OMP_NUM_THREADS"}) {
        const char* v = tttrlib::cpu_features::safe_getenv(name, buf, sizeof(buf));
        if (v != nullptr) {
            int n = std::atoi(v);
            if (n > 0) return (unsigned int) n;
        }
    }
    unsigned int hc = std::thread::hardware_concurrency();
    return hc > 0 ? hc : 1;
}


Correlator::Correlator(
        std::shared_ptr<TTTR> tttr,
        std::string method,
        int n_bins,
        int n_casc,
        bool make_fine
) {
if (is_verbose()) {
    std::clog << "CORRELATOR" << std::endl;
}
    curve.set_n_bins(n_bins);
    curve.set_n_casc(n_casc);
    if (tttr != nullptr) {
if (is_verbose()) {
        std::clog << "-- Passed a TTTR object" << std::endl;
}
        set_tttr(tttr, tttr, make_fine);
    }
    set_correlation_method(method);
}

void Correlator::set_macrotimes(
        unsigned long long *t1v, int n_t1v,
        unsigned long long *t2v, int n_t2v
){
if (is_verbose()) {
    std::clog << "-- Setting macro times..." << std::endl;
    std::clog << "-- n_t1v, n_t2v: " << n_t1v << "," << n_t2v << std::endl;
}
    is_valid = false;
    p1.times.resize(n_t1v);
    p2.times.resize(n_t2v);
    for(int i=0; i<n_t1v; i++) p1.times[i] = t1v[i];
    for(int i=0; i<n_t2v; i++) p2.times[i] = t2v[i];
}

void Correlator::set_weights(
        double* weight_ch1, int n_weights_ch1,
        double* weight_ch2, int n_weights_ch2
){
if (is_verbose()) {
    std::clog << "-- Setting weights..." << std::endl;
    std::clog << "-- n_weights_ch1, n_weights_ch2: " <<
    n_weights_ch1 << "," << n_weights_ch2 << std::endl;
}
    is_valid = false;
    p1.resize(n_weights_ch1);
    p2.resize(n_weights_ch2);
    for(int i=0; i<n_weights_ch1; i++) p1.weights[i] = weight_ch1[i];
    for(int i=0; i<n_weights_ch2; i++) p2.weights[i] = weight_ch2[i];
}

void Correlator::set_events(
        unsigned long long  *t1, int n_t1,
        double* weight_ch1, int n_weights_ch1,
        unsigned long long  *t2, int n_t2,
        double* weight_ch2, int n_weights_ch2
){
    is_valid = false;
    p1.set_events(t1, n_t1, weight_ch1, n_weights_ch1);
    p2.set_events(t2, n_t2, weight_ch2, n_weights_ch2);
}

void Correlator::run(){
if (is_verbose()) {
    std::clog << "-- Running correlator..." << std::endl;
    std::clog << "-- Correlation mode: " << correlation_method << std::endl;
    std::clog << "-- Filling correlation vectors with zero." << std::endl;
}
    if(is_valid){
if (is_verbose()) {
        std::clog << "CORRELATOR::RUN" << std::endl;
        std::clog << "-- Results are already valid." << std::endl;
}
        return;
    } else {
        if(!p1.empty() && !p2.empty()){
            curve.clear();
            if (correlation_method == "wahl"){
                ccf_wahl(
                        get_n_casc(), get_n_bins(),
                        curve.x_axis, 
                        curve.correlation,
                        p1, p2
                );
            } else if (correlation_method == "felekyan") {
                ccf_felekyan(
                        (const unsigned long long *) p1.times.data(),
                        (const unsigned long long *) p2.times.data(),
                        p1.weights.data(), p2.weights.data(),
                        (unsigned int) curve.settings.n_bins,
                        (unsigned int) curve.settings.n_casc,
                        (unsigned int) p1.size(),
                        (unsigned int) p2.size(),
                        curve.x_axis.data(),
                        curve.correlation.data()
                );
            } else if (correlation_method == "laurence") {
                ccf_laurence(curve.x_axis, curve.correlation, p1, p2);
            } else{
                std::cerr << "WARNING: Correlation mode not recognized!" << std::endl;
            }
            normalize(this, this->curve);
        } else{
            std::cerr << "WARNING: No data to correlate!" << std::endl;
        }
        is_valid = true;
    }
}

void Correlator::set_microtimes(
        unsigned short* tac_1, int n_tac_1,
        unsigned short* tac_2, int n_tac_2,
        unsigned int number_of_microtime_channels
        ){
if (is_verbose()) {
    std::clog << "-- Setting micro times..." << std::endl;
}
    is_valid = false;
    p1.make_fine(tac_1, n_tac_1, number_of_microtime_channels);
    p2.make_fine(tac_2, n_tac_2, number_of_microtime_channels);
    dt();
}

uint64_t Correlator::dt(){
    uint64_t dt1 = p1.dt();
    uint64_t dt2 = p2.dt();
    /// The maximum time of an event in the first and second correlation channel, max(t1, t2)
    uint64_t maximum_macro_time = std::max(dt1, dt2);
if (is_verbose()) {
    std::clog << "-- Maximum time (Ch1): " << dt1 << std::endl;
    std::clog << "-- Maximum time (Ch2): " << dt2 << std::endl;
    std::clog << "-- Maximum time: " << maximum_macro_time << std::endl;
}
    return maximum_macro_time;
}

void Correlator::set_tttr(
        std::shared_ptr<TTTR> tttr_1,
        std::shared_ptr<TTTR> tttr_2,
        bool make_fine
){
    is_valid = false;
    p1.set_tttr(tttr_1, make_fine);
    if(tttr_2 == nullptr){
        p2.set_tttr(tttr_1, make_fine);
    } else{
        p2.set_tttr(tttr_2, make_fine);
    }
    if(p1.get_time_axis_calibration() != p2.get_time_axis_calibration()){
        std::cerr << "ERROR: Time axis calibration of photon streams do not match." << std::endl;
    } else{
        curve.settings.macro_time_duration = p1.time_axis_calibration;
    }
    dt();
}

void Correlator::set_filter(
        const std::map<short, std::vector<double>>& filter,
        const std::vector<unsigned int>& micro_times_1,
        const std::vector<signed char>& routing_channels_1,
        const std::vector<unsigned int>& micro_times_2,
        const std::vector<signed char>& routing_channels_2
){
    is_valid = false;
    p1.set_weights(filter, micro_times_1, routing_channels_1);
    p2.set_weights(filter, micro_times_2, routing_channels_2);
}

// Fused coarsening pass for ccf_felekyan's working arrays: optionally halve
// the times, merge runs sharing a time bin and drop zero-weight entries.
// Returns the compacted length. In place (write index never passes the run
// start); accumulation order matches the former pairwise merge loops.
static unsigned int felekyan_coarsen(
        unsigned long long* t, double* w, unsigned int n, bool halve
) {
    unsigned int j = 0;
    if (halve) {
        unsigned int i = 0;
        while (i < n) {
            const unsigned long long tt = t[i] / 2;
            double ww = w[i];
            unsigned int m = i + 1;
            while (m < n && t[m] / 2 == tt) {
                ww += w[m];
                m++;
            }
            if (ww != 0.0) {
                t[j] = tt;
                w[j] = ww;
                j++;
            }
            i = m;
        }
    } else {
        for (unsigned int i = 0; i < n; i++) {
            if (w[i] != 0.0) {
                w[j] = w[i];
                t[j] = t[i];
                j++;
            }
        }
    }
    return j;
}

void Correlator::ccf_felekyan(
        const unsigned long long *t1,
        const unsigned long long *t2,
        const double *weights1,
        const double *weights2,
        unsigned int nc,
        unsigned int nb,
        unsigned int np1,
        unsigned int np2,
        const unsigned long long *xdat, double *corrl
) {
    // t1, t2:              macrotime vectors
    // xdat:                correlation time bins (timeaxis)
    // np1, np2:            number of photons in each channel
    // photons1, photons2:  photon weights
    // nc:                  number of evenly spaced elements per block
    // nb:                  number of blocks of increasing spacing
    // corrl:               pointer to correlation output
if (is_verbose()) {
    std::clog << "-- Copying data to new arrays..." << std::endl;
}
    // the arrays can be modified inplace during the correlation. Thus, copied to a new array.
    auto t1c = (unsigned long long *) malloc(sizeof(unsigned long long) * np1);
    auto t2c = (unsigned long long *) malloc(sizeof(unsigned long long) * np2);
    auto w1 = (double *) malloc(sizeof(double) * np1);
    auto w2 = (double *) malloc(sizeof(double) * np2);
    if (!t1c || !t2c || !w1 || !w2) {
        free(t1c); free(t2c); free(w1); free(w2);
        return;
    }
    std::memcpy(t1c, t1, sizeof(unsigned long long) * np1);
    std::memcpy(t2c, t2, sizeof(unsigned long long) * np2);
    std::memcpy(w1, weights1, sizeof(double) * np1);
    std::memcpy(w2, weights2, sizeof(double) * np2);

    //Initializes some variables for for loops
    unsigned int k=0;

    //Initializes some parameters
    unsigned long long pw;

    // Goes through every block
    for (k=0;k<nb;k++)
    {
        // Determines spacing; used 2time spacing of one
        if (k==0) {pw=1;}
        else {pw=1ULL<<(k-1);};

        const unsigned long long block_offset = xdat[k * nc] / pw;

        // Scans channel-1 photons [i_lo, i_hi) against channel 2 starting at
        // photon p0, accumulating into out (shared curve or a thread-local
        // buffer). Same algorithm as the original single-threaded loop.
        auto scan_block = [&](unsigned int i_lo, unsigned int i_hi,
                              unsigned int p0, double* out) {
            unsigned int p_local = p0;
            for (unsigned int ii = i_lo; ii < i_hi; ii++) {
                // Calculates minimal and maximal time for photons in second array
                unsigned long long l_left = block_offset + t1c[ii];
                unsigned long long l_right = l_left + nc;
                unsigned int jj = p_local;
                while ((jj < np2) && (t2c[jj] <= l_right)) {
                    if (k == 0) { // Special case for the zero time delay bin
                        // If correlation time is positiv OR equal
                        if (t2c[jj] >= l_left) {
                            out[t2c[jj] - l_left + (unsigned long long)(k * nc)] +=
                                    (double) (w1[ii] * w2[jj]);
                        }
                        // Increases starting photon in second array, to save time
                        else { p_local++; }
                    } else {
                        // If correlation time is positiv
                        if (t2c[jj] > l_left) {
                            out[t2c[jj] - l_left + (unsigned long long)(k * nc)] +=
                                    (double) (w1[ii] * w2[jj]);
                        }
                        else { p_local++; }
                    }
                    jj++;
                }
            }
        };

        // Correlate this block, split over std::thread workers when the
        // photon stream is large enough (thread-local curves, reduced after)
        const size_t curve_size = (size_t) nb * nc + 1;
        const size_t min_block = 16384;
        size_t n_threads = std::min<size_t>(correlator_num_threads(), np1 / min_block);
        if (n_threads <= 1) {
            scan_block(0, np1, 0, corrl);
        } else {
            unsigned int block = (unsigned int)((np1 + n_threads - 1) / n_threads);
            std::vector<std::vector<double>> local(n_threads);
            std::vector<std::thread> workers;
            workers.reserve(n_threads);
            for (size_t t = 0; t < n_threads; t++) {
                workers.emplace_back([&, t]() {
                    unsigned int i_lo = (unsigned int)(t * block);
                    unsigned int i_hi = std::min(np1, i_lo + block);
                    if (i_lo >= i_hi) return;
                    // first partner photon this block can pair with
                    unsigned long long l_left0 = block_offset + t1c[i_lo];
                    unsigned int p0 = (k == 0)
                        ? (unsigned int)(std::lower_bound(t2c, t2c + np2, l_left0) - t2c)
                        : (unsigned int)(std::upper_bound(t2c, t2c + np2, l_left0) - t2c);
                    local[t].assign(curve_size, 0.0);
                    scan_block(i_lo, i_hi, p0, local[t].data());
                });
            }
            for (auto &w : workers) w.join();
            for (size_t t = 0; t < n_threads; t++) {
                if (local[t].empty()) continue;
                for (size_t m = 0; m < curve_size; m++) corrl[m] += local[t][m];
            }
        }
        // Coarsen the photon streams for the next block: halve the times
        // (k > 0 only), merge photons sharing a coarse time bin and drop
        // zero-weight entries. Fused single pass, exactly equivalent to the
        // former divide/merge/compact loops.
        np1 = felekyan_coarsen(t1c, w1, np1, k > 0);
        np2 = felekyan_coarsen(t2c, w2, np2, k > 0);
    }
    // Free allocated memory to prevent memory leaks
    free(t1c);
    free(t2c);
    free(w1);
    free(w2);
}

#if TTTRLIB_COMPILE_AVX
// AVX kernel for the Wahl correlation inner loop. Only entered after a runtime
// CPUID check (g_use_avx); the target attribute enables AVX codegen for this
// function alone so the rest of the TU stays portable to non-AVX CPUs.
TTTRLIB_TARGET_AVX
static void ccf_wahl_correlate_avx(
        size_t start_1, size_t end_1, size_t p, size_t end_2,
        size_t i_casc, size_t n_bins, std::vector<double> &corr,
        const unsigned long long *t1, const double *w1,
        const unsigned long long *t2, const double *w2, size_t offset
) {
    size_t index;
    for (size_t i1 = start_1; i1 < end_1; i1++) {
        if (w1[i1] == 0) continue;

        double w1_val = w1[i1];
        size_t edge_l = t1[i1] + offset;
        size_t edge_r = edge_l + n_bins;

        // Broadcast w1[i1] to all lanes of AVX register
        __m256d v_w1 = _mm256_set1_pd(w1_val);

        // Process inner loop with AVX (4 doubles at a time)
        size_t i2 = p;

        // Scalar processing until we find valid range
        while (i2 < end_2 && t2[i2] <= edge_l) {
            p++;
            i2++;
        }

        // Vectorized processing of valid range
        while (i2 + 3 < end_2) {
            // Check if all 4 elements are within bounds
            if (t2[i2 + 3] > edge_r) break;

            // Load 4 weights from w2
            __m256d v_w2 = _mm256_loadu_pd(&w2[i2]);

            // Compute w1[i1] * w2[i2:i2+3]
            __m256d v_product = _mm256_mul_pd(v_w1, v_w2);

            // Calculate indices and accumulate
            // Note: This part needs scalar processing due to indirect indexing
            double products[4];
            _mm256_storeu_pd(products, v_product);

            for (int k = 0; k < 4; k++) {
                if (t2[i2 + k] > edge_l && t2[i2 + k] <= edge_r) {
                    index = t2[i2 + k] - edge_l + i_casc * n_bins;
                    corr[index] += products[k];
                }
            }

            i2 += 4;
        }

        // Process remaining elements with scalar code
        for (; i2 < end_2; i2++) {
            if (t2[i2] > edge_r) break;
            if (t2[i2] > edge_l) {
                index = t2[i2] - edge_l + i_casc * n_bins;
                corr[index] += (w1_val * w2[i2]);
            }
        }
    }
}
#endif // TTTRLIB_COMPILE_AVX

#if TTTRLIB_COMPILE_NEON
// NEON kernel for the Wahl correlation inner loop — 2-wide double.
// Same algorithm as the AVX variant: broadcast w1, vectorize the multiply,
// scalar scatter-store (the index is data-dependent on t2).
static void ccf_wahl_correlate_neon(
        size_t start_1, size_t end_1, size_t p, size_t end_2,
        size_t i_casc, size_t n_bins, std::vector<double> &corr,
        const unsigned long long *t1, const double *w1,
        const unsigned long long *t2, const double *w2, size_t offset
) {
    size_t index;
    for (size_t i1 = start_1; i1 < end_1; i1++) {
        if (w1[i1] == 0) continue;
        double w1_val = w1[i1];
        size_t edge_l = t1[i1] + offset;
        size_t edge_r = edge_l + n_bins;

        float64x2_t v_w1 = vdupq_n_f64(w1_val);

        size_t i2 = p;
        while (i2 < end_2 && t2[i2] <= edge_l) { p++; i2++; }

        // vectorized 2-at-a-time multiply, then scalar scatter-store
        while (i2 + 1 < end_2) {
            if (t2[i2 + 1] > edge_r) break;
            float64x2_t v_w2 = vld1q_f64(&w2[i2]);
            float64x2_t v_prod = vmulq_f64(v_w1, v_w2);
            double products[2];
            vst1q_f64(products, v_prod);
            for (int k = 0; k < 2; k++) {
                if (t2[i2 + k] > edge_l && t2[i2 + k] <= edge_r) {
                    index = t2[i2 + k] - edge_l + i_casc * n_bins;
                    corr[index] += products[k];
                }
            }
            i2 += 2;
        }

        for (; i2 < end_2; i2++) {
            if (t2[i2] > edge_r) break;
            if (t2[i2] > edge_l) {
                index = t2[i2] - edge_l + i_casc * n_bins;
                corr[index] += w1_val * w2[i2];
            }
        }
    }
}
#endif // TTTRLIB_COMPILE_NEON

inline void ccf_wahl_correlate(
        size_t start_1, size_t end_1,
        size_t start_2, size_t end_2,
        size_t i_casc, size_t n_bins,
        std::vector<unsigned long long> &taus, std::vector<double> &corr,
        const unsigned long long *t1, const double *w1, size_t nt1,
        const unsigned long long *t2, const double *w2, size_t nt2
) {
    size_t i1, i2, p, index;
    start_1 = std::max(start_1, (size_t) 0);
    start_2 = std::max(start_2, (size_t) 0);
    end_1 = std::min(nt1, end_1);
    end_2 = std::min(nt2, end_2);
    auto scale = 1ULL << i_casc;
    size_t tau_offset = taus[i_casc * n_bins];
    size_t offset = tau_offset / scale;

    p = start_2;

#if TTTRLIB_COMPILE_AVX
    // Use AVX optimization when available at runtime
    if (g_use_avx) {
        ccf_wahl_correlate_avx(
                start_1, end_1, p, end_2, i_casc, n_bins, corr,
                t1, w1, t2, w2, offset);
        return;
    }
#endif
#if TTTRLIB_COMPILE_NEON
    // NEON is baseline on AArch64 — no runtime check needed.
    {
        ccf_wahl_correlate_neon(
                start_1, end_1, p, end_2, i_casc, n_bins, corr,
                t1, w1, t2, w2, offset);
        return;
    }
#endif
    {
        // Scalar version (used when AVX disabled at runtime or non-x86 build)
        for (i1 = start_1; i1 < end_1; i1++) {
            if (w1[i1] == 0) continue;
            size_t edge_l = t1[i1] + offset;
            size_t edge_r = edge_l + n_bins;
            for (i2 = p; i2 < end_2; i2++) {
                if (t2[i2] > edge_r) break;
                if (t2[i2] > edge_l) {
                    index = t2[i2] - edge_l + i_casc * n_bins;
                    corr[index] += (w1[i1] * w2[i2]);
                } else {
                    p++;
                }
            }
        }
    }
}

void Correlator::ccf_wahl(
        size_t n_casc, size_t n_bins,
        std::vector<unsigned long long> &taus, std::vector<double> &corr,
        CorrelatorPhotonStream &p1,
        CorrelatorPhotonStream &p2
) {
if (is_verbose()) {
    std::clog << "CORRELATOR::CCF" << std::endl;
    std::clog << "-- Copying data to new arrays..." << std::endl;
}
    // the photon streams are modified inplace. Thus, copies are created
    CorrelatorPhotonStream s1(p1);
    CorrelatorPhotonStream s2(p2);
if (is_verbose()) {
    std::clog << "taus:" << std::endl;
    for(auto v: taus){
        std::clog << v << ",";
    }
}

    unsigned int max_threads = correlator_num_threads();
if (is_verbose()) {
    std::clog << "-- Worker threads: " << max_threads << std::endl;
    std::clog << "-- Initial stream sizes: s1=" << s1.size() << ", s2=" << s2.size() << std::endl;
}

    // Cascades are processed in order with incremental coarsening (each
    // cascade coarsens the streams of the previous one once). Within a
    // cascade the first photon stream is split into blocks correlated by
    // std::thread workers into thread-local curves (the curve is small),
    // which are then reduced. This parallelizes on every platform without
    // an OpenMP runtime and avoids re-coarsening per cascade.
    const size_t min_block = 16384; // photons per thread worth forking for
    for (size_t i_casc = 0; i_casc < n_casc; i_casc++) {
        const size_t nt1 = s1.size();
        const size_t nt2 = s2.size();
        size_t n_threads = std::min<size_t>(max_threads, nt1 / min_block);
        if (n_threads <= 1) {
            ccf_wahl_correlate(
                    0, nt1,
                    0, nt2,
                    i_casc, n_bins,
                    taus, corr,
                    s1.times.data(), s1.weights.data(), nt1,
                    s2.times.data(), s2.weights.data(), nt2
            );
        } else {
            const unsigned long long* t1 = s1.times.data();
            const double* w1 = s1.weights.data();
            const unsigned long long* t2 = s2.times.data();
            const double* w2 = s2.weights.data();
            // same offset computation as ccf_wahl_correlate
            auto scale = 1ULL << i_casc;
            size_t offset = ((size_t) taus[i_casc * n_bins]) / scale;
            size_t block = (nt1 + n_threads - 1) / n_threads;
            std::vector<std::vector<double>> local(n_threads);
            std::vector<std::thread> workers;
            workers.reserve(n_threads);
            for (size_t t = 0; t < n_threads; t++) {
                workers.emplace_back([&, t]() {
                    size_t lo = t * block;
                    size_t hi = std::min(nt1, lo + block);
                    if (lo >= hi) return;
                    // first partner photon this block can pair with
                    unsigned long long edge_l = t1[lo] + offset;
                    size_t start_2 = std::upper_bound(t2, t2 + nt2, edge_l) - t2;
                    local[t].assign(corr.size(), 0.0);
                    ccf_wahl_correlate(
                            lo, hi,
                            start_2, nt2,
                            i_casc, n_bins,
                            taus, local[t],
                            t1, w1, nt1,
                            t2, w2, nt2
                    );
                });
            }
            for (auto &w : workers) w.join();
            for (size_t t = 0; t < n_threads; t++) {
                if (local[t].empty()) continue;
                for (size_t i = 0; i < corr.size(); i++) corr[i] += local[t][i];
            }
        }
        s1.coarsen();
        s2.coarsen();
    }
}

void Correlator::ccf_laurence(
            std::vector<unsigned long long> &taus, 
            std::vector<double> &corr,
            CorrelatorPhotonStream &p1,
            CorrelatorPhotonStream &p2
){
    int nbins = static_cast<int>(taus.size());
    long i, j;

    std::vector<long> jmin(taus.size(), 0);
    std::vector<long> jmax(taus.size(), 0);

    // Prefix sums of the partner weights: the weight of a photon window
    // [jmin, jmax) is then a difference instead of an O(window) loop.
    std::vector<double> w2_cumsum(p2.size() + 1, 0.0);
    for (size_t m = 0; m < p2.size(); m++) {
        w2_cumsum[m + 1] = w2_cumsum[m] + p2.weights[m];
    }

    for(i = 0; i < p1.size(); i++){
        auto ti = p1.times[i];
        double w1 = p1.weights[i];

        for(int k = 0; k < nbins - 1; k++){
            double tau_min = static_cast<double>(taus[k + 0]); // lower edge of tau bin
            double tau_max = static_cast<double>(taus[k + 1]); // upper edge of tau bin

            if(k == 0){
                j = jmin[k];
                for(; (j < p2.size()) && ((p2.times[j] - ti) < tau_min); j++);
            }
            jmin[k] = j;

            j = std::max(jmax[k], j);
            for(; (j < p2.size()) && ((p2.times[j] - ti) < tau_max); j++);
            jmax[k] = j;

            // add weight
            corr[k] += w1 * (w2_cumsum[jmax[k]] - w2_cumsum[jmin[k]]);

        }

    }
}

void Correlator::normalize(Correlator* correlator, CorrelatorCurve &curve){
if (is_verbose()) {
    std::clog << "-- Normalizing correlation curve..." << std::endl;
}
    for(size_t i=0; i < curve.corr_normalized.size(); i++) curve.corr_normalized[i] = curve.correlation[i];
    uint64_t maximum_macro_time = correlator->dt();
    if(correlator->correlation_method == "wahl"){
        normalize_ccf_wahl(
                correlator->p1.sum_of_weights(), correlator->p1.dt(),
                correlator->p2.sum_of_weights(), correlator->p2.dt(),
                curve.x_axis,
                curve.corr_normalized,
                curve.settings.n_bins
        );
    } else if (correlator->correlation_method == "felekyan") {
        normalize_ccf_felekyan(
                curve.x_axis, curve.correlation,
                curve.x_axis,
                curve.corr_normalized,
                correlator->p1.mean_count_rate(), correlator->p2.mean_count_rate(),
                curve.settings.n_bins,
                curve.settings.n_casc,
                maximum_macro_time
        );
    } else if (correlator->correlation_method == "laurence") {
        normalize_ccf_laurence(
            correlator->p1,
            correlator->p2,
            curve.x_axis, 
            curve.correlation,
            curve.corr_normalized         
        );
    }
}

void Correlator::normalize_ccf_wahl(
        double np1, uint64_t dt1,
        double np2, uint64_t dt2,
        std::vector<unsigned long long> &x_axis, 
        std::vector<double> &corr,
        size_t n_bins
) {
    double cr1 = (double) np1 / std::max(1.0, (double) dt1);
    double cr2 = (double) np2 / std::max(1.0, (double) dt2);
    double maximum_macro_time = (double) std::max(dt1, dt2);
    // pow(2, floor((j-1)/n_bins)) is a bit shift — cheaper and exact.
    for (unsigned int j = 0; j < x_axis.size(); j++) {
        int shift = static_cast<int>((j > 0) ? static_cast<int>((j - 1) / n_bins) : 0);
        double pw = static_cast<double>(1ULL << shift);
        double delta_t = (double) (maximum_macro_time - x_axis[j]);
        corr[j] /= (pw * cr1 * cr2 * delta_t);
    }
}

void Correlator::normalize_ccf_laurence(
        CorrelatorPhotonStream &p1,
        CorrelatorPhotonStream &p2,
        std::vector<unsigned long long> &axis,
        std::vector<double> &corr,
        std::vector<double> &corr_normalized
) {
    // Symmetric (Schaetzel) normalization. Each lag bin [axis[k], axis[k+1]) is
    // normalized by the count rates measured in the *actually overlapping*
    // sub-intervals rather than by the global mean count rate. For a photon in
    // channel 1 at time t1 to have a partner tau later, it must satisfy
    // t1 <= t2_end - tau; a channel-2 photon at t2 must satisfy t2 >= t1_0 + tau.
    // Using these per-lag monitor sums (S1, S2) instead of the global sums
    // removes the long-lag upturn that global normalization introduces for
    // non-stationary intensities as tau approaches the measurement duration.
    const size_t L = axis.size();
    if (L < 2 || p1.empty() || p2.empty()) return;

    const size_t n1 = p1.size();
    const size_t n2 = p2.size();

    // Prefix sums of the weights so a window sum is an O(1) difference. The
    // event times are chronological, so both monitor pointers advance
    // monotonically as the lag increases (axis is ascending).
    std::vector<double> P1(n1 + 1, 0.0), P2(n2 + 1, 0.0);
    for (size_t i = 0; i < n1; i++) P1[i + 1] = P1[i] + p1.weights[i];
    for (size_t i = 0; i < n2; i++) P2[i + 1] = P2[i] + p2.weights[i];
    const double tot2 = P2[n2];

    const double t1_0 = (double) p1.times.front();
    const double t2_end = (double) p2.times.back();
    const double span = t2_end - t1_0;   // == measurement duration for an ACF

    size_t i2 = 0;    // first p2 index with t2 >= t1_0 + tau  -> S2 = tot2 - P2[i2]
    size_t i1 = n1;   // number of p1 events with t1 <= t2_end - tau -> S1 = P1[i1]
    for (size_t k = 0; k + 1 < L; k++) {
        const double tau = (double) axis[k];
        const double dtau = (double) (axis[k + 1] - axis[k]);   // bin width
        const double overlap = span - tau;
        // The zero-lag bin of an autocorrelation holds the self-correlation
        // (each photon paired with itself) and is not a physical correlation
        // value; zero it, matching the wahl normalization.
        if (tau <= 0.0 || overlap <= 0.0 || dtau <= 0.0) {
            corr_normalized[k] = 0.0;
            continue;
        }
        const double thr2 = t1_0 + tau;      // channel-2 monitor: t2 >= thr2
        while (i2 < n2 && (double) p2.times[i2] < thr2) i2++;
        const double S2 = tot2 - P2[i2];

        const double thr1 = t2_end - tau;    // channel-1 monitor: t1 <= thr1
        while (i1 > 0 && (double) p1.times[i1 - 1] > thr1) i1--;
        const double S1 = P1[i1];

        if (S1 > 0.0 && S2 > 0.0) {
            corr_normalized[k] = corr[k] * overlap / (dtau * S1 * S2);
        } else {
            corr_normalized[k] = 0.0;
        }
    }
    corr_normalized[L - 1] = 0.0;   // last edge has no bin in ccf_laurence
}


void Correlator::normalize_ccf_felekyan(
        std::vector<unsigned long long> &x_axis,
        std::vector<double> &corr,
        std::vector<unsigned long long> &x_axis_normalized,
        std::vector<double> &corr_normalized,
        double cr1, double cr2,
        unsigned int n_bins,
        unsigned int n_casc,
        unsigned long long maximum_macro_time
){
    std::vector<double> divisor;
    // Compute the coarsening factor
    divisor.resize(x_axis.size());
    std::fill(divisor.begin(), divisor.end(), 1.0);
    unsigned int k = n_bins + 1;
    for(unsigned int j=0; j < n_casc; j++) {
        for (unsigned int i = 0; (i < n_bins) && ((unsigned int) k < divisor.size()); i++) {
            divisor[k] = static_cast<double>(1ULL << j);
            k++;
        }
    }
    for(unsigned int i=0; i < corr.size(); i++){
        auto delta_t = (double) (maximum_macro_time - x_axis[i]);
        corr_normalized[i] = corr[i] / (divisor[i] * delta_t * cr1 * cr2);
    }
}

std::pair<std::shared_ptr<TTTR>, std::shared_ptr<TTTR>> Correlator::get_tttr() {
    return {this->p1.tttr, this->p2.tttr};
}

void Correlator::get_x_axis(double** output, int* n_output){
    if(!is_valid) run();
    curve.get_x_axis(output, n_output);
}

void Correlator::get_corr(double** output, int* n_output){
    if(!is_valid) run();
    curve.get_corr(output, n_output);
}

void Correlator::get_corr_normalized(double** output, int* n_output){
    if(!is_valid) run();
    curve.get_corr_normalized(output, n_output);
}

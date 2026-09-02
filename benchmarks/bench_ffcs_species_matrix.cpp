// PRD-129 design probe (out-of-tree, nothing in the shared checkout).
//
// Arm A -- "pair loop": a verbatim transcription of tttrlib's ccf_wahl +
//          CorrelatorPhotonStream::coarsen + normalize_ccf_wahl, run once per
//          species pair. This is what the current chisurf loop does, and what
//          a C++-side pair loop would still do.
// Arm B -- "single pass": the shared time axis walked once, with n weight rows
//          coarsened alongside it, one photon pair updating the whole n*n
//          matrix as a rank-1 block.
//
// Both arms print their matrix so Python can (1) check arm A against the real
// installed tttrlib.Correlator -- proving the transcription faithful -- and
// (2) check arm B against arm A element by element.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

using u64 = unsigned long long;

static unsigned int num_threads() {
    const char* v = getenv("TTTRLIB_NUM_THREADS");
    if (v) { int n = atoi(v); if (n > 0) return (unsigned) n; }
    unsigned hc = std::thread::hardware_concurrency();
    return hc > 0 ? hc : 1;
}

// ---------------------------------------------------------------- axis ------
static std::vector<u64> multi_tau_axis(size_t n_casc, size_t n_bins) {
    std::vector<u64> x(n_casc * n_bins + 1, 0);
    u64 step = 1;
    for (size_t j = 1; j < x.size(); j++) {
        x[j] = x[j - 1] + step;
        if (j % n_bins == 0) step <<= 1;
    }
    return x;
}

static void normalize_wahl(double np1, u64 dt1, double np2, u64 dt2,
                           const std::vector<u64>& x, std::vector<double>& corr,
                           size_t n_bins) {
    double cr1 = np1 / std::max(1.0, (double) dt1);
    double cr2 = np2 / std::max(1.0, (double) dt2);
    double tmax = (double) std::max(dt1, dt2);
    for (size_t j = 0; j < x.size(); j++) {
        int shift = (int) ((j > 0) ? ((j - 1) / n_bins) : 0);
        double pw = (double) (1ULL << shift);
        corr[j] /= (pw * cr1 * cr2 * (tmax - (double) x[j]));
    }
}

// --------------------------------------------------- arm A: the pair loop ---
struct Stream {
    std::vector<u64> t;
    std::vector<double> w;
    void coarsen() {                       // CorrelatorPhotonStream::coarsen
        const size_t n = t.size();
        size_t j = 0, i = 0;
        while (i < n) {
            const u64 tt = t[i] / 2;
            double ww = w[i];
            size_t m = i + 1;
            while (m < n && t[m] / 2 == tt) { ww += w[m]; m++; }
            if (ww != 0.0) { t[j] = tt; w[j] = ww; j++; }
            i = m;
        }
        t.resize(j); w.resize(j);
    }
};

static void wahl_correlate(size_t s1, size_t e1, size_t s2, size_t e2,
                           size_t i_casc, size_t n_bins,
                           const std::vector<u64>& taus, std::vector<double>& corr,
                           const u64* t1, const double* w1,
                           const u64* t2, const double* w2) {
    const size_t scale = (size_t) 1 << i_casc;
    const size_t offset = (size_t) taus[i_casc * n_bins] / scale;
    size_t p = s2;
    for (size_t i1 = s1; i1 < e1; i1++) {
        if (w1[i1] == 0) continue;
        const size_t edge_l = (size_t) t1[i1] + offset;
        const size_t edge_r = edge_l + n_bins;
        for (size_t i2 = p; i2 < e2; i2++) {
            if ((size_t) t2[i2] > edge_r) break;
            if ((size_t) t2[i2] > edge_l)
                corr[(size_t) t2[i2] - edge_l + i_casc * n_bins] += w1[i1] * w2[i2];
            else p++;
        }
    }
}

static void ccf_wahl_pair(size_t n_casc, size_t n_bins,
                          const std::vector<u64>& taus, std::vector<double>& corr,
                          Stream s1, Stream s2) {          // by value: coarsened
    const unsigned max_threads = num_threads();
    const size_t min_block = 16384;
    for (size_t i_casc = 0; i_casc < n_casc; i_casc++) {
        const size_t nt1 = s1.t.size(), nt2 = s2.t.size();
        size_t nth = std::min<size_t>(max_threads, nt1 / min_block);
        if (nth <= 1) {
            wahl_correlate(0, nt1, 0, nt2, i_casc, n_bins, taus, corr,
                           s1.t.data(), s1.w.data(), s2.t.data(), s2.w.data());
        } else {
            const size_t scale = (size_t) 1 << i_casc;
            const size_t offset = (size_t) taus[i_casc * n_bins] / scale;
            const size_t block = (nt1 + nth - 1) / nth;
            std::vector<std::vector<double>> local(nth);
            std::vector<std::thread> ws;
            for (size_t th = 0; th < nth; th++) {
                ws.emplace_back([&, th]() {
                    size_t lo = th * block, hi = std::min(nt1, lo + block);
                    if (lo >= hi) return;
                    u64 edge_l = s1.t[lo] + offset;
                    size_t st2 = std::upper_bound(s2.t.data(), s2.t.data() + nt2, edge_l) - s2.t.data();
                    local[th].assign(corr.size(), 0.0);
                    wahl_correlate(lo, hi, st2, nt2, i_casc, n_bins, taus, local[th],
                                   s1.t.data(), s1.w.data(), s2.t.data(), s2.w.data());
                });
            }
            for (auto& w : ws) w.join();
            for (size_t th = 0; th < nth; th++)
                if (!local[th].empty())
                    for (size_t i = 0; i < corr.size(); i++) corr[i] += local[th][i];
        }
        s1.coarsen(); s2.coarsen();
    }
}

// ------------------------------------------------- arm B: the single pass ---
// Coarsen a shared time axis carrying n_sets weight rows (photon-major:
// w[i * n_sets + s]).  Same rule as coarsen() -- halve, merge photons landing
// in one coarse bin -- except an entry is dropped only when it is zero in
// *every* row.  Dropping is an optimisation, not part of the estimator: a zero
// weight contributes zero to every product, and its absence cannot change
// which survivors merge (they merge on their own times).
static size_t coarsen_matrix(std::vector<u64>& t, std::vector<double>& w,
                             size_t n, size_t n_sets, bool halve) {
    size_t j = 0, i = 0;
    while (i < n) {
        const u64 tt = halve ? t[i] / 2 : t[i];
        size_t m = i + 1;
        if (halve) while (m < n && t[m] / 2 == tt) m++;
        double* dst = w.data() + j * n_sets;
        const double* src = w.data() + i * n_sets;
        if (j != i) for (size_t s = 0; s < n_sets; s++) dst[s] = src[s];
        for (size_t k = i + 1; k < m; k++) {
            const double* add = w.data() + k * n_sets;
            for (size_t s = 0; s < n_sets; s++) dst[s] += add[s];
        }
        bool any = false;
        for (size_t s = 0; s < n_sets; s++) if (dst[s] != 0.0) { any = true; break; }
        if (any) { t[j] = tt; j++; }
        i = m;
    }
    return j;
}

// corr is laid out lag-major / pair-minor -- corr[lag * n_sets^2 + a*n_sets + b]
// -- so one photon pair's contribution is a contiguous n*n rank-1 block.
static void wahl_matrix_correlate(size_t s1, size_t e1, size_t s2, size_t e2,
                                  size_t i_casc, size_t n_bins, size_t n_sets,
                                  const std::vector<u64>& taus, double* corr,
                                  const u64* t, const double* w) {
    const size_t n_pair = n_sets * n_sets;
    const size_t scale = (size_t) 1 << i_casc;
    const size_t offset = (size_t) taus[i_casc * n_bins] / scale;
    size_t p = s2;
    for (size_t i1 = s1; i1 < e1; i1++) {
        const double* wa = w + i1 * n_sets;
        const size_t edge_l = (size_t) t[i1] + offset;
        const size_t edge_r = edge_l + n_bins;
        for (size_t i2 = p; i2 < e2; i2++) {
            if ((size_t) t[i2] > edge_r) break;
            if ((size_t) t[i2] > edge_l) {
                const size_t index = (size_t) t[i2] - edge_l + i_casc * n_bins;
                const double* wb = w + i2 * n_sets;
                double* c = corr + index * n_pair;
                for (size_t a = 0; a < n_sets; a++) {
                    const double va = wa[a];
                    if (va != 0.0)
                        for (size_t b = 0; b < n_sets; b++) c[a * n_sets + b] += va * wb[b];
                }
            } else p++;
        }
    }
}

static void ccf_wahl_matrix(size_t n_casc, size_t n_bins, size_t n_sets,
                            const std::vector<u64>& taus, std::vector<double>& corr,
                            std::vector<u64> t, std::vector<double> w) {
    size_t n = coarsen_matrix(t, w, t.size(), n_sets, false);
    const unsigned max_threads = num_threads();
    const size_t min_block = 16384;
    for (size_t i_casc = 0; i_casc < n_casc; i_casc++) {
        size_t nth = std::min<size_t>(max_threads, n / min_block);
        if (nth <= 1) {
            wahl_matrix_correlate(0, n, 0, n, i_casc, n_bins, n_sets, taus,
                                  corr.data(), t.data(), w.data());
        } else {
            const size_t scale = (size_t) 1 << i_casc;
            const size_t offset = (size_t) taus[i_casc * n_bins] / scale;
            const size_t block = (n + nth - 1) / nth;
            std::vector<std::vector<double>> local(nth);
            std::vector<std::thread> ws;
            const u64* tp = t.data(); const double* wp = w.data();
            for (size_t th = 0; th < nth; th++) {
                ws.emplace_back([&, th]() {
                    size_t lo = th * block, hi = std::min(n, lo + block);
                    if (lo >= hi) return;
                    u64 edge_l = tp[lo] + offset;
                    size_t st2 = std::upper_bound(tp, tp + n, edge_l) - tp;
                    local[th].assign(corr.size(), 0.0);
                    wahl_matrix_correlate(lo, hi, st2, n, i_casc, n_bins, n_sets,
                                          taus, local[th].data(), tp, wp);
                });
            }
            for (auto& x : ws) x.join();
            for (size_t th = 0; th < nth; th++)
                if (!local[th].empty())
                    for (size_t i = 0; i < corr.size(); i++) corr[i] += local[th][i];
        }
        n = coarsen_matrix(t, w, n, n_sets, true);
    }
}

// ------------------------------------------------------------------ main ---
int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: bench <in.bin> <outA.bin> <outB.bin>\n"); return 2; }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror("open"); return 2; }
    long long n_ph, n_sets, n_bins, n_casc;
    if (fread(&n_ph, 8, 1, f) != 1) return 2;
    if (fread(&n_sets, 8, 1, f) != 1) return 2;
    if (fread(&n_bins, 8, 1, f) != 1) return 2;
    if (fread(&n_casc, 8, 1, f) != 1) return 2;
    std::vector<u64> times((size_t) n_ph);
    std::vector<double> wrow((size_t) (n_ph * n_sets));   // row-major (sets, photons)
    if (fread(times.data(), 8, (size_t) n_ph, f) != (size_t) n_ph) return 2;
    if (fread(wrow.data(), 8, (size_t) (n_ph * n_sets), f) != (size_t) (n_ph * n_sets)) return 2;
    fclose(f);

    const auto taus = multi_tau_axis((size_t) n_casc, (size_t) n_bins);
    const size_t n_lags = taus.size();
    const u64 dt = times.back() - times.front();

    std::vector<double> sum_w((size_t) n_sets, 0.0);
    for (size_t s = 0; s < (size_t) n_sets; s++) {
        double acc = 0.0;
        const double* r = wrow.data() + s * n_ph;
        for (size_t i = 0; i < (size_t) n_ph; i++) acc += r[i];
        sum_w[s] = acc;
    }

    // ---- arm A ----
    std::vector<double> A((size_t) (n_sets * n_sets) * n_lags, 0.0);
    auto t0 = std::chrono::steady_clock::now();
    for (size_t a = 0; a < (size_t) n_sets; a++) {
        for (size_t b = 0; b < (size_t) n_sets; b++) {
            Stream s1, s2;
            s1.t = times; s1.w.assign(wrow.begin() + a * n_ph, wrow.begin() + (a + 1) * n_ph);
            s2.t = times; s2.w.assign(wrow.begin() + b * n_ph, wrow.begin() + (b + 1) * n_ph);
            std::vector<double> corr(n_lags, 0.0);
            ccf_wahl_pair((size_t) n_casc, (size_t) n_bins, taus, corr, s1, s2);
            normalize_wahl(sum_w[a], dt, sum_w[b], dt, taus, corr, (size_t) n_bins);
            memcpy(A.data() + (a * n_sets + b) * n_lags, corr.data(), 8 * n_lags);
        }
    }
    auto t1 = std::chrono::steady_clock::now();

    // ---- arm B ----
    std::vector<double> wpm((size_t) (n_ph * n_sets));    // photon-major
    for (size_t s = 0; s < (size_t) n_sets; s++) {
        const double* r = wrow.data() + s * n_ph;
        for (size_t i = 0; i < (size_t) n_ph; i++) wpm[i * n_sets + s] = r[i];
    }
    std::vector<double> B((size_t) (n_sets * n_sets) * n_lags, 0.0);
    auto t2 = std::chrono::steady_clock::now();
    std::vector<double> raw(n_lags * (size_t) (n_sets * n_sets), 0.0);
    ccf_wahl_matrix((size_t) n_casc, (size_t) n_bins, (size_t) n_sets, taus, raw, times, wpm);
    {
        std::vector<double> pair(n_lags);
        for (size_t a = 0; a < (size_t) n_sets; a++)
            for (size_t b = 0; b < (size_t) n_sets; b++) {
                for (size_t j = 0; j < n_lags; j++)
                    pair[j] = raw[j * (size_t) (n_sets * n_sets) + a * n_sets + b];
                normalize_wahl(sum_w[a], dt, sum_w[b], dt, taus, pair, (size_t) n_bins);
                memcpy(B.data() + (a * n_sets + b) * n_lags, pair.data(), 8 * n_lags);
            }
    }
    auto t3 = std::chrono::steady_clock::now();

    double msA = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double msB = std::chrono::duration<double, std::milli>(t3 - t2).count();
    printf("n_ph=%lld n_sets=%lld n_bins=%lld n_casc=%lld  pair-loop(full n^2)=%.1f ms  single-pass=%.1f ms  speedup=%.2fx\n",
           n_ph, n_sets, n_bins, n_casc, msA, msB, msA / msB);
    // the chisurf loop only computes the n(n+1)/2 triangle, so scale arm A
    printf("   arm A scaled to the n(n+1)/2 triangle chisurf actually issues: %.1f ms -> speedup %.2fx\n",
           msA * (double) (n_sets * (n_sets + 1) / 2) / (double) (n_sets * n_sets),
           msA * (double) (n_sets * (n_sets + 1) / 2) / (double) (n_sets * n_sets) / msB);

    FILE* fa = fopen(argv[2], "wb"); fwrite(A.data(), 8, A.size(), fa); fclose(fa);
    FILE* fb = fopen(argv[3], "wb"); fwrite(B.data(), 8, B.size(), fb); fclose(fb);
    return 0;
}

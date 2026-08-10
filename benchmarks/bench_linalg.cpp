// SPDX-License-Identifier: BSD-3-Clause
//
// Dense linear-algebra kernel benchmark — the shared math every ported
// spectroscopy algorithm sits on (Mat.h solvers and GEMM, QREigen.h
// eigendecomposition).
//
// Build (from the repo root):
//   c++ -std=c++17 -O3 -I modules/math/include \
//       benchmarks/bench_linalg.cpp -o benchmarks/bench_linalg
//
//   # with OpenMP (macOS/homebrew libomp):
//   c++ -std=c++17 -O3 -Xpreprocessor -fopenmp -I modules/math/include \
//       -I$(brew --prefix libomp)/include -L$(brew --prefix libomp)/lib -lomp \
//       benchmarks/bench_linalg.cpp -o benchmarks/bench_linalg
//
// Run:
//   ./benchmarks/bench_linalg                                  # print the table
//   ./benchmarks/bench_linalg --write benchmarks/results/linalg_baseline.tsv
//   ./benchmarks/bench_linalg --check benchmarks/results/linalg_baseline.tsv
//
// --check exits non-zero when any case is slower than `--tol` (default 1.30)
// times its recorded baseline, so a regression fails rather than being noticed
// later. Timings are the median of five trials; the tolerance is what absorbs
// the machine-to-machine and run-to-run spread, so a baseline is only
// comparable against the machine that recorded it.

#include "Mat.h"
#include "QREigen.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;

static double ms_between(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

/// Median of five trials, each trial sized to run for at least `min_ms`.
template <class F>
static double measure(F&& body, double min_ms = 40.0) {
    std::vector<double> trials;
    int reps = 1;
    // Calibrate: grow reps until one trial exceeds min_ms.
    for (;;) {
        auto t0 = Clock::now();
        for (int i = 0; i < reps; ++i) body();
        const double t = ms_between(t0, Clock::now());
        if (t >= min_ms || reps >= (1 << 24)) { trials.push_back(t / reps); break; }
        reps = std::max(reps * 2, static_cast<int>(reps * min_ms / std::max(t, 1e-3)));
    }
    for (int k = 0; k < 4; ++k) {
        auto t0 = Clock::now();
        for (int i = 0; i < reps; ++i) body();
        trials.push_back(ms_between(t0, Clock::now()) / reps);
    }
    std::sort(trials.begin(), trials.end());
    return trials[trials.size() / 2];
}

struct Result { std::string name; double ms; };
static std::vector<Result> g_results;

static void record(const std::string& name, double ms) {
    g_results.push_back({name, ms});
    std::printf("  %-34s %10.4f ms\n", name.c_str(), ms);
    std::fflush(stdout);
}

static std::vector<double> random_matrix(int rows, int cols, std::mt19937_64& rng,
                                         bool diag_dominant = false) {
    std::normal_distribution<double> nd;
    std::vector<double> A(static_cast<size_t>(rows) * cols);
    for (auto& v : A) v = nd(rng);
    if (diag_dominant)
        for (int i = 0; i < std::min(rows, cols); ++i)
            A[static_cast<size_t>(i) * cols + i] += rows;
    return A;
}

// ---------------------------------------------------------------------------

static void bench_solvers(std::mt19937_64& rng) {
    std::printf("Dense solvers (Mat.h)\n");

    for (int n : {64, 256}) {
        const auto A = random_matrix(n, n, rng, true);
        const auto b = random_matrix(n, 1, rng);
        record("mat_solve n=" + std::to_string(n), measure([&] {
            auto Aw = A; auto bw = b;
            tttrlib::mat_solve(Aw, bw, n);
        }));
    }

    // The MaxEnt TCSPC active set's fallback path: a square min-norm solve.
    for (int n : {64, 128}) {
        const int m = 4 * n;
        const auto A = random_matrix(m, n, rng);
        const auto b = random_matrix(m, 1, rng);
        record("mat_lstsq_minnorm " + std::to_string(m) + "x" + std::to_string(n),
               measure([&] {
                   auto Aw = A; auto bw = b;
                   tttrlib::mat_lstsq_minnorm(Aw, bw, m, n);
               }));
    }

    // The Kalman burst search's shape: a tiny inverse, once per time bin.
    for (int n : {2, 4}) {
        const auto A = random_matrix(n, n, rng, true);
        std::vector<double> M(A), scratch(static_cast<size_t>(n) * n);
        record("mat_inverse n=" + std::to_string(n) + " x1000", measure([&] {
            for (int i = 0; i < 1000; ++i) {
                M = A;
                tttrlib::mat_inverse_inplace(M.data(), n, scratch.data());
            }
        }));
    }

    // The HMM surrogate's shape: a stochastic matrix raised to a power.
    {
        const int n = 5;
        auto A = random_matrix(n, n, rng);
        for (auto& v : A) v = std::fabs(v);
        tttrlib::row_normalize(A, n, n);
        record("mat_power n=5 p=64 x100", measure([&] {
            for (int i = 0; i < 100; ++i) {
                volatile double sink = tttrlib::mat_power(A.data(), n, 64)[0];
                (void)sink;
            }
        }));
    }
}

static void bench_gemm(std::mt19937_64& rng) {
    std::printf("GEMM (Mat.h)\n");
    for (int n : {128, 256}) {
        tttrlib::Mat A(n, n), B(n, n);
        std::normal_distribution<double> nd;
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j) { A(i, j) = nd(rng); B(i, j) = nd(rng); }
        const std::string sz = std::to_string(n) + "^3";
        record("gemm_nn " + sz, measure([&] { volatile double s = (A * B)(0, 0); (void)s; }));
        record("gemm_nt " + sz, measure([&] { volatile double s = (A * B.t())(0, 0); (void)s; }));
        record("gemm_tn " + sz, measure([&] { volatile double s = (A.t() * B)(0, 0); (void)s; }));
    }
}

static void bench_eigen(std::mt19937_64& rng) {
    std::printf("Eigendecomposition (QREigen.h)\n");
    for (int n : {25, 100, 200}) {
        const auto A = random_matrix(n, n, rng);
        std::vector<tttrlib::cdouble> evals, evecs, inv;
        record("qr_eigendecompose n=" + std::to_string(n), measure([&] {
            tttrlib::qr_eigendecompose(A.data(), n, evals, evecs, inv);
        }));
    }
}

// ---------------------------------------------------------------------------

static int write_baseline(const char* path) {
    std::ofstream out(path);
    if (!out) { std::fprintf(stderr, "cannot write %s\n", path); return 1; }
    for (const auto& r : g_results) out << r.name << '\t' << r.ms << '\n';
    std::printf("\nwrote %zu baselines to %s\n", g_results.size(), path);
    return 0;
}

static int check_baseline(const char* path, double tol) {
    std::ifstream in(path);
    if (!in) { std::fprintf(stderr, "cannot read %s\n", path); return 2; }
    std::map<std::string, double> base;
    std::string line;
    while (std::getline(in, line)) {
        const auto tab = line.rfind('\t');
        if (tab == std::string::npos) continue;
        base[line.substr(0, tab)] = std::stod(line.substr(tab + 1));
    }
    std::printf("\n%-34s %10s %10s %8s\n", "case", "baseline", "now", "ratio");
    int regressions = 0, missing = 0;
    for (const auto& r : g_results) {
        const auto it = base.find(r.name);
        if (it == base.end()) {
            std::printf("%-34s %10s %10.4f %8s  (no baseline)\n",
                        r.name.c_str(), "-", r.ms, "-");
            ++missing;
            continue;
        }
        const double ratio = r.ms / it->second;
        const bool bad = ratio > tol;
        std::printf("%-34s %10.4f %10.4f %7.2fx%s\n", r.name.c_str(), it->second,
                    r.ms, ratio, bad ? "  REGRESSION" : "");
        if (bad) ++regressions;
    }
    if (missing)
        std::printf("\n%d case(s) have no baseline — re-record with --write.\n", missing);
    if (regressions) {
        std::printf("\n%d regression(s) beyond %.2fx.\n", regressions, tol);
        return 1;
    }
    std::printf("\nno regression beyond %.2fx.\n", tol);
    return 0;
}

int main(int argc, char** argv) {
    const char* write_path = nullptr;
    const char* check_path = nullptr;
    double tol = 1.30;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--write") && i + 1 < argc) write_path = argv[++i];
        else if (!std::strcmp(argv[i], "--check") && i + 1 < argc) check_path = argv[++i];
        else if (!std::strcmp(argv[i], "--tol") && i + 1 < argc) tol = std::atof(argv[++i]);
        else {
            std::fprintf(stderr,
                "usage: %s [--write FILE] [--check FILE] [--tol RATIO]\n", argv[0]);
            return 2;
        }
    }

    std::printf("tttrlib dense linear algebra — median of 5 trials\n");
#if TTTRLIB_SIMD_DBL > 0
    std::printf("SIMD: %d doubles wide", TTTRLIB_SIMD_DBL);
#else
    std::printf("SIMD: scalar");
#endif
#ifdef _OPENMP
    std::printf(", OpenMP on\n\n");
#else
    std::printf(", OpenMP off\n\n");
#endif

    std::mt19937_64 rng(20260810);
    bench_solvers(rng);
    bench_gemm(rng);
    bench_eigen(rng);

    if (write_path) return write_baseline(write_path);
    if (check_path) return check_baseline(check_path, tol);
    return 0;
}

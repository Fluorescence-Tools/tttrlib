// SPDX-License-Identifier: BSD-3-Clause
//
// A/B benchmark: Mat.h (self-contained) vs Eigen on the GEMM patterns the
// neural net actually executes.
//
// Build (from the repo root):
//   clang++ -std=c++17 -O3 -I modules/util/include \
//     -I /opt/homebrew/include/eigen3 benchmarks/bench_mat.cpp -o benchmarks/bench_mat

#include "Mat.h"

#include <Eigen/Core>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

using tttrlib::Mat;
using Clock = std::chrono::high_resolution_clock;

static double ms(Clock::time_point t0, Clock::time_point t1) {
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

using EMat = Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;

// Fill BOTH an Eigen and a Mat from the same RNG so correctness is checkable.
struct Pair { EMat e; Mat m; };
static Pair make_pair(int rows, int cols, std::mt19937_64& rng) {
    std::normal_distribution<double> nd;
    Pair p;
    p.e.resize(rows, cols);
    p.m.set_size(rows, cols);
    for (int i = 0; i < rows; ++i)
        for (int j = 0; j < cols; ++j) {
            double v = nd(rng);
            p.e(i, j) = v;
            p.m(i, j) = v;
        }
    return p;
}

static double max_abs_diff(const EMat& e, const Mat& m) {
    double err = 0;
    for (int i = 0; i < e.rows(); ++i)
        for (int j = 0; j < e.cols(); ++j)
            err = std::max(err, std::abs(e(i, j) - m(i, j)));
    return err;
}

// ---------------------------------------------------------------------------
// Forward GEMM (A * W^T) — the inference/training dominant kernel
// ---------------------------------------------------------------------------
void bench_forward(std::mt19937_64& rng) {
    printf("\n=== Forward GEMM: C = A * W^T ===\n");
    printf("%-26s %10s %10s %7s %7s\n", "config", "Eigen ms", "Mat ms", "ratio", "match");

    struct Cfg { const char* name; int M, K, N; };
    static const Cfg cfgs[] = {
        {"inference (1x8, 256x8)",     1,   8, 256},
        {"small batch (64x8)",        64,   8, 256},
        {"training batch (200x8)",   200,   8, 256},
        {"big hidden (200x256)",     200, 256, 256},
        {"output layer (200x128)",   200, 128,   8},
    };

    for (const auto& c : cfgs) {
        Pair A = make_pair(c.M, c.K, rng);
        Pair W = make_pair(c.N, c.K, rng);  // W is N x K, used transposed

        EMat ec(c.M, c.N);
        const int reps = 2000;
        auto t0 = Clock::now();
        for (int r = 0; r < reps; ++r) ec = A.e * W.e.transpose();
        auto t1 = Clock::now();
        Mat mc = A.m * W.m.t();
        auto t2 = Clock::now();
        for (int r = 0; r < reps - 1; ++r) mc = A.m * W.m.t();
        auto t3 = Clock::now();

        double te = ms(t0, t1) / reps;
        double tm = ms(t2, t3) / reps;
        double err = max_abs_diff(ec, mc);
        printf("%-26s %10.4f %10.4f %7.2f %7s\n",
               c.name, te, tm, tm / te, err < 1e-9 ? "ok" : "FAIL");
    }
}

// ---------------------------------------------------------------------------
// Backward GEMM (dA^T * A) — TN case
// ---------------------------------------------------------------------------
void bench_backward_tn(std::mt19937_64& rng) {
    printf("\n=== Backward GEMM: C = dA^T * A ===\n");
    printf("%-26s %10s %10s %7s %7s\n", "config", "Eigen ms", "Mat ms", "ratio", "match");

    struct Cfg { const char* name; int batch, n_out, n_in; };
    static const Cfg cfgs[] = {
        {"small (200x8, 200x256)",    200,   8, 256},
        {"hidden (200x256, 200x256)", 200, 256, 256},
        {"output (200x8, 200x128)",   200,   8, 128},
    };

    for (const auto& c : cfgs) {
        Pair dA = make_pair(c.batch, c.n_out, rng);
        Pair A  = make_pair(c.batch, c.n_in, rng);

        EMat ec(c.n_out, c.n_in);
        const int reps = 2000;
        auto t0 = Clock::now();
        for (int r = 0; r < reps; ++r) ec = dA.e.transpose() * A.e;
        auto t1 = Clock::now();
        Mat mc = dA.m.t() * A.m;
        auto t2 = Clock::now();
        for (int r = 0; r < reps - 1; ++r) mc = dA.m.t() * A.m;
        auto t3 = Clock::now();

        double te = ms(t0, t1) / reps;
        double tm = ms(t2, t3) / reps;
        double err = max_abs_diff(ec, mc);
        printf("%-26s %10.4f %10.4f %7.2f %7s\n",
               c.name, te, tm, tm / te, err < 1e-9 ? "ok" : "FAIL");
    }
}

// ---------------------------------------------------------------------------
// Backward GEMM (dA * W) — NN case
// ---------------------------------------------------------------------------
void bench_backward_nn(std::mt19937_64& rng) {
    printf("\n=== Backward GEMM: C = dA * W ===\n");
    printf("%-26s %10s %10s %7s %7s\n", "config", "Eigen ms", "Mat ms", "ratio", "match");

    struct Cfg { const char* name; int M, K, N; };
    static const Cfg cfgs[] = {
        {"hidden (200x256, 256x256)", 200, 256, 256},
        {"output (200x8, 8x128)",     200,   8, 128},
    };

    for (const auto& c : cfgs) {
        Pair dA = make_pair(c.M, c.K, rng);
        Pair W  = make_pair(c.K, c.N, rng);

        EMat ec(c.M, c.N);
        const int reps = 2000;
        auto t0 = Clock::now();
        for (int r = 0; r < reps; ++r) ec = dA.e * W.e;
        auto t1 = Clock::now();
        Mat mc = dA.m * W.m;
        auto t2 = Clock::now();
        for (int r = 0; r < reps - 1; ++r) mc = dA.m * W.m;
        auto t3 = Clock::now();

        double te = ms(t0, t1) / reps;
        double tm = ms(t2, t3) / reps;
        double err = max_abs_diff(ec, mc);
        printf("%-26s %10.4f %10.4f %7.2f %7s\n",
               c.name, te, tm, tm / te, err < 1e-9 ? "ok" : "FAIL");
    }
}

// ---------------------------------------------------------------------------
// Element-wise: ReLU + bias broadcast
// ---------------------------------------------------------------------------
void bench_elementwise(std::mt19937_64& rng) {
    printf("\n=== Element-wise: ReLU + bias broadcast ===\n");
    printf("%-26s %10s %10s %7s\n", "config", "Eigen ms", "Mat ms", "ratio");

    struct Cfg { const char* name; int rows, cols; };
    static const Cfg cfgs[] = {{"200x256", 200, 256}, {"1000x256", 1000, 256}};

    for (const auto& c : cfgs) {
        Pair Z = make_pair(c.rows, c.cols, rng);
        Eigen::RowVectorXd eb(Z.e.cols());
        Mat mb(1, c.cols);
        std::normal_distribution<double> nd;
        for (int j = 0; j < c.cols; ++j) {
            double v = nd(rng);
            eb(j) = v;
            mb(0, j) = v;
        }

        const int reps = 5000;
        EMat ez = Z.e;
        Mat mz = Z.m;
        auto t0 = Clock::now();
        for (int r = 0; r < reps; ++r) {
            ez = ez.cwiseMax(0.0);
            ez.rowwise() += eb;
        }
        auto t1 = Clock::now();
        for (int r = 0; r < reps; ++r) {
            mz = tttrlib::max(mz, 0.0);
            mz.each_row() += mb;
        }
        auto t2 = Clock::now();

        double te = ms(t0, t1) / reps;
        double tm = ms(t1, t2) / reps;
        printf("%-26s %10.4f %10.4f %7.2f\n", c.name, te, tm, tm / te);
    }
}

int main() {
    std::mt19937_64 rng(42);
    printf("Mat.h vs Eigen A/B benchmark\n");
    printf("=============================\n");
#ifdef __ARM_NEON
    printf("SIMD: NEON (2-wide double)\n");
#elif defined(__AVX__)
    printf("SIMD: AVX (4-wide double)\n");
#elif defined(__SSE2__)
    printf("SIMD: SSE2 (2-wide double)\n");
#else
    printf("SIMD: scalar fallback\n");
#endif

    bench_forward(rng);
    bench_backward_tn(rng);
    bench_backward_nn(rng);
    bench_elementwise(rng);
    printf("\nDone.\n");
    return 0;
}

// SPDX-License-Identifier: BSD-3-Clause
//
// Self-checking tests for the dense linear algebra in Mat.h — the solvers the
// MaxEnt TCSPC active set, the Kalman burst search and the HMM surrogate all
// depend on. Header-only, so this builds with nothing but the include path:
//
//   c++ -std=c++17 -O2 -I modules/math/include \
//       test/cpp/test_mat_linalg.cpp -o /tmp/test_mat_linalg && /tmp/test_mat_linalg
//
// Checks are properties (residual, orthogonality, minimum norm, rank
// detection), not stored numbers, so they stay valid if the implementation
// changes. Exit status is the number of failures.

#include "Mat.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using tttrlib::mat_solve;
using tttrlib::mat_lstsq_minnorm;
using tttrlib::mat_inverse_inplace;
using tttrlib::mat_power;

static int g_failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) { std::printf("  FAIL  %s\n", what); ++g_failures; }
    else       std::printf("  ok    %s\n", what);
}

static void check_close(double a, double b, double tol, const char* what) {
    const double d = std::fabs(a - b);
    if (!(d <= tol)) {
        std::printf("  FAIL  %s (|%.17g - %.17g| = %.3g > %.3g)\n", what, a, b, d, tol);
        ++g_failures;
    } else {
        std::printf("  ok    %s\n", what);
    }
}

// y = A x for a row-major m x n A.
static std::vector<double> matvec(const std::vector<double>& A,
                                  const std::vector<double>& x, int m, int n) {
    std::vector<double> y(m, 0.0);
    for (int i = 0; i < m; ++i)
        for (int j = 0; j < n; ++j)
            y[i] += A[static_cast<size_t>(i) * n + j] * x[j];
    return y;
}

// y = A^T r for a row-major m x n A.
static std::vector<double> matvec_t(const std::vector<double>& A,
                                    const std::vector<double>& r, int m, int n) {
    std::vector<double> y(n, 0.0);
    for (int i = 0; i < m; ++i)
        for (int j = 0; j < n; ++j)
            y[j] += A[static_cast<size_t>(i) * n + j] * r[i];
    return y;
}

static double norm(const std::vector<double>& v) {
    double s = 0.0;
    for (double x : v) s += x * x;
    return std::sqrt(s);
}

static double max_abs(const std::vector<double>& v) {
    double m = 0.0;
    for (double x : v) m = std::max(m, std::fabs(x));
    return m;
}

// ---------------------------------------------------------------------------

static void test_solve_dense(std::mt19937_64& rng) {
    std::printf("mat_solve: well-conditioned dense system\n");
    std::normal_distribution<double> nd;
    const int n = 40;
    std::vector<double> A(static_cast<size_t>(n) * n);
    for (auto& v : A) v = nd(rng);
    for (int i = 0; i < n; ++i) A[static_cast<size_t>(i) * n + i] += n;  // diagonally dominant

    std::vector<double> x_true(n);
    for (auto& v : x_true) v = nd(rng);
    std::vector<double> b = matvec(A, x_true, n, n);

    std::vector<double> Aw(A), bw(b);
    check(mat_solve(Aw, bw, n), "solve reports success");

    std::vector<double> r = matvec(A, bw, n, n);
    for (int i = 0; i < n; ++i) r[i] -= b[i];
    check(norm(r) <= 1e-10 * norm(b), "residual ||Ax-b|| is at round-off");

    double err = 0.0;
    for (int i = 0; i < n; ++i) err = std::max(err, std::fabs(bw[i] - x_true[i]));
    check(err <= 1e-10, "recovers the planted solution");
}

static void test_solve_pivoting() {
    std::printf("mat_solve: needs a row swap on the first pivot\n");
    // A zero leading pivot: the unpivoted algorithm divides by zero here.
    std::vector<double> A = {0.0, 2.0,
                             1.0, 1.0};
    std::vector<double> b = {4.0, 3.0};   // x = (1, 2)
    check(mat_solve(A, b, 2), "solve reports success");
    check_close(b[0], 1.0, 1e-12, "x[0]");
    check_close(b[1], 2.0, 1e-12, "x[1]");
}

static void test_solve_singular_scaled() {
    std::printf("mat_solve: singular detection is scale invariant\n");
    // Rank 1 by construction (an outer product) with entries ~1e8. The row
    // multipliers are not exact in binary, so elimination leaves pivots at
    // rounding level (~1e-8) rather than at zero: an absolute 1e-300 floor
    // calls this regular and returns components of size 1e24.
    const double u[3] = {1.0e4, 1.3e4, 1.7e4};
    const double v[3] = {2.1e4, 0.7e4, 3.3e4};
    std::vector<double> A(9);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) A[static_cast<size_t>(i) * 3 + j] = u[i] * v[j];
    std::vector<double> b = {1.0, 2.0, 3.0};
    check(!mat_solve(A, b, 3), "rank-1 outer product with large entries is rejected");

    // Uniformly tiny but perfectly invertible: must NOT be rejected.
    std::vector<double> B = {1e-150, 0.0,
                             0.0, 2e-150};
    std::vector<double> c = {1e-150, 4e-150};
    check(mat_solve(B, c, 2), "uniformly tiny regular matrix is accepted");
    check_close(c[0], 1.0, 1e-12, "tiny-system x[0]");
    check_close(c[1], 2.0, 1e-12, "tiny-system x[1]");
}

static void test_lstsq_overdetermined(std::mt19937_64& rng) {
    std::printf("mat_lstsq_minnorm: overdetermined, full rank\n");
    std::normal_distribution<double> nd;
    const int m = 60, n = 12;
    std::vector<double> A(static_cast<size_t>(m) * n);
    for (auto& v : A) v = nd(rng);
    std::vector<double> b(m);
    for (auto& v : b) v = nd(rng);

    std::vector<double> Aw(A), bw(b);
    mat_lstsq_minnorm(Aw, bw, m, n);
    check(static_cast<int>(bw.size()) == n, "solution has n entries");

    // The least-squares solution is exactly the one whose residual is
    // orthogonal to every column of A.
    std::vector<double> r = matvec(A, bw, m, n);
    for (int i = 0; i < m; ++i) r[i] -= b[i];
    std::vector<double> g = matvec_t(A, r, m, n);
    check(max_abs(g) <= 1e-9 * norm(b) * std::sqrt(double(m)),
          "normal equations A^T(Ax-b) = 0 hold");

    // And no other point does better.
    std::vector<double> perturbed(bw);
    perturbed[0] += 1e-3;
    std::vector<double> r2 = matvec(A, perturbed, m, n);
    for (int i = 0; i < m; ++i) r2[i] -= b[i];
    check(norm(r2) > norm(r), "perturbing the solution increases the residual");
}

static void test_lstsq_exact_fit(std::mt19937_64& rng) {
    std::printf("mat_lstsq_minnorm: consistent square system matches mat_solve\n");
    std::normal_distribution<double> nd;
    const int n = 25;
    std::vector<double> A(static_cast<size_t>(n) * n);
    for (auto& v : A) v = nd(rng);
    for (int i = 0; i < n; ++i) A[static_cast<size_t>(i) * n + i] += n;

    std::vector<double> x_true(n);
    for (auto& v : x_true) v = nd(rng);
    std::vector<double> b = matvec(A, x_true, n, n);

    std::vector<double> Aw(A), bw(b);
    mat_lstsq_minnorm(Aw, bw, n, n);
    double err = 0.0;
    for (int i = 0; i < n; ++i) err = std::max(err, std::fabs(bw[i] - x_true[i]));
    check(err <= 1e-8, "recovers the planted solution");
}

static void test_lstsq_rank_deficient(std::mt19937_64& rng) {
    std::printf("mat_lstsq_minnorm: rank deficient -> minimum-norm solution\n");
    std::normal_distribution<double> nd;
    const int m = 30, n = 6;
    // Build A with rank 4: two columns are copies of earlier ones.
    std::vector<double> A(static_cast<size_t>(m) * n);
    for (int i = 0; i < m; ++i)
        for (int j = 0; j < 4; ++j)
            A[static_cast<size_t>(i) * n + j] = nd(rng);
    for (int i = 0; i < m; ++i) {
        A[static_cast<size_t>(i) * n + 4] = A[static_cast<size_t>(i) * n + 0];
        A[static_cast<size_t>(i) * n + 5] = A[static_cast<size_t>(i) * n + 1];
    }
    std::vector<double> b(m);
    for (auto& v : b) v = nd(rng);

    std::vector<double> Aw(A), bw(b);
    mat_lstsq_minnorm(Aw, bw, m, n);

    std::vector<double> r = matvec(A, bw, m, n);
    for (int i = 0; i < m; ++i) r[i] -= b[i];
    std::vector<double> g = matvec_t(A, r, m, n);
    check(max_abs(g) <= 1e-8 * norm(b) * std::sqrt(double(m)),
          "normal equations still hold on the rank-deficient system");

    // Adding any null-space direction keeps A x fixed and must lengthen x.
    std::vector<double> nullv(n, 0.0);
    nullv[0] = 1.0; nullv[4] = -1.0;   // column 0 == column 4
    std::vector<double> shifted(bw);
    for (int j = 0; j < n; ++j) shifted[j] += 0.5 * nullv[j];
    std::vector<double> r3 = matvec(A, shifted, m, n);
    for (int i = 0; i < m; ++i) r3[i] -= b[i];
    check(std::fabs(norm(r3) - norm(r)) <= 1e-9 * (1.0 + norm(r)),
          "the null direction really is in the null space");
    check(norm(shifted) > norm(bw), "returned solution has the smaller norm");
}

static void test_lstsq_uniform_scaling(std::mt19937_64& rng) {
    std::printf("mat_lstsq_minnorm: rcond is relative, not absolute\n");
    // Every singular value of this system is below the old absolute 1e-14
    // cutoff, which zeroed the whole solution. Scaling A by s must scale x
    // by 1/s exactly.
    std::normal_distribution<double> nd;
    const int m = 20, n = 5;
    std::vector<double> A0(static_cast<size_t>(m) * n);
    for (auto& v : A0) v = nd(rng);
    std::vector<double> b(m);
    for (auto& v : b) v = nd(rng);

    std::vector<double> Aw(A0), bw(b);
    mat_lstsq_minnorm(Aw, bw, m, n);      // reference at unit scale

    const double s = 1e-15;
    std::vector<double> As(A0);
    for (auto& v : As) v *= s;
    std::vector<double> bs(b);
    mat_lstsq_minnorm(As, bs, m, n);

    check(norm(bs) > 0.0, "a uniformly tiny matrix is not treated as rank 0");
    double rel = 0.0;
    for (int j = 0; j < n; ++j)
        rel = std::max(rel, std::fabs(bs[j] * s - bw[j]) / (1.0 + std::fabs(bw[j])));
    check(rel <= 1e-8, "solution scales as 1/s");
}

static void test_lstsq_underdetermined(std::mt19937_64& rng) {
    std::printf("mat_lstsq_minnorm: underdetermined (m < n)\n");
    std::normal_distribution<double> nd;
    const int m = 4, n = 9;
    std::vector<double> A(static_cast<size_t>(m) * n);
    for (auto& v : A) v = nd(rng);
    std::vector<double> b(m);
    for (auto& v : b) v = nd(rng);

    std::vector<double> Aw(A), bw(b);
    mat_lstsq_minnorm(Aw, bw, m, n);
    std::vector<double> r = matvec(A, bw, m, n);
    for (int i = 0; i < m; ++i) r[i] -= b[i];
    check(norm(r) <= 1e-8 * (1.0 + norm(b)), "fits the data exactly");
    check(static_cast<int>(bw.size()) == n, "solution has n entries");
}

static void test_inverse(std::mt19937_64& rng) {
    std::printf("mat_inverse_inplace\n");
    std::normal_distribution<double> nd;
    const int n = 8;
    std::vector<double> A(static_cast<size_t>(n) * n);
    for (auto& v : A) v = nd(rng);
    for (int i = 0; i < n; ++i) A[static_cast<size_t>(i) * n + i] += n;

    std::vector<double> Ai(A);
    check(mat_inverse_inplace(Ai, n), "inverse reports success");

    double err = 0.0;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            double s = 0.0;
            for (int k = 0; k < n; ++k)
                s += A[static_cast<size_t>(i) * n + k] * Ai[static_cast<size_t>(k) * n + j];
            err = std::max(err, std::fabs(s - (i == j ? 1.0 : 0.0)));
        }
    check(err <= 1e-10, "A * A^-1 == I");

    // Scratch overload writes the same answer.
    std::vector<double> Ai2(A), scratch(static_cast<size_t>(n) * n);
    check(mat_inverse_inplace(Ai2.data(), n, scratch.data()), "scratch overload succeeds");
    double diff = 0.0;
    for (size_t i = 0; i < Ai.size(); ++i) diff = std::max(diff, std::fabs(Ai[i] - Ai2[i]));
    check(diff == 0.0, "scratch overload is bit-identical to the allocating one");

    std::vector<double> S = {1e8, 2e8, 2e8, 4e8};
    check(!mat_inverse_inplace(S, 2), "large-entry singular matrix is rejected");
}

static void test_power(std::mt19937_64& rng) {
    std::printf("mat_power\n");
    std::uniform_real_distribution<double> ud(0.0, 1.0);
    const int n = 5;
    std::vector<double> A(static_cast<size_t>(n) * n);
    for (auto& v : A) v = ud(rng);
    tttrlib::row_normalize(A, n, n);   // a stochastic matrix, as the HMM uses

    for (int p : {0, 1, 2, 3, 7, 16}) {
        std::vector<double> ref(static_cast<size_t>(n) * n, 0.0);
        for (int i = 0; i < n; ++i) ref[static_cast<size_t>(i) * n + i] = 1.0;
        for (int e = 0; e < p; ++e) {
            std::vector<double> tmp(static_cast<size_t>(n) * n, 0.0);
            for (int i = 0; i < n; ++i)
                for (int k = 0; k < n; ++k)
                    for (int j = 0; j < n; ++j)
                        tmp[static_cast<size_t>(i) * n + j] +=
                            ref[static_cast<size_t>(i) * n + k] * A[static_cast<size_t>(k) * n + j];
            ref = tmp;
        }
        std::vector<double> got = mat_power(A.data(), n, p);
        double err = 0.0;
        for (size_t i = 0; i < ref.size(); ++i) err = std::max(err, std::fabs(ref[i] - got[i]));
        char msg[64];
        std::snprintf(msg, sizeof(msg), "A^%d matches repeated multiplication", p);
        check(err <= 1e-12, msg);

        // A power of a stochastic matrix is stochastic.
        double rowmax = 0.0;
        for (int i = 0; i < n; ++i) {
            double s = 0.0;
            for (int j = 0; j < n; ++j) s += got[static_cast<size_t>(i) * n + j];
            rowmax = std::max(rowmax, std::fabs(s - 1.0));
        }
        check(rowmax <= 1e-12, "rows still sum to one");
    }
}

static void test_gemm_shapes(std::mt19937_64& rng) {
    std::printf("GEMM: NN / NT / TN against the textbook triple loop\n");
    std::normal_distribution<double> nd;
    const int shapes[][3] = {{1, 1, 1}, {3, 5, 7}, {4, 8, 16}, {17, 23, 31}, {64, 64, 64}};
    for (const auto& sh : shapes) {
        const int M = sh[0], N = sh[1], K = sh[2];
        tttrlib::Mat A(M, K), B(K, N);
        for (int i = 0; i < M; ++i) for (int j = 0; j < K; ++j) A(i, j) = nd(rng);
        for (int i = 0; i < K; ++i) for (int j = 0; j < N; ++j) B(i, j) = nd(rng);

        tttrlib::Mat C = A * B;
        double err = 0.0;
        for (int i = 0; i < M; ++i)
            for (int j = 0; j < N; ++j) {
                double s = 0.0;
                for (int k = 0; k < K; ++k) s += A(i, k) * B(k, j);
                err = std::max(err, std::fabs(s - C(i, j)));
            }
        char msg[64];
        std::snprintf(msg, sizeof(msg), "NN %dx%dx%d", M, N, K);
        check(err <= 1e-11 * K, msg);

        // C = A * Bt^T with Bt stored N x K
        tttrlib::Mat Bt(N, K);
        for (int i = 0; i < N; ++i) for (int j = 0; j < K; ++j) Bt(i, j) = B(j, i);
        tttrlib::Mat C2 = A * Bt.t();
        err = 0.0;
        for (int i = 0; i < M; ++i)
            for (int j = 0; j < N; ++j) err = std::max(err, std::fabs(C(i, j) - C2(i, j)));
        std::snprintf(msg, sizeof(msg), "NT %dx%dx%d", M, N, K);
        check(err <= 1e-11 * K, msg);

        // C = At^T * B with At stored K x M
        tttrlib::Mat At(K, M);
        for (int i = 0; i < K; ++i) for (int j = 0; j < M; ++j) At(i, j) = A(j, i);
        tttrlib::Mat C3 = At.t() * B;
        err = 0.0;
        for (int i = 0; i < M; ++i)
            for (int j = 0; j < N; ++j) err = std::max(err, std::fabs(C(i, j) - C3(i, j)));
        std::snprintf(msg, sizeof(msg), "TN %dx%dx%d", M, N, K);
        check(err <= 1e-11 * K, msg);
    }
}

int main() {
    std::mt19937_64 rng(20260810);
    test_solve_dense(rng);
    test_solve_pivoting();
    test_solve_singular_scaled();
    test_lstsq_overdetermined(rng);
    test_lstsq_exact_fit(rng);
    test_lstsq_rank_deficient(rng);
    test_lstsq_uniform_scaling(rng);
    test_lstsq_underdetermined(rng);
    test_inverse(rng);
    test_power(rng);
    test_gemm_shapes(rng);

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures;
}

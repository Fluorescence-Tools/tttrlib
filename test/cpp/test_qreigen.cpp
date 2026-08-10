// SPDX-License-Identifier: BSD-3-Clause
//
// Self-checking tests for QREigen.h — the real non-symmetric eigensolver and
// the complex kernels BurstML's likelihood is built on.
//
//   c++ -std=c++17 -O2 -I modules/math/include \
//       test/cpp/test_qreigen.cpp -o /tmp/test_qreigen && /tmp/test_qreigen
//
// The decisive check is the defining property itself: A v = lambda v for every
// pair the solver returns, and A = V diag(lambda) V^-1. A solver can return
// plausible-looking eigenvalues and unusable eigenvectors, and only the
// residual tells them apart. Exit status is the number of failures.

#include "QREigen.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using tttrlib::cdouble;
using tttrlib::qr_eigendecompose;
using tttrlib::zinv;
using tttrlib::zmatmul;

static int g_failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) { std::printf("  FAIL  %s\n", what); ++g_failures; }
    else       std::printf("  ok    %s\n", what);
}

static void report(bool ok, const char* what, double value, double tol) {
    if (!ok) {
        std::printf("  FAIL  %s (%.3g > %.3g)\n", what, value, tol);
        ++g_failures;
    } else {
        std::printf("  ok    %s (%.3g)\n", what, value);
    }
}

static double mat_norm(const std::vector<double>& A) {
    double m = 0.0;
    for (double v : A) m = std::max(m, std::fabs(v));
    return m;
}

/// max_j || A v_j - lambda_j v_j ||_inf, relative to ||A||.
static double eig_residual(const std::vector<double>& A, int n,
                           const std::vector<cdouble>& evals,
                           const std::vector<cdouble>& evecs) {
    double worst = 0.0;
    const double scale = mat_norm(A);
    for (int j = 0; j < n; ++j) {
        double vnorm = 0.0;
        for (int i = 0; i < n; ++i) vnorm += std::norm(evecs[static_cast<size_t>(i) * n + j]);
        vnorm = std::sqrt(vnorm);
        if (vnorm < 1e-300) return 1e300;   // a zero "eigenvector" is no answer
        for (int i = 0; i < n; ++i) {
            cdouble s(0.0);
            for (int k = 0; k < n; ++k)
                s += A[static_cast<size_t>(i) * n + k] * evecs[static_cast<size_t>(k) * n + j];
            s -= evals[j] * evecs[static_cast<size_t>(i) * n + j];
            worst = std::max(worst, std::abs(s) / (vnorm * (scale > 0 ? scale : 1.0)));
        }
    }
    return worst;
}

/// max | (V diag(l) V^-1 - A)_ij | relative to ||A||.
static double reconstruction_error(const std::vector<double>& A, int n,
                                   const std::vector<cdouble>& evals,
                                   const std::vector<cdouble>& evecs,
                                   const std::vector<cdouble>& inv) {
    std::vector<cdouble> VL(static_cast<size_t>(n) * n), R(static_cast<size_t>(n) * n);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            VL[static_cast<size_t>(i) * n + j] = evecs[static_cast<size_t>(i) * n + j] * evals[j];
    zmatmul(VL.data(), inv.data(), R.data(), n);
    double worst = 0.0;
    const double scale = mat_norm(A);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            worst = std::max(worst,
                std::abs(R[static_cast<size_t>(i) * n + j] - A[static_cast<size_t>(i) * n + j]));
    return worst / (scale > 0 ? scale : 1.0);
}

static void run_case(const char* name, const std::vector<double>& A, int n,
                     double tol_resid, double tol_recon) {
    std::printf("%s (n = %d)\n", name, n);
    std::vector<cdouble> evals, evecs, inv;
    check(qr_eigendecompose(A.data(), n, evals, evecs, inv), "decomposition succeeds");

    const double r = eig_residual(A, n, evals, evecs);
    report(r <= tol_resid, "A v = lambda v", r, tol_resid);

    const double e = reconstruction_error(A, n, evals, evecs, inv);
    report(e <= tol_recon, "A = V diag(lambda) V^-1", e, tol_recon);

    // The trace is the sum of the eigenvalues, whatever the eigenvectors do.
    double tr = 0.0;
    cdouble sum(0.0);
    for (int i = 0; i < n; ++i) { tr += A[static_cast<size_t>(i) * n + i]; sum += evals[i]; }
    const double dtr = std::abs(sum - cdouble(tr)) / (1.0 + std::fabs(tr));
    report(dtr <= 1e-9, "sum(lambda) == trace(A)", dtr, 1e-9);
}

// ---------------------------------------------------------------------------

static void test_symmetric(std::mt19937_64& rng) {
    std::normal_distribution<double> nd;
    const int n = 8;
    std::vector<double> A(static_cast<size_t>(n) * n);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j <= i; ++j) {
            const double v = nd(rng);
            A[static_cast<size_t>(i) * n + j] = v;
            A[static_cast<size_t>(j) * n + i] = v;
        }
    run_case("symmetric, real spectrum", A, n, 1e-10, 1e-9);
}

static void test_nonsymmetric(std::mt19937_64& rng) {
    std::normal_distribution<double> nd;
    const int n = 10;
    std::vector<double> A(static_cast<size_t>(n) * n);
    for (auto& v : A) v = nd(rng);
    run_case("general non-symmetric, complex pairs", A, n, 1e-9, 1e-8);
}

static void test_rotation() {
    // A pure 2x2 rotation: eigenvalues exp(+-i theta), no real spectrum.
    const double th = 0.7;
    std::vector<double> A = {std::cos(th), -std::sin(th),
                             std::sin(th),  std::cos(th)};
    run_case("2x2 rotation, purely complex spectrum", A, 2, 1e-12, 1e-11);
}

static void test_rate_matrix() {
    // The shape BurstML actually forms: a kinetic generator (rows sum to zero)
    // with rates spanning several orders of magnitude.
    const int n = 4;
    std::vector<double> A(static_cast<size_t>(n) * n, 0.0);
    const double k[4][4] = {{0, 1e3, 1e-1, 0},
                            {2e2, 0, 0, 5e-2},
                            {1e-2, 0, 0, 3e3},
                            {0, 4e-2, 7e2, 0}};
    for (int i = 0; i < n; ++i) {
        double out = 0.0;
        for (int j = 0; j < n; ++j)
            if (j != i) { A[static_cast<size_t>(i) * n + j] = k[i][j]; out += k[i][j]; }
        A[static_cast<size_t>(i) * n + i] = -out;
    }
    run_case("kinetic generator, rates over 5 decades", A, n, 1e-9, 1e-8);

    // A generator has a zero eigenvalue (rows sum to zero).
    std::vector<cdouble> evals, evecs, inv;
    qr_eigendecompose(A.data(), n, evals, evecs, inv);
    double smallest = 1e300;
    for (const auto& l : evals) smallest = std::min(smallest, std::abs(l));
    report(smallest <= 1e-8 * mat_norm(A), "generator has a zero eigenvalue",
           smallest / mat_norm(A), 1e-8);
}

static void test_badly_scaled(std::mt19937_64& rng) {
    // Row i scaled by 10^(3i), column i by 10^(-3i): the same spectrum as the
    // unscaled matrix, but balancing now has real work to do.
    std::normal_distribution<double> nd;
    const int n = 6;
    std::vector<double> base(static_cast<size_t>(n) * n);
    for (auto& v : base) v = nd(rng);

    std::vector<double> A(base);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            A[static_cast<size_t>(i) * n + j] *= std::pow(10.0, 3.0 * (i - j));

    run_case("similarity-scaled by 10^(3(i-j)) — exercises balancing", A, n, 1e-8, 1e-7);
}

static void test_cyclic() {
    // The classic Francis-QR stress case: a cyclic permutation has all its
    // eigenvalues on the unit circle and no shift from the trailing 2x2 ever
    // separates them, so an implementation without an exceptional shift stalls.
    const int n = 6;
    std::vector<double> A(static_cast<size_t>(n) * n, 0.0);
    for (int i = 0; i < n; ++i) A[static_cast<size_t>(i) * n + ((i + 1) % n)] = 1.0;
    std::printf("cyclic permutation (n = %d)\n", n);
    std::vector<cdouble> evals, evecs, inv;
    const bool ok = qr_eigendecompose(A.data(), n, evals, evecs, inv);
    check(ok, "decomposition reports success");
    if (ok) {
        double worst = 0.0;
        for (const auto& l : evals) worst = std::max(worst, std::fabs(std::abs(l) - 1.0));
        report(worst <= 1e-8, "every eigenvalue is on the unit circle", worst, 1e-8);
    }
}

static void test_repeated_eigenvalue() {
    // A repeated eigenvalue with a FULL eigenspace is diagonalisable and must
    // decompose. Plain inverse iteration returns the same vector for every
    // copy — the zero generator of a disconnected kinetic scheme was rejected
    // exactly that way, even though it is the best-conditioned input there is.
    run_case("zero matrix, twofold zero eigenvalue", {0, 0, 0, 0}, 2, 1e-12, 1e-12);
    run_case("zero matrix, threefold zero eigenvalue",
             std::vector<double>(9, 0.0), 3, 1e-12, 1e-12);

    // Two states exchanging plus one that does not: one zero eigenvalue per
    // component. The scheme a user actually fits when a state is isolated.
    const double k12 = 3.0, k21 = 7.0;
    std::vector<double> G = {-k12,  k21, 0.0,
                              k12, -k21, 0.0,
                              0.0,  0.0, 0.0};
    run_case("disconnected generator, double zero eigenvalue", G, 3, 1e-10, 1e-9);

    // Repeated NONZERO eigenvalue, diagonalisable: char poly (3-l)^2 (1-l)
    // and A - 3I has rank 1, so the eigenspace of 3 is two-dimensional.
    std::vector<double> A = {3.0, 0.0, 0.0,
                             1.0, 2.0, 1.0,
                            -1.0, 1.0, 2.0};
    run_case("repeated nonzero eigenvalue, full eigenspace", A, 3, 1e-9, 1e-8);
}

static void test_defective_is_not_diagonalised() {
    // A Jordan block has one eigenvector for its double eigenvalue. There is
    // no correct answer for V diag(lambda) V^-1, so success here would be a
    // lie: the solver must either fail or return a basis so ill conditioned
    // that the standard cond gate (GopichSzabo uses 1e8) rejects it.
    std::printf("defective 2x2 Jordan block\n");
    std::vector<double> A = {0.0, 1.0,
                             0.0, 0.0};
    std::vector<cdouble> evals, evecs, inv;
    const bool ok = qr_eigendecompose(A.data(), 2, evals, evecs, inv);
    if (!ok) {
        check(true, "rejected outright");
        return;
    }
    auto norm1 = [](const std::vector<cdouble>& M, int n) {
        double best = 0.0;
        for (int j = 0; j < n; ++j) {
            double s = 0.0;
            for (int i = 0; i < n; ++i) s += std::abs(M[static_cast<size_t>(i) * n + j]);
            best = std::max(best, s);
        }
        return best;
    };
    const double cond = norm1(evecs, 2) * norm1(inv, 2);
    check(cond > 1e8, "eigenvector basis is flagged ill-conditioned");
}

static void test_zinv(std::mt19937_64& rng) {
    std::printf("zinv: complex inverse\n");
    std::normal_distribution<double> nd;
    const int n = 7;
    std::vector<cdouble> A(static_cast<size_t>(n) * n), Ai(static_cast<size_t>(n) * n),
                         P(static_cast<size_t>(n) * n);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            A[static_cast<size_t>(i) * n + j] =
                cdouble(nd(rng) + (i == j ? n : 0.0), nd(rng));
    check(zinv(A.data(), Ai.data(), n), "zinv reports success");
    zmatmul(A.data(), Ai.data(), P.data(), n);
    double err = 0.0;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            err = std::max(err, std::abs(P[static_cast<size_t>(i) * n + j] -
                                         cdouble(i == j ? 1.0 : 0.0)));
    report(err <= 1e-10, "A * A^-1 == I", err, 1e-10);
}

int main() {
    std::mt19937_64 rng(20260810);
    test_symmetric(rng);
    test_nonsymmetric(rng);
    test_rotation();
    test_rate_matrix();
    test_badly_scaled(rng);
    test_cyclic();
    test_repeated_eigenvalue();
    test_defective_is_not_diagonalised();
    test_zinv(rng);

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures;
}

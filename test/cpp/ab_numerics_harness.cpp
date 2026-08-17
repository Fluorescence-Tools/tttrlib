// SPDX-License-Identifier: BSD-3-Clause
//
// A/B harness for the header-only numerics in modules/math. It is not a test
// by itself: test/python/misc/test_math_ab_numerics.py compiles it, feeds each
// subcommand its inputs on stdin, and compares the printed result against
// numpy / scipy / a canonical reference implementation. Keeping the reference
// on the Python side means the reference is the real library, not a recorded
// number -- see that file for what each subcommand is checked against.
//
// Build:  c++ -std=c++17 -O2 -I modules/math/include -I modules/util/include \
//             test/cpp/ab_numerics_harness.cpp -o ab_numerics_harness
// Use:    ab_numerics_harness <subcommand>   (numbers on stdin, results on stdout)
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "Mat.h"
#include "NelderMead.h"
#include "QREigen.h"
#include "Random.h"
#include "Sampling.h"
#include "SimPcgRandom.h"
#include "i_lbfgs.h"

using tttrlib::cdouble;

static double rd() { double v; if (!(std::cin >> v)) { std::fprintf(stderr, "short input\n"); std::exit(2); } return v; }
static long long ri() { long long v; if (!(std::cin >> v)) { std::fprintf(stderr, "short input\n"); std::exit(2); } return v; }
static unsigned long long ru() { unsigned long long v; if (!(std::cin >> v)) { std::fprintf(stderr, "short input\n"); std::exit(2); } return v; }
static std::vector<double> rv(size_t n) { std::vector<double> v(n); for (auto& x : v) x = rd(); return v; }
static void pr(double v) { std::printf("%.17g\n", v); }
static void prv(const std::vector<double>& v) { for (double x : v) pr(x); }
static void prc(cdouble z) { std::printf("%.17g %.17g\n", z.real(), z.imag()); }

// --- objectives shared by the two optimisers --------------------------------
// id 0: Rosenbrock (n-dim, chained); 1: ill-conditioned quadratic
//       sum_i (i+1)^2 (x_i - i)^2 with cross term 0.5*sum x_i x_{i+1};
//       2: "bowl": sum (x_i-1)^2 + 0.1*|x_i-1| (non-smooth kink at the optimum).
static int g_obj = 0;
static double objective(const double* x, int n) {
    double f = 0.0;
    if (g_obj == 0) {
        for (int i = 0; i + 1 < n; ++i) {
            const double a = x[i + 1] - x[i] * x[i];
            const double b = 1.0 - x[i];
            f += 100.0 * a * a + b * b;
        }
    } else if (g_obj == 1) {
        for (int i = 0; i < n; ++i) { const double d = x[i] - i; f += (i + 1) * (i + 1) * d * d; }
        for (int i = 0; i + 1 < n; ++i) f += 0.5 * x[i] * x[i + 1];
    } else {
        for (int i = 0; i < n; ++i) { const double d = x[i] - 1.0; f += d * d + 0.1 * std::fabs(d); }
    }
    return f;
}
static void gradient(const double* x, int n, double* g) {
    for (int i = 0; i < n; ++i) g[i] = 0.0;
    if (g_obj == 0) {
        for (int i = 0; i + 1 < n; ++i) {
            const double a = x[i + 1] - x[i] * x[i];
            g[i] += -400.0 * x[i] * a - 2.0 * (1.0 - x[i]);
            g[i + 1] += 200.0 * a;
        }
    } else if (g_obj == 1) {
        for (int i = 0; i < n; ++i) g[i] += 2.0 * (i + 1) * (i + 1) * (x[i] - i);
        for (int i = 0; i + 1 < n; ++i) { g[i] += 0.5 * x[i + 1]; g[i + 1] += 0.5 * x[i]; }
    } else {
        for (int i = 0; i < n; ++i) { const double d = x[i] - 1.0; g[i] = 2.0 * d + (d > 0 ? 0.1 : d < 0 ? -0.1 : 0.0); }
    }
}
struct Ctx { int n; };
static double target_fp(double* x, void* p) { return objective(x, static_cast<Ctx*>(p)->n); }
static double grad_fp(double* x, double* g, void* p) { const int n = static_cast<Ctx*>(p)->n; gradient(x, n, g); return objective(x, n); }
static int g_fd_n = 0;
static void fd_fun(double* x, double& f) { f = objective(x, g_fd_n); }

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <cmd>\n", argv[0]); return 2; }
    const std::string cmd = argv[1];

    if (cmd == "nm") {
        // stdin: obj n bounded x0[n]; prints x[n] f iterations status
        g_obj = int(ri()); const int n = int(ri()); const int bounded = int(ri());
        std::vector<double> x0 = rv(n);
        std::vector<double> lb(n, -INFINITY), ub(n, INFINITY), step(n, 0.5);
        if (bounded) { for (int i = 0; i < n; ++i) { lb[i] = -0.5; ub[i] = 0.75; } }
        auto f = [n](const std::vector<double>& x) { return objective(x.data(), n); };
        auto r = tttrlib::nelder_mead(f, x0, lb, ub, step, 20000, 1e-9, 1e-14);
        prv(r.x); pr(r.fval); pr(r.iterations); pr(r.status);
        return 0;
    }
    if (cmd == "lbfgs") {
        // stdin: obj n analytic bounded maxiter x0[n]; prints x[n] f info
        g_obj = int(ri()); const int n = int(ri()); const int analytic = int(ri());
        const int bounded = int(ri()); const int maxiter = int(ri());
        std::vector<double> x = rv(n);
        Ctx c{n};
        bfgs opt(target_fp, n);
        opt.maxiter = maxiter;
        if (analytic) opt.set_gradient(grad_fp);
        if (bounded) for (int i = 0; i < n; ++i) opt.set_bounds(i, -0.5, 0.75);
        const int info = opt.minimize(x.data(), &c);
        prv(x); pr(objective(x.data(), n)); pr(info);
        return 0;
    }
    if (cmd == "fgrad") {
        // stdin: obj n eps x[n]; prints fgrad1[n] fgrad2[n] fgrad4[n] analytic[n]
        g_obj = int(ri()); const int n = int(ri()); const double eps = rd();
        std::vector<double> x = rv(n), g(n), ga(n);
        g_fd_n = n;
        fgrad1(fd_fun, x.data(), n, eps, g.data()); prv(g);
        fgrad2(fd_fun, x.data(), n, eps, g.data()); prv(g);
        fgrad4(fd_fun, x.data(), n, eps, g.data()); prv(g);
        gradient(x.data(), n, ga.data()); prv(ga);
        return 0;
    }
    if (cmd == "solve") {
        const int n = int(ri()); auto A = rv(size_t(n) * n); auto b = rv(n);
        const bool ok = tttrlib::mat_solve(A, b, n); pr(ok ? 1 : 0); prv(b); return 0;
    }
    if (cmd == "lstsq") {
        const int m = int(ri()); const int n = int(ri()); const double rcond = rd();
        auto A = rv(size_t(m) * n); auto b = rv(m);
        const bool ok = tttrlib::mat_lstsq_minnorm(A, b, m, n, rcond); pr(ok ? 1 : 0); prv(b); return 0;
    }
    if (cmd == "inv") {
        const int n = int(ri()); auto A = rv(size_t(n) * n);
        const bool ok = tttrlib::mat_inverse_inplace(A, n); pr(ok ? 1 : 0); prv(A); return 0;
    }
    if (cmd == "power") {
        const int n = int(ri()); const int p = int(ri()); auto A = rv(size_t(n) * n);
        prv(tttrlib::mat_power(A.data(), n, p)); return 0;
    }
    if (cmd == "gemm") {
        // stdin: M N K A[MxK] B[KxN]; prints A*B, A*B^T (B given as NxK then), (A^T)*B
        const int M = int(ri()); const int N = int(ri()); const int K = int(ri());
        auto a = rv(size_t(M) * K); auto b = rv(size_t(K) * N);
        tttrlib::Mat A(M, K, a.data()), B(K, N, b.data());
        tttrlib::Mat C = A * B; prv(std::vector<double>(C.memptr(), C.memptr() + C.n_elem()));
        // NT: A (MxK) times Bt where Bt is stored NxK -> use B^T^T: build Bn = B^T (NxK) explicitly
        tttrlib::Mat Bn = static_cast<tttrlib::Mat>(B.t());
        tttrlib::Mat C2 = A * Bn.t(); prv(std::vector<double>(C2.memptr(), C2.memptr() + C2.n_elem()));
        tttrlib::Mat An = static_cast<tttrlib::Mat>(A.t());   // KxM
        tttrlib::Mat C3 = An.t() * B; prv(std::vector<double>(C3.memptr(), C3.memptr() + C3.n_elem()));
        // reductions: accu, column sums, row sums
        pr(tttrlib::accu(C));
        tttrlib::Mat cs = tttrlib::sum(C, 0); prv(std::vector<double>(cs.memptr(), cs.memptr() + cs.n_elem()));
        tttrlib::Mat rs = tttrlib::sum(C, 1); prv(std::vector<double>(rs.memptr(), rs.memptr() + rs.n_elem()));
        return 0;
    }
    if (cmd == "eig") {
        const int n = int(ri()); auto A = rv(size_t(n) * n);
        std::vector<cdouble> ev, V, Vi;
        const bool ok = tttrlib::qr_eigendecompose(A.data(), n, ev, V, Vi);
        pr(ok ? 1 : 0);
        if (!ok) return 0;
        for (auto z : ev) prc(z);
        for (auto z : V) prc(z);
        for (auto z : Vi) prc(z);
        return 0;
    }
    if (cmd == "zops") {
        // stdin: n A[n*n as re im] B[n*n] x[n]; prints A*B, A*x, inv(A)
        const int n = int(ri());
        std::vector<cdouble> A(size_t(n) * n), B(size_t(n) * n), x(n), C(size_t(n) * n), y(n), Ai(size_t(n) * n);
        for (auto& z : A) { double re = rd(); double im = rd(); z = cdouble(re, im); }
        for (auto& z : B) { double re = rd(); double im = rd(); z = cdouble(re, im); }
        for (auto& z : x) { double re = rd(); double im = rd(); z = cdouble(re, im); }
        tttrlib::zmatmul(A.data(), B.data(), C.data(), n); for (auto z : C) prc(z);
        tttrlib::zmatvec(A.data(), x.data(), y.data(), n); for (auto z : y) prc(z);
        const bool ok = tttrlib::zinv(A.data(), Ai.data(), n); pr(ok ? 1 : 0);
        for (auto z : Ai) prc(z);
        return 0;
    }
    if (cmd == "philox_stream") {
        // stdin: seed stream n; prints n raw u32 from Random::next_u32
        const uint32_t seed = uint32_t(ru()); const uint32_t stream = uint32_t(ru()); const long long n = ri();
        tttrlib::Random r; r.seed(seed, stream);
        for (long long i = 0; i < n; ++i) std::printf("%u\n", r.next_u32());
        return 0;
    }
    if (cmd == "philox_seek") {
        // stdin: seed stream index n; prints n raw u32 after seek(index)
        const uint32_t seed = uint32_t(ru()); const uint32_t stream = uint32_t(ru());
        const uint64_t idx = ru(); const long long n = ri();
        tttrlib::Random r; r.seed(seed, stream); r.seek(idx);
        for (long long i = 0; i < n; ++i) std::printf("%u\n", r.next_u32());
        return 0;
    }
    if (cmd == "det") {
        // stdin: n then n pairs (seed index); prints Random::deterministic_u32,
        // whose engine is chosen by TTTR_RNG_ENGINE in this process's environment
        const long long n = ri();
        for (long long i = 0; i < n; ++i) { const uint32_t s = uint32_t(ru()); const uint64_t k = ru();
            std::printf("%u\n", tttrlib::Random::deterministic_u32(s, k)); }
        return 0;
    }
    if (cmd == "pcg32") {
        // stdin: base id counter_start n; prints n u32 from SimPcgRandom
        const uint32_t base = uint32_t(ru()); const uint32_t id = uint32_t(ru()); const uint64_t cs = ru(); const long long n = ri();
        tttrlib::SimPcgRandom r; r.reset(base, id, cs);
        for (long long i = 0; i < n; ++i) std::printf("%u\n", r.next32());
        return 0;
    }
    if (cmd == "normal") {
        // stdin: seed n; prints n normals from Random::normal
        const uint32_t seed = uint32_t(ru()); const long long n = ri();
        tttrlib::Random r; r.seed(seed, 0);
        for (long long i = 0; i < n; ++i) pr(r.normal());
        return 0;
    }
    if (cmd == "wchoice") {
        // stdin: nw weights[nw] nout; prints nout indices (global_rng: TTTR_RNG_SEED)
        const int nw = int(ri()); auto w = rv(nw); const int nout = int(ri());
        std::vector<uint32_t> out(nout);
        tttrlib::weighted_choice(w.data(), nw, out.data(), nout);
        for (auto v : out) std::printf("%u\n", v);
        return 0;
    }
    if (cmd == "cdf") {
        // stdin: n axis[n] cdf[n] normalize nout; prints nout samples
        const int n = int(ri()); auto axis = rv(n); auto cdf = rv(n); const int norm = int(ri()); const int nout = int(ri());
        std::vector<double> out(nout);
        tttrlib::sample_from_cdf(axis.data(), n, cdf.data(), n, out.data(), nout, norm != 0);
        prv(out); return 0;
    }
    std::fprintf(stderr, "unknown command %s\n", cmd.c_str());
    return 2;
}

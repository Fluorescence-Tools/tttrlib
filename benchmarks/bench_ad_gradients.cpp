// SPDX-License-Identifier: BSD-3-Clause
//
// PRD-010 phase 3 — MEASURE ONLY. Compares gradient strategies for the L-BFGS
// fitting path without converting any production code.
//
// The objective replicates tttrlib's real decay-fit shape: a multi-exponential
// lifetime spectrum convolved with an IRF via the exact `fconv` recursion
// (src/DecayConvolution.cpp:65-80), scored with the Poisson 2I* statistic over
// NCH channels. It is templated on the scalar type so the same code runs in
// plain `double` and in forward-mode AD.
//
// Strategies compared, at the parameter counts of the real consumers
// (DecayFit26 n=1, DecayFit23 n=4, DecayFit24 n=5). NOTE: the n=18 row is NOT
// representative of ImageLocalization -- see bench_ad_vectorized.cpp, which
// benchmarks its real 2D-Gaussian objective at its real free-parameter counts.
//
//   1. objective            — the cost unit everything else is quoted in
//   2. central differences, h = eps*|x|        (what i_lbfgs.h does today)
//   3. central differences, h = eps^(1/3)*|x|  (the retuned honest baseline)
//   4. vectorized forward AD, Dual<double, Eigen::Array<double,N,1>>
//
// Build (from benchmarks/, with autodiff and Eigen on the include path):
//   clang++ -std=c++17 -O3 -I<autodiff> -I<eigen> bench_ad_gradients.cpp -o bench_ad
//
// Note the SIMD caveat: tttrlib's production `double` path can use fconv_simd(),
// whose intrinsic kernels CANNOT be templated. The AD column therefore loses the
// SIMD kernel while the finite-difference columns keep it, so these scalar-vs-
// scalar numbers are an UPPER bound. bench_ad_vectorized.cpp measures the
// with-SIMD comparison directly.

#include <Eigen/Core>

#include <autodiff/forward/dual.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

// autodiff's Dual can carry an Eigen array as its derivative part, giving all N
// partials in a single pass. That combination is undocumented and needs this
// trait specialization; without it dual.hpp fails to find NumericType.
namespace autodiff {
namespace detail {
template <int N>
struct NumberTraits<Eigen::Array<double, N, 1>> {
    using NumericType = double;
    static constexpr auto Order = 0;
};
}  // namespace detail
}  // namespace autodiff

static const int NCH = 1024;
static std::vector<double> g_irf, g_data;

/// Poisson 2I* over a multi-exponential decay convolved with the IRF.
/// Mirrors fconv()'s recursion exactly so the FLOP mix is representative.
template <typename T>
T objective(const T* x, int n_par) {
    const double dt = 0.05;
    const int numexp = n_par / 2 > 0 ? n_par / 2 : 1;

    std::vector<T> fit(NCH, T(0.0));
    std::vector<double> l2(NCH);
    for (int i = 0; i < NCH; ++i) l2[i] = dt * 0.5 * g_irf[i];

    for (int ne = 0; ne < numexp; ++ne) {
        const T tau = x[(2 * ne + 1) % n_par] + T(1e-3);
        const T a = x[(2 * ne) % n_par];
        const T expcurr = exp(-dt / tau);
        T fitcurr = T(0.0);
        fit[0] += l2[0] * a;
        for (int i = 1; i < NCH; ++i) {
            fitcurr = (fitcurr + l2[i - 1]) * expcurr + l2[i];
            fit[i] += fitcurr * a;
        }
    }

    T sum = T(0.0);
    for (int i = 0; i < NCH; ++i) sum += fit[i];
    T chi = T(0.0);
    for (int i = 0; i < NCH; ++i) {
        const T m = fit[i] / sum * 1.0e5 + T(1e-9);
        const double d = g_data[i];
        chi += m - d + (d > 0.0 ? d * log(d / m) : T(0.0));
    }
    return 2.0 * chi;
}

/// Central-difference gradient: 2N objective evaluations.
void grad_central(std::vector<double>& x, int n, double eps, std::vector<double>& g) {
    for (int j = 0; j < n; ++j) {
        const double t = x[j];
        const double h = (t != 0.0) ? eps * std::fabs(t) : eps;
        x[j] = t + h;
        const double f1 = objective<double>(x.data(), n);
        x[j] = t - h;
        const double f2 = objective<double>(x.data(), n);
        x[j] = t;
        g[j] = 0.5 * (f1 - f2) / h;
    }
}

/// Vectorized forward-mode AD: all N partials in one pass.
template <int N>
void grad_ad(const std::vector<double>& x, std::vector<double>& g) {
    using Arr = Eigen::Array<double, N, 1>;
    using DualN = autodiff::detail::Dual<double, Arr>;

    std::vector<DualN> xd(N);
    for (int j = 0; j < N; ++j) {
        xd[j].val = x[j];
        xd[j].grad = Arr::Zero();
        xd[j].grad[j] = 1.0;
    }
    const DualN f = objective<DualN>(xd.data(), N);
    for (int j = 0; j < N; ++j) g[j] = f.grad[j];
}

template <typename F>
double timeit(F&& fn, int reps) {
    fn();  // warm up
    double best = 1e300;
    for (int r = 0; r < reps; ++r) {
        const auto t0 = std::chrono::steady_clock::now();
        fn();
        const double dt = std::chrono::duration<double>(
                              std::chrono::steady_clock::now() - t0).count();
        if (dt < best) best = dt;
    }
    return best;
}

template <int N>
void run_case() {
    std::vector<double> x(N), g(N), g_ref(N);
    for (int j = 0; j < N; ++j) x[j] = 0.5 + 0.1 * j;

    const double t_obj = timeit([&] { objective<double>(x.data(), N); }, 200);

    const double eps_now = 1e-6;                    // i_lbfgs.h fgrad2
    const double eps_opt = std::cbrt(2.22e-16);     // ~6.06e-6, the theoretical optimum
    const double t_cd_now = timeit([&] { grad_central(x, N, eps_now, g); }, 50);
    const double t_cd_opt = timeit([&] { grad_central(x, N, eps_opt, g); }, 50);
    const double t_ad = timeit([&] { grad_ad<N>(x, g); }, 50);

    // accuracy: AD is exact, so use it as the reference for the FD schemes
    grad_ad<N>(x, g_ref);
    auto rel_err = [&](double eps) {
        grad_central(x, N, eps, g);
        double worst = 0.0;
        for (int j = 0; j < N; ++j) {
            const double d = std::fabs(g[j] - g_ref[j]) /
                             (std::fabs(g_ref[j]) > 1e-30 ? std::fabs(g_ref[j]) : 1.0);
            if (d > worst) worst = d;
        }
        return worst;
    };
    const double e_now = rel_err(eps_now);
    const double e_opt = rel_err(eps_opt);

    printf("  N=%-3d obj=%7.4f ms | CD(now) %6.2fx err=%.1e | CD(tuned) %6.2fx err=%.1e"
           " | AD %6.2fx exact | AD speedup vs tuned CD: %5.2fx\n",
           N, t_obj * 1e3,
           t_cd_now / t_obj, e_now,
           t_cd_opt / t_obj, e_opt,
           t_ad / t_obj,
           t_cd_opt / t_ad);
}

int main() {
    // synthetic IRF (gaussian) + data (bi-exponential decay + noise)
    g_irf.resize(NCH);
    g_data.resize(NCH);
    for (int i = 0; i < NCH; ++i) {
        const double t = (i - 100.0) / 20.0;
        g_irf[i] = std::exp(-0.5 * t * t);
    }
    for (int i = 0; i < NCH; ++i) {
        const double t = i * 0.05;
        g_data[i] = 1000.0 * (0.6 * std::exp(-t / 0.5) + 0.4 * std::exp(-t / 2.0)) + 1.0;
    }

    printf("AD gradient benchmark (NCH=%d), scalar path only -- see AVX caveat in header\n", NCH);
    printf("Parameter counts match the real i_lbfgs.h consumers.\n");
    run_case<1>();   // DecayFit26
    run_case<4>();   // DecayFit23
    run_case<5>();   // DecayFit24
    run_case<8>();
    run_case<18>();  // NOT ImageLocalization -- see bench_ad_vectorized.cpp
    return 0;
}

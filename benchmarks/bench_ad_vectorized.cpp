// SPDX-License-Identifier: BSD-3-Clause
//
// PRD-010 phase 3, part 2 — the two questions bench_ad_gradients.cpp left open.
//
// A. Does AD still win when central differences keep the hand-vectorized
//    convolution kernel? The scalar-vs-scalar numbers in bench_ad_gradients.cpp
//    are an upper bound, because fconv_simd()/fconv_neon_impl() use intrinsics
//    and cannot be templated on the scalar type. Here the CD column calls the
//    real dispatcher (NEON on this AArch64 host, AVX on x86) while the AD column
//    is forced onto the templated scalar path.
//
// B. What does ImageLocalization's objective actually cost? It is a 2D Gaussian
//    PSF fit (target2DGaussian -> model{,Two,Three}2DGaussian + Poisson W2DG),
//    NOT a decay fit -- it never calls fconv. And it never optimises 18 free
//    parameters: entries 12..17 are flags/outputs and are fixed, so the free
//    count is 6 / 9 / 12 for one / two / three Gaussians, minus optional fixes
//    of eps and bg. i_lbfgs minimises in the reduced space of free parameters,
//    so N is what matters for gradient cost.
//
// Build (from the repository root):
//   c++ -std=c++17 -O3 -I modules/math/include -I modules/util/include \
//       -I modules/spectroscopy/decay/include -I thirdparty \
//       benchmarks/bench_ad_vectorized.cpp \
//       modules/spectroscopy/decay/src/DecayConvolution.cpp \
//       modules/util/src/*.cpp -o /tmp/bench_ad_vec

#include <autodiff/forward/dual.hpp>

#include "GradVec.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "DecayConvolution.h"

namespace autodiff {
namespace detail {
template <int N>
struct NumberTraits<tttrlib::GradVec<N>> {
    using NumericType = double;
    static constexpr auto Order = 0;
};
}  // namespace detail
}  // namespace autodiff

static const int NCH = 1024;
static std::vector<double> g_irf, g_data;

// --------------------------------------------------------------------------
// A. decay objective — scalar-templated vs the real vectorized kernel
// --------------------------------------------------------------------------

/// Templated decay objective (AD-capable, scalar convolution only).
template <typename T>
T decay_objective_templated(const T* x, int n_par) {
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

/// Same objective but through tttrlib's real vectorized dispatcher.
/// This is the path central differences would keep and AD would lose.
double decay_objective_vectorized(double* x, int n_par) {
    const double dt = 0.05;
    const int numexp = n_par / 2 > 0 ? n_par / 2 : 1;
    std::vector<double> fit(NCH, 0.0);
    std::vector<double> spectrum(2 * numexp);
    for (int ne = 0; ne < numexp; ++ne) {
        spectrum[2 * ne] = x[(2 * ne) % n_par];
        spectrum[2 * ne + 1] = x[(2 * ne + 1) % n_par] + 1e-3;
    }
    fconv_simd(fit.data(), spectrum.data(), g_irf.data(), numexp, 1, NCH, dt);

    double sum = 0.0;
    for (int i = 0; i < NCH; ++i) sum += fit[i];
    double chi = 0.0;
    for (int i = 0; i < NCH; ++i) {
        const double m = fit[i] / sum * 1.0e5 + 1e-9;
        const double d = g_data[i];
        chi += m - d + (d > 0.0 ? d * std::log(d / m) : 0.0);
    }
    return 2.0 * chi;
}

// --------------------------------------------------------------------------
// B. ImageLocalization's real objective — 2D Gaussian PSF + Poisson W2DG
// --------------------------------------------------------------------------

static int g_xlen = 15, g_ylen = 15;
static std::vector<double> g_img;

/// One 2D Gaussian, mirroring localization::model2DGaussian.
template <typename T>
void model_2d_gaussian(const T* v, std::vector<T>& model, bool add_bg) {
    const T x0 = v[0], y0 = v[1], A = v[2], sigma = v[3], eps = v[4];
    const T bg = add_bg ? v[5] : T(0.0);
    const T tx = T(0.5) / (sigma * sigma);
    const T ty = T(0.5) / (sigma * sigma * eps * eps);
    std::vector<T> ex(g_xlen);
    for (int x = 0; x < g_xlen; ++x) {
        const T d = T(double(x)) - x0;
        ex[x] = exp(-d * d * tx);
    }
    int i = 0;
    for (int y = 0; y < g_ylen; ++y) {
        const T dy = T(double(y)) - y0;
        const T ey = exp(-dy * dy * ty);
        for (int x = 0; x < g_xlen; ++x) {
            model[i] = A * ex[x] * ey + bg;
            ++i;
        }
    }
}

/// Poisson maximum-likelihood-ratio, mirroring localization::W2DG.
template <typename T>
T w2dg(const std::vector<T>& model) {
    const int osize = g_xlen * g_ylen;
    T w = T(0.0);
    for (int i = 0; i < osize; ++i) {
        const double d = g_img[i];
        if (d > 1e-12) w += model[i] - d * log(model[i]);
        else w += model[i];
    }
    return w / double(osize);
}

/// n_gauss = 1, 2 or 3 -> 6, 9 or 12 free parameters, as ImageLocalization sets up.
template <typename T, int NGAUSS>
T gauss_objective(const T* v, int /*n_par*/) {
    const int osize = g_xlen * g_ylen;
    std::vector<T> model(osize), tmp(osize);
    model_2d_gaussian<T>(v, model, true);
    for (int g = 1; g < NGAUSS; ++g) {
        // secondary Gaussians reuse sigma/eps and carry no background
        T sub[6] = {v[3 * g + 3], v[3 * g + 4], v[3 * g + 5], v[3], v[4], T(0.0)};
        model_2d_gaussian<T>(sub, tmp, false);
        for (int i = 0; i < osize; ++i) model[i] += tmp[i];
    }
    return w2dg<T>(model);
}

// --------------------------------------------------------------------------
// harness
// --------------------------------------------------------------------------

template <typename F>
double timeit(F&& fn, int reps) {
    fn();
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

template <typename Obj>
void grad_central(Obj&& f, std::vector<double>& x, int n, double eps, std::vector<double>& g) {
    for (int j = 0; j < n; ++j) {
        const double t = x[j];
        const double h = (t != 0.0) ? eps * std::fabs(t) : eps;
        x[j] = t + h;
        const double f1 = f(x.data(), n);
        x[j] = t - h;
        const double f2 = f(x.data(), n);
        x[j] = t;
        g[j] = 0.5 * (f1 - f2) / h;
    }
}

template <int N, typename ObjT>
void grad_ad(ObjT&& f, const std::vector<double>& x, std::vector<double>& g) {
    using Arr = tttrlib::GradVec<N>;
    using DualN = autodiff::detail::Dual<double, Arr>;
    std::vector<DualN> xd(N);
    for (int j = 0; j < N; ++j) {
        xd[j].val = x[j];
        xd[j].grad = Arr::Unit(j);
    }
    const DualN r = f(xd.data(), N);
    for (int j = 0; j < N; ++j) g[j] = r.grad[j];
}

template <int N>
void decay_case() {
    std::vector<double> x(N), g(N);
    for (int j = 0; j < N; ++j) x[j] = 0.5 + 0.1 * j;
    const double eps_opt = std::cbrt(2.22e-16);

    const double t_scalar = timeit([&] { decay_objective_templated<double>(x.data(), N); }, 200);
    const double t_vec = timeit([&] { decay_objective_vectorized(x.data(), N); }, 200);
    const double t_cd_scalar = timeit(
        [&] { grad_central([](double* p, int n) { return decay_objective_templated<double>(p, n); },
                           x, N, eps_opt, g); }, 50);
    const double t_cd_vec = timeit(
        [&] { grad_central([](double* p, int n) { return decay_objective_vectorized(p, n); },
                           x, N, eps_opt, g); }, 50);
    const double t_ad = timeit(
        [&] { grad_ad<N>([](auto* p, int n) {
                  using T = typename std::remove_reference<decltype(*p)>::type;
                  return decay_objective_templated<T>(p, n); }, x, g); }, 50);

    printf("  N=%-3d (%d exp) | obj scalar %6.4f ms, vectorized %6.4f ms (%.2fx)"
           " | CD-scalar %6.3f ms | CD-vectorized %6.3f ms | AD %6.3f ms"
           " || AD vs CD-scalar %5.2fx | AD vs CD-VECTORIZED %5.2fx\n",
           N, N / 2, t_scalar * 1e3, t_vec * 1e3, t_scalar / t_vec,
           t_cd_scalar * 1e3, t_cd_vec * 1e3, t_ad * 1e3,
           t_cd_scalar / t_ad, t_cd_vec / t_ad);
}

template <int NGAUSS, int N>
void gauss_case() {
    std::vector<double> x(N), g(N), gref(N);
    // plausible start: centre, amplitude, sigma, eccentricity, background
    const double base[6] = {7.0, 7.0, 100.0, 2.0, 1.0, 5.0};
    for (int j = 0; j < N; ++j) x[j] = (j < 6) ? base[j] : 6.0 + 0.5 * j;
    const double eps_now = 1e-6, eps_opt = std::cbrt(2.22e-16);

    auto obj_d = [](double* p, int n) { return gauss_objective<double, NGAUSS>(p, n); };
    const double t_obj = timeit([&] { obj_d(x.data(), N); }, 500);
    const double t_cd_now = timeit([&] { grad_central(obj_d, x, N, eps_now, g); }, 200);
    const double t_cd_opt = timeit([&] { grad_central(obj_d, x, N, eps_opt, g); }, 200);
    const double t_ad = timeit([&] { grad_ad<N>([](auto* p, int n) {
        using T = typename std::remove_reference<decltype(*p)>::type;
        return gauss_objective<T, NGAUSS>(p, n); }, x, g); }, 200);

    grad_ad<N>([](auto* p, int n) {
        using T = typename std::remove_reference<decltype(*p)>::type;
        return gauss_objective<T, NGAUSS>(p, n); }, x, gref);
    grad_central(obj_d, x, N, eps_opt, g);
    double worst = 0.0;
    for (int j = 0; j < N; ++j) {
        const double denom = std::fabs(gref[j]) > 1e-30 ? std::fabs(gref[j]) : 1.0;
        worst = std::max(worst, std::fabs(g[j] - gref[j]) / denom);
    }

    printf("  %d gauss, N=%-2d | obj %7.5f ms | CD(now) %5.2fx | CD(tuned) %5.2fx err=%.1e"
           " | AD %5.2fx exact || AD speedup %5.2fx\n",
           NGAUSS, N, t_obj * 1e3, t_cd_now / t_obj, t_cd_opt / t_obj, worst,
           t_ad / t_obj, t_cd_opt / t_ad);
}

int main() {
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

    g_img.resize(g_xlen * g_ylen);
    for (int y = 0; y < g_ylen; ++y)
        for (int x = 0; x < g_xlen; ++x) {
            const double dx = x - 7.2, dy = y - 6.8;
            g_img[y * g_xlen + x] = 100.0 * std::exp(-(dx * dx + dy * dy) / (2 * 2.0 * 2.0)) + 5.0;
        }

    printf("A. Decay objective: does AD still win when CD keeps the vectorized kernel?\n");
    printf("   (fconv_simd dispatches to NEON on AArch64, AVX+FMA on x86)\n");
    decay_case<4>();
    decay_case<8>();
    decay_case<16>();

    printf("\nB. ImageLocalization's REAL objective: 2D Gaussian PSF + Poisson W2DG.\n");
    printf("   No convolution kernel involved; free-parameter count is 6/9/12, not 18.\n");
    gauss_case<1, 6>();
    gauss_case<2, 9>();
    gauss_case<3, 12>();
    return 0;
}

// SPDX-License-Identifier: BSD-3-Clause
//
// Head-to-head for the derivative carrier of the vectorized forward-mode
// gradient: `tttrlib::GradVec<N>` (modules/math, std-only) against
// `Eigen::Array<double, N, 1>`, which it replaced.
//
// This exists because of the rule in AGENTS.md: replacing a third-party
// numerical dependency with a hand-rolled one is only allowed if the
// replacement is measured, not assumed. The claim under test is narrow -- that
// N fixed-length double loops vectorize as well as Eigen's fixed-size array
// expressions do, at the N the localization fit actually uses.
//
// The objective is ImageLocalization's: the logistic/exponential
// reparameterisation, one to three 2D Gaussians, and a Poisson
// maximum-likelihood cost. N is 6 / 9 / 12 free parameters for one / two /
// three Gaussians -- not 18; entries 12..17 of `vars` are flags and outputs and
// i_lbfgs minimises in the reduced free space.
//
// Build (from the repository root):
//
//   c++ -std=c++17 -O3 -I modules/math/include -I thirdparty \
//       -DHAVE_EIGEN -I "$CONDA_PREFIX/include/eigen3" \
//       benchmarks/bench_gradvec.cpp -o /tmp/bench_gradvec && /tmp/bench_gradvec
//
// Drop -DHAVE_EIGEN and the Eigen include to build it without Eigen present;
// the GradVec column still runs, which is the point -- the library no longer
// needs Eigen to be installed at all.

#include "GradVec.h"

#include <autodiff/forward/dual.hpp>

#ifdef HAVE_EIGEN
#include <Eigen/Core>
#endif

#include <cmath>
#include <cstdio>
#include <ctime>
#include <vector>

/// Thread CPU time, not wall clock.
///
/// The two carriers differ by tens of percent; a shared development machine
/// under load moves a wall-clock burst by a factor of ten. `steady_clock` keeps
/// counting while this thread is descheduled, so it reports the scheduler with
/// a numerical-kernel label on it -- measured here, the same binary produced
/// speedups from 0.22 to 4.77 across consecutive runs. CLOCK_THREAD_CPUTIME_ID
/// counts only cycles this thread was actually given, which is the quantity a
/// kernel comparison wants. Frequency scaling and P-core/E-core placement still
/// leak in, which is what the interleaving and the minimum below are for.
static double cpu_ms() {
    timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec * 1e-6;
}

namespace autodiff {
namespace detail {
template <int N>
struct NumberTraits<tttrlib::GradVec<N>> {
    using NumericType = double;
    static constexpr auto Order = 0;
};
#ifdef HAVE_EIGEN
template <int N>
struct NumberTraits<Eigen::Array<double, N, 1>> {
    using NumericType = double;
    static constexpr auto Order = 0;
};
#endif
}  // namespace detail
}  // namespace autodiff

static const int XLEN = 13;
static const int YLEN = 13;
static std::vector<double> g_data;

static void make_data() {
    g_data.resize(XLEN * YLEN);
    for (int y = 0; y < YLEN; ++y)
        for (int x = 0; x < XLEN; ++x) {
            const double dx = x - 6.2, dy = y - 5.9;
            g_data[y * XLEN + x] =
                std::floor(120.0 * std::exp(-(dx * dx + dy * dy) / 4.0) + 3.0);
        }
}

template <typename T>
static void model_gauss(const T& x0, const T& y0, const T& A, const T& sigma,
                        const T& eps, const T& bg, T* model, bool accumulate) {
    const T tx = T(0.5) / (sigma * sigma);
    const T ty = T(0.5) / (sigma * sigma * eps * eps);
    std::vector<T> ex(XLEN);
    for (int x = 0; x < XLEN; ++x) {
        const T d = T(double(x)) - x0;
        ex[x] = exp(-d * d * tx);
    }
    int i = 0;
    for (int y = 0; y < YLEN; ++y) {
        const T dy = T(double(y)) - y0;
        const T ey = exp(-dy * dy * ty);
        for (int x = 0; x < XLEN; ++x) {
            const T v = A * ex[x] * ey + bg;
            if (accumulate) model[i] += v;
            else model[i] = v;
            ++i;
        }
    }
}

/// n_gauss is a template parameter so the free-parameter count N and the model
/// stay consistent, and so nothing is branchy in the inner loop.
template <typename T, int NGAUSS>
static T cost(const T* u) {
    const int pos_idx[6] = {0, 1, 6, 7, 9, 10};
    const double pos_len[6] = {XLEN, YLEN, XLEN, YLEN, XLEN, YLEN};
    const int np = 3 * NGAUSS + 3;

    T c[12];
    for (int k = 0; k < np; ++k) c[k] = u[k];
    for (int k = 0; k < 2 * NGAUSS; ++k) {
        const int i = pos_idx[k];
        c[i] = T(pos_len[k]) / (T(1.0) + exp(-u[i]));
    }
    c[3] = exp(u[3]);
    c[4] = exp(u[4]);
    c[5] = exp(u[5]);

    const int osize = XLEN * YLEN;
    std::vector<T> model(osize);
    model_gauss<T>(c[0], c[1], c[2], c[3], c[4], c[5], model.data(), false);
    if (NGAUSS >= 2)
        model_gauss<T>(c[6], c[7], c[8], c[3], c[4], T(0.0), model.data(), true);
    if (NGAUSS >= 3)
        model_gauss<T>(c[9], c[10], c[11], c[3], c[4], T(0.0), model.data(), true);

    T w = T(0.0);
    for (int i = 0; i < osize; ++i) {
        if (g_data[i] > 1e-12) w += model[i] - g_data[i] * log(model[i]);
        else w += model[i];
    }
    return w / double(osize);
}

/// Start point: an interior, asymmetric point of the unconstrained space.
static void seed_point(double* u, int np) {
    const double base[12] = {0.31, -0.22, 90.0, std::log(1.6), std::log(1.05),
                             std::log(3.0), -0.7, 0.45, 20.0, 0.9, -1.3, 11.0};
    for (int j = 0; j < np; ++j) u[j] = base[j];
}

/// One timed burst of `reps` gradients.
template <typename Arr, int NGAUSS>
static double time_gradient(int reps, double* checksum) {
    const int NP = 3 * NGAUSS + 3;
    using DualN = autodiff::detail::Dual<double, Arr>;

    double u[12];
    seed_point(u, NP);

    double acc = 0.0;
    const double t0 = cpu_ms();
    for (int r = 0; r < reps; ++r) {
        DualN ud[12];
        for (int j = 0; j < NP; ++j) {
            ud[j].val = u[j] + 1e-9 * r;  // defeat hoisting out of the loop
            ud[j].grad = 0.0;
            ud[j].grad[j] = 1.0;
        }
        const DualN res = cost<DualN, NGAUSS>(ud);
        for (int j = 0; j < NP; ++j) acc += res.grad[j];
    }
    const double t1 = cpu_ms();
    *checksum = acc;
    return (t1 - t0) / reps;
}

/// Minimum over TRIALS interleaved bursts, not the mean -- see cpu_ms().
static const int TRIALS = 9;

template <int NGAUSS>
static void row(int reps) {
    const int NP = 3 * NGAUSS + 3;
    double best_g = 1e300, best_e = 1e300;
    double sum_g = 0.0, sum_e = 0.0;

    for (int t = 0; t < TRIALS; ++t) {
#ifdef HAVE_EIGEN
        const double t_e =
            time_gradient<Eigen::Array<double, 3 * NGAUSS + 3, 1>, NGAUSS>(reps, &sum_e);
        if (t_e < best_e) best_e = t_e;
#endif
        const double t_g =
            time_gradient<tttrlib::GradVec<3 * NGAUSS + 3>, NGAUSS>(reps, &sum_g);
        if (t_g < best_g) best_g = t_g;
    }

#ifdef HAVE_EIGEN
    // Same numbers, or the timing comparison is meaningless.
    const double rel = std::fabs(sum_g - sum_e) / std::max(std::fabs(sum_e), 1e-30);
    std::printf("%-8d %-6d %12.5f %12.5f %8.2f   %s\n", NGAUSS, NP, best_e, best_g,
                best_e / best_g, rel < 1e-12 ? "yes" : "NO");
#else
    (void)sum_e;
    (void)best_e;
    std::printf("%-8d %-6d %12s %12.5f %8s   %s\n", NGAUSS, NP, "-", best_g, "-", "n/a");
#endif
}

int main() {
    make_data();

    std::printf("Vectorized forward-mode gradient of the 2D-Gaussian localization\n");
    std::printf("objective, %dx%d pixels. Cost of ONE full gradient (all N partials),\n",
                XLEN, YLEN);
    std::printf("best of %d interleaved trials.\n\n", TRIALS);
#ifdef HAVE_EIGEN
    std::printf("%-8s %-6s %12s %12s %8s   %s\n", "gauss", "N", "Eigen ms", "GradVec ms",
                "speedup", "match");
#else
    std::printf("(built without Eigen)\n");
    std::printf("%-8s %-6s %12s %12s %8s   %s\n", "gauss", "N", "Eigen ms", "GradVec ms",
                "speedup", "match");
#endif
    std::printf("---------------------------------------------------------------------\n");

    // Warm up, so the first row does not pay for page faults and branch training.
    double warm = 0.0;
    time_gradient<tttrlib::GradVec<6>, 1>(200, &warm);

    row<1>(4000);
    row<2>(4000);
    row<3>(4000);
    return 0;
}

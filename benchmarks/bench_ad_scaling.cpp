// SPDX-License-Identifier: BSD-3-Clause
//
// How does the vectorized forward-mode gradient scale in the parameter count?
//
// The recorded numbers stop at N = 18. Every conversion decision so far has been
// taken in that regime, where AD beats central differences by 4-10x. A
// multi-exponential decay is not in that regime: 200 exponentials is N = 400
// free parameters, and nothing measured says the advantage survives there.
//
// Both methods grow with N, which is the trap -- "AD is 10x faster" is a
// statement about a parameter count, not about AD:
//
//   central differences   2N objective evaluations       -> O(N) * cost(f)
//   vectorized forward    1 pass, every scalar carrying
//                         an N-vector derivative         -> O(N) * cost(f)
//
// So the ratio is a race between two O(N) costs and the answer is decided by
// constants and by memory traffic, which is why it has to be measured rather
// than reasoned about. A Dual<GradVec<N>> is 8(N+1) bytes; at N = 400
// that is 3.2 kB per intermediate, and a 1024-channel model vector of them is
// 3.3 MB -- far outside any cache, while the central-difference path walks a
// plain 8 kB array 2N times.
//
// Build (from the repository root):
//
//   c++ -std=c++17 -O3 -I modules/math/include \
//       benchmarks/bench_ad_scaling.cpp -o /tmp/bench_ad_scaling && /tmp/bench_ad_scaling
//
// Timing uses CLOCK_THREAD_CPUTIME_ID and min-of-trials for the reason recorded
// in benchmarks/README.md: on a loaded machine wall clock reports the scheduler.

#include "Dual.h"
#include "GradVec.h"

#include <cmath>
#include <cstdio>
#include <ctime>
#include <vector>

using tttrlib::GradVec;

static double cpu_ms() {
    timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec * 1e-6;
}

static const int NCH = 1024;
static std::vector<double> g_irf, g_data;

static void make_data() {
    g_irf.resize(NCH);
    g_data.resize(NCH);
    for (int i = 0; i < NCH; ++i) {
        const double t = i * 0.05;
        g_irf[i] = 900.0 * std::exp(-(t - 2.0) * (t - 2.0) / 0.25) + 1.0;
        g_data[i] = 50.0 * std::exp(-t / 3.0) + 5.0;
    }
}

/// Multi-exponential decay + recursive IRF convolution + Poisson 2I*.
/// x is [a0, tau0, a1, tau1, ...]; numexp = n_par / 2.
template <typename T>
static T decay_objective(const T* x, int n_par) {
    const double dt = 0.05;
    const int numexp = n_par / 2;
    std::vector<T> fit(NCH, T(0.0));
    for (int ne = 0; ne < numexp; ++ne) {
        const T amp = x[2 * ne];
        const T expcurr = exp(-dt / x[2 * ne + 1]);
        T fitcurr = T(0.0);
        for (int i = 1; i < NCH; ++i) {
            fitcurr = (fitcurr + (dt * 0.5) * g_irf[i - 1]) * expcurr + (dt * 0.5) * g_irf[i];
            fit[i] += fitcurr * amp;
        }
    }
    T w = T(0.0);
    for (int i = 0; i < NCH; ++i) {
        const T m = fit[i] + T(1e-6);
        w += m - g_data[i] * log(m);
    }
    return w / double(NCH);
}

static void seed(std::vector<double>& x, int n_par) {
    x.assign(n_par, 0.0);
    for (int ne = 0; ne < n_par / 2; ++ne) {
        x[2 * ne] = 0.5 + 0.01 * ne;
        x[2 * ne + 1] = 0.5 + 0.05 * ne;   // distinct, positive lifetimes
    }
}

static const int TRIALS = 5;

template <int N>
static void row() {
    std::vector<double> x;
    seed(x, N);

    // 1. the objective alone -- the unit everything else is quoted in
    double best_f = 1e300, sink = 0.0;
    for (int t = 0; t < TRIALS; ++t) {
        const double t0 = cpu_ms();
        for (int r = 0; r < 3; ++r) sink += decay_objective<double>(x.data(), N);
        const double e = (cpu_ms() - t0) / 3;
        if (e < best_f) best_f = e;
    }

    // 2. central differences: 2N objective evaluations
    double best_cd = 1e300;
    for (int t = 0; t < TRIALS; ++t) {
        const double t0 = cpu_ms();
        std::vector<double> xx = x;
        const double h13 = std::cbrt(2.220446049250313e-16);
        for (int j = 0; j < N; ++j) {
            const double h = h13 * std::fabs(x[j]);
            xx[j] = x[j] + h;
            sink += decay_objective<double>(xx.data(), N);
            xx[j] = x[j] - h;
            sink += decay_objective<double>(xx.data(), N);
            xx[j] = x[j];
        }
        const double e = cpu_ms() - t0;
        if (e < best_cd) best_cd = e;
    }

    // 3. vectorized forward AD: one pass, all N partials
    using Arr = GradVec<N>;
    using DualN = tttrlib::Dual<Arr>;
    double best_ad = 1e300;
    for (int t = 0; t < TRIALS; ++t) {
        const double t0 = cpu_ms();
        std::vector<DualN> xd(N);
        for (int j = 0; j < N; ++j) {
            xd[j] = DualN(x[j], Arr::Unit(j));
        }
        const DualN r = decay_objective<DualN>(xd.data(), N);
        const double e = cpu_ms() - t0;
        sink += r.val;
        if (e < best_ad) best_ad = e;
    }

    const double bytes_dual = double(sizeof(DualN));
    const double mb_model = bytes_dual * NCH / (1024.0 * 1024.0);
    std::printf("%6d %6d %11.4f %11.3f %8.1fx %11.3f %8.1fx %9.2fx %9.0f %9.2f\n",
                N / 2, N, best_f, best_cd, best_cd / best_f, best_ad, best_ad / best_f,
                best_cd / best_ad, bytes_dual, mb_model);
    if (sink == 12345.6789) std::printf("");  // keep the optimiser honest
}

int main() {
    make_data();
    std::printf("Vectorized forward AD vs tuned central differences, %d-channel\n", NCH);
    std::printf("multi-exponential decay. Cost of ONE gradient, best of %d.\n\n", TRIALS);
    std::printf("%6s %6s %11s %11s %9s %11s %9s %9s %9s %9s\n",
                "n_exp", "N", "obj ms", "CD ms", "vs obj", "AD ms", "vs obj",
                "AD gain", "B/dual", "MB/model");
    std::printf("---------------------------------------------------------------"
                "------------------------------------\n");
    row<4>();
    row<8>();
    row<16>();
    row<32>();
    row<64>();
    row<128>();
    row<256>();
    row<400>();
    std::printf("\n'AD gain' is CD/AD: above 1 AD wins. 'MB/model' is one "
                "1024-channel\nintermediate of duals -- the working set AD adds.\n");
    return 0;
}

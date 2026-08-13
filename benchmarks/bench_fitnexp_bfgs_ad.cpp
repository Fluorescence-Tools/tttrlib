// SPDX-License-Identifier: BSD-3-Clause
//
// PROTOTYPE. Does NOT touch DecayFitNExp.cpp -- the shipped path stays
// exactly Brent (coordinate-wise, multistart) + EM (amplitude profiling).
//
// Investigates the request behind this file: "re-evaluate FitNExp with bfgs
// and AD." DecayFitNExp.cpp deliberately never constructs a `bfgs`
// (PRD-010 Phase 5e) -- amplitudes are profiled by EM (closed-form given
// fixed lifetimes, exploiting that they enter the model linearly), and
// lifetimes are updated one coordinate at a time via a Brent search that is
// explicitly multistart-aware ("a profiled mixture likelihood need not be
// unimodal in one lifetime", DecayFitNExp.cpp:614-616). A generic joint
// optimizer has to reproduce both properties to be competitive; this
// prototype does, using the envelope theorem rather than reimplementing
// them from scratch:
//
//   - Amplitudes are still profiled by the *same* EM idea (reimplemented
//     here in plain double -- profile_weights()), at whatever lifetimes the
//     line search is currently trying.
//   - The AD gradient is taken with those amplitudes held CONSTANT. This is
//     exact, not an approximation: at the EM optimum d(NLL)/d(weight) = 0,
//     so by the envelope theorem d/d(tau)[profiled NLL] = the partial
//     derivative of NLL(tau, weights) holding weights fixed at their
//     EM-converged values -- the term through weights' own dependence on
//     tau vanishes identically. No need to differentiate through the EM
//     iteration itself.
//   - Multistart robustness is not reimplemented; instead this compares two
//     starting regimes: (a) refining the *actual* Brent+EM answer (tests
//     whether joint optimization finds something coordinate descent's
//     one-at-a-time updates missed) and (b) starting from a naive guess
//     (tests whether AD alone, with no multistart, is basin-robust enough to
//     stand in for Brent+EM's grid scan).
//
// Uses fconv_per_cs_ad (DecayConvolution.h) and Dual<GradVec<N>> (Dual.h/
// GradVec.h) -- the same production AD machinery DecayFit23 ships with.
//
// Build (from the repository root, against the already-built library):
//   c++ -std=c++17 -O3 -I modules/math/include -I modules/spectroscopy/decay/include \
//       benchmarks/bench_fitnexp_bfgs_ad.cpp -L build_new -ltttrlib \
//       -Wl,-rpath,build_new -o /tmp/bench_fitnexp_bfgs_ad && /tmp/bench_fitnexp_bfgs_ad

#include "Dual.h"
#include "GradVec.h"
#include "DecayConvolution.h"
#include "DecayFitNExp.h"
#include "i_lbfgs.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <random>
#include <vector>

namespace {

constexpr double kProbabilityFloor = 1.0e-300;
constexpr double kWeightFloor = 1.0e-15;

/// Fills `out[0..n_bins)` with the IRF-convolved, area-normalized decay shape
/// for lifetime `tau`. Templated so the same body serves the plain-double
/// objective and the AD gradient -- mirrors DecayFitNExp.cpp's
/// fill_component(), reusing the production fconv_per_cs_ad kernel rather
/// than a second convolution implementation.
template <typename T>
void fill_component(T* out, int n_bins, const T& tau, const double* irf,
                    double dt, double period, int conv_stop) {
    T spectrum[2] = {T(1.0), tau};
    fconv_per_cs_ad(out, spectrum, irf, 1, n_bins - 1, n_bins, period, conv_stop, dt);
    T sum(0.0);
    for (int i = 0; i < n_bins; ++i) {
        if (!(out[i] > 0.0)) out[i] = T(0.0);
        sum += out[i];
    }
    if (sum > 0.0) {
        for (int i = 0; i < n_bins; ++i) out[i] = out[i] / sum;
    }
}

/// Profiled negative log-likelihood, `weights` held constant -- see the file
/// header for why that is exact under AD, not an approximation.
template <typename T>
T evaluate_nll(const T* tau, int n_exp, const double* weights,
              const double* counts, const double* irf, int n_bins,
              double dt, double period, int conv_stop) {
    std::vector<std::vector<T>> components(n_exp, std::vector<T>(n_bins));
    for (int k = 0; k < n_exp; ++k)
        fill_component(components[k].data(), n_bins, tau[k], irf, dt, period, conv_stop);

    std::vector<T> prob(n_bins, T(0.0));
    for (int k = 0; k < n_exp; ++k)
        for (int i = 0; i < n_bins; ++i)
            prob[i] += T(weights[k]) * components[k][i];

    T nll(0.0);
    for (int i = 0; i < n_bins; ++i) {
        if (counts[i] > 0.0) {
            T p = (prob[i] > kProbabilityFloor) ? prob[i] : T(kProbabilityFloor);
            nll -= T(counts[i]) * log(p);
        }
    }
    return nll;
}

/// EM amplitude profiling in plain double, at fixed lifetimes -- a direct
/// reimplementation of DecayFitNExp.cpp's profile_amplitudes() (anonymous
/// namespace there, not linkable from here). Same algorithm, same floors.
void profile_weights(const double* tau, int n_exp, const double* counts,
                     const double* irf, int n_bins, double dt, double period,
                     int conv_stop, int max_em_iterations, double em_tolerance,
                     double* weights /* in: seed, out: profiled */) {
    std::vector<std::vector<double>> components(n_exp, std::vector<double>(n_bins));
    for (int k = 0; k < n_exp; ++k)
        fill_component(components[k].data(), n_bins, tau[k], irf, dt, period, conv_stop);

    const double photons = std::accumulate(counts, counts + n_bins, 0.0);
    if (!(photons > 0.0)) return;

    std::vector<double> prob(n_bins), next(n_exp);
    for (int iteration = 0; iteration < max_em_iterations; ++iteration) {
        std::fill(prob.begin(), prob.end(), 0.0);
        for (int k = 0; k < n_exp; ++k)
            for (int i = 0; i < n_bins; ++i)
                prob[i] += weights[k] * components[k][i];

        std::fill(next.begin(), next.end(), 0.0);
        for (int k = 0; k < n_exp; ++k) {
            double responsibility = 0.0;
            for (int i = 0; i < n_bins; ++i) {
                if (counts[i] <= 0.0) continue;
                const double p = std::max(prob[i], kProbabilityFloor);
                responsibility += counts[i] * components[k][i] / p;
            }
            next[k] = weights[k] * responsibility / photons;
        }
        double sum = 0.0;
        for (double& w : next) { w = std::max(w, kWeightFloor); sum += w; }
        for (double& w : next) w /= sum;

        double largest_change = 0.0;
        for (int k = 0; k < n_exp; ++k)
            largest_change = std::max(largest_change, std::fabs(next[k] - weights[k]));
        std::copy(next.begin(), next.end(), weights);
        if (largest_change <= em_tolerance) break;
    }
}

/// Everything the bfgs callbacks need, threaded through i_lbfgs's void*.
struct JointContext {
    int n_exp;
    const double* counts;
    const double* irf;
    int n_bins;
    double dt, period;
    int conv_stop;
    int max_em_iterations;
    double em_tolerance;
    std::vector<double> weights;  // profiled at the current x on every call
};

double joint_target(double* x, void* pv) {
    auto* ctx = static_cast<JointContext*>(pv);
    profile_weights(x, ctx->n_exp, ctx->counts, ctx->irf, ctx->n_bins, ctx->dt,
                    ctx->period, ctx->conv_stop, ctx->max_em_iterations,
                    ctx->em_tolerance, ctx->weights.data());
    return evaluate_nll<double>(x, ctx->n_exp, ctx->weights.data(), ctx->counts,
                                ctx->irf, ctx->n_bins, ctx->dt, ctx->period,
                                ctx->conv_stop);
}

template <int N>
double joint_gradient(double* x, double* grad_out, void* pv) {
    auto* ctx = static_cast<JointContext*>(pv);
    profile_weights(x, N, ctx->counts, ctx->irf, ctx->n_bins, ctx->dt,
                    ctx->period, ctx->conv_stop, ctx->max_em_iterations,
                    ctx->em_tolerance, ctx->weights.data());

    using Grad = tttrlib::GradVec<N>;
    using D = tttrlib::Dual<Grad>;
    D tau_d[N];
    for (int k = 0; k < N; ++k) tau_d[k] = D(x[k], Grad::Unit(k));
    const D r = evaluate_nll<D>(tau_d, N, ctx->weights.data(), ctx->counts,
                                ctx->irf, ctx->n_bins, ctx->dt, ctx->period,
                                ctx->conv_stop);
    for (int k = 0; k < N; ++k) grad_out[k] = r.grad[k];
    return r.val;
}

double cpu_ms() {
    timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec * 1e-6;
}

template <typename F>
double timeit_ms(F&& fn, int reps) {
    fn();
    double best = 1e300;
    for (int r = 0; r < reps; ++r) {
        const double t0 = cpu_ms();
        fn();
        best = std::min(best, cpu_ms() - t0);
    }
    return best;
}

template <int N>
struct JointResult {
    std::vector<double> tau, weights;
    double nll;
    int info;
};

template <int N>
JointResult<N> run_joint(const std::vector<double>& start,
                         const double* counts, const double* irf, int n_bins,
                         double dt, double period, int conv_stop,
                         const std::vector<double>& seed_weights) {
    JointContext ctx{N, counts, irf, n_bins, dt, period, conv_stop, 500, 1e-10,
                     seed_weights};
    bfgs opt(joint_target, N);
    opt.set_gradient(joint_gradient<N>);
    opt.maxiter = 200;
    std::vector<double> x = start;
    const int info = opt.minimize(x.data(), &ctx);
    profile_weights(x.data(), N, counts, irf, n_bins, dt, period, conv_stop,
                    500, 1e-10, ctx.weights.data());
    const double nll = evaluate_nll<double>(x.data(), N, ctx.weights.data(),
                                            counts, irf, n_bins, dt, period,
                                            conv_stop);
    return {x, ctx.weights, nll, info};
}

/// A synthetic multi-exponential decay with Poisson-simulated counts, and the
/// IRF it was convolved against -- deterministic (fixed seed) so comparisons
/// are reproducible.
struct Case {
    const char* name;
    std::vector<double> true_tau, true_amp;
    int n_bins;
    double dt, period;
};

void run_case(const Case& c) {
    std::printf("\n=== %s ===  true tau = [", c.name);
    for (double t : c.true_tau) std::printf("%.3f ", t);
    std::printf("]\n");

    std::vector<double> irf(c.n_bins, 0.0);
    for (int i = 0; i < c.n_bins; ++i) {
        const double t = (i - 6.0) / 1.5;
        irf[i] = std::exp(-0.5 * t * t);
    }
    const int conv_stop = c.n_bins - 1;

    std::vector<double> shape(c.n_bins, 0.0);
    for (std::size_t k = 0; k < c.true_tau.size(); ++k) {
        std::vector<double> component(c.n_bins);
        fill_component(component.data(), c.n_bins, c.true_tau[k], irf.data(),
                       c.dt, c.period, conv_stop);
        for (int i = 0; i < c.n_bins; ++i) shape[i] += c.true_amp[k] * component[i];
    }
    std::mt19937 rng(20260813);
    std::vector<double> counts(c.n_bins);
    const double total_photons = 20000.0;
    for (int i = 0; i < c.n_bins; ++i) {
        std::poisson_distribution<int> pois(shape[i] * total_photons);
        counts[i] = static_cast<double>(pois(rng));
    }

    // Baseline: the REAL shipped fitter (Brent, multistart, EM).
    DecayFitNExpOptions options;
    options.dt = c.dt;
    options.period = c.period;
    options.tau_min = 1.0e-3;
    options.tau_max = 50.0;
    std::vector<double> guess_tau(c.true_tau.size()), guess_amp(c.true_tau.size(), 1.0);
    for (std::size_t k = 0; k < guess_tau.size(); ++k)
        guess_tau[k] = 1.0 + 0.7 * k;  // deliberately naive, not the truth
    std::vector<int> fixed(c.true_tau.size(), 0);

    const auto brent_result = [&] {
        return DecayFitNExp::fit(counts, irf, {}, guess_tau, guess_amp, fixed, options);
    };
    DecayFitNExpResult baseline = brent_result();
    const double t_brent = timeit_ms([&] { brent_result(); }, 20);

    std::printf("Brent+EM (shipped):  tau = [");
    for (double t : baseline.lifetimes) std::printf("%.4f ", t);
    std::printf("]  NLL = %.6f  %.3f ms\n", baseline.negative_log_likelihood, t_brent);

    // Seed weights for the joint runs: uniform over components (EM finds its
    // own way from there regardless, same as the shipped path's seeding).
    std::vector<double> seed_weights(c.true_tau.size(), 1.0 / c.true_tau.size());

    const int n = static_cast<int>(c.true_tau.size());
    if (n == 2) {
        // (a) refine the Brent+EM answer jointly.
        const double t0 = cpu_ms();
        auto refined = run_joint<2>(baseline.lifetimes, counts.data(), irf.data(),
                                    c.n_bins, c.dt, c.period, conv_stop, seed_weights);
        const double t_refine = cpu_ms() - t0;
        std::printf("bfgs+AD refine from Brent+EM: tau = [%.4f %.4f]  NLL = %.6f"
                   "  info=%d  %.3f ms\n",
                   refined.tau[0], refined.tau[1], refined.nll, refined.info, t_refine);

        // (b) joint search from the same naive start Brent+EM used -- no
        // multistart at all, tests basin-robustness on its own.
        const double t1 = cpu_ms();
        auto naive = run_joint<2>(guess_tau, counts.data(), irf.data(), c.n_bins,
                                  c.dt, c.period, conv_stop, seed_weights);
        const double t_naive = cpu_ms() - t1;
        std::printf("bfgs+AD from naive start:    tau = [%.4f %.4f]  NLL = %.6f"
                   "  info=%d  %.3f ms\n",
                   naive.tau[0], naive.tau[1], naive.nll, naive.info, t_naive);
    } else if (n == 3) {
        const double t0 = cpu_ms();
        auto refined = run_joint<3>(baseline.lifetimes, counts.data(), irf.data(),
                                    c.n_bins, c.dt, c.period, conv_stop, seed_weights);
        const double t_refine = cpu_ms() - t0;
        std::printf("bfgs+AD refine from Brent+EM: tau = [%.4f %.4f %.4f]  NLL = %.6f"
                   "  info=%d  %.3f ms\n",
                   refined.tau[0], refined.tau[1], refined.tau[2], refined.nll,
                   refined.info, t_refine);

        std::vector<double> naive_start = guess_tau;
        const double t1 = cpu_ms();
        auto naive = run_joint<3>(naive_start, counts.data(), irf.data(), c.n_bins,
                                  c.dt, c.period, conv_stop, seed_weights);
        const double t_naive = cpu_ms() - t1;
        std::printf("bfgs+AD from naive start:    tau = [%.4f %.4f %.4f]  NLL = %.6f"
                   "  info=%d  %.3f ms\n",
                   naive.tau[0], naive.tau[1], naive.tau[2], naive.nll,
                   naive.info, t_naive);
    }
}

}  // namespace

/// At-scale check: is the refinement pass actually cheap against a *batched*
/// Brent+EM baseline at realistic per-curve photon counts, not just cheap
/// against one cold single-curve call?
///
/// PERF.md's headline "Per-pixel reconvolution MLE (CPU) 140ms / 6.3x vs GPU"
/// number turns out NOT to exercise this at all: bench_tttrlib.py's fit_map
/// call uses `fixed=[1]` (ext/python/FitNExpWrapper.py:91 -- "1 holds the
/// lifetime fixed"), so that benchmark profiles amplitudes at a FIXED
/// reference tau per pixel and never runs Brent's lifetime search. The
/// relevant "at scale, free lifetime" comparison is the *other* headline
/// number instead -- bench_tttrlib.py's bench_fit_curve() batched
/// `fit_many`/`fit_batch_flat` path (n_bins=256, dt=0.05, tau free) -- so this
/// mirrors those parameters as closely as a synthetic case can, extended to
/// N=2 (fit_curve's own case is N=1, where there is no cross-lifetime
/// correlation for a joint step to recover -- see the case results above).
void run_at_scale(int n_rows) {
    std::printf("\n=== at scale: batched 2-exp fit, %d rows (bench_tttrlib.py's "
               "n_bins/dt/tau-range, moderate per-curve photon count) ===\n", n_rows);
    std::printf("(diagnostic: single-row timing first, before committing to the full batch)\n");

    const int n_bins = 256;
    const double dt = 0.05, period = n_bins * dt;
    const int conv_stop = n_bins - 1;
    std::vector<double> irf(n_bins);
    for (int i = 0; i < n_bins; ++i) {
        const double t = (i - 20.0) / 2.0;
        irf[i] = std::exp(-0.5 * t * t);
    }
    const double s = std::accumulate(irf.begin(), irf.end(), 0.0);
    for (double& v : irf) v /= s;

    const std::vector<double> true_tau = {1.2, 3.5};
    const std::vector<double> true_amp = {0.5, 0.5};
    std::vector<double> shape(n_bins, 0.0);
    for (std::size_t k = 0; k < true_tau.size(); ++k) {
        std::vector<double> component(n_bins);
        fill_component(component.data(), n_bins, true_tau[k], irf.data(), dt,
                       period, conv_stop);
        for (int i = 0; i < n_bins; ++i) shape[i] += true_amp[k] * component[i];
    }

    std::mt19937 rng(20260813);
    const double photons_per_row = 2000.0;  // a realistic per-pixel/per-burst count
    std::vector<double> matrix(static_cast<std::size_t>(n_rows) * n_bins);
    for (int r = 0; r < n_rows; ++r) {
        for (int i = 0; i < n_bins; ++i) {
            std::poisson_distribution<int> pois(shape[i] * photons_per_row);
            matrix[static_cast<std::size_t>(r) * n_bins + i] =
                    static_cast<double>(pois(rng));
        }
    }

    DecayFitNExpOptions options;
    options.dt = dt;
    options.period = period;
    options.tau_min = 0.2;
    options.tau_max = 8.0;
    const std::vector<double> initial_lifetimes = {1.0, 2.5};
    const std::vector<double> initial_amplitudes = {1.0, 1.0};
    const std::vector<int> fixed(2, 0);

    const auto run_batch = [&] {
        return DecayFitNExp::fit_batch_flat(matrix, static_cast<std::size_t>(n_rows),
                                            static_cast<std::size_t>(n_bins), irf,
                                            {}, initial_lifetimes, initial_amplitudes,
                                            fixed, options);
    };
    std::printf("running the first (untimed) fit_batch_flat call...\n");
    std::vector<double> rows = run_batch();
    std::printf("...done. Timing it now (1 warmup + 3 reps)...\n");
    const double t_batch = timeit_ms([&] { run_batch(); }, 3);
    std::printf("Brent+EM fit_batch_flat (real production entry point): %.2f ms total,"
               " %.5f ms/row\n", t_batch, t_batch / n_rows);

    const int row_width = 4 + 2 * 2;

    // Single-row diagnostic first: the earlier small cases (n_bins=128,
    // 20000 photons) showed 3-20ms/row for the refinement, but this uses
    // realistic parameters (n_bins=256, 2000 photons -- more EM iterations to
    // converge at lower counts, more heap traffic per call at more bins), so
    // check the real per-row cost here before committing CPU time to a full
    // n_rows loop that could be orders of magnitude slower than assumed.
    {
        const double* row0 = &rows[0];
        std::vector<double> tau0 = {row0[4], row0[5]};
        std::vector<double> w0 = {row0[6], row0[7]};
        double s0 = w0[0] + w0[1];
        if (s0 > 0.0) { w0[0] /= s0; w0[1] /= s0; } else w0 = {0.5, 0.5};
        const double t_one = timeit_ms([&] {
            run_joint<2>(tau0, matrix.data(), irf.data(), n_bins, dt, period,
                        conv_stop, w0);
        }, 5);
        std::printf("single-row refinement diagnostic: %.4f ms\n", t_one);
        const double projected_total = t_one * n_rows;
        std::printf("projected total for %d rows (serial, no threading): %.1f ms\n",
                   n_rows, projected_total);
        if (projected_total > 20000.0) {
            std::printf("projected cost is large -- capping the full loop below to "
                       "keep this benchmark finishing in reasonable time.\n");
            n_rows = std::max(50, static_cast<int>(15000.0 / t_one));
            std::printf("capped n_rows for the full loop: %d\n", n_rows);
        }
    }

    // Refinement pass over every row's result -- single-threaded here (the
    // point is per-row cost; parallelizing across rows is the same
    // embarrassingly-parallel structure fit_batch_flat itself already uses).
    const auto run_refine_all = [&] {
        for (int r = 0; r < n_rows; ++r) {
            const double* row = &rows[static_cast<std::size_t>(r) * row_width];
            std::vector<double> tau = {row[4], row[5]};
            std::vector<double> weights = {row[6], row[7]};
            double sum = weights[0] + weights[1];
            if (sum > 0.0) { weights[0] /= sum; weights[1] /= sum; }
            else { weights = {0.5, 0.5}; }
            run_joint<2>(tau, &matrix[static_cast<std::size_t>(r) * n_bins],
                        irf.data(), n_bins, dt, period, conv_stop, weights);
        }
    };
    const double t_refine = timeit_ms(run_refine_all, 1);
    std::printf("+ bfgs+AD refinement, all rows:                        %.2f ms total,"
               " %.5f ms/row\n", t_refine, t_refine / n_rows);
    std::printf("refinement overhead: %.1f%% of the batched Brent+EM cost\n",
               100.0 * t_refine / t_batch);

    int n_improved = 0;
    double total_nll_before = 0.0, total_nll_after = 0.0;
    for (int r = 0; r < n_rows; ++r) {
        const double* row = &rows[static_cast<std::size_t>(r) * row_width];
        std::vector<double> tau = {row[4], row[5]};
        std::vector<double> weights = {row[6], row[7]};
        double sum = weights[0] + weights[1];
        if (sum > 0.0) { weights[0] /= sum; weights[1] /= sum; } else weights = {0.5, 0.5};
        auto refined = run_joint<2>(tau, &matrix[static_cast<std::size_t>(r) * n_bins],
                                    irf.data(), n_bins, dt, period, conv_stop, weights);
        total_nll_before += row[0];
        total_nll_after += refined.nll;
        if (refined.nll < row[0] - 1e-9) ++n_improved;
    }
    std::printf("rows strictly improved by refinement: %d / %d (%.1f%%)\n",
               n_improved, n_rows, 100.0 * n_improved / n_rows);
    std::printf("mean NLL: %.6f -> %.6f\n", total_nll_before / n_rows,
               total_nll_after / n_rows);
}

int main() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);  // line-buffered: visible when piped/redirected
    std::printf("FitNExp: shipped Brent+EM vs prototype joint bfgs+AD (same VARPRO structure)\n");
    std::printf("info codes: -1 wrong params, 0 aborted, 1 EpsF, 2 EpsX, 4 EpsG, 5 iter limit\n");

    run_case({"2-exp, well separated", {0.5, 3.0}, {0.4, 0.6}, 128, 0.05, 8.0});
    run_case({"2-exp, close/correlated", {1.8, 2.2}, {0.5, 0.5}, 128, 0.05, 8.0});
    run_case({"3-exp", {0.4, 1.5, 4.0}, {0.3, 0.4, 0.3}, 128, 0.05, 8.0});

    run_at_scale(100);
    return 0;
}

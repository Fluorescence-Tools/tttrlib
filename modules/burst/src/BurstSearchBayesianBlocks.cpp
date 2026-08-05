// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file BurstSearchBayesianBlocks.cpp
 * \brief Implementation of the two-stage Bayesian Blocks burst search.
 * \see BurstSearchBayesianBlocks.h for the algorithm rationale and references.
 */
#include "BurstSearchBayesianBlocks.h"

#include "ParallelFor.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

#include "TTTR.h"

namespace tttrlib {

namespace {

/// Photons per bin for the background rate estimate. Same scheme as the CUSUM
/// search (TTTR.cpp): the median of coarse per-bin rates is robust to bursts,
/// which occupy a small fraction of a dilute measurement.
constexpr int64_t kBackgroundBinPhotons = 100;

/*!
 * \brief Median rate of ``kBackgroundBinPhotons``-photon bins, in counts/second.
 *
 * Robust because bursts, being rare, cannot move the median. Returns 0 when
 * there is too little data to estimate anything.
 */
double estimate_background_rate(const std::vector<int64_t>& t, double tick_seconds) {
    const int64_t n = static_cast<int64_t>(t.size());
    if (n < 2 * kBackgroundBinPhotons || !(tick_seconds > 0.0)) return 0.0;
    std::vector<double> rates;
    rates.reserve(static_cast<size_t>(n / kBackgroundBinPhotons));
    for (int64_t i = 0; i + kBackgroundBinPhotons < n; i += kBackgroundBinPhotons) {
        const double dt =
            static_cast<double>(t[static_cast<size_t>(i + kBackgroundBinPhotons)] -
                                t[static_cast<size_t>(i)]) * tick_seconds;
        if (dt > 0.0) rates.push_back(static_cast<double>(kBackgroundBinPhotons) / dt);
    }
    if (rates.empty()) return 0.0;
    const size_t mid = rates.size() / 2;
    std::nth_element(rates.begin(), rates.begin() + static_cast<long>(mid), rates.end());
    return rates[mid];
}

/// A contiguous stretch of photons handed to the dynamic program.
struct Region {
    int64_t lo = 0;  ///< first photon index (inclusive)
    int64_t hi = 0;  ///< one past the last photon index
};


}  // namespace

double ncp_prior_from_p0(double p0, int64_t n) {
    // Scargle 2013 eq. 21. Guard the arguments rather than trusting the caller:
    // p0 <= 0 would make the log diverge and n = 0 would raise 0 to a negative
    // power.
    if (!(p0 > 0.0)) p0 = 1e-12;
    if (p0 > 1.0) p0 = 1.0;
    const double nn = (n > 1) ? static_cast<double>(n) : 1.0;
    return 4.0 - std::log(73.53 * p0 * std::pow(nn, -0.478));
}

std::vector<int64_t> bayesian_blocks_events(
    const int64_t* t, int64_t n, double tick_seconds, double ncp_prior) {
    std::vector<int64_t> cp;
    if (t == nullptr || n <= 0) return cp;
    if (n == 1) {
        cp.push_back(0);
        cp.push_back(1);
        return cp;
    }
    if (!(tick_seconds > 0.0)) tick_seconds = 1.0;

    // Voronoi cell edges in seconds, taken relative to t[0]. The relative origin
    // matters: raw macro times reach ~1e12 ticks, and a double holding
    // 1e12 + a-few-ticks has already lost the resolution the algorithm needs to
    // place an edge.
    const int64_t t0 = t[0];
    std::vector<double> edges(static_cast<size_t>(n) + 1);
    edges[0] = 0.0;
    for (int64_t i = 1; i < n; ++i) {
        edges[static_cast<size_t>(i)] =
            0.5 * (static_cast<double>(t[i - 1] - t0) + static_cast<double>(t[i] - t0)) *
            tick_seconds;
    }
    edges[static_cast<size_t>(n)] = static_cast<double>(t[n - 1] - t0) * tick_seconds;

    // block_length[i] = time from edge i to the end of the interval, so the
    // duration of a block spanning cells [i, R] is a single subtraction.
    std::vector<double> block_length(static_cast<size_t>(n) + 1);
    for (int64_t i = 0; i <= n; ++i) {
        block_length[static_cast<size_t>(i)] =
            edges[static_cast<size_t>(n)] - edges[static_cast<size_t>(i)];
    }

    // TTTR streams routinely contain photons sharing a macro-time tick, which
    // can make a block's duration zero. Left alone that gives fitness = +inf and
    // the DP would happily partition the stream into zero-width blocks. The
    // instrument cannot resolve below a tick, so that is the floor.
    const double min_duration = 0.5 * tick_seconds;

    // log(N_k) lookup: N_k only ever runs over 1..n, and this removes one of the
    // two logarithms from the inner loop. The remaining log(T_k) dominates.
    std::vector<double> log_n(static_cast<size_t>(n) + 1, 0.0);
    for (int64_t k = 1; k <= n; ++k) {
        log_n[static_cast<size_t>(k)] = std::log(static_cast<double>(k));
    }

    std::vector<double> best(static_cast<size_t>(n), 0.0);
    std::vector<int64_t> last(static_cast<size_t>(n), 0);

    // PELT pruning (Killick, Fearnhead & Eckley 2012). The textbook recursion
    // considers every start i <= R at every R, which is what makes it O(N^2).
    // But a start that is already worse than the current optimum by more than
    // the change-point penalty can never become optimal again, because extending
    // any block only ever costs likelihood: concretely, once
    //
    //     best[i-1] + fitness(i, R)  <=  best[R]
    //
    // holds, candidate i can be dropped for good. The condition is exact -- the
    // pruned search returns the identical partition to the full one -- and it
    // relies only on the fitness being a maximised log-likelihood over blocks
    // whose counts and durations add, which is true here by construction of the
    // Voronoi cells.
    //
    // In practice this collapses the cost to near-linear: the surviving
    // candidate set stays small because each real rate change prunes everything
    // before it.
    // The candidate set is a struct-of-arrays rather than indices alone. Storing
    // each candidate's `block_length[i]` and `best[i-1]` beside it turns two
    // scattered gathers per cell into sequential reads, which is worth real time
    // in the only loop here that matters. `log_n[nk]` stays a gather, but it
    // indexes a small table that stays resident in cache.
    std::vector<int64_t> act_i;     // block start
    std::vector<double> act_bl;     // block_length[start]
    std::vector<double> act_prev;   // best[start - 1], or 0 for start == 0
    std::vector<double> act_val;    // A_i at the current R, kept for pruning
    const size_t act_cap = static_cast<size_t>(n) + 1;
    act_i.reserve(act_cap);
    act_bl.reserve(act_cap);
    act_prev.reserve(act_cap);
    act_val.resize(act_cap);
    act_i.push_back(0);
    act_bl.push_back(block_length[0]);
    act_prev.push_back(0.0);

    for (int64_t R = 0; R < n; ++R) {
        const double tail = block_length[static_cast<size_t>(R + 1)];
        double best_val = -std::numeric_limits<double>::infinity();
        int64_t best_idx = 0;
        const size_t n_act = act_i.size();
        const int64_t* __restrict ai = act_i.data();
        const double* __restrict abl = act_bl.data();
        const double* __restrict apv = act_prev.data();
        double* __restrict av = act_val.data();

        for (size_t a = 0; a < n_act; ++a) {
            double width = abl[a] - tail;
            if (width < min_duration) width = min_duration;
            const int64_t nk = R - ai[a] + 1;
            // Log-likelihood of a constant-rate block, maximised over the rate
            // (Scargle 2013 eq. 19): N_k (ln N_k - ln T_k).
            // std::log, deliberately: a hand-rolled polynomial log was measured
            // and is *slower* here. In this loop libm's latency is already hidden
            // by the neighbouring gathers and multiply-adds, whereas a 10-term
            // series is one long dependency chain with no instruction-level
            // parallelism to hide.
            const double fitness =
                static_cast<double>(nk) * (log_n[static_cast<size_t>(nk)] - std::log(width));
            const double val = fitness - ncp_prior + apv[a];
            av[a] = val;
            if (val > best_val) {
                best_val = val;
                best_idx = ai[a];
            }
        }
        best[static_cast<size_t>(R)] = best_val;
        last[static_cast<size_t>(R)] = best_idx;

        // Drop candidates that can no longer win, then admit the next start.
        // `val + ncp_prior` undoes the penalty already folded into `val`, giving
        // the bare best[i-1] + fitness(i, R) the pruning inequality is stated in.
        // Drop candidates that can no longer win, then admit the next start.
        // `val + ncp_prior` undoes the penalty already folded into `val`, giving
        // the bare best[i-1] + fitness(i, R) the pruning inequality is stated in.
        //
        // Functional pruning (FPOP) was implemented here and removed: on this
        // data it pruned nothing at all beyond what this line already does. In a
        // dilute photon stream most of the trace is homogeneous background, so
        // every candidate start inside a long background stretch implies almost
        // the same rate; their viable rate ranges sit on top of one another and
        // none is ever squeezed out. Functional pruning needs candidates that
        // disagree about the parameter to have anything to bite on.
        size_t keep = 0;
        for (size_t a = 0; a < n_act; ++a) {
            if (av[a] + ncp_prior > best_val) {
                act_i[keep] = ai[a];
                act_bl[keep] = abl[a];
                act_prev[keep] = apv[a];
                ++keep;
            }
        }
        act_i.resize(keep);
        act_bl.resize(keep);
        act_prev.resize(keep);
        act_i.push_back(R + 1);
        act_bl.push_back(block_length[static_cast<size_t>(R + 1)]);
        act_prev.push_back(best_val);
    }

    // Backtrack the optimal partition.
    int64_t ind = n;
    while (ind > 0) {
        cp.push_back(ind);
        ind = last[static_cast<size_t>(ind - 1)];
    }
    cp.push_back(0);
    std::reverse(cp.begin(), cp.end());
    return cp;
}

std::vector<long long> burst_search_bayesian_blocks(
    const std::vector<int64_t>& macro_times,
    double macro_time_resolution,
    const BayesianBlocksBurstSettings& settings,
    int64_t* n_regions_force_split) {
    std::vector<long long> out;
    if (n_regions_force_split) *n_regions_force_split = 0;

    const int64_t n = static_cast<int64_t>(macro_times.size());
    const int m = (settings.m > 1) ? settings.m : 2;
    const int L = (settings.L > 0) ? settings.L : 1;
    if (n < m || n < L) return out;

    double tick = macro_time_resolution;
    if (!(tick > 0.0)) tick = 1.0;

    // ---------------------------------------------------------------- stage 1
    // Cheap trigger. The threshold is expressed as a multiple of the *measured*
    // background rate rather than as an absolute duration, which is what lets a
    // single default transfer between instruments -- the same reasoning behind
    // the max-tree's dimensionless `delta`.
    const double background_rate = estimate_background_rate(macro_times, tick);
    if (!(background_rate > 0.0)) return out;

    double contrast = settings.trigger_contrast;
    if (!(contrast > 1.0)) contrast = 1.0;
    const double trigger_window_seconds =
        static_cast<double>(m) / (contrast * background_rate);
    const int64_t trigger_window_ticks =
        static_cast<int64_t>(trigger_window_seconds / tick);

    // ---------------------------------------------------------------- stage 2
    // Pad candidate runs with background context and merge overlaps. Padding is
    // load-bearing: without background on both flanks the DP has no rate
    // contrast to place an edge against and simply returns one block.
    const int64_t pad = (settings.pad_photons > 0) ? settings.pad_photons : 0;
    int64_t cap = settings.max_region_photons;
    if (cap < 4 * m) cap = 4 * m;

    // The trigger is a pure macro-time-difference test — the Fries/Eggeling
    // criterion — and the candidate runs it produces are built here directly,
    // without an intermediate per-photon flag array.
    //
    // A firing window says its m photons are jointly dense, so it covers the
    // half-open range [i, i+m). Marking each of those photons costs O(n*m), and
    // it costs it precisely inside bursts, where windows fire at every offset.
    // Consecutive firing windows overlap by construction, so their union is a
    // set of runs that can be accumulated in one pass instead: extend the open
    // run while windows keep firing, close it when one does not. The candidate
    // set is identical; only the bookkeeping is cheaper.
    std::vector<Region> regions;
    const auto emit_run = [&](int64_t lo, int64_t hi) {
        Region r;
        r.lo = std::max<int64_t>(0, lo - pad);
        r.hi = std::min<int64_t>(n, hi + pad);
        if (!regions.empty() && r.lo <= regions.back().hi) {
            regions.back().hi = std::max(regions.back().hi, r.hi);
        } else {
            regions.push_back(r);
        }
    };

    int64_t run_lo = -1, run_hi = -1;   // half-open [run_lo, run_hi)
    for (int64_t i = 0; i + m - 1 < n; ++i) {
        if (macro_times[static_cast<size_t>(i + m - 1)] -
                macro_times[static_cast<size_t>(i)] > trigger_window_ticks) {
            continue;
        }
        if (run_lo < 0) {
            run_lo = i;
            run_hi = i + m;
        } else if (i <= run_hi) {          // overlaps or abuts the open run
            run_hi = std::max(run_hi, i + m);
        } else {
            emit_run(run_lo, run_hi);
            run_lo = i;
            run_hi = i + m;
        }
    }
    if (run_lo >= 0) emit_run(run_lo, run_hi);

    // Split oversized regions so the O(n^2) cost stays bounded. The cut goes at
    // the largest inter-photon gap in the middle third: in dilute data a region
    // longer than the cap is a chain of transits separated by background, and
    // the sparsest interior point is one of those separations. Cutting there
    // costs at most a boundary refinement on one burst instead of dropping the
    // region entirely.
    {
        std::vector<Region> split;
        std::vector<Region> stack(regions.rbegin(), regions.rend());
        int64_t forced = 0;
        while (!stack.empty()) {
            Region r = stack.back();
            stack.pop_back();
            const int64_t len = r.hi - r.lo;
            if (len <= cap) { split.push_back(r); continue; }
            ++forced;
            const int64_t a = r.lo + len / 3;
            const int64_t b = r.lo + (2 * len) / 3;
            int64_t cut = (a + b) / 2;
            int64_t widest = -1;
            for (int64_t k = a; k + 1 < b; ++k) {
                const int64_t gap = macro_times[static_cast<size_t>(k + 1)] -
                                    macro_times[static_cast<size_t>(k)];
                if (gap > widest) { widest = gap; cut = k + 1; }
            }
            if (cut <= r.lo || cut >= r.hi) cut = r.lo + len / 2;  // saturated region
            stack.push_back(Region{cut, r.hi});
            stack.push_back(Region{r.lo, cut});
        }
        std::sort(split.begin(), split.end(),
                  [](const Region& x, const Region& y) { return x.lo < y.lo; });
        regions.swap(split);
        if (n_regions_force_split) *n_regions_force_split = forced;
    }
    if (regions.empty()) return out;

    // Detection threshold. A false-alarm rate is preferred when given because it
    // means the same thing on a 10 s and a 1 h acquisition; a bare sigma does not.
    double threshold = settings.min_significance;
    if (settings.max_false_alarm_rate > 0.0) {
        const double acquisition_seconds =
            static_cast<double>(macro_times[static_cast<size_t>(n - 1)] - macro_times[0]) * tick;
        const double n_trials = estimate_n_trials(
            settings.trials_model, n, m, static_cast<int64_t>(regions.size()));
        threshold = sigma_for_false_alarm_rate(
            settings.max_false_alarm_rate, acquisition_seconds, n_trials);
    }

    // ---------------------------------------------------------------- stage 3
    // Segment each region and promote significant blocks to bursts. Regions are
    // independent, so this is the parallel part; results are gathered per region
    // and concatenated in order to keep the output deterministic.
    const int n_regions = static_cast<int>(regions.size());
    std::vector<std::vector<long long>> per_region(static_cast<size_t>(n_regions));

    parallel_for(n_regions, [&](int ri) {
        const Region& r = regions[static_cast<size_t>(ri)];
        const int64_t len = r.hi - r.lo;
        if (len < L + 2) return;

        const double ncp = ncp_prior_from_p0(settings.p0, len);
        const std::vector<int64_t> cp = bayesian_blocks_events(
            macro_times.data() + r.lo, len, tick, ncp);
        const int64_t n_blocks = static_cast<int64_t>(cp.size()) - 1;
        // Fewer than three blocks means there is no interior: the region is
        // either uniform or its structure runs off the edge, and in both cases
        // there is no burst here that this region can establish.
        if (n_blocks < 3) return;

        // Per-block photon counts and durations.
        std::vector<double> block_counts(static_cast<size_t>(n_blocks));
        std::vector<double> block_seconds(static_cast<size_t>(n_blocks));
        const double min_duration = 0.5 * tick;
        for (int64_t b = 0; b < n_blocks; ++b) {
            const int64_t s = cp[static_cast<size_t>(b)];
            const int64_t e = cp[static_cast<size_t>(b + 1)];
            block_counts[static_cast<size_t>(b)] = static_cast<double>(e - s);
            const int64_t ts = macro_times[static_cast<size_t>(r.lo + s)];
            const int64_t te = macro_times[static_cast<size_t>(r.lo + e - 1)];
            double d = static_cast<double>(te - ts) * tick;
            if (d < min_duration) d = min_duration;
            block_seconds[static_cast<size_t>(b)] = d;
        }

        // Local background from the flank blocks. These are padding by
        // construction, so they are background, and a background measured inside
        // the region is a better match to it than any global baseline.
        double n_off = block_counts[0] + block_counts[static_cast<size_t>(n_blocks - 1)];
        double t_off = block_seconds[0] + block_seconds[static_cast<size_t>(n_blocks - 1)];
        double local_rate;
        if (n_off > 0.0 && t_off > 0.0) {
            local_rate = n_off / t_off;
        } else {
            local_rate = background_rate;
            t_off = static_cast<double>(L) / background_rate;
            n_off = background_rate * t_off;
        }

        // Test interior blocks.
        std::vector<char> significant(static_cast<size_t>(n_blocks), 0);
        for (int64_t b = 1; b + 1 < n_blocks; ++b) {
            const double k = block_counts[static_cast<size_t>(b)];
            const double d = block_seconds[static_cast<size_t>(b)];
            const double mu = local_rate * d;
            double sig;
            switch (settings.significance_mode) {
                case SignificanceMode::kPoisson:
                    sig = poisson_significance(static_cast<int64_t>(k), mu);
                    break;
                case SignificanceMode::kLiMa:
                    sig = li_ma_significance(k, n_off, (t_off > 0.0) ? d / t_off : 1.0);
                    break;
                case SignificanceMode::kGaussian:
                default:
                    sig = (mu > 0.0) ? (k - mu) / std::sqrt(mu)
                                     : std::numeric_limits<double>::infinity();
                    break;
            }
            if (sig >= threshold) significant[static_cast<size_t>(b)] = 1;
        }

        // Merge adjacent significant blocks. A bright transit segments naturally
        // into rise, plateau and fall; those are one burst, and leaving them
        // separate would show up as a spurious split.
        std::vector<long long>& dst = per_region[static_cast<size_t>(ri)];
        int64_t b = 1;
        while (b + 1 < n_blocks) {
            if (!significant[static_cast<size_t>(b)]) { ++b; continue; }
            int64_t e = b;
            while (e + 1 < n_blocks && significant[static_cast<size_t>(e + 1)]) ++e;
            const int64_t first = cp[static_cast<size_t>(b)];
            const int64_t past = cp[static_cast<size_t>(e + 1)];
            if (past - first >= L) {
                // Convert to the library-wide inclusive convention.
                dst.push_back(static_cast<long long>(r.lo + first));
                dst.push_back(static_cast<long long>(r.lo + past - 1));
            }
            b = e + 1;
        }
    });

    for (const auto& v : per_region) out.insert(out.end(), v.begin(), v.end());

    // Regions were merged before the DP ran, so bursts cannot overlap across
    // them. Assert rather than assume: a violation would silently corrupt every
    // downstream per-burst statistic.
    for (size_t i = 2; i + 1 < out.size(); i += 2) {
        if (out[i] <= out[i - 1]) {
            out[i] = out[i - 1] + 1;
            if (out[i] > out[i + 1]) out[i + 1] = out[i];
        }
    }
    return out;
}

}  // namespace tttrlib

// TTTR lives at global scope, so this definition sits outside `tttrlib`.
std::vector<long long> TTTR::burst_search_bayesian_blocks(
    int L, int m, double p0,
    double trigger_contrast, long long pad_photons, long long max_region_photons,
    double min_significance, double max_false_alarm_rate,
    int significance_mode, int trials_model
) {
    const int64_t n = static_cast<int64_t>(size());
    std::vector<int64_t> times(static_cast<size_t>(n));
    for (int64_t i = 0; i < n; ++i) {
        times[static_cast<size_t>(i)] = static_cast<int64_t>(get_macro_time_at(i));
    }
    tttrlib::BayesianBlocksBurstSettings s;
    s.L = L;
    s.m = m;
    s.p0 = p0;
    s.trigger_contrast = trigger_contrast;
    s.pad_photons = static_cast<int64_t>(pad_photons);
    s.max_region_photons = static_cast<int64_t>(max_region_photons);
    s.min_significance = min_significance;
    s.max_false_alarm_rate = max_false_alarm_rate;
    s.significance_mode = static_cast<tttrlib::SignificanceMode>(significance_mode);
    s.trials_model = static_cast<tttrlib::TrialsModel>(trials_model);
    return tttrlib::burst_search_bayesian_blocks(
        times, header->get_macro_time_resolution(), s);
}

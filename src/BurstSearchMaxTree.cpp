// SPDX-License-Identifier: BSD-3-Clause
#include "BurstSearchMaxTree.h"

#include "ParallelFor.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

#include "TTTR.h"

namespace tttrlib {

namespace {

/// Grid points kept inside one background window when estimating the baseline.
constexpr double kBackgroundGridPoints = 256.0;
/// Only every Nth sample feeds the median estimates. A median needs a
/// representative sample, not every sample, and the copy it makes is otherwise
/// one of the larger allocations here.
constexpr int64_t kMedianStride = 16;

/// Median over every `stride`-th element of `v` (the caller's data is untouched).
double median_of(const std::vector<double>& v, int64_t stride = 1) {
    if (v.empty()) return 0.0;
    stride = std::max<int64_t>(1, stride);
    std::vector<double> tmp;
    tmp.reserve(v.size() / static_cast<size_t>(stride) + 1);
    for (size_t i = 0; i < v.size(); i += static_cast<size_t>(stride)) {
        tmp.push_back(v[i]);
    }
    const size_t mid = tmp.size() / 2;
    std::nth_element(tmp.begin(), tmp.begin() + mid, tmp.end());
    return tmp[mid];
}

/*!
 * Running extremum of `f` over a window defined in *time* rather than in samples,
 * centred on each sample: out[j] = min/max{ f[k] : |tc[k] - tc[j]| <= half_width }.
 *
 * A monotonic deque keeps this O(N) whatever the window width — the window bounds
 * are monotone in j, so each sample enters and leaves the deque once. Using a time
 * window (not a sample window) matters here because the signal lives on the photon
 * index axis, where sample spacing varies with the count rate.
 */
void running_extremum_range(
    const std::vector<double>& f,
    const int64_t* tc,
    int64_t half_width,
    bool want_min,
    std::vector<double>& out,
    std::vector<int32_t>& scratch,
    int64_t a,
    int64_t b
) {
    const int64_t n = static_cast<int64_t>(f.size());
    if (a >= b) return;
    // A plain array used as a deque: indices only ever move forward, and at most
    // `n` are ever pushed, so `head`/`tail` need no wraparound. This is measurably
    // faster than std::deque, whose chunked allocation dominates this loop. The
    // buffer is caller-owned so the erosion and dilation passes share one
    // allocation, and 32-bit indices halve its memory traffic.
    if (static_cast<int64_t>(scratch.size()) < n) scratch.resize(static_cast<size_t>(n));
    int32_t* dq = scratch.data();
    int64_t head = 0, tail = 0;   // [head, tail)

    // Start the sweep at the first sample that could still be inside sample a's
    // window. Everything earlier is already out of range for every output this
    // call produces, so the chunk needs no warm-up pass -- which is what makes
    // splitting the range across threads exact rather than approximate.
    int64_t r = a;
    while (r > 0 && tc[a] - tc[r - 1] <= half_width) --r;

    for (int64_t j = a; j < b; ++j) {
        while (r < n && tc[r] - tc[j] <= half_width) {
            while (tail > head &&
                   (want_min ? f[dq[tail - 1]] >= f[r] : f[dq[tail - 1]] <= f[r])) {
                --tail;
            }
            dq[tail++] = static_cast<int32_t>(r);
            ++r;
        }
        while (tail > head && tc[j] - tc[dq[head]] > half_width) {
            ++head;
        }
        // j itself is always inside its own window, so the window is never empty.
        out[static_cast<size_t>(j)] = f[dq[head]];
    }
}

/*!
 * \brief Running min/max over a time window, split across threads.
 *
 * Each chunk re-derives its own deque from a halo of preceding samples, so the
 * result is identical to the serial sweep -- this is a parallelisation, not an
 * approximation. The halo costs one extra window's worth of work per chunk,
 * which is negligible while the window is much shorter than a chunk.
 */
void running_extremum(
    const std::vector<double>& f,
    const int64_t* tc,
    int64_t half_width,
    bool want_min,
    std::vector<double>& out,
    std::vector<int32_t>& scratch
) {
    const int64_t n = static_cast<int64_t>(f.size());
    // Every element is written below, so skip the zero-fill that assign() would do.
    if (static_cast<int64_t>(out.size()) != n) out.resize(static_cast<size_t>(n));
    if (n == 0) return;

    // Below this the thread hand-off costs more than the sweep saves, and the
    // halo overhead stops being negligible.
    constexpr int64_t kMinSamplesPerThread = 8192;
    int n_chunks = static_cast<int>(std::min<int64_t>(
        parallel_for_threads(static_cast<int>(std::min<int64_t>(n, 1 << 20))),
        std::max<int64_t>(1, n / kMinSamplesPerThread)));
    if (n_chunks <= 1) {
        running_extremum_range(f, tc, half_width, want_min, out, scratch, 0, n);
        return;
    }

    // One scratch deque per chunk: the caller's buffer cannot be shared once the
    // sweeps run concurrently.
    std::vector<std::vector<int32_t>> pads(static_cast<size_t>(n_chunks));
    const int64_t per = (n + n_chunks - 1) / n_chunks;
    parallel_for(n_chunks, [&](int c) {
        const int64_t a = static_cast<int64_t>(c) * per;
        const int64_t b = std::min<int64_t>(a + per, n);
        running_extremum_range(f, tc, half_width, want_min, out,
                               pads[static_cast<size_t>(c)], a, b);
    });
}

} // namespace

std::vector<MaxTreeNode> build_max_tree_1d(const std::vector<int>& levels) {
    std::vector<MaxTreeNode> nodes;
    const int64_t n = static_cast<int64_t>(levels.size());
    if (n == 0) return nodes;
    nodes.reserve(static_cast<size_t>(n) / 4 + 16);

    // Stack of currently open components, strictly increasing in level from the
    // bottom of the stack upwards. Sweeping left to right, a drop in level closes
    // every open component above the new level; a rise opens a new one.
    std::vector<int64_t> stack;
    const int SENTINEL = -1;  // below every valid level: flushes the stack at the end

    for (int64_t j = 0; j <= n; ++j) {
        const int lv = (j < n) ? levels[static_cast<size_t>(j)] : SENTINEL;
        int64_t lo = j;
        while (!stack.empty() && nodes[static_cast<size_t>(stack.back())].level > lv) {
            const int64_t closed = stack.back();
            stack.pop_back();
            nodes[static_cast<size_t>(closed)].hi = j - 1;
            lo = nodes[static_cast<size_t>(closed)].lo;
            if (!stack.empty() &&
                nodes[static_cast<size_t>(stack.back())].level >= lv) {
                // An open component at or below the new level already encloses it.
                nodes[static_cast<size_t>(closed)].parent = stack.back();
            } else if (lv > SENTINEL) {
                // Nothing open at exactly `lv`: the closed component's enclosing
                // component starts here, spanning from the closed component's start.
                MaxTreeNode parent;
                parent.level = lv;
                parent.lo = lo;
                nodes.push_back(parent);
                const int64_t idx = static_cast<int64_t>(nodes.size()) - 1;
                nodes[static_cast<size_t>(closed)].parent = idx;
                stack.push_back(idx);
            } else {
                nodes[static_cast<size_t>(closed)].parent = -1;  // root
            }
        }
        if (j < n && (stack.empty() ||
                      nodes[static_cast<size_t>(stack.back())].level < lv)) {
            MaxTreeNode node;
            node.level = lv;
            node.lo = lo;
            nodes.push_back(node);
            stack.push_back(static_cast<int64_t>(nodes.size()) - 1);
        }
    }
    return nodes;
}

std::vector<long long> burst_search_maxtree(
    const std::vector<int64_t>& macro_times,
    double macro_time_resolution,
    const MaxTreeBurstSettings& settings
) {
    const int64_t n_photons = static_cast<int64_t>(macro_times.size());
    const int m = std::max(2, settings.m);
    const int n_levels = std::max(16, settings.n_levels);
    if (n_photons < static_cast<int64_t>(m) + 1) return {};
    if (!(macro_time_resolution > 0.0)) return {};

    // --- 1. local log2 count rate, assigned to the window centre -----------------
    const int64_t n_samples = n_photons - m + 1;
    const int64_t offset = (m - 1) / 2;
    const double log2_counts = std::log2(static_cast<double>(m - 1));
    // A zero-tick window would be an infinite rate; half a tick is the finest
    // duration the clock can express and keeps the signal finite.
    const double min_dt = 0.5 * macro_time_resolution;

    // The window-centre time of sample j is simply macro_times[j + offset], so the
    // centre-time axis is a view into the input rather than a copy of it.
    const int64_t* centre_time = macro_times.data() + offset;

    // One std::log2 per photon, with no dependence between samples, so this is
    // split across threads whenever there is enough of it to be worth the hand-off.
    std::vector<double> signal(static_cast<size_t>(n_samples));
    {
        constexpr int64_t kMinSamplesPerThread = 16384;
        const int n_chunks = static_cast<int>(std::max<int64_t>(
            1, std::min<int64_t>(parallel_for_threads(1 << 20),
                                 n_samples / kMinSamplesPerThread)));
        const int64_t per = (n_samples + n_chunks - 1) / n_chunks;
        parallel_for(n_chunks, [&](int c) {
            const int64_t lo = static_cast<int64_t>(c) * per;
            const int64_t hi = std::min<int64_t>(lo + per, n_samples);
            for (int64_t j = lo; j < hi; ++j) {
                const int64_t ticks = macro_times[static_cast<size_t>(j + m - 1)] -
                                      macro_times[static_cast<size_t>(j)];
                double dt = static_cast<double>(ticks) * macro_time_resolution;
                if (dt < min_dt) dt = min_dt;
                signal[static_cast<size_t>(j)] = log2_counts - std::log2(dt);
            }
        });
    }

    // --- 2. rolling-ball background: subtract a morphological opening ------------
    // `background` keeps the estimated baseline log2 rate, which the significance
    // test below needs; the tree itself is built on the background-free signal.
    bool background_subtracted = false;
    std::vector<double> background;      // baseline log2 rate on a coarse grid
    int64_t background_stride = 1;       // samples per grid point
    if (settings.background_window > 0.0) {
        const int64_t half_width = static_cast<int64_t>(
            0.5 * settings.background_window / macro_time_resolution);
        if (half_width > 0) {
            const double median_signal = median_of(signal, kMedianStride);

            // The opening is evaluated on a coarse grid and interpolated back. The
            // background varies on the scale of `background_window`, which spans
            // thousands of photons, so resolving it per photon is wasted work; the
            // stride is chosen to keep a few hundred grid points inside one window,
            // which leaves the baseline shape intact. This is the single most
            // expensive stage otherwise.
            const int64_t span_ticks =
                centre_time[static_cast<size_t>(n_samples - 1)] - centre_time[0];
            int64_t stride = 1;
            if (span_ticks > 0) {
                const double per_window =
                    static_cast<double>(n_samples) *
                    (2.0 * static_cast<double>(half_width) /
                     static_cast<double>(span_ticks));
                stride = std::max<int64_t>(
                    1, static_cast<int64_t>(per_window / kBackgroundGridPoints));
            }

            std::vector<double> coarse, coarse_bg, eroded;
            std::vector<int64_t> coarse_time;
            std::vector<int32_t> scratch;
            const int64_t n_coarse = (n_samples + stride - 1) / stride;
            coarse.resize(static_cast<size_t>(n_coarse));
            coarse_time.resize(static_cast<size_t>(n_coarse));
            for (int64_t k = 0; k < n_coarse; ++k) {
                coarse[static_cast<size_t>(k)] = signal[static_cast<size_t>(k * stride)];
                coarse_time[static_cast<size_t>(k)] =
                    centre_time[static_cast<size_t>(k * stride)];
            }
            running_extremum(coarse, coarse_time.data(), half_width, true, eroded,
                             scratch);
            running_extremum(eroded, coarse_time.data(), half_width, false, coarse_bg,
                             scratch);

            // Subtract block by block, walking a linear ramp between grid points.
            // Interpolating per sample instead would need an integer division per
            // photon, which costs more than everything else in this loop combined.
            for (int64_t k = 0; k < n_coarse; ++k) {
                const int64_t j0 = k * stride;
                const int64_t j1 = std::min(j0 + stride, n_samples);
                const double b0 = coarse_bg[static_cast<size_t>(k)];
                const double b1 =
                    coarse_bg[static_cast<size_t>(std::min(k + 1, n_coarse - 1))];
                const double step = (b1 - b0) / static_cast<double>(stride);
                double b = b0;
                for (int64_t j = j0; j < j1; ++j, b += step) {
                    signal[static_cast<size_t>(j)] -= b;
                }
            }
            // The significance test needs the baseline only at component centres,
            // so the grid is kept and looked up there rather than expanded to a
            // full-resolution array nobody reads more than a few thousand times.
            background.swap(coarse_bg);
            background_stride = stride;
            // An opening rides the *lower* envelope of the rate, so it is a biased
            // estimate of the mean background — good enough to flatten drift, but
            // it would inflate the Poisson significance below by a fixed factor and
            // make the sigma scale meaningless. Shift it onto the median rate, which
            // is unbiased because bursts are a small minority of the trace, while
            // keeping the opening's local shape (that is what tracks the drift).
            const double bias = median_signal - median_of(background);
            for (double& b : background) b += bias;
            background_subtracted = true;
        }
    }

    // --- 3. quantize --------------------------------------------------------------
    const auto minmax = std::minmax_element(signal.begin(), signal.end());
    const double v_min = *minmax.first;
    const double v_max = *minmax.second;
    if (!(v_max - v_min > 1e-12)) return {};  // flat signal: no structure to segment
    const double scale = (n_levels - 1) / (v_max - v_min);

    std::vector<int> levels(static_cast<size_t>(n_samples));
    for (int64_t j = 0; j < n_samples; ++j) {
        levels[static_cast<size_t>(j)] = static_cast<int>(
            std::lround((signal[static_cast<size_t>(j)] - v_min) * scale));
    }

    // Contrast is measured in log2 units above the baseline. With the background
    // subtracted the signal already *is* log2(rate / background); otherwise fall
    // back to the median rate, which is robust because bursts are a small minority.
    double baseline_log2 = 0.0;
    if (!background_subtracted) baseline_log2 = median_of(signal, kMedianStride);
    const double min_contrast_log2 =
        (settings.min_contrast > 0.0) ? std::log2(settings.min_contrast)
                                      : -std::numeric_limits<double>::infinity();

    // --- 4. max-tree ---------------------------------------------------------------
    const std::vector<MaxTreeNode> nodes = build_max_tree_1d(levels);
    const int64_t n_nodes = static_cast<int64_t>(nodes.size());
    if (n_nodes == 0) return {};

    const int delta_levels =
        std::max(1, static_cast<int>(std::lround(settings.delta * scale)));

    // Detection threshold. When a false-alarm rate is requested it replaces the
    // bare sigma, because "4 sigma" is not a false-positive rate until you say how
    // many places you looked -- and this search looks in a great many. The trials
    // factor is estimated from the number of independent rate windows rather than
    // the node count: max-tree nodes are nested, so a single burst contributes a
    // whole correlated chain of them and counting those would wildly over-correct.
    double significance_threshold = settings.min_significance;
    if (settings.max_false_alarm_rate > 0.0 && n_photons > 1) {
        const double acquisition_seconds =
            static_cast<double>(macro_times[static_cast<size_t>(n_photons - 1)] -
                                macro_times[0]) * macro_time_resolution;
        const double n_trials = estimate_n_trials(
            settings.trials_model, n_photons, settings.m, n_nodes);
        significance_threshold = sigma_for_false_alarm_rate(
            settings.max_false_alarm_rate, acquisition_seconds, n_trials);
    }

    struct Candidate {
        double variation;
        int64_t start;
        int64_t stop;
    };
    std::vector<Candidate> candidates;

    for (int64_t i = 0; i < n_nodes; ++i) {
        const MaxTreeNode& node = nodes[static_cast<size_t>(i)];
        if (node.parent < 0) continue;  // the root spans everything: never a burst

        const int64_t extent = node.hi - node.lo + 1;
        if (extent < settings.L) continue;

        // Attribute: duration between the first and last photon of the component.
        const int64_t first = node.lo + offset;
        const int64_t last = node.hi + offset;
        const double duration =
            static_cast<double>(macro_times[static_cast<size_t>(last)] -
                                macro_times[static_cast<size_t>(first)]) *
            macro_time_resolution;
        if (settings.min_duration > 0.0 && duration < settings.min_duration) continue;
        if (settings.max_duration > 0.0 && duration > settings.max_duration) continue;

        // Attribute: contrast of the level at which this component exists.
        const double level_log2 = v_min + node.level / scale;
        if (level_log2 - baseline_log2 < min_contrast_log2) continue;

        // Attribute: Poisson significance of the photon excess over the local
        // background. Stability is a statement about the *shape* of a component;
        // this is the statement that there are more photons here than background
        // can account for. Both are needed — a shot-noise clump can be sharply
        // bounded, and a genuine dim burst can have soft edges.
        //
        // Which statistic does the arithmetic is set by `significance_mode`; see
        // BurstSignificance.h. The Gaussian form below is the historical default
        // and is kept bit-exact, but it is an approximation that degrades exactly
        // where it matters most, at the low counts of a dim burst.
        if (significance_threshold > 0.0 && duration > 0.0) {
            double bg_log2 = baseline_log2;
            if (background_subtracted) {
                const int64_t k = std::min<int64_t>(
                    ((node.lo + node.hi) / 2) / background_stride,
                    static_cast<int64_t>(background.size()) - 1);
                bg_log2 = background[static_cast<size_t>(k)];
            }
            const double bg_rate = std::exp2(bg_log2);
            const double expected = bg_rate * duration;
            if (expected > 0.0) {
                double significance;
                switch (settings.significance_mode) {
                    case SignificanceMode::kPoisson:
                        significance = poisson_significance(extent, expected);
                        break;
                    case SignificanceMode::kLiMa: {
                        // The rolling-ball baseline already gives us the on/off
                        // geometry Li & Ma assumes: the component is the on
                        // region, the background window it was measured over is
                        // the off region.
                        const double t_off = (settings.background_off_ratio > 0.0)
                            ? duration * settings.background_off_ratio
                            : ((settings.background_window > 0.0)
                                   ? settings.background_window
                                   : duration * 10.0);
                        const double n_off = bg_rate * t_off;
                        significance = li_ma_significance(
                            static_cast<double>(extent), n_off,
                            (t_off > 0.0) ? duration / t_off : 1.0);
                        break;
                    }
                    case SignificanceMode::kGaussian:
                    default:
                        significance =
                            (static_cast<double>(extent) - expected) / std::sqrt(expected);
                        break;
                }
                if (significance < significance_threshold) continue;
            }
        }

        // MSER variation: how much the component grows when the level is lowered
        // by `delta`. A well-defined burst has steep flanks and grows very little;
        // a noise fluctuation on a slope grows continuously.
        const int target_level = node.level - delta_levels;
        int64_t ancestor = i;
        while (nodes[static_cast<size_t>(ancestor)].parent >= 0 &&
               nodes[static_cast<size_t>(ancestor)].level > target_level) {
            ancestor = nodes[static_cast<size_t>(ancestor)].parent;
        }
        const MaxTreeNode& anc = nodes[static_cast<size_t>(ancestor)];
        const double variation =
            static_cast<double>((anc.hi - anc.lo + 1) - extent) /
            static_cast<double>(extent);
        if (variation > settings.max_variation) continue;

        candidates.push_back({variation, first, last});
    }

    // --- 5. resolve overlaps: the most stable component wins ------------------------
    // Nested candidates are common by construction (a burst is stable over a range
    // of levels). Accepting greedily in order of stability keeps the best-defined
    // description of each event, and lets two stable children beat their merged
    // parent — which is where deblending actually happens.
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) {
                  if (a.variation != b.variation) return a.variation < b.variation;
                  if (a.start != b.start) return a.start < b.start;
                  return a.stop < b.stop;
              });

    std::set<std::pair<int64_t, int64_t>> accepted;  // ordered by start
    for (const Candidate& c : candidates) {
        auto it = accepted.lower_bound({c.start, std::numeric_limits<int64_t>::min()});
        bool overlaps = (it != accepted.end() && it->first <= c.stop);
        if (!overlaps && it != accepted.begin()) {
            auto prev = std::prev(it);
            overlaps = (prev->second >= c.start);
        }
        if (!overlaps) accepted.insert({c.start, c.stop});
    }

    std::vector<long long> bursts;
    bursts.reserve(accepted.size() * 2);
    for (const auto& b : accepted) {
        bursts.push_back(static_cast<long long>(b.first));
        bursts.push_back(static_cast<long long>(b.second));
    }
    return bursts;
}

} // namespace tttrlib

// TTTR lives at global scope, so this definition sits outside `tttrlib`.
std::vector<long long> TTTR::burst_search_maxtree(
    int L, int m,
    double delta, double max_variation,
    double background_window, double min_contrast,
    double min_duration, double max_duration,
    int n_levels, double min_significance,
    int significance_mode, double max_false_alarm_rate, double background_off_ratio
) {
    const int64_t n = static_cast<int64_t>(size());
    std::vector<int64_t> times(static_cast<size_t>(n));
    for (int64_t i = 0; i < n; ++i) {
        times[static_cast<size_t>(i)] =
            static_cast<int64_t>(get_macro_time_at(i));
    }
    tttrlib::MaxTreeBurstSettings s;
    s.L = L;
    s.m = m;
    s.delta = delta;
    s.max_variation = max_variation;
    s.background_window = background_window;
    s.min_contrast = min_contrast;
    s.min_duration = min_duration;
    s.max_duration = max_duration;
    s.n_levels = n_levels;
    s.min_significance = min_significance;
    s.significance_mode = static_cast<tttrlib::SignificanceMode>(significance_mode);
    s.max_false_alarm_rate = max_false_alarm_rate;
    s.background_off_ratio = background_off_ratio;
    return tttrlib::burst_search_maxtree(
        times, header->get_macro_time_resolution(), s);
}

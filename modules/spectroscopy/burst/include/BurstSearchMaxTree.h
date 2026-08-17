// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file BurstSearchMaxTree.h
 * \brief Threshold-free burst search by max-tree attribute filtering.
 *
 * Classical burst searches pick one rate threshold (a fixed `m`/`T` ratio, or an
 * automatic level from e.g. Otsu) and cut the photon stream there. A single global
 * level cannot resolve a dim and a bright burst that coexist in the same trace:
 * whatever level separates the dim burst from background also swallows the bright
 * one, and vice versa.
 *
 * This module takes the approach image segmentation adopted instead of global
 * thresholding: build the *component tree* (max-tree) of the local count-rate
 * signal — every connected component at every level, nested as a tree — and then
 * select nodes by their **attributes** rather than by their level. A burst is a
 * component that is *maximally stable*: its extent barely changes as the level is
 * varied (the MSER criterion of Matas et al.), and whose photon count, duration
 * and contrast are physically plausible. Dim and bright bursts are then selected
 * at their own natural levels, from different depths of the tree.
 *
 * Deblending comes for free: two overlapping molecule transits appear as two
 * stable children of one unstable parent, so they are separated by the tree
 * structure without a watershed pass.
 *
 * Pipeline (all steps O(N), single pass each):
 *
 *  1. **Rate signal.** For each photon `j`, the duration of the window spanning
 *     `m` consecutive photons, converted to a log2 count rate. Working in log
 *     space makes the background/burst separation roughly symmetric (for a Poisson
 *     background the linear-rate distribution is heavy-tailed), and it makes
 *     "contrast" a difference rather than a ratio. The value is assigned to the
 *     window's *centre* photon, so burst boundaries are not biased early by m/2.
 *  2. **Background.** Optional grayscale morphological *opening* with a time
 *     window longer than any plausible burst — the 1D form of ImageJ's rolling-ball
 *     background. What survives an opening is the slowly varying baseline, so
 *     subtracting it tracks bleaching and drift non-parametrically, with no
 *     chunking. Implemented as running min followed by running max over a
 *     time-defined window (monotonic deque, O(N) regardless of window width).
 *  3. **Max-tree.** In 1D a component at level h is a maximal interval where the
 *     signal is >= h, so the whole component tree is built by one monotonic-stack
 *     sweep — no union-find, no priority queue.
 *  4. **Attribute filtering + MSER selection**, then greedy overlap resolution by
 *     stability, so the returned bursts are disjoint.
 *
 * **Assumption — bursts are a minority of the trace.** The baseline is estimated
 * as a morphological opening shifted onto the median rate, and both the contrast
 * and significance filters measure a component against it. That is only a
 * background estimate while most photons are *not* in a burst. Measured against a
 * simulated ground truth, with ~24% of photons belonging to a molecule the two
 * filters are what make the method work (F1 0.95 with them, 0.59 without); at
 * ~69% occupancy they invert and reject real bursts (F1 0.63 with, 0.91 without),
 * because by then the median rate is itself a burst rate. For densely occupied
 * traces set `min_contrast` and `min_significance` to 0, or dilute the sample.
 * The stability criterion itself does not depend on this assumption.
 *
 * Additive: does not modify any existing burst search.
 */
#ifndef TTTRLIB_BURSTSEARCHMAXTREE_H
#define TTTRLIB_BURSTSEARCHMAXTREE_H

// Validation: KNOWN-ANSWER-TESTED 2026-08-17 -- no independent implementation of a photon-stream max-tree burst search
//   exists; injected bursts recovered, significance filters, dispatch, degenerate inputs
//   in test/python/test_burst_search_maxtree.py.
//   Register: okf/testing/algorithm-validation.md

#include <cstdint>
#include <vector>

#include "BurstSignificance.h"

namespace tttrlib {

/*!
 * \brief Parameters of the max-tree burst search.
 *
 * The defaults target a dilute confocal single-molecule measurement with
 * millisecond transits. Note that there is no rate threshold among them: the
 * level at which each burst is detected is chosen per burst by the stability
 * criterion.
 */
struct MaxTreeBurstSettings {
    /// Photons per sliding window used to estimate the local rate. Same meaning
    /// as `m` in the sliding-window search, so the two can be compared directly.
    int m = 10;
    /// Minimum photons per burst; smaller components are discarded (as `L`).
    int L = 20;
    /*! MSER stability offset, in **log2 rate units**. A component is judged
     *  stable by how much its extent grows when the level is lowered by this
     *  much, so `delta = 0.15` probes a rate change of 2^0.15 ~ 1.11x. Being
     *  dimensionless in rate, it transfers across instruments in a way a kHz
     *  threshold does not.
     *
     *  Smaller values probe a smaller level drop, so components grow less over
     *  it, so more of them pass `max_variation` -- a *lower* delta is therefore
     *  the more permissive setting, and it leans more on the contrast and
     *  significance filters to reject what stability then lets through. Since
     *  those two are the statistically principled tests and stability is a
     *  shape heuristic, that turns out to be the better division of labour:
     *  against the simulated ground truth, moving from 0.5 to 0.15 raised
     *  completeness from 0.714 to 0.800 and purity from 0.897 to 0.973 while
     *  lowering the 50% detection limit from 15.3 to 12.0 photons -- and ran
     *  slightly faster. Below ~0.1 the gains flatten and the extra candidates
     *  start to cost time. */
    double delta = 0.15;
    /// Reject components whose relative extent growth over `delta` exceeds this.
    /// Larger = more permissive (accepts less well-defined bursts).
    double max_variation = 0.5;
    /*! Rolling-ball background window in seconds; must be much longer than a
     *  burst. 0 disables background estimation (contrast is then measured
     *  against the median rate instead).
     *
     *  Lengthening this to 0.2-0.5 s measured better still on the simulated
     *  ground truth (completeness 0.830, limit 10.2 at 0.5 s). It is left at
     *  0.05 s deliberately: the simulation has a *constant* background, so a
     *  longer window is free there in a way it will not be on real data, where
     *  the whole point of the rolling ball is to track drift. Raise it if the
     *  baseline is known to be stable. */
    double background_window = 0.05;
    /// Minimum burst-to-background rate ratio. 0 disables the contrast filter.
    double min_contrast = 2.0;
    /// Minimum Poisson significance of the photon excess over the local background,
    /// in units of sigma: `(k - lambda*d) / sqrt(lambda*d)` for a component of `k`
    /// photons spanning `d` seconds against a background rate `lambda`.
    ///
    /// Stability alone does not reject background: a shot-noise clump of 20 photons
    /// can be perfectly well-defined in shape and still be pure background. This
    /// filter is what separates "a sharp-edged component" from "more photons than
    /// background can explain", and it is close to free because both quantities are
    /// already known. It costs dim bursts almost nothing — a 25-photon transit in
    /// 1 ms against 2 kcps expects 2 background photons and so is hugely
    /// significant — while removing the slow, broad clumps that pass on shape alone.
    /// 0 disables the test.
    double min_significance = 4.0;
    /*! Which statistic computes that significance; see SignificanceMode.
     *
     * The default is deliberately the Gaussian form, so results produced before
     * this option existed are reproduced bit-exactly. It is nonetheless the
     * *wrong* statistic for this regime: `(k - mu)/sqrt(mu)` assumes the Poisson
     * distribution is already normal, and at the counts described above (20
     * photons over 2 expected) it is visibly skewed and discrete, so a "4 sigma"
     * threshold does not deliver the false-positive rate it appears to promise.
     * kLiMa is the better choice for new work: the background here is always
     * measured from the rolling-ball baseline rather than known, and Li & Ma
     * (1983) is the standard correction for that. */
    SignificanceMode significance_mode = SignificanceMode::kGaussian;
    /*! Expected spurious bursts per second. When > 0 this replaces
     *  `min_significance` with a post-trials threshold from
     *  sigma_for_false_alarm_rate(), which unlike a bare sigma means the same
     *  thing on a 10 s and a 1 h acquisition. See the warning there on how
     *  approximate the trials factor is. */
    double max_false_alarm_rate = 0.0;
    /*! t_off/t_on for the Li & Ma test. 0 derives it from `background_window`,
     *  which is the natural choice: that window *is* the off region the
     *  rolling-ball baseline was measured over. */
    double background_off_ratio = 0.0;
    /// Trials-factor estimator used by `max_false_alarm_rate`; see TrialsModel.
    TrialsModel trials_model = TrialsModel::kIndependentWindows;
    /// Minimum / maximum burst duration in seconds. 0 disables the respective bound.
    double min_duration = 0.0;
    double max_duration = 0.0;
    /// Quantization levels of the log-rate signal. 1024 is far finer than shot
    /// noise on any realistic burst; lowering it mainly saves memory.
    int n_levels = 1024;
};

/*!
 * \brief Run the max-tree burst search on a macro-time array.
 *
 * \param macro_times sorted photon macro times, in macro-time ticks.
 * \param macro_time_resolution seconds per macro-time tick.
 * \param settings   see MaxTreeBurstSettings.
 * \return interleaved `[start0, stop0, start1, stop1, ...]` **inclusive** photon
 *         indices, sorted by start and non-overlapping — the same layout the other
 *         `TTTR::burst_search*` methods return.
 */
std::vector<long long> burst_search_maxtree(
    const std::vector<int64_t>& macro_times,
    double macro_time_resolution,
    const MaxTreeBurstSettings& settings
);

/*!
 * \brief A node of the 1D max-tree, exposed for diagnostics and testing.
 *
 * In 1D a connected component is an interval, so the node is fully described by
 * its level and its sample range; `parent` indexes into the same node vector
 * (-1 for the root).
 */
struct MaxTreeNode {
    int level = 0;      ///< quantized log-rate level of the component
    int64_t lo = 0;     ///< first signal sample of the component (inclusive)
    int64_t hi = 0;     ///< last signal sample of the component (inclusive)
    int64_t parent = -1;///< index of the enclosing (lower-level) component
};

/*!
 * \brief Build the 1D max-tree of a quantized signal (monotonic-stack sweep).
 *
 * Exposed so the tree itself can be inspected or filtered by other attributes;
 * `burst_search_maxtree` is a particular attribute filter over this structure.
 *
 * \param levels quantized signal, values >= 0.
 * \return nodes in creation order; every node's `parent` has a strictly lower level.
 */
std::vector<MaxTreeNode> build_max_tree_1d(const std::vector<int>& levels);

} // namespace tttrlib

#endif // TTTRLIB_BURSTSEARCHMAXTREE_H

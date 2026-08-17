/*!
 * \file Sampling.h
 * \brief Drawing from a discrete distribution given as weights or as a CDF.
 *
 * Two samplers that every consumer of this library ends up writing for itself:
 * pick an index in proportion to a weight vector, and invert a tabulated
 * cumulative distribution. Both are three lines of maths and easy to get
 * subtly wrong -- the usual mistakes are a linear scan where a binary search
 * belongs (O(n) per draw instead of O(log n)), and normalising the caller's CDF
 * in place, which turns a second draw from the same table into a draw from a
 * different distribution.
 *
 * They draw from :func:`tttrlib::global_rng`, so `TTTR_RNG_SEED` and
 * `TTTR_RNG_ENGINE` control them like every other random number in the
 * library rather than each caller reaching for its own generator.
 *
 * \see Random.h for the engine, the seeding contract and the counter-based
 *      jump-ahead that makes parallel streams reproducible.
 */

#ifndef TTTRLIB_SAMPLING_H
#define TTTRLIB_SAMPLING_H

// Validation: A/B-TESTED 2026-08-17 -- vs np.searchsorted on the cumulative weights / CDF with the same Philox
//   uniforms (index-exact) and numpy choice(p=) frequencies. test/python/misc/test_math_ab_numerics.py,
//   test/python/misc/test_math_ab_imaging.py.
//   Register: okf/testing/math-kernel-validation.md

#include <algorithm>
#include <cstdint>
#include <vector>

#include "Random.h"

namespace tttrlib {

/*!
 * \brief Draw indices in proportion to a weight vector.
 *
 * Index `i` is returned with probability `weights[i] / sum(weights)`. The
 * weights need not be normalised and need not sum to one.
 *
 * \param weights   Non-negative weights, one per selectable index.
 * \param n_weights Number of weights.
 * \param out       Output buffer, filled with `n_out` indices.
 * \param n_out     Number of draws.
 *
 * Implementation is the running-total plus binary search: the cumulative sum
 * is built once, and each draw is a `lower_bound` rather than a scan from the
 * start, which is what makes drawing many samples from a long weight vector
 * affordable.
 *
 * A degenerate weight vector (empty, or summing to zero) yields index 0 for
 * every draw. That is deliberate: it keeps the output in range for a caller
 * that built its weights from data that happened to be all zero, rather than
 * returning an index that would then be used to subscript something.
 */
inline void weighted_choice(
        const double* weights, int n_weights,
        uint32_t* out, int n_out
) {
    if (n_out <= 0) return;
    if (n_weights <= 0) {
        std::fill(out, out + n_out, uint32_t(0));
        return;
    }

    std::vector<double> totals(static_cast<size_t>(n_weights));
    double running = 0.0;
    for (int i = 0; i < n_weights; ++i) {
        running += weights[i];
        totals[static_cast<size_t>(i)] = running;
    }
    if (!(running > 0.0)) {
        std::fill(out, out + n_out, uint32_t(0));
        return;
    }

    Random& rng = global_rng();
    for (int j = 0; j < n_out; ++j) {
        const double draw = rng.random() * running;
        // lower_bound gives the first total >= draw, matching a scan that
        // stops at the first `draw <= totals[i]`.
        const auto it = std::lower_bound(totals.begin(), totals.end(), draw);
        int index = static_cast<int>(it - totals.begin());
        if (index >= n_weights) index = n_weights - 1;  // draw < running, but guard rounding
        out[j] = static_cast<uint32_t>(index);
    }
}

/*!
 * \brief Sample an axis by inverting a tabulated cumulative distribution.
 *
 * For each draw `r` uniform on [0, 1), the returned value is `axis[i]` for the
 * first `i` with `cdf[i] >= r`.
 *
 * \param axis      Values to return, one per CDF entry; must be ascending if
 *                  the samples are to be ordered meaningfully.
 * \param n_axis    Number of axis points.
 * \param cdf       Non-decreasing cumulative distribution.
 * \param n_cdf     Number of CDF points; must equal `n_axis`.
 * \param out       Output buffer, filled with `n_out` samples.
 * \param n_out     Number of draws.
 * \param normalize Divide the CDF by its last entry before sampling.
 *
 * \b The \b CDF \b is \b never \b modified. `normalize` scales a working copy;
 * scaling the caller's array in place is the bug this signature exists to
 * prevent, because it makes a second draw from the same table sample a
 * *different* distribution and nothing about the call site says so.
 *
 * A draw above the last CDF entry -- reachable when `normalize` is false and
 * the table does not reach one -- yields `0.0` rather than the last axis
 * point, which is what an inverse-CDF scan that simply finds no match does.
 */
inline void sample_from_cdf(
        const double* axis, int n_axis,
        const double* cdf, int n_cdf,
        double* out, int n_out,
        bool normalize = true
) {
    if (n_out <= 0) return;
    if (n_axis <= 0 || n_cdf <= 0 || n_axis != n_cdf) {
        std::fill(out, out + n_out, 0.0);
        return;
    }

    std::vector<double> table(cdf, cdf + n_cdf);
    if (normalize) {
        const double last = table.back();
        if (last != 0.0) {
            for (double& value : table) value /= last;
        }
    }

    Random& rng = global_rng();
    for (int j = 0; j < n_out; ++j) {
        const double draw = rng.random();
        const auto it = std::lower_bound(table.begin(), table.end(), draw);
        const int index = static_cast<int>(it - table.begin());
        out[j] = (index < n_cdf) ? axis[index] : 0.0;
    }
}

}  // namespace tttrlib

#endif  // TTTRLIB_SAMPLING_H

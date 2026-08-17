// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file BackgroundEstimation.h
 * \brief Background count-rate estimation from inter-photon time histograms.
 *
 * For a Poisson (background-only) process, inter-photon arrival times follow
 * an exponential distribution. The maximum-likelihood decay constant of the
 * upper tail of the sorted inter-photon times gives the background rate, in
 * kHz (typical single-molecule background: 0.2-3 kHz).
 */
#ifndef TTTRLIB_BACKGROUNDESTIMATION_H
#define TTTRLIB_BACKGROUNDESTIMATION_H

// Validation: A/B-TESTED 2026-08-17 -- vs FRETBursts expon_fit (tail MLE with the
//   threshold subtracted) and the true Poisson rate, tail_fraction 1/0.5/0.2/0.05
//   (the A/B found the threshold missing: 0.59x bias at 0.5, fixed same day).
//   test/python/corrections/test_ab_corrections_reference.py.
//   Register: okf/testing/algorithm-validation.md

#include <vector>

namespace tttrlib {

/*!
 * \brief Estimate background rate from inter-photon arrival times.
 *
 * Exponential maximum likelihood on the upper tail of the sorted inter-photon
 * times: the largest `tail_fraction` of them are `t_thr + Exp(lambda)`, so
 * lambda = N / sum(t_i - t_thr) with t_thr the smallest tail value (the form
 * FRETBursts' `expon_fit` uses; `tail_fraction >= 1` is the untruncated
 * whole-sample MLE). No histogram is formed.
 *
 * \param interphoton_times_ms inter-photon times in milliseconds
 * \param bin_size_ms unused; kept for signature stability
 * \param tail_fraction fraction of the sorted times to use (from the end)
 * \return background rate in kHz (inter-photon times are in ms, so
 *         N / sum(t - t_thr) is already 1/ms)
 */
double estimate_background_rate(
    const std::vector<double>& interphoton_times_ms,
    double bin_size_ms = 0.1,
    double tail_fraction = 0.5
);

} // namespace tttrlib

#endif // TTTRLIB_BACKGROUNDESTIMATION_H

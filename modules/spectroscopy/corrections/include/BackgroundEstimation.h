// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file BackgroundEstimation.h
 * \brief Background count-rate estimation from inter-photon time histograms.
 *
 * For a Poisson (background-only) process, inter-photon arrival times follow
 * an exponential distribution. By fitting A*exp(-lambda*t) to the tail of the
 * inter-photon time histogram, the decay constant lambda gives the background
 * rate in Hz.
 */
#ifndef TTTRLIB_BACKGROUNDESTIMATION_H
#define TTTRLIB_BACKGROUNDESTIMATION_H

#include <vector>

namespace tttrlib {

/*!
 * \brief Estimate background rate from inter-photon arrival times.
 *
 * Fits an exponential to the tail of the inter-photon time histogram
 * via Poisson maximum likelihood.
 *
 * \param interphoton_times_ms inter-photon times in milliseconds
 * \param bin_size_ms histogram bin width in ms
 * \param tail_fraction fraction of histogram to use for fit (from the end)
 * \return background rate in kHz
 */
double estimate_background_rate(
    const std::vector<double>& interphoton_times_ms,
    double bin_size_ms = 0.1,
    double tail_fraction = 0.5
);

} // namespace tttrlib

#endif // TTTRLIB_BACKGROUNDESTIMATION_H

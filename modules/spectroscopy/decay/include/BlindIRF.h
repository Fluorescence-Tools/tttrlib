// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file BlindIRF.h
 * \brief Blind instrument response function estimation from fluorescence decays.
 *
 * Infers the IRF directly from measured TCSPC data without a separate scatter
 * measurement, using truncated exponential fitting and Richardson-Lucy
 * deconvolution.
 *
 * Reference: Gomez-Sanchez et al., "Blind instrument response function
 * identification from fluorescence decays", Biophysical Reports, 2024.
 */
#ifndef TTTRLIB_BLINDIRF_H
#define TTTRLIB_BLINDIRF_H

#include <vector>

namespace tttrlib {

/*!
 * \brief Estimate IRF from fluorescence decay data via blind deconvolution.
 *
 * Pipeline:
 *   1. Savitzky-Golay derivative to find decay start/end per channel
 *   2. Fit truncated exponential (shared decay rate k, per-channel A and C)
 *   3. Build normalized exponential kernel
 *   4. Richardson-Lucy deconvolution to extract the IRF
 *
 * \param data flat (n_samples * n_channels) decay data, row-major
 * \param n_samples number of time bins
 * \param n_channels number of detection channels
 * \param dt time step between samples
 * \param rl_iterations RL deconvolution iterations
 * \param regularization median filter window size (0 or 1 to disable)
 * \param sg_window Savitzky-Golay window (must be odd)
 * \param sg_order Savitzky-Golay polynomial order
 * \return flat (n_samples * n_channels) estimated IRF, row-major
 */
std::vector<double> blind_irf_estimate(
    const std::vector<double>& data,
    int n_samples, int n_channels,
    double dt = 1.0,
    int rl_iterations = 500,
    int regularization = 3,
    int sg_window = 11,
    int sg_order = 3
);

} // namespace tttrlib

#endif // TTTRLIB_BLINDIRF_H

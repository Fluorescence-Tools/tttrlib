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
 * identification from fluorescence decays", Biophysical Reports, 2024 --
 * implementation: https://github.com/VicidominiLab/birfi (torch). Differences
 * kept on purpose: birfi rolls its output by n/2 (an ifftshift after each FFT
 * product) -- here the IRF sits where it is in the data; the decay rate is a
 * Poisson-weighted log-linear fit of the tail (birfi: Adam on the MSE, seeded
 * by the same centroid); the RL back-projection is the exact adjoint of the
 * circular forward model (birfi: convolution with the time-reversed kernel,
 * one bin off). The circular (periodic) forward model is birfi's.
 */
#ifndef TTTRLIB_BLINDIRF_H
#define TTTRLIB_BLINDIRF_H

// Validation: A/B-TESTED 2026-08-17 -- vs VicidominiLab's birfi (github.com/VicidominiLab/birfi,
//   Gomez-Sanchez et al. 2024, the method implemented here; junk checkout run in a subprocess), four
//   simulated configurations incl. two channels, 30 and 500 RL iterations: aligned IRF estimates
//   correlate > 0.97 (0.978-0.999 observed), peaks within 0.15 ns, and never worse than birfi
//   against the truth. Two reference conventions are pinned rather than copied: birfi's
//   partial_convolution ifftshifts after every FFT product, so its IRF is rolled by n/2; and its
//   Adam MSE lifetime fit does not converge (k 5-38% off) while the RL step forgives it. The
//   port of birfi is a second check (corr > 0.95). Known answer: > 95% of the mass within +-0.5 ns
//   of a 0.15 ns-sigma Gaussian IRF, corr > 0.99. The A/B found the port's own defects the same
//   day: the SG derivative mixed a dt-scaled abscissa with unscaled weights (birfi uses scipy's
//   savgol_filter), and the lifetime was the centroid birfi uses only as an initial guess.
//   Benchmarked vs birfi on 25 ch x 1024 bins x 500 RL it.: 3.9x faster, IRF-truth corr 0.996 vs 0.993
//   (benchmarks/bench_vicidomini.py + check_vicidomini.py, PERF.md). The reference model (shared k,
//   per-channel A, C) is solved by variable projection here; birfi fits it with Adam.
//   test/python/decayfit/test_ab_decay_reference.py::TestBlindIrfAgainstBirfi, ::TestBlindIrfKnownAnswer.
//   Register: okf/testing/algorithm-validation.md

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

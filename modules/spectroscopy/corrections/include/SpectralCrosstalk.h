// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file SpectralCrosstalk.h
 * \brief Spectral crosstalk correction and linear mixing utilities.
 *
 * Three-cube FRET correction (Gordon/Nagy/Hellenkamp 2018) and general
 * linear mixing/unmixing for multi-colour fluorescence.
 */
#ifndef TTTRLIB_SPECTRALCROSSTALK_H
#define TTTRLIB_SPECTRALCROSSTALK_H

#include <vector>

namespace tttrlib {

/*!
 * \brief Three-cube ratiometric FRET correction.
 *
 * Computes corrected FRET efficiency E and corrected fluorescence Fc from
 * donor-excitation intensities using the standard correction factors:
 *
 *   Fc = I_DA - alpha * I_DD - delta * I_AA
 *   E  = Fc / (Fc + gamma * I_DD)
 *
 * \param i_dd donor-excitation donor detection
 * \param i_da donor-excitation acceptor detection (sensitized emission)
 * \param i_aa acceptor-excitation acceptor detection
 * \param gamma detection/quantum-yield ratio
 * \param alpha donor leakage coefficient
 * \param delta direct acceptor excitation coefficient
 * \param bg_dd, bg_da, bg_aa background counts per channel
 * \return flat array [E, S, Fc] where S is stoichiometry
 */
std::vector<double> correct_three_cube(
    double i_dd, double i_da, double i_aa,
    double gamma, double alpha, double delta,
    double bg_dd = 0.0, double bg_da = 0.0, double bg_aa = 0.0
);

/*!
 * \brief Vectorized three-cube correction for multiple bursts.
 *
 * \param i_dd, i_da, i_aa per-burst intensities (length n)
 * \param gamma, alpha, delta correction factors (scalars)
 * \param bg_dd, bg_da, bg_aa background (scalars)
 * \return flat array [E0, S0, Fc0, E1, S1, Fc1, ...] (3*n entries)
 */
std::vector<double> correct_three_cube_batch(
    const std::vector<double>& i_dd,
    const std::vector<double>& i_da,
    const std::vector<double>& i_aa,
    double gamma, double alpha, double delta,
    double bg_dd = 0.0, double bg_da = 0.0, double bg_aa = 0.0
);

/*!
 * \brief Matrix pseudo-inverse via normal equations (A^T A + lambda I)^-1 A^T.
 *
 * For small matrices where pulling in LAPACK is not warranted.
 * matrix is n_sources x n_detectors (row-major), measured is n_detectors.
 * Returns n_sources recovered signals.
 */
std::vector<double> invert_mixing_ridge(
    const std::vector<double>& matrix,
    const std::vector<double>& measured,
    int n_sources, int n_detectors,
    double ridge = 0.0
);

} // namespace tttrlib

#endif // TTTRLIB_SPECTRALCROSSTALK_H

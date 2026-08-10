// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file PhotonCountingHistogram.h
 * \brief Photon-counting histogram (PCH) and FIDA distributions.
 *
 * PCH computes P(k) — the probability of detecting k photons in a time bin —
 * for a 3-D Gaussian detection volume. The Poisson term is evaluated in log
 * space to avoid double overflow at k > 171.
 *
 * FIDA (Fluorescence Intensity Distribution Analysis, Kask et al., PNAS 1999)
 * computes the same P(k) through the probability generating function (PGF)
 * evaluated on the complex unit circle and inverted via FFT.
 */
#ifndef TTTRLIB_PHOTONCOUNTINGHISTOGRAM_H
#define TTTRLIB_PHOTONCOUNTINGHISTOGRAM_H

#include <vector>
#include <complex>

namespace tttrlib {

// ─── PCH ───────────────────────────────────────────────────────────────────

/*!
 * \brief P(k) for a single molecule in a 3-D Gaussian detection volume.
 *
 * \param k_max largest photon count (returns P(0..k_max))
 * \param brightness molecular brightness (counts/molecule/bin at peak)
 * \param n_grid spatial integration grid points (default 1000)
 * \param x_max spatial integration limit in beam-waist units (default 5.0)
 * \return P(k) for k = 0..k_max
 */
std::vector<double> pch_single_species(
    int k_max, double brightness,
    int n_grid = 1000, double x_max = 5.0
);

/*!
 * \brief P(k) for an open system with Poisson-distributed particle number.
 *
 * \param k_max largest photon count
 * \param brightness molecular brightness per particle
 * \param avg_n average number of particles in the observation volume
 * \param max_n maximum particle number for Poisson summation
 */
std::vector<double> pch_open_system(
    int k_max, double brightness, double avg_n, int max_n = 30
);

/*!
 * \brief P(k) for a mixture of species via convolution.
 *
 * \param k_max largest photon count
 * \param brightnesses per-species brightness values
 * \param avg_numbers per-species average particle numbers
 */
std::vector<double> pch_mixture(
    int k_max,
    const std::vector<double>& brightnesses,
    const std::vector<double>& avg_numbers
);

// ─── FIDA ──────────────────────────────────────────────────────────────────

/*!
 * \brief Spatial brightness profile w(x) for a 3-D Gaussian detection volume.
 *
 * Returns (x, w) where w is normalized to unit integral.
 *
 * \param n_bins number of brightness bins
 * \param x_min lower brightness cutoff
 * \return flat array: first n_bins entries are x, next n_bins are w
 */
std::vector<double> fida_dvdx_gaussian(
    int n_bins = 256, double x_min = 1e-4
);

/*!
 * \brief P(k) via the FIDA generating function (PGF/IFFT).
 *
 * \param k_max largest photon count
 * \param species_flat flat array [q0, N0, q1, N1, ...]
 * \param n_species number of species
 * \param background mean background counts per bin
 * \param profile_flat flat brightness profile [x0..xn, w0..wn], or empty for default
 * \param n_profile_bins profile length (ignored if profile_flat is empty)
 * \param oversample FFT length factor
 */
std::vector<double> fida_pch(
    int k_max,
    const std::vector<double>& species_flat,
    int n_species,
    double background = 0.0,
    const std::vector<double>& profile_flat = {},
    int n_profile_bins = 0,
    int oversample = 8
);

} // namespace tttrlib

#endif // TTTRLIB_PHOTONCOUNTINGHISTOGRAM_H

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
 *
 * \par Which "number of molecules" -- the reference volume
 * Chen et al. 1999 (Biophys J 77:553) show that an open-system PCH does not
 * depend on the reference volume the single-molecule term is normalised to,
 * as long as the mean particle number refers to that same volume; by
 * convention they report N_PSF, the mean number in the PSF volume
 * V_PSF = (pi/2)^{3/2} w0^2 z0. This file's single-molecule term is the
 * radial integral \f$\int x^2\,\mathrm{Poi}(k, \varepsilon e^{-2x^2})\,dx\f$
 * without the 4 pi and without dividing by a volume, i.e. it is referenced to
 * V0 = 4 pi w0^3 (isotropic w0 = z0). Hence
 *
 *   avg_n (here) = N_PSF * 16 / sqrt(2 pi) = 6.383 N_PSF,   <k> = avg_n * eps * sqrt(2 pi)/16 = N_PSF * eps.
 *
 * The k >= 1 shape and the brightness are convention-free (checked against
 * pysimfcs' Chen eq. 16 implementation to 1e-5); only the meaning of `avg_n`
 * carries the factor. Convert before comparing with literature N_PSF values.
 */
#ifndef TTTRLIB_PHOTONCOUNTINGHISTOGRAM_H
#define TTTRLIB_PHOTONCOUNTINGHISTOGRAM_H

// Validation: A/B-TESTED 2026-08-17 -- pch_single_species vs scipy.integrate.quad of the radial
//   integral (3e-4, the C++ is a 1000-point Riemann sum); pch_open_system / pch_mixture vs an
//   independent compound-Poisson PGF/FFT and vs the analytic moments (E[k] = N eps sqrt(2pi)/16,
//   Mandel Q = eps/(2 sqrt 2), 1e-9); fida_pch vs an independent PGF inversion (1e-9), analytic
//   moments (1e-10), Poisson for background only, the FIDA generating function in NumPy (1e-12), and equal to
//   pch_open_system under the x^2 dx <-> w(b) db change of variables with a converged profile.
//   CAVEAT found: fida_pch's N is grid-relative -- the default 256-bin profile makes N ~6.8x
//   the converged-profile N (shape unchanged to 3e-3). Independent implementation: pysimfcs (J. Unruh)
//   p3DG / singlespecies (Chen 1999 eq. 16, N in V_PSF): same k>=1 shape to 1e-5, open-system P(k)
//   equal to 5e-5 once avg_n = N_PSF * 16/sqrt(2 pi) -- avg_n is referenced to V0 = 4 pi w0^3 (see \par).
//   test/python/fluctuation/test_ab_pch_reference.py.
//   Register: okf/testing/algorithm-validation.md

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
 * \param avg_n mean number of particles referenced to V0 = 4 pi w0^3 (= 6.383 N_PSF; see the file note)
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

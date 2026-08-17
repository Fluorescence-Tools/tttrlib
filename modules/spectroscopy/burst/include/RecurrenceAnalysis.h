// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file RecurrenceAnalysis.h
 * \brief Recurrence Analysis of Single Particles (RASP).
 *
 * Extracts slow (ms-s) conformational dynamics from freely-diffusing
 * single-molecule FRET data by correlating burst arrival times.
 *
 * Reference: Hoffmann et al., Phys. Chem. Chem. Phys. 2011.
 */
#ifndef TTTRLIB_RECURRENCEANALYSIS_H
#define TTTRLIB_RECURRENCEANALYSIS_H

// Validation: A/B-TESTED 2026-08-17 -- pair_statistics vs NumPy pair counting and the Poisson expectation
//   with edge correction (exact); same_molecule_probability layout; known answers: Poisson
//   bursts give G = 1, planted recurrences give P_same = N_rec/(N_rec + E_random) (2%);
//   recurrence_efficiencies vs a NumPy pairing (exact).
//   test/python/bva/test_ab_bva_2cde_recurrence_reference.py.
//   Register: okf/testing/algorithm-validation.md

#include <vector>
#include <tuple>

namespace tttrlib {

/*!
 * \brief Counted and expected burst pairs per lag bin.
 *
 * \param burst_times sorted burst arrival times in seconds
 * \param edges lag-bin edges in seconds (n_bins+1)
 * \param edge_correction correct for finite acquisition time
 * \return flat array [counts | expected], each of length n_bins
 */
std::vector<double> pair_statistics(
    const std::vector<double>& burst_times,
    const std::vector<double>& edges,
    bool edge_correction = true
);

/*!
 * \brief Same-molecule probability P_same(tau) = 1 - 1/G(tau).
 *
 * \param burst_times burst arrival times in seconds
 * \param tau_min minimum lag in seconds
 * \param tau_max maximum lag in seconds
 * \param n_bins number of log-spaced lag bins
 * \param edge_correction correct for finite acquisition time
 * \return flat array of length 3*n_bins: [tau_centers | p_same | g]
 */
std::vector<double> same_molecule_probability(
    const std::vector<double>& burst_times,
    double tau_min = 1e-3, double tau_max = 1.0,
    int n_bins = 50, bool edge_correction = true
);

/*!
 * \brief FRET efficiencies of bursts recurring after an initial sub-population.
 *
 * \param burst_times sorted arrival times
 * \param efficiencies per-burst efficiency
 * \param e_range (min, max) efficiency window for initial population
 * \param dt_range (t1, t2) recurrence time window in seconds
 * \return efficiencies of recurring bursts
 */
std::vector<double> recurrence_efficiencies(
    const std::vector<double>& burst_times,
    const std::vector<double>& efficiencies,
    double e_min, double e_max,
    double dt_min, double dt_max
);

} // namespace tttrlib

#endif // TTTRLIB_RECURRENCEANALYSIS_H

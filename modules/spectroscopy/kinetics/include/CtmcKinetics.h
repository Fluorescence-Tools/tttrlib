// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file CtmcKinetics.h
 * \brief Continuous-time Markov chain (CTMC) rate-matrix utilities.
 *
 * Pure functions for building generators, finding equilibrium populations,
 * and converting between flat rate lists and full rate matrices. These are
 * the building blocks for every CTMC analysis (Gopich-Szabo, dynamic PDA,
 * time-averaged moments).
 *
 * Convention: rate matrices are column-major — ``rate_matrix[target, source]``
 * is the ``source -> target`` rate in Hz. The generator Q has off-diagonal
 * entries matching the rate matrix and a diagonal that makes each column sum
 * to zero.
 */
#ifndef TTTRLIB_CTMCKINETICS_H
#define TTTRLIB_CTMCKINETICS_H

// Validation: A/B-TESTED 2026-08-17 -- generator vs the column-sum-zero transcription (1e-14),
//   equilibrium vs scipy.linalg.null_space and expm(Q t) p0 (1e-9), flat-rate round trip exact.
//   test/python/kinetics/test_ab_kinetics_reference.py.
//   Register: okf/testing/algorithm-validation.md

#include <vector>
#include <cmath>

namespace tttrlib {

/// Build the CTMC generator from a rate matrix. Column-sum-zero convention.
std::vector<double> generator_from_rate_matrix(
    const std::vector<double>& rate_matrix, int n
);

/// Solve for equilibrium populations via augmented linear least-squares.
std::vector<double> equilibrium_populations(
    const std::vector<double>& rate_matrix, int n
);

/// Build a full rate matrix from a flat list of off-diagonal rates.
/// Flat order: source-major, skipping diagonal: [s0->s1, s0->s2, s1->s0, ...]
std::vector<double> rate_matrix_from_rates(
    const std::vector<double>& rates, int n_states
);

/// Extract flat off-diagonal rates from a full rate matrix.
std::vector<double> rates_from_rate_matrix(
    const std::vector<double>& matrix, int n
);

} // namespace tttrlib

#endif // TTTRLIB_CTMCKINETICS_H

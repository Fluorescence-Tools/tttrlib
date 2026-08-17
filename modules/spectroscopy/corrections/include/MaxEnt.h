// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file MaxEnt.h
 * \brief Maximum entropy regularized inversion.
 *
 * Minimizes ||Ax - b||^2 - nu^2 * S(x) where S is Shannon entropy relative to
 * a uniform prior, via the shared Skilling-Bryan MEM engine (see
 * modules/math/include/MaxEntQp.h) -- an active-set bound-constrained QP
 * inside an outer Newton-like iteration, not gradient descent.
 */
#ifndef TTTRLIB_MAXENT_H
#define TTTRLIB_MAXENT_H

// Validation: A/B-TESTED 2026-08-17 -- maxent_invert vs scipy L-BFGS-B on the
//   documented objective ||Ax-b||^2 - nu^2 S (KKT + objective 1e-7, incl. the
//   H = 2 A^T A, nu_run = 2 nu^2 translation into MaxEntQp).
//   test/python/misc/test_math_ab_probabilistic.py.
//   Register: okf/testing/algorithm-validation.md

#include <vector>

namespace tttrlib {

/*!
 * \brief Maximum entropy inversion: find x minimizing ||Ax - b||^2 - nu^2*S(x).
 *
 * \param A matrix n_rows x n_cols (row-major)
 * \param b measurements (n_rows)
 * \param nu regularization strength (entropy weight)
 * \param n_rows, n_cols matrix dimensions
 * \param max_iter maximum iterations
 * \param tol convergence tolerance on gradient norm
 * \return solution x (n_cols)
 */
std::vector<double> maxent_invert(
    const std::vector<double>& A,
    const std::vector<double>& b,
    double nu,
    int n_rows, int n_cols,
    int max_iter = 500,
    double tol = 1e-8
);

} // namespace tttrlib

#endif // TTTRLIB_MAXENT_H

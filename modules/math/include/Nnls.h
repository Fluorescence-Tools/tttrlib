// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file Nnls.h
 * \brief Non-negative least squares: min ||Ax - b||, x >= 0, exactly (Lawson
 * & Hanson, 1974, algorithm NNLS -- the same algorithm scipy.optimize.nnls
 * wraps).
 *
 * This is a different tool from `tttrlib::quadpr_bound` (MaxEntQp.h), which
 * that header's own docstring is explicit about: quadpr_bound's active-set
 * sweep never *releases* a variable once clamped and never checks dual
 * feasibility, so it is adequate as the inner solve of one MEM Newton step
 * (itself iterated to convergence by run_mem's outer loop) but is not a
 * general-purpose, KKT-correct bounded solver. NNLS below is that solver: at
 * termination every passive-set gradient component is (numerically) zero and
 * every active-set one is non-negative -- the KKT conditions for
 * min ||Ax-b||^2 s.t. x >= 0 -- because the algorithm can move an index
 * between the two sets in either direction, not just zero it.
 */
#ifndef TTTRLIB_NNLS_H
#define TTTRLIB_NNLS_H

#include <vector>

namespace tttrlib {

/*!
 * \brief Solve min ||Ax - b||_2, subject to x >= 0, exactly (Lawson-Hanson).
 *
 * \param A m x n design matrix, row-major.
 * \param b length-m target.
 * \param m, n matrix dimensions.
 * \param max_iter outer-loop iteration cap (default 3n, matching scipy).
 * \param tol dual-feasibility tolerance on the passive-set gradient
 *        (Lawson & Hanson's `w_j <= tol` stopping test).
 * \return x, length n, x >= 0.
 */
std::vector<double> nnls(
    const std::vector<double>& A, const std::vector<double>& b,
    int m, int n, int max_iter = 0, double tol = 1e-10
);

} // namespace tttrlib

#endif // TTTRLIB_NNLS_H

// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file Pda3cCore.h
 * \brief Three-colour PDA compute core: Gauss-Hermite quadrature and transfer matrix.
 *
 * The burst likelihood is already handled by PdaBurstLikelihood (K-channel).
 * This provides the species integration (Gauss-Hermite quadrature grid for
 * trivariate Gaussian distance distributions) and the physics (distances ->
 * channel probabilities via cascading FRET transfer matrix).
 */
#ifndef TTTRLIB_PDA3CCORE_H
#define TTTRLIB_PDA3CCORE_H

// Validation: A/B-TESTED 2026-08-17 -- transfer matrix vs the competing-acceptor cascade formula (1e-12)
//   and ChiSurf pda3c.physics; channel probabilities, Gauss-Hermite grid and species forward
//   model vs ChiSurf (1e-10). test/python/pda/test_ab_pda_reference.py, test_pda3c_core.py.
//   Register: okf/testing/algorithm-validation.md

#include <vector>

namespace tttrlib {

/*!
 * \brief Gauss-Hermite quadrature grid for a multivariate Gaussian species.
 *
 * Uses R = mu + L * z (z already carries the sqrt(2) Hermite->Gauss mapping)
 * to integrate a Gaussian distance distribution exactly.
 *
 * \param means mean distances (K values)
 * \param cholesky lower-triangular Cholesky factor (K*K, row-major)
 * \param nodes_1d Gauss-Hermite (physicist) abscissae, length n_nodes
 * \param weights_1d Gauss-Hermite (physicist) weights, length n_nodes
 *        (typically from numpy.polynomial.hermite.hermgauss)
 * \param K number of dimensions (typically 3 for 3-colour)
 * \param truncate drop nodes with weight below this (0 to keep all)
 * \return flat array: first M*K entries are the points (row-major),
 *         next M entries are the normalized weights
 */
std::vector<double> gauss_hermite_grid(
    const std::vector<double>& means,
    const std::vector<double>& cholesky,
    const std::vector<double>& nodes_1d,
    const std::vector<double>& weights_1d,
    int K,
    double truncate = 0.0
);

/*!
 * \brief Compute the upper-triangular FRET transfer matrix from distances.
 *
 * For K dyes, builds a K*K matrix where entry (i, j) is the transfer efficiency
 * from dye i to dye j with all competing acceptors accounted for.
 *
 * \param distances flat (K*(K-1)/2) inter-dye distances in Angstroms,
 *        upper triangle in row-major order: [d01, d02, d12] for K=3
 * \param forster_radii flat (K*(K-1)/2) Forster radii matching distances
 * \param K number of dyes
 * \return flat (K*K) transfer matrix, row-major
 */
std::vector<double> transfer_matrix_3c(
    const std::vector<double>& distances,
    const std::vector<double>& forster_radii,
    int K = 3
);

/*!
 * \brief Compute per-channel photon probabilities from a transfer matrix.
 *
 * \param transfer flat (K*K) transfer matrix (row-major)
 * \param excitation flat (K) excitation probabilities per dye
 * \param emission flat (K*K) emission/detection matrix (dyes x channels)
 * \param K number of dyes
 * \param n_channels number of detection channels
 * \return flat (n_channels) channel probabilities
 */
std::vector<double> channel_probabilities_3c(
    const std::vector<double>& transfer,
    const std::vector<double>& excitation,
    const std::vector<double>& emission,
    int K, int n_channels
);

/*!
 * \brief Batched forward model: transfer + channel probabilities over a grid.
 *
 * Processes every distance point in a single C++ call — no per-node Python
 * round-trip — so it beats the vectorised-numpy alternative exactly where the
 * per-node work (pow, cascade, matmul) lives.
 *
 * \param distance_grid flat (M * n_pairs) inter-dye distances, n_pairs = K*(K-1)/2
 * \param forster_radii flat (n_pairs) Forster radii
 * \param excitation flat (K) excitation probabilities per dye (per node repeat)
 * \param emission flat (K * n_channels) emission/detection matrix (dyes x channels)
 * \param M number of distance points
 * \param K number of dyes
 * \param n_channels number of detection channels
 * \return flat (M * n_channels) per-node channel probabilities, row-major
 */
std::vector<double> channel_probabilities_batch(
    const std::vector<double>& distance_grid,
    const std::vector<double>& forster_radii,
    const std::vector<double>& excitation,
    const std::vector<double>& emission,
    int M, int K, int n_channels
);

/*!
 * \brief Full forward model: average channel probabilities over a quadrature grid.
 *
 * Combines gauss_hermite_grid over a species with the batched transfer/channel
 * computation, weighted by the quadrature weights — the complete per-species
 * PDA3c forward pass in one C++ call.
 *
 * \param means mean distances (K values)
 * \param cholesky lower-triangular Cholesky factor (K*K, row-major)
 * \param nodes_1d Gauss-Hermite abscissae (length n_nodes)
 * \param weights_1d Gauss-Hermite weights (length n_nodes)
 * \param forster_radii flat (n_pairs) Forster radii
 * \param excitation flat (K) excitation probabilities
 * \param emission flat (K * n_channels) emission/detection matrix
 * \param K number of dyes
 * \param n_channels number of detection channels
 * \return flat (n_channels) weight-averaged channel probabilities
 */
std::vector<double> species_forward_model(
    const std::vector<double>& means,
    const std::vector<double>& cholesky,
    const std::vector<double>& nodes_1d,
    const std::vector<double>& weights_1d,
    const std::vector<double>& forster_radii,
    const std::vector<double>& excitation,
    const std::vector<double>& emission,
    int K, int n_channels
);

} // namespace tttrlib

#endif // TTTRLIB_PDA3CCORE_H

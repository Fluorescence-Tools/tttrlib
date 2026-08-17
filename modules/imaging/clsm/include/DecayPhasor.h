// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_PHASOR_H
#define TTTRLIB_PHASOR_H

// Validation: A/B-TESTED 2026-08-17 -- raw, IRF and calibrated phasors bit-for-bit (1e-13) against phasorpy 0.4
//   (phasor_from_signal + phasor_transform), harmonics 1 and 2; g()/s() are the complex
//   division. Recorded fixture test/data/reference/phasor_phasorpy_reference.npz.
//   test/python/clsm/test_ab_phasor_reference.py.
//   Benchmarked vs phasorpy on 100k decays x 256 bins through compute_phasor_bincounts_batch: 3.4x, identical
//   (bench_sciref.py, check_sciref.py).
//   Register: okf/testing/algorithm-validation.md

#include <vector>
#include <cmath>
#include <algorithm> /* std::max */
#include <numeric>   /* std::accumulate */
#include <stdexcept> /* std::invalid_argument */
#include <string>   /* std::to_string */

#include "TTTR.h" /* TTTR */
#include "CLSMImage.h" /* CLSMImage */


/**
 * @brief Utility class for computing phasor values in decay analysis.
 *
 * **Error contract.** Two failure modes are deliberately distinguished:
 *
 * - *Too few photons* is a property of the data, not a mistake. The
 *   `compute_phasor*` functions return the sentinel `{-1, -1}`, as they always
 *   have.
 * - *Invalid arguments* are programmer error and throw `std::invalid_argument`
 *   (surfaced as `ValueError` in Python). Previously several of these returned
 *   `nan` or read out of bounds instead.
 *
 * **The IRF phasor must be non-degenerate.** The correction divides by
 * \f$g_{irf}^2 + s_{irf}^2\f$, so `(0, 0)` is not "no IRF" — it is a division
 * by zero that used to yield a silent `nan`. The identity, meaning an ideal
 * delta-function instrument response, is **`(1, 0)`**, which is the default.
 */
class DecayPhasor{

public:

    /**
     * @brief Compute the phasor (g, s) for a selection of microtimes.
     *
     * This function computes the phasor (g, s) for a set of microtimes that are selected
     * using a second vector of indices. The indices specify which elements of the microtimes
     * vector are used to compute the phasor.
     *
     * @param[in] microtimes Vector of microtimes.
     * @param[in] n_microtimes Number of elements in the microtimes vector.
     * @param[in] frequency The frequency of the phasor.
     * @param[in] minimum_number_of_photons Minimum number of photons.
     * @param[in] g_irf G-value of instrument response phasor.
     * @param[in] s_irf S-value of instrument response phasor.
     * @param[in] idxs Vector of selected indices. Every index must be within
     *            `[0, n_microtimes)`; an out-of-range index throws rather than
     *            reading out of bounds.
     * @return Vector of length 2: first element g-value, second element s-value,
     *         or `{-1, -1}` when the selection holds too few photons.
     * @throws std::invalid_argument on a degenerate or non-finite IRF phasor, a
     *         non-finite or non-positive frequency, a negative count, a null
     *         `microtimes` with `n_microtimes > 0`, or an out-of-range index.
     */
    static std::vector<double> compute_phasor(
            unsigned short* microtimes, int n_microtimes,
            double frequency = 1.0,
            int minimum_number_of_photons = 1,
            double g_irf = 1.0,
            double s_irf = 0.0,
            std::vector<int>* idxs = nullptr
    );


    /**
     * @brief Compute the phasor (g, s) for a histogram/bincounts of microtimes.
     *
     * This function computes the phasor (g, s) for bincounted microtimes.
     *
     * @param[in] bincounts Vector of bincounts.
     * @param[in] frequency The frequency of the phasor.
     * @param[in] minimum_number_of_photons Minimum number of photons.
     * @param[in] g_irf G-value of instrument response phasor.
     * @param[in] s_irf S-value of instrument response phasor. Together with
     *            `g_irf` this must not be `(0, 0)`; the identity is `(1, 0)`.
     * @return Vector of length 2: first element g-value, second element s-value,
     *         or `{-1, -1}` when the histogram holds too few photons.
     * @throws std::invalid_argument on a degenerate or non-finite IRF phasor, or
     *         a non-finite or non-positive frequency.
     *
     * @note Negative bin counts are permitted — a background-subtracted decay
     *       legitimately has them — but the *total* must exceed
     *       `minimum_number_of_photons` and be positive, or the sentinel is
     *       returned.
     */
    static std::vector<double> compute_phasor_bincounts(
            std::vector<int> &bincounts,
            double frequency = 1.0,
            int minimum_number_of_photons = 1,
            double g_irf = 1.0, double s_irf = 0.0
    );

    /**
     * @brief The phasor of every row of a (n_decays x n_bins) histogram stack
     *        in one call -- `compute_phasor_bincounts` for each row, with the
     *        cos/sin table shared and the rows in parallel.
     *
     * Same arithmetic as the per-decay method, digit for digit (the table holds
     * the same `cos(mt * factor)` values that method computes on the fly), so a
     * pixel map computed here equals the per-pixel loop. One call for a stack
     * is the granularity rule of `okf/bindings/marshalling-cost.md`; the
     * per-decay method costs more in the binding than in the arithmetic.
     *
     * @param[in] bincounts2d row-major (n_decays x n_bins) counts
     * @param[out] output allocated (n_decays x 2) array of (g, s), the sentinel
     *             `(-1, -1)` for rows with too few photons
     */
    static void compute_phasor_bincounts_batch(
            const int* bincounts2d, int n_decays, int n_bins,
            double** output, int* dim1, int* dim2,
            double frequency = 1.0,
            int minimum_number_of_photons = 1,
            double g_irf = 1.0, double s_irf = 0.0
    );


    /**
     * @brief Calculate g-value for a given set of phasor parameters.
     *
     * Reference: https://journals.plos.org/plosone/article/file?type=supplementary&id=info:doi/10.1371/journal.pone.0194578.s001
     *
     * @param[in] g_irf G-value of instrument response phasor.
     * @param[in] s_irf S-value of instrument response phasor.
     * @param[in] g_exp Experimental g-value.
     * @param[in] s_exp Experimental s-value.
     * @return Computed g-value.
     * @throws std::invalid_argument if the IRF phasor is degenerate or
     *         non-finite. `(0, 0)` is not "no IRF"; the identity is `(1, 0)`.
     */
    static double g(
            double g_irf, double s_irf,
            double g_exp, double s_exp
    );


    /**
     * @brief Calculate s-value for a given set of phasor parameters.
     *
     * Reference: https://journals.plos.org/plosone/article/file?type=supplementary&id=info:doi/10.1371/journal.pone.0194578.s001
     *
     * @param[in] g_irf G-value of instrument response phasor.
     * @param[in] s_irf S-value of instrument response phasor.
     * @param[in] g_exp Experimental g-value.
     * @param[in] s_exp Experimental s-value.
     * @return Computed s-value.
     * @throws std::invalid_argument if the IRF phasor is degenerate or
     *         non-finite. `(0, 0)` is not "no IRF"; the identity is `(1, 0)`.
     */
    static double s(
            double g_irf, double s_irf,
            double g_exp, double s_exp
    );

};


#endif //TTTRLIB_PHASOR_H

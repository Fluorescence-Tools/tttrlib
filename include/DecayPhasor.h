#ifndef TTTRLIB_PHASOR_H
#define TTTRLIB_PHASOR_H

#include <vector>
#include <cmath>
#include <algorithm> /* std::max */

#include "TTTR.h" /* TTTR */
#include "CLSMImage.h" /* CLSMImage */


/**
 * @brief Utility class for computing phasor values in decay analysis.
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
     * @param[in] idxs Vector of selected indices.
     * @return Vector of length 2: first element g-value, second element s-value.
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
     * @param[in] s_irf S-value of instrument response phasor.
     * @return Vector of length 2: first element g-value, second element s-value.
     */
    static std::vector<double> compute_phasor_bincounts(
            std::vector<int> &bincounts,
            double frequency,
            int minimum_number_of_photons,
            double g_irf, double s_irf
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
     */
    static double s(
            double g_irf, double s_irf,
            double g_exp, double s_exp
    );

};


#endif //TTTRLIB_PHASOR_H

//
// Created by Thomas-Otavio Peulen on 4/2/20.
//

#ifndef TTTRLIB_CORRELATE_LAMB_H
#define TTTRLIB_CORRELATE_LAMB_H

#include <iostream>
#include <cstdio>
#include <cmath>
#include <vector>
#include <list>
#include <iterator>
#include <functional>
#include <numeric>
#include <algorithm>
#include <climits>

namespace lamb_lab{
    /*!
     *
     * CAUTION: the arrays t1 and t2 are modified inplace by this function!!
     *
     * @param t1 macrotime vector
     * @param t2 macrotime vector
     * @param photons1
     * @param photons2
     * @param nc number of evenly spaced elements per block
     * @param nb number of blocks of increasing spacing
     * @param np1 number of photons in first channel
     * @param np2 number of photons in second channel
     * @param xdat correlation time bins (timeaxis)
     * @param corrl pointer to correlation output
     */
    void CCF(
            const unsigned long long *t1,
            const unsigned long long *t2,
            const double *weights1,
            const double *weights2,
            unsigned int nc,
            unsigned int nb,
            unsigned int np1,
            unsigned int np2,
            const unsigned long long *xdat, double *corrl
    );

    /*!
     *
     * @param x_axis the uncorrected x-axis (the correlation times)
     * @param corr the uncorrected correlation amplitudes
     * @param corr_normalized the corrected correlation amplitudes (output)
     * @param x_axis_normalized the corrected x-axis (output)
     * @param cr1 count rate in channel 1
     * @param cr2 count rate in channel 2
     * @param n_bins number of evenly spaced elements per block
     * @param n_casc number of blocks of increasing spacing
     * @param maximum_macro_time the maximum macro time
     * @return
     */
    void normalize(
            std::vector<unsigned long long> &x_axis,
            std::vector<double> &corr,
            std::vector<unsigned long long> &x_axis_normalized,
            std::vector<double> &corr_normalized,
            float cr1, float cr2,
            int n_bins, int n_casc, unsigned long long maximum_macro_time
    );
}

#endif //TTTRLIB_CORRELATE_LAMB_H

//
// Created by Thomas-Otavio Peulen on 4/2/20.
//

#ifndef TTTRLIB_PEULEN_H
#define TTTRLIB_PEULEN_H

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
#include <cstring> // memcpy


namespace peulen{


    inline void correlation_shell(unsigned long n, unsigned long long *a) {
        unsigned long i, j, inc;
        unsigned long long v;
        inc = 1; //Determine the starting increment.
        do {
            inc *= 3;
            inc++;
        } while (inc <= n);
        do { //Loop over the partial sorts.
            inc /= 3;
            for (i = inc + 1; i <= n; i++) //Outer loop of straight insertion.
            {
                v = a[i];
                j = i;
                while (a[j - inc] > v) //Inner loop of straight insertion.
                {
                    a[j] = a[j - inc];
                    j -= inc;
                    if (j <= inc) break;
                }
                a[j] = v;
            }
        } while (inc > 1);
    }

    /*!
     * This function implements a correlation algorithm as described by Wahl 2003
     *
     *
     * M. Wahl, I. Gregor, M. Patting, and J. Enderlein, "Fast calculation
     * of fluorescence correlation data with asynchronous time-correlated
     * single-photon counting," Opt. Express  11, 3583-3591 (2003).
     *
     * @param n_casc the number of correlation blocks
     * @param n_bins the number of equaly spaced bins per corrleation block
     * @param taus
     * @param corr
     * @param t1 array of photon arrival times in correlation channel 1
     * @param w1 array of weights of the photons in correlation channel 1
     * @param nt1 number of photons in correlation channel 1
     * @param t2 array of photon arrival times in correlation channel 2
     * @param w2 array of weights of the photons in correlation channel 2
     * @param nt2 number of photons in correlation channel 2
     */
    void correlation_full(
            size_t n_casc, size_t n_bins,
            std::vector<unsigned long long> &taus, std::vector<double> &corr,
            const unsigned long long  *t1, const double *w1, size_t nt1,
            const unsigned long long  *t2, const double *w2, size_t nt2
    );

    /*!
     * Coarsens the time events
     *
     *
     * This function coarsens a set of time events by dividing the times by two.
     * In case two consecutive time events in the array have the same time, the
     * weights of the two events are added to the following weight element and the
     * value of the previous weight is set to zero.
     *
     * @param[in,out] t A vector of the time events of the first channel
     * @param[in,out] w A vector of weights for the time events of the first channel
     * @param[in] nt The number of time events in the first channel
     */
    inline void correlation_coarsen(
            unsigned long long * t, double* w, size_t nt
    );

    /*!
     * Calculates the cross-correlation between two arrays containing time
     * events.
     *
     *
     * Cross-correlates two weighted arrays of events using an approach that
     * utilizes a linear spacing of the bins within a cascade and a logarithmic
     * spacing of the cascades. The function works inplace on the input times, i.e,
     * during the correlation the values of the input times and weights are
     * changed to coarsen the times and weights for every correlation cascade.
     *
     * The start position parameters @param start_1 and @param start_2 and the
     * end position parameters @param end_1 and @param end_1 define which part
     * of the time array of the first and second correlation channel are used
     * for the correlation analysis.
     *
     * The correlation algorithm combines approaches of the following papers:
     *
     *  - Fast calculation of fluorescence correlation data with asynchronous
     *  time-correlated single-photon counting, Michael Wahl, Ingo Gregor,
     *  Matthias Patting, Joerg Enderlein, Optics Express Vol. 11, No. 26, p. 3383
     *
     *  - Fast, flexible algorithm for calculating photon correlations, Ted A.
     *  Laurence, Samantha Fore, Thomas Huser, Optics Express Vol. 31 No. 6, p.829
     *
     *
     * @param[in] start_1 The start position on the time event array of the first channel.
     * @param[in] end_1  The end position on the time event array of the first channel.
     * @param[in] start_2 The start position on the time event array of the second channel.
     * @param[in] end_2  The end position on the time event array of the second channel.
     * @param[in] i_casc The number of the current cascade
     * @param[in] n_bins The number of bins per cascase
     * @param[in] taus A vector containing the correlation times of all cascades
     * @param[out] corr A vector to that the correlation is written by the function
     * @param[in,out] t1 A vector of the time events of the first channel
     * @param[in,out] w1 A vector of weights for the time events of the first channel
     * @param[in] nt1 The number of time events in the first channel
     * @param[in,out] t2 A vector of the time events of the second channel
     * @param[in,out] w2 A vector of weights for the time events of the second channel
     * @param[in] nt2 The number of time events in the second channel
     *
     *
     */
    void correlation(
            size_t start_1, size_t end_1,
            size_t start_2, size_t end_2,
            size_t i_casc, size_t n_bins,
            std::vector<unsigned long long> &taus,
            std::vector<double> &corr,
            const unsigned long long  *t1, const double *w1, size_t nt1,
            const unsigned long long  *t2, const double *w2, size_t nt2
    );

    /*!
     * Normalizes a correlation curve.
     *
     *
     * This normalization applied to correlation curves that were calculated using a
     * linear/logrithmic binning as described in
     *
     *  - Fast calculation of fluorescence correlation data with asynchronous time-correlated
     *  single-photon counting, Michael Wahl, Ingo Gregor, Matthias Patting, Joerg Enderlein,
     *  Optics Express Vol. 11, No. 26, p. 3383
     *
     * @param[in] np1 The sum of the weights in the first correlation channel
     * @param[in] dt1 The time difference between the first event and the last event in the
     * first correlation channel
     * @param[in] np2 The sum of the weights in the second correlation channel
     * @param[in] dt2 The time difference between the first event and the last event in the
     * second correlation channel
     * @param[in,out] x_axis The x-axis of the correlation
     * @param[in,out] corr The array that contains the original correlation that is modified
     * in place.
     * @param[in] n_bins The number of bins per cascade of the correlation
     */
    void correlation_normalize(
            double np1, uint64_t dt1,
            double np2, uint64_t dt2,
            std::vector<unsigned long long> &x_axis, std::vector<double> &corr,
            size_t n_bins,
            bool correct_x_axis = true
    );

    /*!
     * Changes the time events by adding the micro time to the macro time
     *
     *
     * Changes the time events by adding the micro time to the macro time.
     * The micro times should match the macro time, i.e., the length of
     * the micro time array should be the at least the same length as the
     * macro time array.
     *
     * @param[in,out] t An array containing the time events (macro times)
     * @param[in] n_times The number of macro times.
     * @param[in] tac An array containing the micro times of the corresponding macro times
     * @param[in] tac The number of micro time channels (TAC channels)
     */
    void make_fine_times(
            unsigned long long *t,
            unsigned int n_times,
            unsigned int* tac,
            unsigned int n_tac
    );

}


#endif //TTTRLIB_PEULEN_H

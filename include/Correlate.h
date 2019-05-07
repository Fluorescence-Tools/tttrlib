/****************************************************************************
 * Copyright (C) 2019 by Thomas-Otavio Peulen                               *
 *                                                                          *
 * This file is part of the library tttrlib.                                *
 *                                                                          *
 *   tttrlib is free software: you can redistribute it and/or modify it     *
 *   under the terms of the MIT License.                                    *
 *                                                                          *
 *   tttrlib is distributed in the hope that it will be useful,             *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                   *
 *                                                                          *
 ****************************************************************************/

/**
 * @file correlate.h
 * @author Thomas-Otavio Peulen
 * @date 20 Feb 2019
 * @brief File containing example of doxygen usage for quick reference.
 *
 * Here typically goes a more extensive explanation of what the header
 * defines. Doxygens tags are words preceeded by either a backslash @\
 * or by an at symbol @@.
 * @see http://www.stack.nl/~dimitri/doxygen/docblocks.html
 * @see http://www.stack.nl/~dimitri/doxygen/commands.html
 */


//
// Created by thomas on 2/18/19.
//

#ifndef TTTRLIB_CORRELATE_H
#define TTTRLIB_CORRELATE_H

#include <iostream>
#include <stdio.h>
#include <cmath>
#include <vector>
#include <list>
#include <iterator>
#include <functional>
#include <numeric>
#include <algorithm>
#include <limits.h>
#include <taskflow/taskflow.hpp>


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
void normalize_correlation(
        double np1, uint64_t dt1,
        double np2, uint64_t dt2,
        std::vector<unsigned long long> &x_axis, std::vector<double> &corr,
        size_t n_bins
);



/*!
 * Changes the time events by adding the micro time to the macro time
 *
 *
 * Changes the time events by adding the micro time to the macro time.
 * The micro times shoudl match the macro time, i.e., the length of
 * the micro time array should be the at least the same length as the
 * macro time array.
 *
 * @param[in,out] t An array containing the time events (macro times)
 * @param[in] n_times The number of macro times.
 * @param[in] tac An array containing the micro times of the corresponding macro times
 * @param[in] tac The number of micro time channels (TAC channels)

 */
void make_fine_times(unsigned long long *t, unsigned int n_times, unsigned int* tac, unsigned int n_tac);



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
void correlate(
        size_t start_1, size_t end_1,
        size_t start_2, size_t end_2,
        size_t i_casc, size_t n_bins,
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
void coarsen(
        unsigned long long * t, double* w, size_t nt
);



class Correlator {

private:

    /// The number of cascades that coarsen the data
    size_t n_casc;
    /// The number of bins (correlation channels) per cascade
    size_t n_bins;
    /// The total number of correlation channels
    size_t n_corr;

    /// The array containing the times points of the first correlation channel
    unsigned long long* t1;
    /// The array containing the weights of the first correlation channel
    double* w1;
    /// The number of time points in the first correlation channel
    size_t n_t1;

    /// The array containing the times points of the second correlation channel
    unsigned long long * t2;
    /// The array containing the weights of the second correlation channel
    double* w2;
    /// The number of time points in the second correlation channel
    size_t n_t2;

    /// The x-axis (the time axis) of the correlation
    std::vector<unsigned long long> x_axis;
    /// The non-normalized correlation
    std::vector<double> corr;

    /// The normalized x-axis (the time axis) of the correlation
    std::vector<unsigned long long> x_axis_normalized;
    /// The normalized correlation
    std::vector<double> corr_normalized;

    /// The maximum the times in the first and second correlation channel, max(t1, t2)
    uint64_t maximum_macro_time;

    /// The time difference between the first and the last event of the first correlation channel
    uint64_t dt1;
    /// The time difference between the first and the last event of the second correlation channel
    uint64_t dt2;

    /// This flag ist true, if the time events were changed to generate a fine time-axis,
    /// i.e., consider the TAC channels for correlation.
    bool is_fine;


protected:

    /*!
     *
     * tau_j = tau_(i_casc -1 ) + 2 ** (floor(i_casc-1 / n_bins))
     *
     * for n_casc = 3, n_bins = 10 tau_j = [0, 1, 2, ..., 9, 10, 12, ..., 28, 30, 34, ..., 70] (j=0...n_casc*n_bins)
     *
     * Updates x-axis to the current @param n_bins and @param n_casc and reserves memory for the correlation
     */
    void update_axis();


public:

    Correlator() :
        n_casc(1),
        n_bins(1),
        is_fine(false)
    {};
    ~Correlator() = default;

    /*!
     * Sets the number of cascades of the correlation curve and
     * updates the correlation axis.
     * @param[in] n_casc
     */
    void set_n_casc(int n_casc){
        Correlator::n_casc = (size_t) n_casc;
        update_axis();
    }

    /*!
     *
     * @return
     */
    size_t get_n_casc(){
        return n_casc;
    }

    void set_n_bins(int n_bins){
        Correlator::n_bins = (size_t) n_bins;
        update_axis();
    }

    size_t get_n_bins(){
        return n_bins;
    }

    size_t get_n_corr(){
        return n_corr;
    }

    /*!
     * Changes the time axis to consider the micro times.
     * @param[in] tac_1 The micro times of the first correlation channel
     * @param[in] n_tac_1 The number of events in the first correlation channel
     * @param[in] tac_2 The micro times of the second correlation channel
     * @param[in] n_tac_2 The number of events in the second correlation channel
     * @param[in] n_tac The maximum number of TAC channels of the micro times.
     */
    void make_fine(unsigned int* tac_1, unsigned int n_tac_1, unsigned int* tac_2, unsigned int n_tac_2, unsigned int n_tac);

    /*!
     * Get the x-axis of the correlation
     *
     *
     * @param[out] x_axis a pointer to an array that will contain the x-axis
     * @param[out] n_out a pointer to the an integer that will contain the number of elements of the x-axis
     */
    void get_x_axis(unsigned long long** x_axis, int* n_out);


    /*!
     * Get the correlation.
     *
     *
     * @param[out] corr a pointer to an array that will contain the correlation
     * @param[out] n_out a pointer to the an integer that will contain the number of elements of the x-axis
     */
    void get_corr(double** corr, int* n_out);


    /*!
     * Get the normalized x-axis of the correlation
     *
     *
     * @param[out] x_axis a pointer to an array that will contain the x-axis
     * @param[out] n_out a pointer to the an integer that will contain the number of elements
     * of the x-axis
     */
    void get_x_axis_normalized(unsigned long long** x_axis, int* n_out);


    /*!
     * Get the normalized correlation.
     *
     *
     * @param[out] corr a pointer to an array that will contain the normalized correlation
     * @param[out] n_out a pointer to the an integer that will contain the number of elements
     * of the normalized x-axis
     */
    void get_corr_normalized(double** corr, int* n_out);


    /*!
    *
    * @param[in, out] Array t1 of the time events of the first channel (the array is modified in place)
    * @param[in] n_t1 The number of time events in the first channel
    * @param w1 A vector of weights for the time events of the first channel
    * @param n_weights_ch1 The number of weights of the first channel
    * @param t2 A vector of the time events of the second channel
    * @param n_t2 The number of time events in the second channel
    * @param w2 A vector of weights for the time events of the second channel
    * @param n_weights_ch2 The number of weights of the second channel
    */
    void set_events(
            unsigned long long  *t1, int n_t1,
            double* weight_ch1, int n_weights_ch1,
            unsigned long long  *t2, int n_t2,
            double* weight_ch2, int n_weights_ch2
            );

    /*!
     * Calculates the normalized correlation amplitudes and x-axis
     *
     *
     * Makes a copy of the current correlation curve, i.e., the x-axis and and the corresponding
     * correlation amplitudes and calculates the values of the normalized correlation.
     */
    void normalize();

    void run();

};


#endif //TTTRLIB_CORRELATE_H

/****************************************************************************
 * Copyright (C) 2020 by Thomas-Otavio Peulen                               *
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
 * @file correlation.h
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

#ifndef TTTRLIB_CORRELATION_H
#define TTTRLIB_CORRELATION_H

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
//#include <taskflow/taskflow.hpp>

#include <include/correlation/correlate_wahl.h>
#include <include/correlation/correlate_lamb.h>
#include <include/correlation/correlate_seidel.h>


class Correlator{

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

    /// The time difference between the first and the last event of the first
    // correlation channel
    uint64_t dt1;
    /// The time difference between the first and the last event of the
    /// second correlation channel
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
     * @param[out] n_out a pointer to the an integer that will contain the
     * number of elements of the x-axis
     */
    void get_x_axis(unsigned long long** x_axis, int* n_out);

    /*!
     * Get the correlation.
     *
     *
     * @param[out] corr a pointer to an array that will contain the correlation
     * @param[out] n_out a pointer to the an integer that will contain the
     * number of elements of the x-axis
     */
    void get_corr(double** corr, int* n_out);

    /*!
     * Get the normalized x-axis of the correlation
     *
     *
     * @param[out] x_axis a pointer to an array that will contain the x-axis
     * @param[out] n_out a pointer to the an integer that will contain the
     * number of elements
     * of the x-axis
     */
    void get_x_axis_normalized(unsigned long long** x_axis, int* n_out);

    /*!
     * Get the normalized correlation.
     *
     *
     * @param[out] corr a pointer to an array that will contain the
     * normalized  correlation
     * @param[out] n_out a pointer to the an integer that will contain the
     * number of elements of the normalized x-axis
     */
    void get_corr_normalized(double** corr, int* n_out);

    /*!
    *
    * @param[in, out] Array t1 of the time events of the first channel (the
    * array is modified in place)
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
     * Makes a copy of the current correlation curve, i.e., the x-axis and
     * and the corresponding correlation amplitudes and calculates the values
     * of the normalized correlation.
     */
    void normalize(
            std::string correlation_method ="wahl"
            );

    /*!
     * Compute the correlation function
     *
     * @param correlation_method either "wahl" or "lamb", "seidel"
     */
    void run(std::string correlation_method = "wahl");

};


#endif //TTTRLIB_CORRELATION_H

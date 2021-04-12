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


#ifndef TTTRLIB_HISTOGRAM_H
#define TTTRLIB_HISTOGRAM_H

#include <algorithm>
#include <vector>   
#include <cstdio>
#include <string.h>
#include <string>

#include <map>
#include <cmath>
#include "HistogramAxis.h"



template<class T>
class Histogram {

private:

    std::map<size_t , HistogramAxis<T>> axes;

    T* histogram; // A 1D array of that contains the histogram
    int number_of_axis;
    int n_total_bins;
    size_t getAxisDimensions(){
        return axes.size();
    }

public:

    void update(T *data, int n_rows_data, int n_cols_data){
        int axis_index;
        int global_bin_idx;
        int global_bin_offset;
        int n_axis;
        int current_bin_idx, current_n_bins;
        HistogramAxis<T> *current_axis;
        T data_value;

        // update the axes
        for(const auto& p : axes){
            axes[p.first].update();
        }

        // initialize a new empty histogram
        // clear the memory of the old histogram
        free(histogram);
        // 1. count the total number of bins
        n_total_bins = 1;
        n_axis = 0;
        for(const auto& p : axes){
            axis_index = p.first;
            n_axis += 1;
            n_total_bins *= axes[axis_index].getNumberOfBins();
        }
        // 2. fill the histogram with zeros
        histogram = (T*) malloc(sizeof(T) * (n_total_bins));
        for(global_bin_idx=0; global_bin_idx < n_total_bins; global_bin_idx++){
            histogram[global_bin_idx] = 0.0;
        }

        // Fill the histogram
        // Very instructive for multi-dimensional array indexing
        // https://eli.thegreenplace.net/2015/memory-layout-of-multi-dimensional-arrays/
        bool is_inside;
        for(int i_row=0; i_row<n_rows_data; i_row++){
            // in this loop the position within the 1D array is calculated
            global_bin_offset = 0;
            is_inside = true;
            for(const auto& p : axes){
                axis_index = p.first;
                current_axis = &axes[axis_index];

                data_value = data[i_row*n_axis + axis_index];
                current_bin_idx = current_axis->getBinIdx(data_value);
                current_n_bins = current_axis->getNumberOfBins();

                if( (current_bin_idx < 0) || (current_bin_idx >= current_n_bins) ){
                    is_inside = false;
                    break;
                }
                global_bin_offset = current_bin_idx + current_n_bins * global_bin_offset;
            }
            if(is_inside){
                histogram[global_bin_offset] += 1.0;
            }
        }
    }

    void get_histogram(T** hist, int* dim){
        *hist = histogram;
        *dim = n_total_bins;
    }

    void set_axis(size_t data_column, HistogramAxis<T> &new_axis){
        axes[data_column] = new_axis;
    }

    void set_axis(
            size_t data_column,
            std::string name,
            T begin, T end, int n_bins,
            std::string axis_type
            ){
        HistogramAxis<T> new_axis(name, begin, end, n_bins, axis_type);
        set_axis(data_column, new_axis);
    }

    HistogramAxis<T> get_axis(size_t axis_index){
        return axes[axis_index];
    }

    Histogram() = default;

    ~Histogram() = default;

};


void bincount1D(int *data, int n_data, int *bins, int n_bins);


/*!
 *
 * @tparam T
 * @param data
 * @param n_data
 * @param weights
 * @param n_weights
 * @param bin_edges contains the edges of the histogram in ascending order (from small to large)
 * @param n_bins the number of bins in the histogram
 * @param hist
 * @param n_hist
 * @param axis_type
 * @param use_weights if true the weights specified by @param weights are used for the calculation of the histogram
 * instead of simply counting the frequency.
 */
template<typename T>
void histogram1D(
        T *data, int n_data,
        double *weights, int n_weights,
        T *bin_edges, int n_bins,
        double *hist, int n_hist,
        const char *axis_type,
        bool use_weights
) {
    T v; // stores the data value in iterations
    int i, bin_idx;
    T lower, upper, bin_width;
    bool is_log10 = !strcmp(axis_type, "log10");
    bool is_lin = !strcmp(axis_type, "lin");

    if (is_lin || is_log10) {
        if (is_log10) {
            lower = std::log10(bin_edges[0]);
            upper = std::log10(bin_edges[n_bins - 1]);
        } else {
            lower = bin_edges[0];
            upper = bin_edges[n_bins - 1];
        }
        bin_width = (upper - lower) / (n_bins - 1);

        for (i = 0; i < n_data; i++) {
            v = data[i];
            if(is_log10){
                if(v == 0){
                    continue;
                } else {
                    v = std::log10(v);
                }
            }
            bin_idx = calc_bin_idx(lower, bin_width, v);
            // ignore values outside of the bounds
            if ((bin_idx <= n_bins) && (bin_idx >= 0)){
                hist[bin_idx] += (use_weights) ? weights[i] : 1;
            }
        }
    } else {
        for (i = 0; i < n_data; i++) {
            v = data[i];
            bin_idx = search_bin_idx(v, bin_edges, n_bins);
            if(bin_idx > 0)
                hist[bin_idx] += (use_weights) ? weights[i] : 1;
        }
    }

}


#endif //TTTRLIB_HISTOGRAM_H

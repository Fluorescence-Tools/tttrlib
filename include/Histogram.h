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


#ifndef TTTRLIB_HISTOGRAM_H
#define TTTRLIB_HISTOGRAM_H

#include <algorithm>
#include <vector>   
#include <stdio.h>
#include <string.h>
#include <string>

#include <map>
#include <cmath>




/*! Searches for the bin index of a value within a list of bin edges
 *
 * If a value is inside the bounds find the bin.
 * The search partitions the bin_edges in upper and lower ranges and
 * adapts the edge for the upper and lower range depending if the target
 * value is bigger or smaller than the bin in the middle.

 * @tparam T
 * @param value
 * @param bin_edges
 * @param n_bins
 * @return negative value if the search value is out of the bounds. Otherwise the bin number
 * is returned.
 */
template <typename T>
inline int search_bin_idx(T value, T *bin_edges, int n_bins){
    int b, e, m;

    // ignore values outside of the bounds
    if ((value < bin_edges[0]) || (value > bin_edges[n_bins - 2])) {
        return -1;
    }

    b = 0;
    e = n_bins;
    do {
        m = (e - b) / 2 + b;
        if (value > bin_edges[m]) {
            b = m;
        } else {
            e = m;
        }
    } while ((value < bin_edges[m]) || (value >= bin_edges[m + 1]));
    return m;
}


template <typename T>
inline void linspace(double start, double stop, T *bin_edges, int n_bins){
    double bin_width = (stop - start) / n_bins;
    for(int i=0; i<n_bins; i++){
        bin_edges[i] = start + ((T) i) * bin_width;
    }
}


template <typename T>
inline void logspace(double start, double stop, T *bin_edges, int n_bins){
    linspace(std::log(start), std::log(stop), bin_edges, n_bins);
    for(int i=0; i<n_bins; i++){
        bin_edges[i] = std::pow(10.0, bin_edges[i]);
    }
}


/*!
 * Calculates for a linear axis the bin index for a particular value.
 *
 * @tparam T
 * @param begin
 * @param bin_width
 * @param value
 * @return
 */
template <typename T>
inline int calc_bin_idx(T begin, T bin_width, T value){
    return  ((value - begin) / bin_width);
}



template<class T>
class HistogramAxis{

private:
    std::string name;
    double begin;
    double end;
    int n_bins;
    double bin_width; // for logarithmic spacing the bin_width in logarithms
    T* bin_edges;
    int axis_type;

protected:


public:

    /*!
     * Recalculates the bin edges of the axis
    */
    void update(){
        free(bin_edges);
        bin_edges = (T*) malloc(n_bins * sizeof(T));
        switch (HistogramAxis::axis_type){
            case 0:
                // linear axis
                bin_width = (end - begin) / n_bins;
                linspace(begin, end, bin_edges, n_bins);
                break;
            case 1:
                // logarithmic axis
                bin_width = (std::log(end) - std::log(begin)) / n_bins;
                logspace(begin, end, bin_edges, n_bins);
                break;
        }
    }

    void setAxisType(const std::string &axis_type) {
        if(axis_type == "log10")
            HistogramAxis::axis_type = 1;
        if(axis_type == "lin")
            HistogramAxis::axis_type = 0;
    }

    int getNumberOfBins(){
        return n_bins;
    }


    int getBinIdx(T value){
        switch (axis_type){
            case 0:
                // linear
                return calc_bin_idx(begin, bin_width, value);
            case 1:
                // logarithm
                return calc_bin_idx(begin, bin_width, std::log10(value));
            default:
                return search_bin_idx(value, bin_edges, n_bins);
        }
    }

    T* getBins(){
        return bin_edges;
    }

    void getBins(T* bin_edges, int n_bins){
        for(int i = 0; i < n_bins; i++){
            bin_edges[i] = this->bin_edges[i];
        }
    }

    const std::string &getName() const {
        return name;
    }

    void setName(const std::string &name) {
        HistogramAxis::name = name;
    }

    HistogramAxis() = default;
    HistogramAxis(
            std::string name,
            T begin,
            T end,
            int n_bins,
            std::string axis_type
            ){

        // make sure that begin < end
        if(begin > end){
            T temp = begin;
            begin = end;
            end = temp;
        }

        HistogramAxis::begin = begin;
        HistogramAxis::end = end;
        HistogramAxis::n_bins = n_bins;
        HistogramAxis::setAxisType(axis_type);
        HistogramAxis::name = name;
        HistogramAxis::update();
    }
};


template<class T>
class Histogram {

private:
    T* data;
    int n_rows_data,  n_cols_data;

    T* weights;
    int n_rows_weights,  n_cols_weights;

    std::map<size_t , HistogramAxis<T>> axes;

    T* histogram; // A 1D array of that contains the histogram
    int number_of_axis;

public:
    int n_total_bins;

    size_t getAxisDimensions(){
        return axes.size();
    }

    void update(){
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

    void getHistogram(T** hist, int* dim){
        *hist = histogram;
        *dim = n_total_bins;
    }

    void setAxis(size_t data_column, HistogramAxis<T> &new_axis){
        axes[data_column] = new_axis;
    }

    void setAxis(size_t data_column, std::string name, T begin, T end, int n_bins, std::string axis_type){
        HistogramAxis<T> new_axis(name, begin, end, n_bins, axis_type);
        setAxis(data_column, new_axis);
    }

    HistogramAxis<T> getAxis(size_t axis_index){
        return axes[axis_index];
    }

    void setWeights(T *weights, int n_rows, int n_cols) {
        Histogram::weights = weights;
        Histogram::n_rows_weights = n_rows;
        Histogram::n_rows_weights = n_cols;
    }

    void setData(T *data, int n_rows, int n_cols) {
        Histogram::data = data;
        Histogram::n_rows_data = n_rows;
        Histogram::n_cols_data = n_cols;
    }

    Histogram() = default;

    ~Histogram() = default;
};



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


void bincount1D(int *data, int n_data, int *bins, int n_bins);

#endif //TTTRLIB_HISTOGRAM_H

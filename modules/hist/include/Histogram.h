// SPDX-License-Identifier: BSD-3-Clause
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

    T* histogram = nullptr; // A 1D array of that contains the histogram
    int number_of_axis = 0;
    int n_total_bins = 0;
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
        if (histogram != nullptr) {
            free(histogram);
        }
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

    /**
     * @brief Gets the histogram as a raw pointer and size.
     * 
     * Allocates a copy of the internal data that Python can safely own and free.
     * The SWIG typemap will call free() on this pointer when the numpy array is garbage collected.
     *
     * @param hist Pointer to receive the data pointer (T*)
     * @param dim Pointer to receive the number of elements
     */
    void get_histogram(T** hist, int* dim){
        if (histogram == nullptr || n_total_bins <= 0) {
            *hist = nullptr;
            *dim = 0;
            return;
        }
        // Allocate a copy using malloc() so Python can safely free() it
        // This aligns with SWIG's ARGOUTVIEWM_ARRAY1 typemap
        *hist = (T*) malloc(sizeof(T) * n_total_bins);
        if (*hist != nullptr) {
            memcpy(*hist, histogram, sizeof(T) * n_total_bins);
        }
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

    virtual ~Histogram() {
        if (histogram != nullptr) {
            free(histogram);
            histogram = nullptr;
        }
    }

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


/*!
 * \brief Find the bin for one value on one axis, or -1 if it falls outside.
 *
 * Split out of histogram1D so the two-dimensional case cannot drift from the
 * one-dimensional one: an event belongs in bin (i, j) exactly when it would
 * have landed in bin i of a 1D histogram over x and bin j of one over y.
 */
template<typename T>
inline int histogram_bin_of(T value, T *bin_edges, int n_bins,
                            bool is_lin, bool is_log10) {
    if (is_lin || is_log10) {
        T lower, upper;
        if (is_log10) {
            if (value <= 0) return -1;      // log10 of a non-positive value
            lower = std::log10(bin_edges[0]);
            upper = std::log10(bin_edges[n_bins - 1]);
            value = std::log10(value);
        } else {
            lower = bin_edges[0];
            upper = bin_edges[n_bins - 1];
        }
        const T bin_width = (upper - lower) / (n_bins - 1);
        const int idx = calc_bin_idx(lower, bin_width, value);
        return (idx >= 0 && idx < n_bins) ? idx : -1;
    }
    const int idx = search_bin_idx(value, bin_edges, n_bins);
    return (idx > 0 && idx < n_bins) ? idx : -1;
}


/*!
 * \brief Two-dimensional histogram of paired values.
 *
 * Each axis is binned exactly as histogram1D bins its one, and independently:
 * the two may use different bin counts and different axis types, so a
 * lifetime-versus-intensity plot can be linear in one and logarithmic in the
 * other without the caller pre-transforming anything.
 *
 * The output is row-major with x as the slow axis, i.e. `hist[i * n_bins_y + j]`
 * is the count for x-bin i and y-bin j. That is what numpy reshapes to
 * `(n_bins_x, n_bins_y)` without a copy.
 *
 * Pairs are dropped when EITHER coordinate falls outside its axis. Clamping
 * them to the edge bins instead would pile everything outside the range onto
 * the border, which reads as structure that is not in the data.
 *
 * @tparam T value type of the two data arrays
 * @param data_x, n_data_x first coordinate of each pair
 * @param data_y, n_data_y second coordinate; must be the same length as data_x
 * @param weights, n_weights per-pair weights, used only when use_weights is true
 * @param bin_edges_x, n_bins_x bin edges of the x axis, in ascending order
 * @param bin_edges_y, n_bins_y bin edges of the y axis, in ascending order
 * @param hist, n_hist output, n_bins_x * n_bins_y entries, added to (not cleared)
 * @param axis_type_x, axis_type_y "lin", "log10", or anything else for a search
 *        over arbitrary edges
 * @param use_weights add weights[i] instead of 1
 */
template<typename T>
void histogram2D(
        T *data_x, int n_data_x,
        T *data_y, int n_data_y,
        double *weights, int n_weights,
        T *bin_edges_x, int n_bins_x,
        T *bin_edges_y, int n_bins_y,
        double *hist, int n_hist,
        const char *axis_type_x,
        const char *axis_type_y,
        bool use_weights
) {
    // A shorter y array would otherwise be read past its end for every pair
    // beyond it -- silently, and with plausible-looking output.
    const int n = std::min(n_data_x, n_data_y);
    if (n_hist < n_bins_x * n_bins_y) return;

    const bool x_is_log10 = !strcmp(axis_type_x, "log10");
    const bool x_is_lin   = !strcmp(axis_type_x, "lin");
    const bool y_is_log10 = !strcmp(axis_type_y, "log10");
    const bool y_is_lin   = !strcmp(axis_type_y, "lin");

    for (int i = 0; i < n; i++) {
        const int ix = histogram_bin_of(data_x[i], bin_edges_x, n_bins_x, x_is_lin, x_is_log10);
        if (ix < 0) continue;
        const int iy = histogram_bin_of(data_y[i], bin_edges_y, n_bins_y, y_is_lin, y_is_log10);
        if (iy < 0) continue;
        hist[ix * n_bins_y + iy] +=
                (use_weights && i < n_weights) ? weights[i] : 1;
    }
}


#endif //TTTRLIB_HISTOGRAM_H

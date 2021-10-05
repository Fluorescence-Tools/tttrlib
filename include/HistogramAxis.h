#ifndef TTTRLIB_HISTOGRAMAXIS_H
#define TTTRLIB_HISTOGRAMAXIS_H

#include <algorithm>
#include <vector>
#include <cstdio>
#include <string>
#include <map>
#include <cmath>

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
    std::vector<T> bin_edges;
    int axis_type;

public:

    /*!
     * Recalculates the bin edges of the axis
    */
    void update(){
        bin_edges.resize(n_bins);
        switch (HistogramAxis::axis_type){
            case 0:
                // linear axis
                bin_width = (end - begin) / n_bins;
                linspace(begin, end, bin_edges.data(), bin_edges.size());
                break;
            case 1:
                // logarithmic axis
                bin_width = (log(end) - log(begin)) / n_bins;
                logspace(begin, end, bin_edges.data(), bin_edges.size());
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
                return search_bin_idx(value, bin_edges.data(), bin_edges.size());
        }
    }

    T* getBins(){
        return bin_edges.data();
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

#endif //TTTRLIB_HISTOGRAMAXIS_H

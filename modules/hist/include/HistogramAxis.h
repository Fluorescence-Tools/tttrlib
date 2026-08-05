// SPDX-License-Identifier: BSD-3-Clause
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


/*!
 * \brief The bin edges a range implies, so a caller can get them back.
 *
 * A front end that says "64 bins from 0 to 100" still has to label the axis it
 * just asked for, and reconstructing the edges by hand is where an off-by-one
 * or a half-bin shift gets in. `n_bins` values are written: bin i starts at
 * `lo + i * (hi - lo) / (n_bins - 1)`, geometrically spaced when `log`.
 */
template<typename T>
inline void make_bin_edges(T* edges_out, int n_edges_out, double lo, double hi, bool log) {
    // edges_out, not bin_edges: this one is written to rather than read from, so
    // it needs a different SWIG typemap -- and %apply is global and keyed by
    // parameter name, so reusing the name silently redefines what
    // (double* bin_edges, int n_bins) means for histogram1D as well.
    if (edges_out == nullptr || n_edges_out <= 0) return;
    if (n_edges_out == 1) { edges_out[0] = static_cast<T>(lo); return; }
    if (log) {
        const double a = std::log(lo), b = std::log(hi);
        const double w = (b - a) / (n_edges_out - 1);
        for (int i = 0; i < n_edges_out; i++)
            edges_out[i] = static_cast<T>(std::exp(a + w * i));
    } else {
        const double w = (hi - lo) / (n_edges_out - 1);
        for (int i = 0; i < n_edges_out; i++)
            edges_out[i] = static_cast<T>(lo + w * i);
    }
}


/*!
 * \brief A binning rule reduced to what an inner loop needs.
 *
 * Every histogram in tttrlib was doing the same three things per value --
 * decide linear or logarithmic, work out where the axis starts and how wide a
 * bin is, then map the value -- and doing them per VALUE rather than per axis.
 * histogram1D recomputed `log10(bin_edges[0])` and `log10(bin_edges[n-1])` for
 * every photon it binned. This does that work once, in the constructor, and
 * leaves the loop with a subtract, a multiply and a floor.
 *
 * Three conventions have to coexist, because they are all in use:
 *
 * - **edges**: bins start at `bin_edges[0]`, and the listed edges are edges.
 *   What histogram1D and histogram2D use.
 * - **centered**: the listed values are bin CENTRES, so the axis actually
 *   starts half a bin lower. What Pda::get_1dhistogram uses -- its bins are
 *   the x values it reports back, and shifting them by half a bin would move
 *   every point in a published PDA plot.
 * - **search**: arbitrary, not-necessarily-uniform edges, resolved by binary
 *   search. No affine map exists, so this one keeps its loop.
 *
 * Logarithms are natural, not base 10, even for a "log10" axis. The bin index
 * is `(log(v) - log(lo)) / ((log(hi) - log(lo)) / (n - 1))`, and the base
 * cancels out of that ratio exactly; std::log is the cheaper of the two.
 */
template<typename T>
class HistogramBinning {
public:
    /// Bins delimited by `bin_edges`. `axis_type` is "lin", "log10", or
    /// anything else to mean arbitrary edges resolved by search.
    HistogramBinning(const T* bin_edges, int n_bins, const char* axis_type)
            : n_bins_(n_bins), edges_(bin_edges) {
        const bool is_log = axis_type != nullptr && !strcmp(axis_type, "log10");
        const bool is_lin = axis_type != nullptr && !strcmp(axis_type, "lin");
        if (!is_log && !is_lin) { search_ = true; return; }
        if (n_bins < 2 || bin_edges == nullptr) { degenerate_ = true; return; }
        log_ = is_log;
        double lo = static_cast<double>(bin_edges[0]);
        double hi = static_cast<double>(bin_edges[n_bins - 1]);
        if (log_) {
            if (!(lo > 0.0) || !(hi > 0.0)) { degenerate_ = true; return; }
            lo = std::log(lo);
            hi = std::log(hi);
        }
        set_affine(lo, hi, n_bins, /*centered=*/false);
    }

    /*!
     * Bins CENTRED on `n_bins` values spanning `lo` to `hi`.
     *
     * \param lo, hi first and last bin centre
     * \param n_bins number of bins
     * \param log the centres are geometrically rather than linearly spaced
     */
    static HistogramBinning<T> centered(double lo, double hi, int n_bins, bool log) {
        HistogramBinning<T> b;
        b.n_bins_ = n_bins;
        if (n_bins < 2) { b.degenerate_ = true; return b; }
        b.log_ = log;
        if (log) {
            if (!(lo > 0.0) || !(hi > 0.0)) { b.degenerate_ = true; return b; }
            lo = std::log(lo);
            hi = std::log(hi);
        }
        b.set_affine(lo, hi, n_bins, /*centered=*/true);
        return b;
    }

    /// The bin for `value`, or -1 if it falls outside the axis.
    inline int bin_of(T value) const {
        if (search_) {
            const int idx = search_bin_idx(value, const_cast<T*>(edges_), n_bins_);
            return (idx > 0 && idx < n_bins_) ? idx : -1;
        }
        if (degenerate_) return -1;
        double v = static_cast<double>(value);
        if (log_) {
            // log of a non-positive value is not a bin index.
            if (!(v > 0.0)) return -1;
            v = std::log(v);
        }
        const int idx = static_cast<int>(std::floor((v - offset_) * inv_width_));
        return (idx >= 0 && idx < n_bins_) ? idx : -1;
    }

    int n_bins() const { return n_bins_; }
    /// Width of one bin, in the axis's own (possibly logarithmic) units.
    double bin_width() const { return inv_width_ != 0.0 ? 1.0 / inv_width_ : 0.0; }

private:
    HistogramBinning() = default;

    void set_affine(double lo, double hi, int n_bins, bool centered) {
        const double width = (hi - lo) / (static_cast<double>(n_bins) - 1.0);
        if (!(width != 0.0) || !std::isfinite(width)) { degenerate_ = true; return; }
        inv_width_ = 1.0 / width;
        offset_ = centered ? lo - 0.5 * width : lo;
    }

    int n_bins_ = 0;
    const T* edges_ = nullptr;
    double offset_ = 0.0;
    double inv_width_ = 0.0;
    bool log_ = false;
    bool search_ = false;
    bool degenerate_ = false;
};

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

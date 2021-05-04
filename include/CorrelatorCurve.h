//
// Created by tpeulen on 10/30/20.
//

#ifndef TTTRLIB_CORRELATORCURVE_H
#define TTTRLIB_CORRELATORCURVE_H

#include <iostream>
#include <vector>
#include <algorithm>  /* std::max */
#include <cmath> /* pow */


struct CorrelationCurveSettings{

    /// Time calibration the duration of a single macro time unit
    double macro_time_duration = 1.0;

    /// The number of cascades that coarsen the data
    unsigned int n_casc = 25;

    /// The number of bins (correlation channels) per cascade
    unsigned int n_bins = 17;

    /// The number of points in a correlation curve
    unsigned int get_ncorr() const{
        return n_casc * n_bins + 1;
    }

    std::string correlation_method;

};


class CorrelatorCurve{

    friend class Correlator;

private:

    /*!
     *
     * tau_j = tau_(i_casc -1 ) + 2 ** (floor(i_casc-1 / n_bins))
     *
     * for n_casc = 3, n_bins = 10 tau_j = [0, 1, 2, ..., 9, 10, 12, ..., 28, 30, 34, ..., 70] (j=0...n_casc*n_bins)
     *
     * Updates x-axis to the current parameters
     */
    void update_axis();

protected:

    /// Stores the settings of the correlation curve, i.e., the number of correlation bins
    CorrelationCurveSettings settings;

    /// The x-axis (the time axis) of the correlation
    std::vector<unsigned long long> x_axis;

    /// The non-normalized correlation
    std::vector<double> correlation;

    /// The normalized correlation
    std::vector<double> corr_normalized;

    void resize(size_t n){
        x_axis.resize(n);
        correlation.resize(n);
        corr_normalized.resize(n);
    }

    void clear(){
        std::fill(correlation.begin(), correlation.end(), 0.0);
    }

public:

    size_t size(){
        return x_axis.size();
    }

    /*!
     * Get the x-axis of the correlation
     *
     *
     * @param[out] x_axis a pointer to an array that will contain the x-axis
     * @param[out] n_out a pointer to the an integer that will contain the
     * number of elements of the x-axis
     */
    void get_x_axis(double **output, int *n_output);

    /*!
     * @param[in] v  the number of equally spaced correaltion channels per block
     */
    void set_n_bins(int v) {
        settings.n_bins = std::max(1, v);
        update_axis();
    }

    unsigned int get_n_bins() {
        return settings.n_bins;
    }

    /*!
     * Sets the number of cascades (also called blocks) of the correlation curve
     * and updates the correlation axis.
     * @param[in] n_casc
     */
    void set_n_casc(int v) {
        settings.n_casc = std::max(1, v);
        update_axis();
    }

    /*!
     * @return number of correlation blocks
     */
    unsigned int get_n_casc() const {
        return settings.n_casc;
    }

    void get_corr(double** output, int* n_output);

    void get_corr_normalized(double** output, int* n_output);

};


#endif //TTTRLIB_CORRELATORCURVE_H

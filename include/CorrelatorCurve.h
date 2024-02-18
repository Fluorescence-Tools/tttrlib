#ifndef TTTRLIB_CORRELATORCURVE_H
#define TTTRLIB_CORRELATORCURVE_H

#include <iostream>
#include <vector>
#include <algorithm>  /* std::max */
#include <cmath> /* pow */
#include <cstdint> // include this header for uint64_t



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
     * @brief Updates x-axis (correlation bins) to the current parameters
     *  
     * tau_j = tau_(i_casc -1 ) + 2 ** (floor(i_casc-1 / n_bins))
     *
     * for n_casc = 3, n_bins = 10 tau_j = [0, 1, 2, ..., 9, 10, 12, ..., 28, 30, 34, ..., 70] (j=0...n_casc*n_bins)
     *
     * 
     */
    void update_axis();

protected:

    /// The x-axis (the time axis) of the correlation
    std::vector<unsigned long long> x_axis;

    /// The non-normalized correlation
    std::vector<double> correlation;

    /// The normalized correlation
    std::vector<double> corr_normalized;

    /**
     * @brief Resizes the internal vectors to a specified size.
     *
     * @param n The new size for the vectors.
     */
    void resize(size_t n){
        x_axis.resize(n);
        correlation.resize(n);
        corr_normalized.resize(n);
    }

    /**
     * @brief Clear the correlation values.
     *
     * This method sets all values in the correlation vector to zero, effectively
     * resetting the correlation curve. After calling this method, the correlation
     * curve will be empty.
     *
     * Example Usage:
     * @code
     *   // Clear the correlation values
     *   correlatorCurve.clear();
     * @endcode
     */
    void clear(){
        std::fill(correlation.begin(), correlation.end(), 0.0);
    }

public:

    /// Stores the settings of the correlation curve, i.e., the number of correlation bins
    CorrelationCurveSettings settings;

    /**
     * @brief Get the size of the correlation curve.
     *
     * This method returns the size of the correlation curve, which corresponds
     * to the number of elements in the x-axis and correlation vectors.
     *
     * @return The size of the correlation curve.
     *
     * Example Usage:
     * @code
     *   // Get the size of the correlation curve
     *   size_t curveSize = correlatorCurve.size();
     * @endcode
     */
    size_t size(){
        return x_axis.size();
    }

    /**
     * @brief Get the x-axis of the correlation.
     *
     * This method retrieves the x-axis (time axis) of the correlation curve.
     *
     * @param[out] output A pointer to an array that will contain the x-axis.
     * @param[out] n_output A pointer to an integer that will contain the
     * number of elements in the x-axis.
     */
    void get_x_axis(double **output, int *n_output);

    /*!
     * @brief Set the x-axis to arbitrary bin values.
     *
     * Attention: Make sure that the correlation method supports arbitrary bin spacing.
     *
     * @param[in] input A vector of long long unsigned integers representing the desired bin values.
     */
    void set_x_axis(std::vector<long long unsigned int> input);

    /*!
     * @brief Set the number of equally spaced correlation channels per block.
     *
     * This method updates the number of bins in the correlation curve. The number
     * of bins is set to the maximum of the specified value and 1. After updating
     * the settings, the x-axis (correlation bins) is also updated accordingly.
     *
     * @param[in] v The number of equally spaced correlation channels per block.
     */
    void set_n_bins(int v) {
        settings.n_bins = std::max(1, v);
        update_axis();
    }

    /*!
     * @brief Get the number of equally spaced correlation channels per block.
     *
     * This method retrieves the number of bins in the correlation curve.
     *
     * @return The number of equally spaced correlation channels per block.
     */
    unsigned int get_n_bins() {
        return settings.n_bins;
    }

    /*!
     * @brief Set the number of cascades (blocks) in the correlation curve and update the correlation axis.
     *
     * This method sets the number of cascades (blocks) in the correlation curve and ensures it is at least 1.
     * It then updates the correlation axis accordingly.
     *
     * @param[in] v The desired number of cascades.
     */
    void set_n_casc(int v) {
        settings.n_casc = std::max(1, v);
        update_axis();
    }

    /*!
     * @brief Get the number of cascades (blocks) in the correlation curve.
     *
     * This method returns the current number of cascades (blocks) in the correlation curve.
     *
     * @return The number of cascades.
     */
    unsigned int get_n_casc() const {
        return settings.n_casc;
    }

    /*!
     * @brief Get the non-normalized correlation values.
     *
     * @param[out] output A pointer to an array that will contain the non-normalized correlation values.
     * @param[out] n_output A pointer to an integer that will contain the number of elements in the correlation array.
     */
    void get_corr(double** output, int* n_output);

    /*!
     * @brief Get the normalized correlation values.
     *
     * @param[out] output A pointer to an array that will contain the normalized correlation values.
     * @param[out] n_output A pointer to an integer that will contain the number of elements in the normalized correlation array.
     */
    void get_corr_normalized(double** output, int* n_output);

};


#endif //TTTRLIB_CORRELATORCURVE_H

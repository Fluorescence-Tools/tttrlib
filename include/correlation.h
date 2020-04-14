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
#include <map>
//#include <taskflow/taskflow.hpp>

#include <include/tttr.h>
#include <include/correlation/peulen.h>
#include <include/correlation/lamb.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))


class Correlator {

friend class CLSMImage;

private:

    TTTR *tttr;

    /// The used correlation method. Currently either "peulen" or
    /// "lamb".
    std::string correlation_method = "default";

    /// Time axis calibration is a factor that converts the time axsis
    /// of the time events to milliseconds. By default this value is set
    /// to 1.0 and the returned time axis is in units of the time events.
    double time_axis_calibration = 1.0;

    /// This is set to true if the output of the correlator is valid, i.e.,
    /// if the correlation function corresponds to the input
    bool is_valid = false;

    /// The number of cascades that coarsen the data
    int n_casc;
    /// The number of bins (correlation channels) per cascade
    int n_bins;
    /// The total number of correlation channels
    int n_corr = 0;

    /// The array containing the times points of the first correlation channel
    unsigned long long *t1 = nullptr;
    /// The array containing the weights of the first correlation channel
    double *w1 = nullptr;
    /// The number of time points in the first correlation channel
    size_t n_t1 = 0;

    /// The array containing the times points of the second correlation channel
    unsigned long long *t2 = nullptr;
    /// The array containing the weights of the second correlation channel
    double *w2 = nullptr;
    /// The number of time points in the second correlation channel
    size_t n_t2 = 0;

protected:
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
    /// i.e., consider the TAC channels for a full correlation.
    bool is_fine;


protected:

    /*!
     * Computes the the delta t for Ch1, Ch2 and the maximum delta t. Delta t
     * is the difference between the first and the last photon.
     */
    void compute_dt();

    /*!
     *
     * tau_j = tau_(i_casc -1 ) + 2 ** (floor(i_casc-1 / n_bins))
     *
     * for n_casc = 3, n_bins = 10 tau_j = [0, 1, 2, ..., 9, 10, 12, ..., 28, 30, 34, ..., 70] (j=0...n_casc*n_bins)
     *
     * Updates x-axis to the current @param n_bins and @param n_casc and reserves memory for the correlation
     */
    void update_axis();


    /*!
     * Get the x-axis of the correlation
     *
     *
     * @param[out] x_axis a pointer to an array that will contain the x-axis
     * @param[out] n_out a pointer to the an integer that will contain the
     * number of elements of the x-axis
     */
    void get_x_axis(unsigned long long **output, int *n_output);

    /*!
     * Calculates the normalized correlation amplitudes and x-axis
     *
     *
     * Makes a copy of the current correlation curve, i.e., the x-axis and
     * and the corresponding correlation amplitudes and calculates the values
     * of the normalized correlation.
     */
    void normalize();


public:
    void set_time_axis_calibration(double v) {
#if VERBOSE
        std::clog << "-- Time axis calibration [ms/bin]: " << v << std::endl;
#endif
        time_axis_calibration = v;
        is_valid = false;
    }

    /*!
     *
     * @param tttr an optional TTTR object. The macro and micro time calibdation
     * of the header in the TTTR object calibrate the correlator.
     * @param method name of correlation method that is used by the correlator
     * @param n_bins the number of equally spaced correlation bins per block
     * @param n_casc the number of blocks
     */
    Correlator(
            TTTR *tttr = nullptr,
           std::string method = "default",
           int n_bins = 17,
           int n_casc = 25
    );

    /// Destructor
    ~Correlator() {
        free(t1); free(t2);
        free(w1); free(w2);
    };


    /*!
     * Sets the number of cascades (also called blocks) of the correlation curve
     * and updates the correlation axis.
     * @param[in] n_casc
     */
    void set_n_casc(int v) {
        is_valid = false;
        n_casc = MAX(1, v);
        update_axis();
    }

    /*!
     * @return number of correlation blocks
     */
    int get_n_casc() {
        return n_casc;
    }

    /*!
     * @param[in] v  the number of equally spaced correaltion channels per block
     */
    void set_n_bins(int v) {
        is_valid = false;
        n_bins = MAX(1, v);
        update_axis();
    }

    /*!
     * @return the number of equally spaced correlation channels per block
     */
    int get_n_bins() {
        return n_bins;
    }

    /*!
     * @return the overall (total number) of correlation channels
     */
    size_t get_n_corr() {
        return n_corr;
    }

    /*!
     * Set method that to correlate the data
     * @param[in] cm the name of the method
     */
    void set_correlation_method(std::string cm) {
        is_valid = false;
        correlation_method = cm;
    }

    /*!
     * @return name of the used correlation method
     */
    std::string get_correlation_method() {
        return correlation_method;
    }

    /*!
     * Changes the time axis to consider the micro times.
     * @param[in] tac_1 The micro times of the first correlation channel
     * @param[in] n_tac_1 The number of events in the first correlation channel
     * @param[in] tac_2 The micro times of the second correlation channel
     * @param[in] n_tac_2 The number of events in the second correlation channel
     * @param[in] n_tac The maximum number of TAC channels of the micro times.
     */
    void set_microtimes(
            unsigned int *tac_1,
            unsigned int n_tac_1,
            unsigned int *tac_2,
            unsigned int n_tac_2,
            unsigned int n_tac
    );

    /*!
    * @param[in] t1 time events in the the first correlation channel
    * @param[in] n_t1 The number of time events in the first channel
    * @param[in] t1 time events in the the second correlation channel
    * @param[in] n_t2 The number of time events in the second channel
    * @param[in] inplace If true (default false) the arrays are modified in place
    */
    void set_macrotimes(
            unsigned long long *t1, int n_t1,
            unsigned long long *t2, int n_t2,
            bool inplace = false
    );

    /*!
    *
    * @param[in] time events of the first correlation channel
    * @param[in] n_t1 The number of time events in the first channel
    * @param[in] w1 A vector of weights for the time events of the first channel
    * @param[in] n_weights_ch1 The number of weights of the first channel
    * @param[in] t2 A vector of the time events of the second channel
    * @param[in] n_t2 The number of time events in the second channel
    * @param[in] w2 A vector of weights for the time events of the second channel
    * @param[in] n_weights_ch2 The number of weights of the second channel
    * @param[in] inplace If true (default false) the arrays are modified in place
    */
    void set_events(
            unsigned long long *t1, int n_t1,
            double *weight_ch1, int n_weights_ch1,
            unsigned long long *t2, int n_t2,
            double *weight_ch2, int n_weights_ch2,
            bool inplace = false
    );

    /*!
     * Get the normalized x-axis of the correlation
     *
     * @param[out] output x_axis / time axis of the correlation
     * @param[out] n_out number of elements in the axis
     * of the x-axis
     */
    void get_x_axis_normalized(double **output, int *n_output);

    /*!
     * Set the weights that are used in the correlation channels
     *
     *
     * @param w1 A vector of weights for the time events of the first channel
     * @param n_weights_ch1 The number of weights of the first channel
     * @param w2 A vector of weights for the time events of the second channel
     * @param n_weights_ch2 The number of weights of the second channel
     * @param[in] inplace If true (default false) the arrays are modified in place
     */
    void set_weights(
            double *weight_ch1, int n_weights_ch1,
            double *weight_ch2, int n_weights_ch2,
            bool inplace = false
    );

    /*!
     * Get the normalized correlation.
     *
     * @param[out] output an array that containing normalized  correlation
     * @param[out] n_output the number of elements of output
     */
    void get_corr_normalized(double **output, int *n_output);

    /*!
     * Get the correlation.
     *
    *
    * @param[out] corr a pointer to an array that will contain the correlation
    * @param[out] n_out a pointer to the an integer that will contain the
    * number of elements of the x-axis
    */
    void get_corr(double **corr, int *n_out);


    /*!
     * Compute the correlation function. Usually calling this method is
     * not necessary the the validity of the correlation function is tracked
     * by the attribute is_valid.
     *
     */
    void run();

    /*!
     * This method sets the time and the weights using TTTR objects.
     *
     * The header of the first TTTR object is used for calibration. Both TTTR
     * objects should have the same calibration (this is not checked).
     *
     * @param tttr_1
     * @param tttr_2
     * @param make_fine if true a full correlation is computed that uses the
     * micro time in the TTTR objects (default is false).
     * @param set_weights if set to true (default) the weights for all passed events
     * is set to one.
     */
    void set_tttr(
            TTTR *tttr_1, TTTR *tttr_2,
            bool make_fine = false,
            bool set_weights = true
            );

    /*!
     * Updates the weights. Non-zero weights are assigned a filter value that
     * is defined by a filter map and the micro time of the event.
     *
     * @param micro_times[in]
     * @param routing_channels[in]
     * @param filter[in] map of filters the first element in the map is the routing
     * channel number, the second element of the map is a vector that maps a
     * micro time to a filter value.
     */
    void set_filter(
            const std::vector<unsigned int>& micro_times_1,
            const std::vector<short>& routing_channels_1,
            const std::vector<unsigned int>& micro_times_2,
            const std::vector<short>& routing_channels_2,
            const std::map<short, std::vector<double>>& filter
    );

};


#endif //TTTRLIB_CORRELATION_H

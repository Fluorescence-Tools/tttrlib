#ifndef TTTRLIB_CORRELATOR_H
#define TTTRLIB_CORRELATOR_H

#include <iostream>
#include <cstdio>
#include <vector>
#include <list>
#include <iterator>
#include <functional>
#include <utility> /* std::pair */
#include <algorithm>
#include <climits>
#include <map>

#include "TTTR.h"
#include "CorrelatorPhotonStream.h"
#include "CorrelatorCurve.h"
#include "CLSMImage.h"


class Correlator {

    friend class CorrelatorCurve;
    friend class CLSMImage;

private:

    std::string correlation_method = "default";

    /// Flag if set to true if the output of the correlator is valid, i.e.,
    /// if the correlation function corresponds to the input
    bool is_valid = false;

protected:

    /*!
     *
     * CAUTION: the arrays t1 and t2 are modified inplace by this function!!
     *
     * @param t1 macrotime vector
     * @param t2 macrotime vector
     * @param photons1
     * @param photons2
     * @param nc number of evenly spaced elements per block
     * @param nb number of blocks of increasing spacing
     * @param np1 number of photons in first channel
     * @param np2 number of photons in second channel
     * @param xdat correlation time bins (timeaxis)
     * @param corrl pointer to correlation output
     */
    static void ccf_lamb(
            const unsigned long long *t1,
            const unsigned long long *t2,
            const double *weights1,
            const double *weights2,
            unsigned int nc,
            unsigned int nb,
            unsigned int np1,
            unsigned int np2,
            const unsigned long long *xdat, double *corrl
    );

    /*!
     *
     * @param x_axis the uncorrected x-axis (the correlation times)
     * @param corr the uncorrected correlation amplitudes
     * @param corr_normalized the corrected correlation amplitudes (output)
     * @param x_axis_normalized the corrected x-axis (output)
     * @param cr1 count rate in channel 1
     * @param cr2 count rate in channel 2
     * @param n_bins number of evenly spaced elements per block
     * @param n_casc number of blocks of increasing spacing
     * @param maximum_macro_time the maximum macro time
     * @return
     */
    static void normalize_ccf_lamb(
            std::vector<unsigned long long> &x_axis,
            std::vector<double> &corr,
            std::vector<unsigned long long> &x_axis_normalized,
            std::vector<double> &corr_normalized,
            double cr1, double cr2,
            unsigned int n_bins,
            unsigned int n_casc,
            unsigned long long maximum_macro_time
    );

    /*!
     * Calculates the cross-correlation between two arrays containing time
     * events.
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
     */
    static void ccf(
            size_t n_casc, size_t n_bins,
            std::vector<unsigned long long> &taus, std::vector<double> &corr,
            CorrelatorPhotonStream &p1,
            CorrelatorPhotonStream &p2
    );

    /*!
     * Normalizes a correlation curve.
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
    static void normalize_ccf(
            double np1, uint64_t dt1,
            double np2, uint64_t dt2,
            std::vector<unsigned long long> &x_axis, std::vector<double> &corr,
            size_t n_bins,
            bool correct_x_axis = false
    );

    /*!
     * Normalized the correlation amplitudes of a cure
     *
     * Makes a copy of the current correlation curve, i.e., the x-axis and
     * and the corresponding correlation amplitudes and calculates the values
     * of the normalized correlation.
     */
    static void normalize(
            Correlator* correlator,
            CorrelatorCurve &curve
            );

public:

    CorrelatorPhotonStream p1;
    CorrelatorPhotonStream p2;
    CorrelatorCurve curve;

    /*!
     * Computes the the delta t for Ch1, Ch2 and the maximum delta t. Delta t
     * is the difference between the first and the last photon.
     */
    uint64_t dt();

    /*!
     *
     * @param tttr an optional TTTR object. The macro and micro time calibration
     * of the header in the TTTR object calibrate the correlator.
     * @param method name of correlation method that is used by the correlator
     * @param n_bins the number of equally spaced correlation bins per block
     * @param n_casc the number of blocks
     */
    Correlator(
            std::shared_ptr<TTTR> tttr = nullptr,
            std::string method = "default",
            int n_bins = 17,
            int n_casc = 25,
            bool make_fine = false
    );

    /// Destructor
    ~Correlator() = default;

    /*!
     * Sets the number of cascades (also called blocks) of the correlation curve
     * and updates the correlation axis.
     * @param[in] n_casc
     */
    void set_n_casc(int v) {
        curve.set_n_casc(v);
        is_valid = false;
    }

    CorrelatorCurve *get_curve() {
        if (!is_valid) run();
        return &curve;
    }

    /*!
     * @return number of correlation blocks
     */
    unsigned int get_n_casc() {
        return curve.get_n_casc();
    }

    /*!
     * @param[in] v  the number of equally spaced correaltion channels per block
     */
    void set_n_bins(int v) {
        curve.set_n_bins(v);
        is_valid = false;
    }

    /*!
     * @return the number of equally spaced correlation channels per block
     */
    unsigned int get_n_bins() const {
        return curve.settings.n_bins;
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
     * @param[in] number_of_microtime_channels The maximum number of TAC channels of the micro times.
     */
    void set_microtimes(
            unsigned short *tac_1, int n_tac_1,
            unsigned short *tac_2, int n_tac_2,
            unsigned int number_of_microtime_channels
    );

    /*!
    * @param[in] t1 time events in the the first correlation channel
    * @param[in] n_t1 The number of time events in the first channel
    * @param[in] t1 time events in the the second correlation channel
    * @param[in] n_t2 The number of time events in the second channel
    */
    void set_macrotimes(
            unsigned long long *t1, int n_t1,
            unsigned long long *t2, int n_t2
    );

    std::pair<std::vector<unsigned long long>, std::vector<unsigned long long>>
    get_macrotimes() {
        return {this->p1.times, this->p2.times};
    }

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
    */
    void set_events(
            unsigned long long *t1, int n_t1,
            double *weight_ch1, int n_weights_ch1,
            unsigned long long *t2, int n_t2,
            double *weight_ch2, int n_weights_ch2
    );

    /*!
    * Set the weights that are used in the correlation channels
     *
     *
     * @param w1 A vector of weights for the time events of the first channel
     * @param n_weights_ch1 The number of weights of the first channel
     * @param w2 A vector of weights for the time events of the second channel
     * @param n_weights_ch2 The number of weights of the second channel
     */
    void set_weights(
            double *weight_ch1, int n_weights_ch1,
            double *weight_ch2, int n_weights_ch2
    );

    std::pair<std::vector<double>, std::vector<double>> get_weights() {
        return {this->p1.weights, this->p2.weights};
    }

    /*!
     * Get the normalized x-axis of the correlation
     *
     * @param[out] output x_axis / time axis of the correlation
     * @param[out] n_out number of elements in the axis
     * of the x-axis
     */
    void get_x_axis(double **output, int *n_output);

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
    * @param[out] output a pointer to an array that will contain the correlation
    * @param[out] n_output a pointer to the an integer that will contain the
    * number of elements of the x-axis
    */
    void get_corr(double** output, int* n_output);

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
     * objects should have the same calibration (this is not checked). Weights
     * are set to one by default.
     *
     * @param tttr_1
     * @param tttr_2
     * @param make_fine if true a full correlation is computed that uses the
     * micro time in the TTTR objects (default is false).
     */
    void set_tttr(
            std::shared_ptr<TTTR> tttr_1,
            std::shared_ptr<TTTR> tttr_2 = nullptr ,
            bool make_fine = false
    );

    std::pair<std::shared_ptr<TTTR>, std::shared_ptr<TTTR>> get_tttr();
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
            const std::map<short, std::vector<double>> &filter,
            const std::vector<unsigned int> &micro_times_1,
            const std::vector<signed char> &routing_channels_1,
            const std::vector<unsigned int> &micro_times_2,
            const std::vector<signed char> &routing_channels_2
    );

};

#endif //TTTRLIB_CORRELATOR_H

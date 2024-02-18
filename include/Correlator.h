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

    std::string correlation_method = "wahl";

    /// Monitor flag set to true if the output of the correlator is valid, i.e.,
    /// if the correlation function corresponds to the input
    bool is_valid = false;

protected:

    /*!
     * Compute the correlation for two macro time vectors and weight 
     *  
     * @param t1 macrotime vector of first correlation channel
     * @param t2 macrotime vector of second correlation channel
     * @param weights1 weights of first correlation channel
     * @param weights2 weights of second correlation channel
     * @param nc number of evenly spaced elements per block
     * @param nb number of blocks of increasing spacing
     * @param np1 number of photons in first channel
     * @param np2 number of photons in second channel
     * @param xdat correlation time bins (timeaxis)
     * @param corrl correlation output
     */
    static void ccf_felekyan(
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
    static void normalize_ccf_felekyan(
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
     * @brief Calculates the cross-correlation between two event streams.
     *
     * Cross-correlates two event stream using an approach that
     * utilizes a linear spacing of the bins within a cascade and a logarithmic
     * spacing of the cascades. The function works inplace. During the correlation
     * the content of the event streams are modified.
     * 
     * Based on:
     *  Fast calculation of fluorescence correlation data with asynchronous
     *  time-correlated single-photon counting, Michael Wahl, Ingo Gregor,
     *  Matthias Patting, Joerg Enderlein, Optics Express Vol. 11, No. 26, p. 3383
     *
     * @param[in] n_casc The number of the cascades
     * @param[in] n_bins The number of bins per cascase
     * @param[in] taus Correlation bins
     * @param[out] corr Correlation
     * @param[in] p1 Event stream of first correlation channel (time + weights)
     * @param[in] p2 Event stream of second correlation channel (time + weights)
     */
    static void ccf_wahl(
            size_t n_casc, size_t n_bins,
            std::vector<unsigned long long> &taus, 
            std::vector<double> &corr,
            CorrelatorPhotonStream &p1,
            CorrelatorPhotonStream &p2
    );

    /*!
     * @brief Normalize computed correlation 
     *
     * This normalization applied to correlation curves that were calculated using a
     * linear/logrithmic binning.
     * 
     * Based on:
     *  Fast calculation of fluorescence correlation data with asynchronous
     *  time-correlated single-photon counting, Michael Wahl, Ingo Gregor,
     *  Matthias Patting, Joerg Enderlein, Optics Express Vol. 11, No. 26, p. 3383
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
    static void normalize_ccf_wahl(
            double np1, uint64_t dt1,
            double np2, uint64_t dt2,
            std::vector<unsigned long long> &x_axis, 
            std::vector<double> &corr,
            size_t n_bins
    );
    
    /*!
     * @brief Compute correlation on arbitray correlation axis
     *
     * @param[in] taus correlation axis (bins)
     * @param[out] corr correlation
     * @param[in] p1 first event stream
     * @param[in] p2 second event stream 
     * 
     * Based on:
     *  Fast, flexible algorithm for calculating photon correlations, Ted A.
     *  Laurence, Samantha Fore, Thomas Huser, Optics Express Vol. 31 No. 6, p.829
    */
    static void ccf_laurence(
            std::vector<unsigned long long> &taus, 
            std::vector<double> &corr,
            CorrelatorPhotonStream &p1,
            CorrelatorPhotonStream &p2
    );
    
    /*!
     * @brief Normalize the correlation
     * 
     * @param[in] p1 first event stream
     * @param[in] p2 second event stream
     * @param[in] x_axis correlation bins
     * @param[in] corr correlation
     * @param[out] corr_normalized normalized correlation
     * 
    */
    static void normalize_ccf_laurence(
            CorrelatorPhotonStream &p1,
            CorrelatorPhotonStream &p2,
            std::vector<unsigned long long> &x_axis, 
            std::vector<double> &corr,
            std::vector<double> &corr_normalized
    );

   /*!
    * @brief Normalize the correlation curve.
    *
    * This static function makes a copy of the current correlation curve,
    * including the x-axis and corresponding correlation amplitudes,
    * and calculates the values of the normalized correlation.
    *
    * @param[in]  correlator    Reads necessary normalization parameters from the correlator settings.
    * @param[in,out] curve       Correlation curve to be normalized.
    */
   static void normalize(Correlator* correlator, CorrelatorCurve &curve);

public:

    CorrelatorPhotonStream p1;
    CorrelatorPhotonStream p2;
    CorrelatorCurve curve;

    /*!
     * @brief Compute the time difference in macro time units between the first and the last event.
     *
     * This function calculates the time difference in macro time units between the first
     * and the last event in the event stream.
     *
     * @return Time difference in macro time units.
     */
    uint64_t dt();

    /*!
     * @brief Correlator Constructor.
     *
     * Constructs a Correlator object with the specified parameters.
     *
     * @param[in] tttr       Optional TTTR object. If provided, the macro and micro time calibration
     *                       of the TTTR object header calibrate the correlator.
     * @param[in] method     Name of the correlation method used by the correlator.
     * @param[in] n_bins     Number of equally spaced correlation bins per block (determines correlation bins).
     * @param[in] n_casc     Number of blocks (determines correlation bins).
     * @param[in] make_fine  If true, macro and micro time are combined.
     */
    Correlator(
        std::shared_ptr<TTTR> tttr = nullptr,
        std::string method = "wahl",
        int n_bins = 17,
        int n_casc = 25,
        bool make_fine = false
    );


    ~Correlator() = default;

    /*!
     * @brief Set the number of cascades (blocks) of the correlation curve.
     *
     * This function sets the number of cascades (blocks) of the correlation curve
     * and updates the curve accordingly.
     *
     * @param[in] n_casc Number of cascades (blocks) of the correlation curve.
     */
    void set_n_casc(int n_casc) {
     curve.set_n_casc(n_casc);
     is_valid = false;
    }


    /*!
     * @brief Get the correlation curve.
     *
     * Computes the correlation (if necessary) and returns the correlation curve.
     *
     * @return Correlation curve.
     */
    CorrelatorCurve *get_curve() {
        if (!is_valid) run();
        return &curve;
       }


    /*!
     * @brief Get the number of correlation blocks.
     *
     * Returns the number of correlation blocks (cascades) in the correlation curve.
     *
     * @return Number of correlation blocks.
     */
    unsigned int get_n_casc() {
        return curve.get_n_casc();
       }

    /*!
     * @brief Set the number of equally spaced correlation channels per block.
     *
     * This function sets the number of equally spaced correlation channels per block
     * and updates the correlation curve accordingly.
     *
     * @param[in] v Number of equally spaced correlation channels per block.
     */
    void set_n_bins(int v) {
        curve.set_n_bins(v);
        is_valid = false;
       }

    /*!
     * @brief Get the number of equally spaced correlation channels per block.
     *
     * Returns the number of equally spaced correlation channels per block in the correlation curve.
     *
     * @return Number of equally spaced correlation channels per block.
     */
    unsigned int get_n_bins() const {
        return curve.settings.n_bins;
    }


    /*!
     * @brief Set the correlation method.
     *
     * This function sets the correlation method used by the correlator.
     *
     * @param[in] cm The name of the method. Options: "felekyan", "wahl", or "laurence".
     *
     * References:
     * - Felekyan, S., et al., 2005. Full correlation from picoseconds to seconds by time-resolved and
     *   time-correlated single photon detection. Review of Scientific Instruments, 76(8), p.083104.
     * - Wahl, M., et al., 2003. Fast calculation of fluorescence correlation data with asynchronous
     *   time-correlated single-photon counting, Opt Express Vol. 11, No. 26, p. 3383.
     * - Laurence, T.A., et al., 2006. Fast, flexible algorithm for calculating photon correlations,
     *   Opt Lett. 15;31(6):829-31.
     */
    void set_correlation_method(std::string cm) {
        is_valid = false;
        correlation_method = cm;
       }


    /*!
     * @brief Get the name of the used correlation method.
     *
     * Returns the name of the correlation method currently set in the correlator.
     *
     * @return Name of the used correlation method.
     */
    std::string get_correlation_method() {
        return correlation_method;
       }

    /*!
     * @brief Add microtime information to the event stream.
     *
     * This function adds microtime information to the event stream for correlation analysis.
     *
     * @param[in] tac_1 The micro times of the first correlation channel.
     * @param[in] n_tac_1 The number of events in the first correlation channel.
     * @param[in] tac_2 The micro times of the second correlation channel.
     * @param[in] n_tac_2 The number of events in the second correlation channel.
     * @param[in] number_of_microtime_channels The maximum number of TAC channels of the micro times.
     */
    void set_microtimes(
        unsigned short *tac_1, int n_tac_1,
        unsigned short *tac_2, int n_tac_2,
        unsigned int number_of_microtime_channels
    );

    /*!
     * @brief Add macrotimes information to the event stream.
     *
     * This function adds macrotimes information to the event stream for correlation analysis.
     *
     * @param[in] t1 Time events in the first correlation channel.
     * @param[in] n_t1 The number of time events in the first channel.
     * @param[in] t2 Time events in the second correlation channel.
     * @param[in] n_t2 The number of time events in the second channel.
     */
    void set_macrotimes(
        unsigned long long *t1, int n_t1,
        unsigned long long *t2, int n_t2
    );

    /*!
     * @brief Get event times of the first and second correlation channel.
     *
     * This function returns the event times of the first and second correlation channel.
     *
     * @return A pair of vectors containing event times of the first and second correlation channel.
     */
     std::pair<std::vector<unsigned long long>, std::vector<unsigned long long>> get_macrotimes() {
      return {this->p1.times, this->p2.times};
     }


    /*!
     * @brief Set time events and weights for the first and second correlation channels.
     *
     * This function sets the time events and weights for the first and second correlation channels.
     *
     * @param[in] t1 An array containing the time events of the first channel.
     * @param[in] n_t1 The number of time events in the first channel.
     * @param[in] weight_ch1 A vector of weights for the time events of the first channel.
     * @param[in] n_weights_ch1 The number of weights for the first channel.
     * @param[in] t2 An array containing the time events of the second channel.
     * @param[in] n_t2 The number of time events in the second channel.
     * @param[in] weight_ch2 A vector of weights for the time events of the second channel.
     * @param[in] n_weights_ch2 The number of weights for the second channel.
     */
    void set_events(
            unsigned long long *t1, int n_t1,
            double *weight_ch1, int n_weights_ch1,
            unsigned long long *t2, int n_t2,
            double *weight_ch2, int n_weights_ch2
    );


    /*!
     * @brief Set weights used for correlation.
     *
     * This function sets and updates weights for the events in the first and second correlation channels.
     *
     * @param[in] weight_ch1 A vector of weights for the time events of the first channel.
     * @param[in] n_weights_ch1 The number of weights for the first channel.
     * @param[in] weight_ch2 A vector of weights for the time events of the second channel.
     * @param[in] n_weights_ch2 The number of weights for the second channel.
     */
    void set_weights(
            double *weight_ch1, int n_weights_ch1,
            double *weight_ch2, int n_weights_ch2
    );

    /*!
     * @brief Get weights used for correlation.
     *
     * This function retrieves the weights for the events in the first and second correlation channels.
     *
     * @return A pair of vectors containing weights for the first and second correlation channels, respectively.
     */
    std::pair<std::vector<double>, std::vector<double>> get_weights(){
        return {this->p1.weights, this->p2.weights};
    }

    /*!
     * @brief Get correlation bins (axis)
     *
     * @param[out] output x_axis / time axis of the correlation
     * @param[out] n_output number of elements in the axis
     * of the x-axis
     */
    void get_x_axis(double **output, int *n_output);

    /*!
     * @brief Get the normalized correlation.
     *
     * @param[out] output an array that containing normalized  correlation
     * @param[out] n_output the number of elements of output
     */
    void get_corr_normalized(double **output, int *n_output);

    /*!
     * @brief Get the (unnormalized) correlation.
     *
     * @param[out] output a pointer to an array that will contain the correlation
     * @param[out] n_output a pointer to the an integer that will contain the
     * number of elements of the x-axis
     */
    void get_corr(double** output, int* n_output);

    /*!
     * @brief compute the correlation
     * 
     * Compute the correlation function. Usually calling this method is
     * not necessary the the validity of the correlation function is tracked
     * by the attribute is_valid.
     *
     */
    void run();

    /*!
     * @brief Sets the time and the weights using TTTR objects.
     *
     * Set the event times (and weights) using TTTR objects. By default
     * the weights are all set to one.
     * 
     * The header of the first TTTR object is used for calibration. Both TTTR
     * objects should have the same calibration (this is not checked).
     *
     * @param[in] tttr_1
     * @param[in] tttr_2
     * @param[in] make_fine if true a full correlation is computed that uses the
     * micro time in the TTTR objects (default is false).
     */
    void set_tttr(
            std::shared_ptr<TTTR> tttr_1,
            std::shared_ptr<TTTR> tttr_2 = nullptr ,
            bool make_fine = false
    );

    std::pair<std::shared_ptr<TTTR>, std::shared_ptr<TTTR>> get_tttr();

    /*!
     * @brief Update weights based on a filter map and event information.
     *
     * This function updates the weights. Non-zero weights are assigned a filter value based on a filter map
     * and the micro time of the event.
     *
     * @param filter[in] A map of filters where the first element in the map is the routing channel number,
     *                  and the second element is a vector that maps a micro time to a filter value.
     * @param micro_times_1[in] Micro times of events in the first correlation channel.
     * @param routing_channels_1[in] Routing channels of events in the first correlation channel.
     * @param micro_times_2[in] Micro times of events in the second correlation channel.
     * @param routing_channels_2[in] Routing channels of events in the second correlation channel.
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

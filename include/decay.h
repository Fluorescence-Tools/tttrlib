//
// Created by Thomas-Otavio Peulen on 4/22/20.
//

#ifndef TTTRLIB_DECAY_H
#define TTTRLIB_DECAY_H

#include <cmath>
#include <iostream>
#include <vector>
#include <stdlib.h>
#include <numeric>
#include <algorithm>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

class Decay {

private:

    /// Stores the weighted residuals
    std::vector<double> _weighted_residuals;

    /// Proportional to the area of the model. If negative, the area is scaled
    /// to the data.
    double _total_area = -1;

    /// The constant offset in the model
    double _constant_background = 0.0;

    /// The fraction of the irf (scattered light) in the model decay
    double _areal_fraction_scatter = 0.0;

    /// The time shift of the irf
    double _irf_shift_channels = 0.0;

    /// Background counts of irf. This number is sutracted from the counts
    /// in each channel before convolution.
    double _irf_background_counts = 0.0;

    /// The repetiotion period (usually in nano seconds)
    double _period = 100.;

    /// The instrument response function
    std::vector<double> _irf;

    /// The experimental data (used for scaling)
    std::vector<double> _data;

    /// The weights of the experimental data (used for scaling)
    std::vector<double> _weights;

    /// The time axis of the data (used in convolution)
    std::vector<double> _time_axis;

    /// The used to store the model function
    std::vector<double> _model_function;

    /// The lifetime spectrum
    std::vector<double> _lifetime_spectrum;

    /// Set to valid if the output decay matched the input
    bool _is_valid = false;

    /// If set to true will add pile up to the model function
    bool _correct_pile_up=false;

    /// Beginning index of the comvolution
    int _convolution_start = 0;

    /// Stop index of the convolution
    int _convolution_stop = -1;

    /// Amplitude thresholf used to discriminate low populated lifetimes
    double _amplitude_threshold = 1e10;

    /// If set to true discriminates low populated lifetimes in convolution.
    bool _use_amplitude_threshold = false;


public:

    bool is_valid() const{
        return _is_valid;
    }

    void set_data(double* input, int n_input){
        _is_valid = false;
        _data.assign(input, input+n_input);
    }

    void get_data(double **output, int *n_output){
        *output = _data.data();
        *n_output = _data.size();
    }

    void set_use_amplitude_threshold(bool v){
        _is_valid = false;
        _use_amplitude_threshold = v;
    }

    bool get_use_amplitude_threshold() const{
        return _use_amplitude_threshold;
    }

    void set_amplitude_threshold(double v){
        _is_valid = false;
        _amplitude_threshold = v;
    }

    double get_amplitude_threshold() const{
        return _amplitude_threshold;
    }


    void set_total_area(double v){
        _is_valid = false;
        _total_area = v;
    }

    double get_total_area() const{
        return _total_area;
    }

    void set_period(double v){
        _is_valid = false;
        _period = v;
    }

    double get_period() const{
        return _period;
    }

    void set_irf_shift_channels(double v){
        _is_valid = false;
        _irf_shift_channels = v;
    }

    double get_irf_shift_channels() const{
        return _irf_shift_channels;
    }

    void set_areal_fraction_scatter(double v){
        _is_valid = false;
        _areal_fraction_scatter = std::min(1., std::max(0.0, v));
    }

    double get_areal_fraction_scatter() const{
        return _areal_fraction_scatter;
    }

    void set_constant_background(double v){
        _is_valid = false;
        _constant_background = v;
    }

    double get_constant_background() const{
        return _constant_background;
    }

    void set_convolution_start(int v){
        _is_valid = false;
        _convolution_start = v;
    }

    int get_convolution_start() const{
        return _convolution_start;
    }

    void set_convolution_stop(int v){
        _is_valid = false;
        _convolution_stop = v;
    }

    int get_convolution_stop() const{
        return _convolution_stop;
    }

    void set_correct_pile_up(bool v){
        _is_valid = false;
        _correct_pile_up = v;
    }

    bool get_correct_pile_up() const{
        return _correct_pile_up;
    }

    void set_irf(double* input, int n_input){
        _is_valid = false;
        _irf.clear();
        _irf.assign(input, input+n_input);
    }

    void get_irf(double **output, int *n_output){
        *output = _irf.data();
        *n_output = _irf.size();
    }

    void get_model(double **output, int *n_output){
        if(!_is_valid){
            evaluate();
        }
        *output = _model_function.data();
        *n_output = _model_function.size();
    }

    void set_lifetime_spectrum(double* input, int n_input){
        _is_valid = false;
        _lifetime_spectrum.clear();
        _lifetime_spectrum.assign(input, input+n_input);
    }

    void get_lifetime_spectrum(double **output, int *n_output){
        *output = _lifetime_spectrum.data();
        *n_output = _lifetime_spectrum.size();
    }

    void set_weights(double* input, int n_input){
        _is_valid = false;
        _weights.clear();
        _weights.assign(input, input+n_input);
    }

    void get_weights(double **output, int *n_output){
        *output = _weights.data();
        *n_output = _weights.size();
    }

    void set_time_axis(double* input, int n_input){
        _is_valid = false;
        _time_axis.assign(input, input+n_input);
    }

    void get_time_axis(double **output, int *n_output){
        *output = _time_axis.data();
        *n_output = _time_axis.size();
    }

    void set_irf_background_counts(double irf_background_counts){
        _irf_background_counts = irf_background_counts;
        _is_valid = false;
    }

    double get_irf_background_counts(){
        return _irf_background_counts;
    }

    /*!
     *
     * @param data the data to which the decay is fitted
     * @param time_axis the time axis that belongs to the data
     * @param dt the spacing between the points in the time axis. This optional
     * parameter is used to compute a time axis if not time axis was provided by
     * the parameter time_axis
     * @param weights the weights of the data points. If the weights are not provided
     * (nullptr / None) the weights are computed assuming Poisson noise.
     * @param instrument_response_function The instrument response function (IRF)
     * that is used for convolution. If no IRF is provided
     * @param start The start index in the IRF used for convolution. Points in the
     * IRF before the start index are not used for convolution.
     * @param stop The stop index in the IRF used for convolution. Points beyond the
     * stop index are not convolved.
     * @param use_amplitude_threshold If this is set to true (default value is true)
     * the values that are smaller then a specified threshold are omitted
     * @param amplitude_threshold The amplitude threshold that is used if the
     * parameter use_amplitude_threshold is set to true (the default value is 1e10)
     * @param correct_pile_up If this is set to true (the default value is false)
     * the convolved model function is 'piled up' to match pile up artifacts in the
     * data.
     * @param period the repetition period, .i.e, the time between subsequent
     * excitation pulses.
     */
    Decay(
        std::vector<double> data = std::vector<double>(),
        std::vector<double> time_axis = std::vector<double>(),
        std::vector<double> weights = std::vector<double>(),
        std::vector<double> instrument_response_function = std::vector<double>(),
        int start = 0, int stop = -1,
        bool use_amplitude_threshold = false,
        double amplitude_threshold = 1e10,
        bool correct_pile_up = false,
        double period = 100.0,
        double dt = 1.0,
        double irf_background_counts = 0.0,
        double areal_fraction_scatter = 0.0,
        double constant_background = 0.0
    ){
        set_data(data.data(), data.size());
        set_time_axis(time_axis.data(), time_axis.size());
        set_irf(instrument_response_function.data(), instrument_response_function.size());
        set_weights(weights.data(), weights.size());
        _convolution_start = std::max(0, start);
        _convolution_stop = std::min(stop, (int) data.size());
        _amplitude_threshold = amplitude_threshold;
        _use_amplitude_threshold = use_amplitude_threshold;
        _correct_pile_up = correct_pile_up;
        _period = period;
        _irf_background_counts = irf_background_counts;
        _areal_fraction_scatter = areal_fraction_scatter;
        _constant_background = constant_background;

        if(_irf.empty() && !data.empty()){
#if VERBOSE
            std::clog << "-- Setting IRF to data size" << std::endl;
#endif
            _irf.resize(data.size());
            for(int i=1; i<data.size(); i++) _irf[i] = 0.0;
            _irf[0] = 1.0;
        }
        if(!weights.empty()){
            _weights = weights;
        } else{
#if VERBOSE
            std::clog << "-- Assuming Poisson noise" << std::endl;
#endif
            _weights.resize(data.size());
            for(int i=0; i<data.size(); i++){
                _weights[i] = (_data[i] <= 0.0) ?
                        0.0 :
                        1. / std::sqrt(_data[i]);
            }
        }
        if(time_axis.empty()){
#if VERBOSE
            std::clog << "-- Filling time axis, dt: " << dt << std::endl;
#endif
            _time_axis.resize(data.size());
            for(int i=0; i<data.size(); i++){
                _time_axis[i] = i * dt;
            }
        }
        _model_function.resize(data.size());
        if(
            (data.size() != _time_axis.size()) ||
            (data.size() != _weights.size()) ||
            (data.size() != _irf.size())
        ){
            std::cerr << "ERROR: The size of the data, time, weight array, or "
                         "irf do not match" << std::endl;
        }
    }

    /*!
    * Compute the fluorescence decay for a lifetime spectrum and a instrument
    * response function.
    *
    * Fills the pre-allocated output array `output_decay` with a fluorescence
    * intensity decay defined by a set of fluorescence lifetimes defined by the
    * parameter `lifetime_spectrum`. The fluorescence decay will be convolved
    * (non-periodically) with an instrumental response function that is defined
    * by `instrument_response_function`.
    *
    * This function calculates a fluorescence intensity model_decay that is
    * convolved with an instrument response function (IRF). The fluorescence
    * intensity model_decay is specified by its fluorescence lifetime spectrum,
    * i.e., an interleaved array containing fluorescence lifetimes with
    * corresponding amplitudes.
    *
    * This convolution works also with uneven spaced time axes.
    *
    * @param inplace_output[in,out] Inplace output array that is filled with the
    * values of the computed fluorescence intensity decay model
    * @param n_output[in] Number of elements in the output array
    * @param time_axis[in] the time-axis of the model_decay
    * @param n_time_axis[in] length of the time axis
    * @param irf[in] the instrument response function array
    * @param n_irf[in] length of the instrument response function array
    * @param lifetime_spectrum[in] Interleaved array of amplitudes and fluorescence
    * lifetimes of the form (amplitude, lifetime, amplitude, lifetime, ...)
    * @param n_lifetime_spectrum[in] number of elements in the lifetime spectrum
    * @param convolution_start[in] Start channel of convolution (position in array
    * of IRF)
    * @param convolution_stop[in] convolution stop channel (the index on the time-axis)
    * @param use_amplitude_threshold[in] If this value is True (default False)
    * fluorescence lifetimes in the lifetime spectrum which have an amplitude
    * with an absolute value of that is smaller than `amplitude_threshold` are
    * not omitted in the convolution.
    * @param amplitude_threshold[in] Threshold value for the amplitudes
    */
    static void convolve_lifetime_spectrum(
             double* inplace_output, int n_output,
             double *time_axis, int n_time_axis,
             double *instrument_response_function, int n_instrument_response_function,
             double *lifetime_spectrum, int n_lifetime_spectrum,
             int convolution_start = 0,
             int convolution_stop = -1,
             bool use_amplitude_threshold=false,
             double amplitude_threshold=1e10
    );

    /*!
    * Compute the fluorescence decay for a lifetime spectrum and a instrument
    * response function considering periodic excitation.
    *
    * Fills the pre-allocated output array `output_decay` with a fluorescence
    * intensity decay defined by a set of fluorescence lifetimes defined by the
    * parameter `lifetime_spectrum`. The fluorescence decay will be convolved
    * (non-periodically) with an instrumental response function that is defined
    * by `instrument_response_function`.
    *
    * This function calculates a fluorescence intensity model_decay that is
    * convolved with an instrument response function (IRF). The fluorescence
    * intensity model_decay is specified by its fluorescence lifetime spectrum,
    * i.e., an interleaved array containing fluorescence lifetimes with
    * corresponding amplitudes.
    *
    * This convolution only works with evenly linear spaced time axes.
    *
    * @param inplace_output[in,out] Inplace output array that is filled with the values
    * of the computed fluorescence intensity decay model
    * @param n_output[in] Number of elements in the output array
    * @param time_axis[in] the time-axis of the model_decay
    * @param n_time_axis[in] length of the time axis
    * @param irf[in] the instrument response function array
    * @param n_irf[in] length of the instrument response function array
    * @param lifetime_spectrum[in] Interleaved array of amplitudes and fluorescence
    * lifetimes of the form (amplitude, lifetime, amplitude, lifetime, ...)
    * @param n_lifetime_spectrum[in] number of elements in the lifetime spectrum
    * @param convolution_start[in] Start channel of convolution (position in array of IRF)
    * @param convolution_stop[in] convolution stop channel (the index on the time-axis)
    * @param use_amplitude_threshold[in] If this value is True (default False)
    * fluorescence lifetimes in the lifetime spectrum which have an amplitude
    * with an absolute value of that is smaller than `amplitude_threshold` are
    * not omitted in the convolution.
    * @param amplitude_threshold[in] Threshold value for the amplitudes
    * @param period Period of repetition in units of the lifetime (usually,
    * nano-seconds)
    */
    static void convolve_lifetime_spectrum_periodic(
            double* inplace_output, int n_output,
            double* time_axis, int n_time_axis,
            double *instrument_response_function, int n_instrument_response_function,
            double* lifetime_spectrum, int n_lifetime_spectrum,
            int convolution_start=0,
            int convolution_stop=-1,
            bool use_amplitude_threshold=false,
            double amplitude_threshold=1e10,
            double period=100.0
    );

    /*!
     * Compute a scaling factor for a given experimental histogram and model
     * function and scale (optionally) the model function to the data.
     *
     * The scaling factor is computed considering the weighted photon counts
     * in a specified range.
     *
     * @param model_function array containing the model function
     * @param n_model_function number of elements in the model function
     * @param data array of the experimental data
     * @param n_data number of experimental data points
     * @param weights the weights of the experimental data
     * @param n_weights the number of weights of the experimental dat
     * @param background the background counts of the data
     * @param start starting index that is used to compute the scaling factor
     * @param stop stop index to compute the scaling factor
     * @param scale_inplace if set to true (default is true) the model function
     * is scaled inplace
     * @return the computed scaling factor
     */
     static double compute_scale(
            double* model_function, int n_model_function,
            double* data, int n_data,
            double* weights, int n_weights,
            double background=0.0,
            int start=0,
            int stop=-1
    );

    /*!
     * Shift an input array by a floating number.
     *
     * @param input[in] the input array
     * @param n_input[in] length of the input array
     * @param output output array
     * @param n_output length of the output array
     * @param shift[in] the shift of the output
     */
    static void shift_array(
        double* input, int n_input,
        double** output, int *n_output,
        double shift,
        bool set_outside=true,
        double outside_value=0.0
    );

    /*!
     * Computes the sum of two arrays considering their respective
     * areal fraction.
     *
     * A weighted sum of two curves is computed. The weighted sum is
     * computed by the area of the first curve and the areal fraction
     * of the second curve. The area of the computed curve equals to
     * the area of the first input curve while the areal fraction of
     * the second input curve will be equal to the specified value in
     * the range specified by the input parameters.
     *
     * @param output the computed output curve (array)
     * @param n_output the number of points in the output
     * @param curve1[in] the first input curve / array
     * @param n_curve1[in] number of points in the first array
     * @param curve2[in] second curve / array
     * @param n_curve2[in] number of points in the second array
     * @param areal_fraction_curve2[in] areal fraction of the second curve in the
     * output array
     * @param start[in] start index used for the area calculation
     * @param stop[in] stop index used for the area calculation
     */
    static void add_curve(
        double** output, int *n_output,
        double* curve1, int n_curve1,
        double* curve2, int n_curve2,
        double areal_fraction_curve2,
        int start=0,
        int stop=-1
    );

    /*!
     * Correct the model function for pile up
     *
     * @param model[in,out] The array containing the model function
     * @param n_model[in] Number of elements in the model array
     * @param data[in] The array containing the experimental decay
     * @param n_data[in] number of elements in experimental decay
     * @param repetition_rate[in] The repetition-rate in MHz
     * @param dead_time[in] The dead-time of the detection system in nanoseconds
     * @param measurement_time[in] The measurement time in seconds
     */
    void add_pile_up(
        double* model, int n_model,
        double* data, int n_data,
        double repetition_rate,
        double dead_time,
        double measurement_time
    );

    /*!
     *
     * @param model_function
     * @param n_model_function
     * @param data
     * @param n_data
     * @param weights
     * @param n_weights
     * @param time_axis
     * @param n_time_axis
     * @param instrument_response_function
     * @param n_instrument_response_function
     * @param lifetime_spectrum
     * @param n_lifetime_spectrum
     * @param start
     * @param stop
     * @param irf_background_counts
     * @param irf_shift_channels
     * @param irf_areal_fraction
     * @param period
     * @param constant_background
     * @param total_area the area to which the model fluorescence decay is scaled
     * to. If this value is negative (default value is -1) the model fluorescence
     * decay is scaled to the data in the range defined by the parameters start,
     * stop
     * @param use_amplitude_threshold
     * @param amplitude_threshold
     * @param correct_pile_up
     */
    static void compute_decay(
            double* model_function, int n_model_function,
            double* data, int n_data,
            double* weights, int n_weights,
            double* time_axis, int n_time_axis,
            double* instrument_response_function, int n_instrument_response_function,
            double* lifetime_spectrum, int n_lifetime_spectrum,
            int start=0, int stop=-1,
            double irf_background_counts=0.0,
            double irf_shift_channels=0.0,
            double irf_areal_fraction=0.0,
            double period=1000.0,
            double constant_background=0.0,
            double total_area=-1,
            bool use_amplitude_threshold=false,
            double amplitude_threshold=1e10,
            bool correct_pile_up=false
    );

    void get_weighted_residuals(double** output, int* n_output){
#if VERBOSE
        std::clog << "Compute weighted residuals..." << std::endl;
        std::clog << "-- points in model function:" << _model_function.size() << std::endl;
        std::clog << "-- points in weights:" << _weights.size() << std::endl;
        std::clog << "-- points in data:" << _data.size() << std::endl;
#endif
        evaluate();
        if((_model_function.size() == _data.size()) &&
            (_weights.size() == _data.size())){
            _weighted_residuals.resize(_data.size());
            for(int i=0; i<_data.size(); i++){
                _weighted_residuals[i] = (_data[i] - _model_function[i]) * _weights[i];
            }
            *n_output = _weighted_residuals.size();
            *output = _weighted_residuals.data();
        }else{
            std::cerr << "WARNING: the dimensions of the data, the model, and the"
                         "weights do not match.";
            *n_output = 0;
            *output = nullptr;
        }
    }

    void evaluate(
            std::vector<double> lifetime_spectrum=std::vector<double>()
    ){
#if VERBOSE
        std::cout << "evaluate..." << std::endl;
#endif
        if(!lifetime_spectrum.empty()){
            set_lifetime_spectrum(
                    lifetime_spectrum.data(),
                    lifetime_spectrum.size()
            );
        }
        if(!_is_valid){
            compute_decay(
                    _model_function.data(),
                    _model_function.size(),
                    _data.data(),
                    _data.size(),
                    _weights.data(),
                    _weights.size(),
                    _time_axis.data(),
                    _time_axis.size(),
                    _irf.data(),
                    _irf.size(),
                    _lifetime_spectrum.data(),
                    _lifetime_spectrum.size(),
                    _convolution_start, _convolution_stop,
                    _irf_background_counts, _irf_shift_channels,
                    _areal_fraction_scatter,
                    _period,
                    _constant_background,
                    _total_area,
                    _use_amplitude_threshold,
                    _amplitude_threshold,
                    _correct_pile_up
            );
            _is_valid = true;
        }
    }

};

#endif //TTTRLIB_DECAY_H

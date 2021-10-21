//!  Convolution, scaling, and lamp shift routines

#ifndef FIT2X_FSCONV_H
#define FIT2X_FSCONV_H

#include <cmath>  /* std::ceil */
#include <numeric> /* std::accumulate */
#include <iostream>
#include <vector>
#include <algorithm> /* std::max */
#include <string.h> /* strcmp */

#include "omp.h"

#if defined(_MSC_VER)
/* Microsoft C/C++-compatible compiler */
#include <intrin.h>
#include <immintrin.h>
#endif
#if defined(__GNUC__) || defined(__clang__)
#include <immintrin.h>
#include <avxintrin.h>
#endif
#if !defined(__FMA__) && defined(__AVX2__)
#define __FMA__ 1
#endif




/*!
 * @brief Scale model function to the data (old version)
 *
 * This function rescales the model function (fit) to the data by the number
 * of photons between a start and a stop micro time counting channel. The number
 * of photons between start and stop are counted and the model function is scaled
 * to match the data by area.
 *
 * This rescaling function does not consider the noise in the data when rescaling
 * the model.
 *
 * @param fit[in,out] model function that is scaled (modified in-place)
 * @param decay[in] the experimental data to which the model function is scaled
 * @param scale[out] the scaling parameter (the factor) by which the model
 * function is multiplied.
 * @param start[in] The start micro time channel
 * @param stop[in] The stop micro time channel
 */
void rescale(double *fit, double *decay, double *scale, int start, int stop);


/*!
 * @brief Scale model function to the data (with weights)
 *
 * This function rescales the model function (fit) to the data by the number
 * of photons between a start and a stop micro time counting channel. The number
 * of photons between start and stop are counted and the model function is scaled
 * to match the data by area considering the noise of the data.
 *
 * The scaling factor is computed by:
 *
 * scale = sum(fit*decay/w^2)/sum(fit^2/w^2)
 *
 * @param fit[in,out] model function that is scaled (modified in-place)
 * @param decay[in] the experimental data to which the model function is scaled
 * @param w_sq[in] squared weights of the data.
 * @param scale[out] the scaling parameter (the factor) by which the model
 * function is multiplied.
 * @param start[in] The start micro time channel
 * @param stop[in] The stop micro time channel
 */
void rescale_w(double *fit, double *decay, double *w_sq, double *scale, int start, int stop);


/*!
 * @brief Scale model function to the data (with weights and background)
 *
 * This function scales the model function (fit) to the data by the number
 * of photons between a start and a stop micro time counting channel. The number
 * of photons between start and stop are counted and the model function is scaled
 * to match the data by area considering the noise of the data and a constant
 * offset of the data.
 *
 * scale = sum(fit*(decay-bg)/w^2)/sum(fit^2/w^2)
 *
 * @param fit[in,out] model function that is scaled (modified in-place)
 * @param decay[in] the experimental data to which the model function is scaled
 * @param w_sq[in] squared weights of the data.
 * @param bg[in] constant background of the data
 * @param scale[out] the scaling parameter (the factor) by which the model
 * function is multiplied.
 * @param start[in] The start micro time channel
 * @param stop[in] The stop micro time channel
 */
void rescale_w_bg(double *fit, double *decay, double *e_sq, double bg, double *scale, int start, int stop);


/*!
 * @brief Convolve lifetime spectrum with instrument response (fast convolution,
 * low repetition rate)
 *
 * This function computes the convolution of a lifetime spectrum (a set of
 * lifetimes with corresponding amplitudes) with a instrument response function
 * (irf). This function does not consider periodic excitation and is suited for
 * experiments at low repetition rate.
 *
 * @param fit[out] model function. The convoluted decay is written to this array
 * @param x[in] lifetime spectrum (amplitude1, lifetime1, amplitude2, lifetime2, ...)
 * @param lamp[in] instrument response function
 * @param numexp[in] number of fluorescence lifetimes
 * @param start[in] start micro time index for convolution (not used)
 * @param stop[in] stop micro time index for convolution.
 * @param dt[in] time difference between two micro time channels
 */
void fconv(double *fit, double *x, double *lamp, int numexp, int start, int stop, double dt=0.05);


/*!
 * @brief Convolve lifetime spectrum with instrument response (fast convolution,
 * AVX optimized for large lifetime spectra)
 *
 * This function is a modification of fconv for large lifetime spectra. The
 * lifetime spectrum is processed by AVX intrinsics. Four lifetimes are convolved
 * at once. Spectra with lifetimes that are not multiple of four are zero padded.
 *
 * @param fit
 * @param x
 * @param lamp
 * @param numexp
 * @param start
 * @param stop
 * @param n_points
 * @param dt
 */
void fconv_avx(double *fit, double *x, double *lamp, int numexp, int start, int stop, double dt=0.05);


/*!
 * @brief Convolve lifetime spectrum with instrument response (fast convolution,
 * high repetition rate)
 *
 * This function computes the convolution of a lifetime spectrum (a set of
 * lifetimes with corresponding amplitudes) with a instrument response function
 * (irf). This function does consider periodic excitation and is suited for experiments
 * at high repetition rate.
 *
 * @param fit[out] model function. The convoluted decay is written to this array
 * @param x[in] lifetime spectrum (amplitude1, lifetime1, amplitude2, lifetime2, ...)
 * @param lamp[in] instrument response function
 * @param numexp[in] number of fluorescence lifetimes
 * @param start[in] start micro time index for convolution (not used)
 * @param stop[in] stop micro time index for convolution.
 * @param n_points number of points in the model function.
 * @param period excitation period in units of the fluorescence lifetimes (typically
 * nanoseconds)
 * @param dt[in] time difference between two micro time channels
 */
void fconv_per(
        double *fit, double *x, double *lamp, int numexp, int start, int stop,
        int n_points, double period, double dt=0.05
);
/*!
 * @brief Convolve lifetime spectrum with instrument response (fast convolution,
 * high repetition rate), AVX optimized version
 *
 * This function computes the convolution of a lifetime spectrum (a set of
 * lifetimes with corresponding amplitudes) with a instrument response function
 * (irf). This function does consider periodic excitation and is suited for experiments
 * at high repetition rate.
 *
 * @param fit[out] model function. The convoluted decay is written to this array
 * @param x[in] lifetime spectrum (amplitude1, lifetime1, amplitude2, lifetime2, ...)
 * @param lamp[in] instrument response function
 * @param numexp[in] number of fluorescence lifetimes
 * @param start[in] start micro time index for convolution (not used)
 * @param stop[in] stop micro time index for convolution.
 * @param n_points number of points in the model function.
 * @param period excitation period in units of the fluorescence lifetimes (typically
 * nanoseconds)
 * @param dt[in] time difference between two micro time channels
 */
void fconv_per_avx(
        double *fit, double *x, double *lamp, int numexp, int start, int stop,
        int n_points, double period, double dt=0.05
);


/*!
 * @brief Convolve lifetime spectrum - fast convolution, high repetition rate,
 * with convolution stop
 *
 * fast convolution, high repetition rate, with convolution stop for Paris
 *
 * @param fit[out] model function. The convoluted decay is written to this array
 * @param x[in] lifetime spectrum (amplitude1, lifetime1, amplitude2, lifetime2, ...)
 * @param lamp[in] instrument response function
 * @param numexp[in] number of fluorescence lifetimes
 * @param stop[in] stop micro time index for convolution.
 * @param n_points number of points in the model function.
 * @param period excitation period in units of the fluorescence lifetimes (typically
 * nanoseconds)
 * @param conv_stop convolution stop micro channel number
 * @param dt[in] time difference between two micro time channels
 */
void fconv_per_cs(double *fit, double *x, double *lamp, int numexp, int stop,
                  int n_points, double period, int conv_stop, double dt);


/*!
 * @brief Convolve lifetime spectrum - fast convolution with reference compound
 * decay
 *
 * This function convolves a set of fluorescence lifetimes and with associated
 * amplitudes with an instrument response function. The provided amplitudes are
 * scaled prior to the convolution by area using a reference fluorescence lifetime.
 * The amplitudes are computed by
 *
 * amplitude_corrected = a * ( 1 /tauref - 1 / tau)
 *
 * where a and tau are provided amplitudes.
 *
 * @param fit[out] model function. The convoluted decay is written to this array
 * @param x[in] lifetime spectrum (amplitude1, lifetime1, amplitude2, lifetime2, ...)
 * @param lamp[in] instrument response function
 * @param numexp[in] number of fluorescence lifetimes
 * @param start[in] start micro time index for convolution (not used)
 * @param stop[in] stop micro time index for convolution.
 * @param tauref a reference lifetime used to rescale the amplitudes of the
 * fluorescence lifetime spectrum
 * @param dt[in] time difference between two micro time channels
 */
void fconv_ref(double *fit, double *x, double *lamp, int numexp, int start, int stop, double tauref, double dt=0.05);


/*!
 * @brief Convolve fluorescence decay curve with irf
 *
 * This function computes a convolved model function for a fluorescence decay
 * curve.
 *
 * @param fit convolved model function
 * @param p model function before convolution - fluorescence decay curve
 * @param lamp instrument response function
 * @param start start index of the convolution
 * @param stop stop index of the convolution
 */
void sconv(double *fit, double *p, double *lamp, int start, int stop);


/*!
 * @brief shift instrumnet response function
 *
 * @param lampsh
 * @param lamp
 * @param ts
 * @param n_points
 * @param out_value the value of the shifted response function outside of the
 * valid indices
 */
void shift_lamp(double *lampsh, double *lamp, double ts, int n_points, double out_value=0.0);


/*!
 * @brief Add a pile-up distortion to the model function
 *
 * This function adds a pile up distortion to a model fluorescence decay. The
 * model used to compute the pile-up distortion follows the description of Coates
 * (1968, eq. 2 and eq. 4)
 *
 * Reference:
 * Coates, P.: The correction for photonpile-up in the measurement of radiative
 * lifetimes. J. Phys. E: Sci. Instrum. 1(8), 878–879 (1968)
 *
 * @param model[in,out] The array containing the model function
 * @param n_model[in] Number of elements in the model array
 * @param data[in] The array containing the experimental decay
 * @param n_data[in] number of elements in experimental decay
 * @param repetition_rate[in] The repetition-rate (excitation rate) in MHz
 * @param instrument_dead_time[in] The overall dead-time of the detection system in nanoseconds
 * @param measurement_time[in] The measurement time in seconds
 * @param pile_up_model[in] The model used to compute the pile up distortion.
 * @param start Start index for pile up
 * @param stop Stop index for pile up
 * (default "coates")
 */
void add_pile_up_to_model(
        double* model, int n_model,
        double* data, int n_data,
        double repetition_rate,
        double instrument_dead_time,
        double measurement_time,
        std::string pile_up_model = "coates",
        int start = 0,
        int stop = -1
);


/*!
 * Threshold the amplitudes
 *
 * Amplitudes with absolute values smaller than the specified threshold are
 * set to zero.
 *
 * @param lifetime_spectrum interleaved lifetime spectrum (amplitude, lifetime)
 * @param n_lifetime_spectrum number of elements in lifetime spectrum
 * @param amplitude_threshold
 */
void discriminate_small_amplitudes(
        double* lifetime_spectrum, int n_lifetime_spectrum,
        double amplitude_threshold
);


/*!
* Compute the fluorescence decay for a lifetime spectrum and a instrument
* response function considering periodic excitation.
*
* Fills the pre-allocated output array `output_decay` with a fluorescence
* intensity decay defined by a set of fluorescence lifetimes defined by the
* parameter `lifetime_handler`. The fluorescence decay will be convolved
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
* @param period Period of repetition in units of the lifetime (usually,
* nano-seconds)
*/
void fconv_per_cs_time_axis(
        double *model, int n_model,
        double *time_axis, int n_time_axis,
        double *irf, int n_irf,
        double *lifetime_spectrum, int n_lifetime_spectrum,
        int convolution_start = 0,
        int convolution_stop = -1,
        double period = 100.0
);


/*!
* Compute the fluorescence decay for a lifetime spectrum and a instrument
* response function.
*
* Fills the pre-allocated output array `output_decay` with a fluorescence
* intensity decay defined by a set of fluorescence lifetimes defined by the
* parameter `lifetime_handler`. The fluorescence decay will be convolved
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
void fconv_cs_time_axis(
        double *inplace_output, int n_output,
        double *time_axis, int n_time_axis,
        double *instrument_response_function, int n_instrument_response_function,
        double *lifetime_spectrum, int n_lifetime_spectrum,
        int convolution_start = 0,
        int convolution_stop = -1
);


#endif //FIT2X_FSCONV_H

// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_FSCONV_H
#define TTTRLIB_FSCONV_H

// Validation: A/B-TESTED 2026-08-17 -- fconv/fconv_per/fconv_per_cs (scalar and SIMD lifetime counts), sconv,
//   fconv_ref, the *_time_axis wrapper vs NumPy transcriptions of the trapezoid convolution
//   integral (np.convolve, no recursion) and a brute-force periodic sum (IRF tiled over 40
//   periods, folded back): 1e-13 abs; shift_lamp vs np.interp; rescale* vs the closed
//   formulas; add_pile_up_to_model vs a Coates (1968) transcription (inclusive cumulative
//   sum). Two things on record there: fconv_per_cs bin 0 carries a (1+exp(-dt/tau)) factor
//   fconv_per does not, and the Python `fconv_ref` binding drops `dt`. test/python/decayfit/test_ab_decay_reference.py.
//   Register: okf/testing/algorithm-validation.md

#include <cmath>  /* std::ceil */
#include <numeric> /* std::accumulate */
#include <iostream>
#include <vector>
#include <algorithm> /* std::max */
#include <string.h> /* strcmp */

/* SIMD intrinsics and the runtime-dispatch macros live in info.h; the AVX
 * (x86_64) and NEON (AArch64) kernels in DecayConvolution.cpp are compiled in
 * per target and selected at runtime. No compiler-wide SIMD flag is required
 * here. */


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
 * \brief Scale the model function to the data considering weights and background.
 *
 * This function scales the model function (fit) to the data by counting the number
 * of photons between a start and a stop micro time counting channel. The number
 * of photons between start and stop is counted, and the model function is scaled
 * to match the data by area, considering the noise of the data and a constant
 * offset of the data.
 *
 * The scaling parameter (scale) is calculated using the formula:
 * \f$ \text{scale} = \frac{\sum \text{fit} \cdot (\text{decay} - \text{bg}) \cdot (e^2 + 10^{-12})}{\sum \text{fit}^2 \cdot (e^2 + 10^{-12})} \f$
 *
 * where `e` is the third argument: the *inverse* error of each channel (so
 * `e^2` is the weight `1/sigma^2`, and the `1e-12` floor keeps a zero-error
 * channel from vanishing). Callers that hold errors pass `1/sigma`, not
 * `sigma^2` -- the A/B against the closed formula pins this reading.
 *
 * @param fit [in,out] Model function that is scaled (modified in-place).
 * @param decay [in] Experimental data to which the model function is scaled.
 * @param w_sq [in] Inverse errors `e = 1/sigma` of the data (weight = e^2 + 1e-12).
 * @param bg [in] Constant background of the data.
 * @param scale [out] The scaling parameter (the factor) by which the model
 * function is multiplied.
 * @param start [in] The start micro time channel.
 * @param stop [in] The stop micro time channel.
 */
void rescale_w_bg(double *fit, double *decay, double *w_sq, double bg, double *scale, int start, int stop);


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
 * SIMD optimized for large lifetime spectra)
 *
 * This function is a modification of fconv for large lifetime spectra. The
 * lifetime spectrum is processed with SIMD intrinsics, several lifetimes at
 * once; spectra whose lifetime count is not a multiple of the register width
 * are zero padded.
 *
 * @deprecated Alias of fconv(). The scalar/SIMD choice is made at runtime
 * inside fconv() itself (AVX+FMA on x86_64, NEON on AArch64, scalar
 * elsewhere), so this name promises a decision the caller does not have.
 * Call fconv(); this shim goes away after one release.
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
void fconv_simd(double *fit, double *x, double *lamp, int numexp, int start, int stop, double dt=0.05);


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
 * high repetition rate), SIMD optimized version
 *
 * @deprecated Alias of fconv_per(), which already selects the best kernel at
 * runtime; see fconv_simd(). Call fconv_per(); this shim goes away after one
 * release.
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
void fconv_per_simd(
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
 * @brief Two-channel periodic convolution (as fconv_per_cs) for a pair of
 * detection channels that share the same set of lifetimes (e.g. the parallel
 * and perpendicular channels of a polarisation-resolved decay).
 *
 * Equivalent to calling fconv_per_cs twice, but on AArch64 the two channels are
 * evaluated together in NEON float64x2 lanes. The per-channel recurrence is
 * latency-bound, so packing the two channels into one register advances both
 * per FMA-latency and roughly doubles convolution throughput. ``x0[2k+1]`` must
 * equal ``x1[2k+1]`` for every lifetime k (only the amplitudes and IRF differ).
 */
void fconv_per_cs_2ch(double *fit0, double *fit1,
                      const double *x0, const double *x1,
                      const double *lamp0, const double *lamp1,
                      int numexp, int stop, int n_points,
                      double period, int conv_stop, double dt);


/*!
 * @brief Templated scalar core of fconv_per_cs(), for automatic
 * differentiation.
 *
 * Same recursion as fconv_per_cs's own scalar fallback, transcribed onto a
 * template parameter so it also runs under `tttrlib::Dual<GradVec<N>>`
 * (modules/math/include/Dual.h): one forward-mode pass then yields the model
 * *and* its gradient with respect to whichever entries of `x` carry a
 * derivative, in place of a central-difference gradient's 2N evaluations.
 *
 * `lamp` (the IRF) stays `double` -- it is measured data, never a fit
 * parameter, so there is nothing to differentiate it with respect to. The
 * runtime-dispatched NEON/AVX kernels in DecayConvolution.cpp are untouched
 * and still serve the plain-`double` objective through fconv_per_cs(); this
 * is a second, separate body for the type that needs to carry a derivative.
 *
 * `exp` is called unqualified so ADL finds `tttrlib::exp` for
 * `T = Dual<G>`; the local `using std::exp` supplies the `T = double`
 * overload.
 *
 * **The species are interleaved, because the recursion is latency-bound.**
 * Each species is a serial dependency chain over the channels --
 * `fitcurr = (fitcurr + a) * expcurr + c` -- so one species at a time leaves
 * the pipeline mostly empty: at ~4 cycles per dependent FMA, 53 species x
 * 512 channels x 2 loops x 4 is ~62 us of a measured 103 at 3.5 GHz. The
 * species are *independent of one another*, so `FCONV_AD_BLOCK` of their
 * recursions are advanced together and the latency is hidden. Measured on
 * this header, 512 channels, min-of-many interleaved in one process:
 *
 *     species    serial     B=2      B=4      B=8
 *           1    2.0 us    1.9      2.0      2.5
 *           2    3.9 us    1.9      2.0      2.5
 *           4    7.8 us    3.8      2.0      2.5
 *          53  103.2 us   50.3     27.6     17.4     <- 5.9x
 *          97  188.2 us   91.2     49.3     32.4     <- 5.8x
 *
 * It is **ILP, not vectorisation**: plain `-O3` and `-mcpu=native` give the
 * same figures, so it holds on x86 too -- which matters, because x86 has no
 * AVX kernel for this variant and takes the scalar path for the plain-double
 * case as well.
 *
 * **What moves.** Interleaving changes the *order* the per-species
 * contributions are summed into `fit[i]`: a block is summed and added once,
 * rather than each species being added in turn. That is a few ULP --
 * measured at **5e-16** relative to the curve peak against the serial body,
 * where the callers that pin curves do so at 1e-10 to 1e-14. A spectrum of
 * one species has nothing to interleave and B=8 is *slower* there than the
 * plain recursion, so `numexp < FCONV_AD_BLOCK_MIN` keeps the serial body --
 * which also leaves every single-exponential result bit-identical.
 */
/// How many independent species recursions fconv_per_cs_ad advances at once.
/// 8 on the measurement in the comment above; the block is a compile-time
/// bound so the inner loop unrolls into registers rather than a memory array.
#define FCONV_AD_BLOCK 8

/// Below this many species there is not enough work to fill the pipeline and
/// the blocked body is *slower*, so the plain recursion is used -- which also
/// keeps every one-species result bit-identical to what it always was.
#define FCONV_AD_BLOCK_MIN 2

/// The plain recursion, one species at a time. Reproduces fconv_per_cs's own
/// scalar fallback op-for-op, and is what fconv_per_cs_ad falls back to for a
/// spectrum too small to interleave.
template <typename T>
void fconv_per_cs_ad_serial(T *fit, const T *x, const double *lamp, int numexp,
                            int stop, int n_points, double period,
                            int conv_stop, double dt) {
    using std::exp;
    const int period_n = (int)std::ceil(period / dt - 0.5);
    const double deltathalf = dt * 0.5;
    for (int i = 0; i <= stop; i++) fit[i] = T(0.0);
    const int stop1 = (period_n > n_points - 1) ? n_points - 1 : period_n;

    for (int ne = 0; ne < numexp; ne++) {
        const T expcurr = exp(-dt / x[2 * ne + 1]);
        const T tail_a = 1.0 / (1.0 - exp(-period / x[2 * ne + 1]));
        T fitcurr(0.0);
        fit[0] += (deltathalf * lamp[0]) * (expcurr + 1.0) * x[2 * ne];
        int i = 1;
        for (; i <= conv_stop; i++) {
            fitcurr = (fitcurr + deltathalf * lamp[i - 1]) * expcurr + deltathalf * lamp[i];
            fit[i] += fitcurr * x[2 * ne];
        }
        for (; i <= stop1; i++) {
            fitcurr = fitcurr * expcurr;
            fit[i] += fitcurr * x[2 * ne];
        }
        // wrap-around tail -- see fconv_per_cs's own scalar fallback for why
        // fitcurr is used as-is at bin 0 before it steps.
        fitcurr = fitcurr * exp(-(period_n - stop1) * dt / x[2 * ne + 1]);
        for (i = 0; i <= stop; i++) {
            fit[i] += fitcurr * x[2 * ne] * tail_a;
            fitcurr = fitcurr * expcurr;
        }
    }
}

template <typename T>
void fconv_per_cs_ad(T *fit, const T *x, const double *lamp, int numexp,
                     int stop, int n_points, double period, int conv_stop,
                     double dt) {
    using std::exp;
    if (numexp < FCONV_AD_BLOCK_MIN) {
        fconv_per_cs_ad_serial(fit, x, lamp, numexp, stop, n_points, period,
                               conv_stop, dt);
        return;
    }
    const int period_n = (int)std::ceil(period / dt - 0.5);
    const double deltathalf = dt * 0.5;
    for (int i = 0; i <= stop; i++) fit[i] = T(0.0);
    const int stop1 = (period_n > n_points - 1) ? n_points - 1 : period_n;

    for (int base = 0; base < numexp; base += FCONV_AD_BLOCK) {
        const int nb = (numexp - base < FCONV_AD_BLOCK) ? numexp - base
                                                        : FCONV_AD_BLOCK;
        // Per-species constants, hoisted so the channel loops below carry
        // nothing but the recursion itself.
        T expcurr[FCONV_AD_BLOCK], amp[FCONV_AD_BLOCK];
        T tail_a[FCONV_AD_BLOCK], post[FCONV_AD_BLOCK], fitcurr[FCONV_AD_BLOCK];
        for (int b = 0; b < FCONV_AD_BLOCK; b++) {
            if (b < nb) {
                const T tau = x[2 * (base + b) + 1];
                expcurr[b] = exp(-dt / tau);
                amp[b] = x[2 * (base + b)];
                tail_a[b] = 1.0 / (1.0 - exp(-period / tau));
                post[b] = exp(-(period_n - stop1) * dt / tau);
            } else {
                // A padding lane. Zero amplitude and zero decay make it
                // contribute exactly nothing, which is cheaper than a branch
                // in the inner loop and is what keeps that loop unrollable.
                expcurr[b] = T(0.0); amp[b] = T(0.0);
                tail_a[b] = T(0.0); post[b] = T(0.0);
            }
            fitcurr[b] = T(0.0);
        }
        {
            T acc(0.0);
            for (int b = 0; b < FCONV_AD_BLOCK; b++)
                acc += (expcurr[b] + 1.0) * amp[b];
            fit[0] += (deltathalf * lamp[0]) * acc;
        }
        int i = 1;
        for (; i <= conv_stop; i++) {
            const double lo = deltathalf * lamp[i - 1];
            const double hi = deltathalf * lamp[i];
            T acc(0.0);
            for (int b = 0; b < FCONV_AD_BLOCK; b++) {
                fitcurr[b] = (fitcurr[b] + lo) * expcurr[b] + hi;
                acc += fitcurr[b] * amp[b];
            }
            fit[i] += acc;
        }
        for (; i <= stop1; i++) {
            T acc(0.0);
            for (int b = 0; b < FCONV_AD_BLOCK; b++) {
                fitcurr[b] = fitcurr[b] * expcurr[b];
                acc += fitcurr[b] * amp[b];
            }
            fit[i] += acc;
        }
        // wrap-around tail -- see fconv_per_cs's own scalar fallback for why
        // fitcurr is used as-is at bin 0 before it steps.
        for (int b = 0; b < FCONV_AD_BLOCK; b++)
            fitcurr[b] = fitcurr[b] * post[b];
        for (i = 0; i <= stop; i++) {
            T acc(0.0);
            for (int b = 0; b < FCONV_AD_BLOCK; b++) {
                acc += fitcurr[b] * amp[b] * tail_a[b];
                fitcurr[b] = fitcurr[b] * expcurr[b];
            }
            fit[i] += acc;
        }
    }
}


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
 * @brief Header-only core of shift_lamp(), so a consumer that only has this
 * header can shift a response function.
 *
 * Same body as shift_lamp() -- which now calls it -- rather than a second
 * one: `imp.bff` vendors this header byte for byte and evaluates a TCSPC
 * decay as a node in its model graph, and the timeshift is a *fit parameter*
 * there, so the shift has to be reachable without linking the library. The
 * alternative was a third implementation of a ten-line interpolation, which
 * is how two sides of a boundary end up disagreeing about what a shift is.
 * `fconv_per_cs_ad` above is the same arrangement for the same reason.
 *
 * Templated on the sample type for the same reason `fconv_per_cs_ad` is: the
 * response is measured data, but the *shift* may carry a derivative under
 * `tttrlib::Dual`, and then the interpolated output does too.
 *
 * @param lampsh[out] the shifted response function
 * @param lamp[in] the response function
 * @param ts[in] shift in samples; positive moves the response to *earlier*
 * indices (`lampsh[j] = lamp[j + ts]`). Note the sign: chisurf's
 * `shift_array(v, s)` is this function called with `-s`.
 * @param n_points[in] number of samples
 * @param out_value[in] what to write where the shift has no source sample
 */
template <typename T>
void shift_lamp_ad(double *lampsh, const double *lamp, T ts, int n_points,
                   double out_value = 0.0) {
    using std::floor;
    const int tsint = (int) (floor(ts));
    const T tsdbl = ts - (double) tsint;
    int out_left = 0, out_right = 0, j;

    if (tsint < 0) out_left = -tsint;
    if (tsint + 1 > 0) out_right = tsint + 1;

    for (j = 0; j < out_left; j++) lampsh[j] = out_value;
    for (j = out_left; j < (n_points - out_right); j++)
        lampsh[j] = lamp[j + tsint] * (1 - tsdbl) + lamp[j + tsint + 1] * (tsdbl);
    for (j = (n_points - out_right); j < n_points; j++) lampsh[j] = out_value;
}


/*!
 * @brief Coates pile-up scaling factors, header-only, model type templated
 *
 * The body of \ref add_pile_up_to_model, kept in the header (like
 * `shift_lamp_ad` above it) so a consumer holding only the header --
 * imp.bff's TCSPC decay node -- applies pile-up with this implementation
 * rather than a second one of its own. The scaling factors depend only on
 * the *data* (plain doubles); the model may be an autodiff type, which is
 * why only the final multiply touches `T`.
 *
 * Three edge cases carry chisurf's semantics (they were fixed there first,
 * 2026-09-02, and the two implementations must not disagree):
 *
 * - a measurement time that accounts for fewer excitation pulses than there
 *   are detected photons makes Coates' eq. 4 diverge; the model is left
 *   *unscaled* rather than turned into NaN;
 * - the detection probability is capped strictly below one so eq. 4 stays
 *   finite for a barely long enough measurement;
 * - a channel without a detected photon makes eq. 4 a 0/0 expression; its
 *   analytic p->0 limit is the number of excitation pulses remaining, so
 *   the scaling stays smooth instead of zeroing the model in every empty
 *   channel.
 *
 * @param model[in,out] the model function, scaled in place over [start, stop)
 * @param n_model[in] number of elements in the model array
 * @param data[in] the experimental decay (counts per channel)
 * @param n_data[in] number of elements in the experimental decay
 * @param repetition_rate[in] the repetition (excitation) rate in MHz
 * @param instrument_dead_time[in] the detection dead-time in nanoseconds
 * @param measurement_time[in] the measurement time in seconds
 * @param start[in] first channel of the window
 * @param stop[in] one past the last channel (negative: to the end)
 */
template <typename T>
void add_pile_up_to_model_ad(
        T* model, int n_model,
        const double* data, int n_data,
        double repetition_rate,
        double instrument_dead_time,
        double measurement_time,
        int start = 0,
        int stop = -1
) {
    stop = stop < 0 ? n_data : std::min(n_data, stop);
    stop = std::min(stop, n_model);
    start = start < 0 ? 0 : std::min(n_data, start);
    if (stop <= start) return;

    const double rate_hz = repetition_rate * 1e6;
    const double dead_s = instrument_dead_time * 1e-9;
    std::vector<double> cum_sum(n_data);
    std::partial_sum(data, data + n_data, cum_sum.begin(), std::plus<double>());
    const double n_pulse_detected = cum_sum[cum_sum.size() - 1];
    const double live_time = measurement_time - n_pulse_detected * dead_s;
    const double n_excitation_pulses = live_time * rate_hz;

    // Fewer excitation pulses than detected photons: the correction is
    // undefined (eq. 2's denominator reaches zero); leave the model alone.
    if (n_excitation_pulses <= n_pulse_detected) return;

    const double p_max = 1.0 - 1e-12;
    std::vector<double> sf(n_data, 0.0);
    double s = 0.0;
    for (int i = start; i < stop; i++) {
        const double remaining = n_excitation_pulses - cum_sum[i];
        const double p = std::min(data[i] / remaining, p_max);
        const double rescaled = -std::log(1.0 - p);
        // 0/0 in an empty channel; the analytic p->0 limit is `remaining`.
        sf[i] = (rescaled <= 0.0) ? remaining : data[i] / rescaled;
        s += sf[i];
    }
    if (!(s > 0.0)) return;
    const double norm = (double)(stop - start) / s;
    for (int i = start; i < stop; i++)
        model[i] = model[i] * (sf[i] * norm);
}


/*!
 * @brief Add a pile-up distortion to the model function
 *
 * This function adds a pile up distortion to a model fluorescence decay. The
 * model used to compute the pile-up distortion follows the description of Coates
 * (1968, eq. 2 and eq. 4). The body is \ref add_pile_up_to_model_ad, with
 * chisurf's edge-case semantics (no NaN on a too-short measurement time, a
 * capped detection probability, the analytic limit in empty channels).
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
 * \brief Threshold the amplitudes in the interleaved lifetime spectrum.
 *
 * Amplitudes with absolute values smaller than the specified threshold are
 * set to zero.
 *
 * @param lifetime_spectrum [in,out] Interleaved lifetime spectrum (amplitude, lifetime).
 * @param n_lifetime_spectrum [in] Number of elements in the lifetime spectrum.
 * @param amplitude_threshold [in] Threshold value for amplitude discrimination.
 */
void discriminate_small_amplitudes(
    double* lifetime_spectrum, int n_lifetime_spectrum,
    double amplitude_threshold
);


/*!
* \brief Compute the fluorescence decay for a lifetime spectrum and an instrument
* response function considering periodic excitation.
*
* Fills the pre-allocated output array `model` with a fluorescence
* intensity decay defined by a set of fluorescence lifetimes specified in the
* `lifetime_spectrum` parameter. The fluorescence decay is convolved
* (non-periodically) with an instrumental response function defined by
* `irf`.
*
* This function calculates a fluorescence intensity model that is
* convolved with an instrument response function (IRF). The fluorescence
* intensity model is specified by its fluorescence lifetime spectrum,
* represented by an interleaved array containing fluorescence lifetimes with
* corresponding amplitudes.
*
* This convolution only works with evenly linear spaced time axes.
*
* @param model [in,out] In-place output array that is filled with the values
* of the computed fluorescence intensity decay model.
* @param n_model [in] Number of elements in the output array.
* @param time_axis [in] Time-axis of the model.
* @param n_time_axis [in] Length of the time axis.
* @param irf [in] Instrument response function array.
* @param n_irf [in] Length of the instrument response function array.
* @param lifetime_spectrum [in] Interleaved array of amplitudes and fluorescence
* lifetimes in the form (amplitude, lifetime, amplitude, lifetime, ...).
* @param n_lifetime_spectrum [in] Number of elements in the lifetime spectrum.
* @param convolution_start [in] Start channel of convolution (position in array
* of IRF).
* @param convolution_stop [in] Convolution stop channel (the index on the time-axis).
* @param period [in] Period of repetition in units of the lifetime (usually,
* nano-seconds). Default value is 100.0.
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
 * \brief Compute the fluorescence decay for a lifetime spectrum and an instrument response function.
 *
 * Fills the pre-allocated output array `inplace_output` with a fluorescence
 * intensity decay defined by a set of fluorescence lifetimes specified in the
 * `lifetime_spectrum` parameter. The fluorescence decay is convolved
 * (non-periodically) with an instrumental response function defined by
 * `instrument_response_function`.
 *
 * This function calculates a fluorescence intensity decay model that is
 * convolved with an instrument response function (IRF). The fluorescence
 * intensity decay model is specified by its fluorescence lifetime spectrum,
 * represented by an interleaved array containing fluorescence lifetimes with
 * corresponding amplitudes.
 *
 * The convolution supports unevenly spaced time axes.
 *
 * @param inplace_output [in,out] In-place output array that is filled with the
 * values of the computed fluorescence intensity decay model.
 * @param n_output [in] Number of elements in the output array.
 * @param time_axis [in] Time-axis of the fluorescence intensity decay model.
 * @param n_time_axis [in] Length of the time axis.
 * @param instrument_response_function [in] Instrument response function array.
 * @param n_instrument_response_function [in] Length of the instrument response function array.
 * @param lifetime_spectrum [in] Interleaved array of amplitudes and fluorescence
 * lifetimes in the form (amplitude, lifetime, amplitude, lifetime, ...).
 * @param n_lifetime_spectrum [in] Number of elements in the lifetime spectrum.
 * @param convolution_start [in] Start channel of convolution (position in array
 * of IRF).
 * @param convolution_stop [in] Convolution stop channel (the index on the time-axis).
 */
void fconv_cs_time_axis(
    double *inplace_output, int n_output,
    double *time_axis, int n_time_axis,
    double *instrument_response_function, int n_instrument_response_function,
    double *lifetime_spectrum, int n_lifetime_spectrum,
    int convolution_start = 0,
    int convolution_stop = -1
);



#endif //TTTRLIB_FSCONV_H

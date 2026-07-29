// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file DecayFitDFA.h
 * \brief Polarisation-resolved donor⊗FRET⊗anisotropy decays, in the frequency domain.
 *
 * A fluorescence decay measured through a polariser carries three independent
 * rate processes at once: the donor's own de-excitation, the FRET transfer that
 * competes with it, and the rotational depolarisation that decides how the
 * photons divide between the parallel and perpendicular channels. This model
 * keeps them separate — a *spectrum* of rates for each — and forms the decay as
 * their outer product, so a FRET-rate distribution is fitted as a distribution
 * rather than collapsed into an apparent lifetime.
 *
 * \par The frequency domain is NOT the fast path — measure before choosing it
 * Excitation repeats, so the measured decay is the periodic sum of the response.
 * In frequency space that has a closed form: for a rate \f$k\f$ per bin and a
 * period of \f$n_r\f$ bins the periodic decay is \f$e^{-kt}/(1-e^{-k n_r})\f$,
 * whose discrete transform collapses to
 *
 * \f[
 *     D(w) \;=\; \frac{1}{1 - e^{-k}\,e^{-2\pi i w / n_r}}
 * \f]
 *
 * which is elegant, and **slower** than the recursive time-domain convolution
 * (`fconv_per_cs`) this library already has. Both are
 * \f$O(n \cdot n_{\text{rates}})\f$ — the closed form costs one complex division
 * per rate and frequency, against one multiply-add per rate and bin for the
 * recursion — so the transform buys no better scaling, only worse constants, and
 * the recursion is additionally SIMD-optimised. Measured (`bench_convolution.py`,
 * M1 Pro): **1.7x** slower at a single lifetime, **4.8x** at 16 and **6.3x** at
 * 64, because the closed form is what dominates, not the transform. The gap
 * *widens* with the rate count — which is exactly the regime a donor(x)FRET outer
 * product lives in, so the frequency domain is at its worst where this model
 * needs it most.
 *
 * So a model over rate *spectra* should convolve with the recursion. What the
 * frequency domain is genuinely for is the two things the recursion cannot do:
 * convolving an arbitrary **measured pattern** (an autofluorescence reference is
 * not a sum of exponentials), and a **sub-bin timeshift**, which is one
 * transform of the response per evaluation regardless of how many rates there
 * are (measured: +24%, 53 to 66 us at n = 1024 and 16 rates). These functions
 * provide both, plus the closed form as an independent check on the recursion.
 *
 * \par The two conventions differ by exactly half a bin
 * `fconv_per_cs` integrates the response with the trapezoid rule
 * (\f$\tfrac{dt}{2}(\mathrm{irf}_{i-1} + \mathrm{irf}_i)\f$), which centres it
 * between samples; a spectral product is a rectangular rule, which does not.
 * Left uncorrected the two sit half a bin apart — which would show up as an
 * unexplained lifetime bias between two models fitted to the same data, not as
 * an error. `convolve()` therefore pre-filters the response with the same
 * \f$[\tfrac12, \tfrac12]\f$ kernel the trapezoid rule applies before the
 * spectral product, so the two backends describe the *same* instrument rather
 * than two half a bin apart. What is left is genuine second-order
 * discretisation, \f$O((dt/\tau)^2)\f$: measured at 2.0e-2 for a lifetime of
 * 3 bins, 1.2e-4 at 51 bins and 1.0e-5 at 205 bins. Sample a decay finely enough
 * to fit and the two agree to five digits; sample it at three bins per lifetime
 * and no discretisation will save the fit anyway.
 *
 * \par A note on a formula that circulates for this model
 * A widely-copied form of the above carries two extra factors,
 * \f$-\mathrm{expm1}(-kn)/\mathrm{expm1}(-k n_r)\f$. They evaluate to exactly
 * \f$-1\f$ when \f$n = n_r\f$ and therefore cancel; taken with \f$n\f$ equal to
 * the length of the (half-spectrum) frequency grid they do not, and instead
 * scale each rate by a different factor — about 3.5% at \f$k = 0.1\f$ per bin.
 * That distorts the *relative* weights of a rate spectrum, which is the very
 * thing a FRET-rate distribution is meant to measure. The closed form above is
 * used here instead, and `test_dfa_closed_form_matches_direct_periodic_sum`
 * pins it against a direct time-domain sum.
 *
 * \par Model
 * With donor rates \f$k_d\f$ (weights \f$p_d\f$), FRET rates \f$k_f\f$
 * (\f$p_f\f$) and depolarisation rates \f$k_a\f$ (\f$p_a\f$):
 *
 * \f[
 *   f(t) = \sum_{d,f} p_d p_f e^{-(k_d + k_f)t}, \qquad
 *   (fr)(t) = \sum_{d,f,a} p_d p_f p_a e^{-(k_d + k_f + k_a)t}
 * \f]
 *
 * and the two detected channels are \f$VV = f + 2 r_0 (fr)\f$ and
 * \f$VH = g\,[f - r_0 (fr)]\f$. Setting the FRET spectrum to a single zero rate
 * gives the donor-only reference, so a D0 measurement and its DA partner are the
 * same model with different weights rather than two implementations.
 *
 * \par The anisotropy is not quite \f$r_0 e^{-k_a t}\f$
 * Under repetitive excitation the photons left over from earlier pulses have
 * depolarised further than the ones just created, so \f$f\f$ and \f$fr\f$ wrap
 * with different factors and the *measured* anisotropy carries a constant
 * scaling:
 *
 * \f[
 *   r(t) = r_0\,e^{-k_a t}\,
 *          \frac{1 - e^{-k_f n_r}}{1 - e^{-(k_f + k_a) n_r}}
 * \f]
 *
 * It is small — 0.16% for a 10 ns lifetime and a 20 ns rotational correlation
 * time at a 32 ns period — but it is systematic, and it is easy to mistake for a
 * modelling error when comparing against the textbook expression. The model
 * reproduces the expression above to machine precision.
 */
#ifndef TTTRLIB_DECAYFITDFA_H
#define TTTRLIB_DECAYFITDFA_H

#include <complex>
#include <cstddef>
#include <vector>

/*!
 * \brief The periodic multiexponential and its VV/VH projection.
 *
 * Free functions rather than a class: they are the numerical core, and keeping
 * them callable on their own is what makes them testable against a direct
 * time-domain sum without constructing a fit.
 */
namespace dfa {

/*!
 * \brief Half-spectrum of a periodic multiexponential decay.
 *
 * \param rates       Decay rates **per bin** (that is, \f$dt/\tau\f$).
 * \param weights     Amplitude of each rate; same length as \p rates.
 * \param n_bins      Bins in one excitation period.
 * \param spectrum    Output, `n_bins / 2 + 1` complex values.
 *
 * The repetition is already contained in the closed form, so no wrap-around
 * correction is applied afterwards.
 */
void periodic_spectrum(const std::vector<double> &rates,
                       const std::vector<double> &weights,
                       std::size_t n_bins,
                       std::vector<std::complex<double>> &spectrum);

/*!
 * \brief Multiply a half-spectrum by the phase ramp of a sub-bin timeshift.
 *
 * A shift of a whole bin is a roll; a fractional shift is not, and rounding it
 * to the nearest bin is a systematic error on the fitted lifetime when bins are
 * coarse. In frequency space any shift — fractional included — is a phase ramp.
 *
 * \warning **Apply this to the instrument response, not to the decay.** A phase
 * ramp is *band-limited* interpolation, so it rings wherever the signal has a
 * step. The decay has one: it jumps at the period boundary, when the next pulse
 * arrives. Shifting the decay by half a bin makes its tail oscillate — measured
 * here at roughly ±0.14 against a true value of 0.002, and **negative**, which a
 * Poisson likelihood cannot take the logarithm of. The instrument response is a
 * compact pulse that is near zero at both ends, so shifting it does not ring
 * (verified: strictly positive, and every bin interpolates between the two
 * neighbouring integer shifts).
 *
 * This is also where the shift belongs physically: it describes a misalignment
 * between the recorded response and the data, not a property of the decay.
 * Convolution is commutative, so the fitted model is the same either way — only
 * the numerics differ, and only one of the two is usable.
 *
 * \param shift_bins Shift in bins; may be fractional and may be negative.
 */
void apply_timeshift(std::vector<std::complex<double>> &spectrum,
                     std::size_t n_bins,
                     double shift_bins);

/*!
 * \brief Real decay of length \p n_bins from a half-spectrum.
 */
void inverse(const std::vector<std::complex<double>> &spectrum,
             std::size_t n_bins,
             std::vector<double> &decay);

/*!
 * \brief Forward half-spectrum of a real signal, normalised to unit sum.
 *
 * Used for the instrument response: a convolution must not change the number of
 * photons, so the response is normalised once rather than at every evaluation.
 * Negative entries (which a background-subtracted response can have) are
 * clipped, because a negative response has no physical meaning and drives the
 * likelihood to nonsense.
 */
void normalised_spectrum(const std::vector<double> &signal,
                         std::size_t n_bins,
                         std::vector<std::complex<double>> &spectrum);

/*! \brief How a decay is convolved with the instrument response. */
enum class ConvolutionMethod {
    /*!
     * Recursive single-pole filter in the time domain (`fconv_per_cs`).
     * **The default.** ~7x faster, SIMD-optimised, and the discretisation every
     * other fit in this library uses — so results are comparable across models.
     */
    Recursive = 0,
    /*!
     * Closed-form periodic spectrum multiplied by the response's spectrum.
     * Slower, but it is the only path that can convolve an arbitrary measured
     * pattern or apply a sub-bin timeshift, and it is an independent check on
     * the recursion (different algorithm, same physics).
     */
    Spectral = 1
};

/*!
 * \brief Convolve a periodic multiexponential with the instrument response.
 *
 * One call, two backends, the same answer to within discretisation — see
 * ConvolutionMethod for which to pick and the file header for the measured
 * difference between them.
 *
 * \param method      Which backend.
 * \param rates       Decay rates per bin.
 * \param weights     Amplitude of each rate.
 * \param irf         Instrument response, at least \p n_bins long.
 * \param n_bins      Bins in one excitation period.
 * \param shift_bins  Sub-bin shift of the response; see apply_timeshift. A
 *                    non-zero shift forces one spectral transform of the
 *                    response even under `Recursive`, because the recursion
 *                    cannot express a fractional shift — the cost is one
 *                    transform, independent of how many rates there are.
 * \param decay       Output, \p n_bins values.
 */
void convolve(ConvolutionMethod method,
              const std::vector<double> &rates,
              const std::vector<double> &weights,
              const std::vector<double> &irf,
              std::size_t n_bins,
              double shift_bins,
              std::vector<double> &decay);

/*!
 * \brief The VV/VH decay of a donor⊗FRET⊗anisotropy rate spectrum.
 *
 * \param kd,pd  Donor rates (per bin) and their weights.
 * \param kf,pf  FRET rates and weights. A single zero rate is donor-only.
 * \param ka,pa  Depolarisation rates and weights.
 * \param r0     Fundamental anisotropy.
 * \param g      Detection-efficiency ratio of the two channels.
 * \param n_bins Bins per channel (one excitation period).
 * \param vv,vh  Outputs, `n_bins` each.
 *
 * The decay is *not* convolved here and carries no background — those are the
 * caller's, so this function stays testable in isolation.
 */
void vv_vh_decay(const std::vector<double> &kd, const std::vector<double> &pd,
                 const std::vector<double> &kf, const std::vector<double> &pf,
                 const std::vector<double> &ka, const std::vector<double> &pa,
                 double r0, double g, std::size_t n_bins,
                 std::vector<double> &vv, std::vector<double> &vh);

/*!
 * \brief The VV/VH decay, convolved with the instrument response.
 *
 * The form a fit actually compares against data. Both rate products — the
 * fluorescence and the fluorescence-times-anisotropy — are convolved with the
 * chosen backend before the polarisation projection, which is the correct order:
 * the response acts on the photons, not on the anisotropy.
 */
void vv_vh_convolved(ConvolutionMethod method,
                     const std::vector<double> &kd, const std::vector<double> &pd,
                     const std::vector<double> &kf, const std::vector<double> &pf,
                     const std::vector<double> &ka, const std::vector<double> &pa,
                     double r0, double g,
                     const std::vector<double> &irf,
                     std::size_t n_bins, double shift_bins,
                     std::vector<double> &vv, std::vector<double> &vh);

}  // namespace dfa

#endif // TTTRLIB_DECAYFITDFA_H

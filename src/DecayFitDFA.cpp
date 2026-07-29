// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file DecayFitDFA.cpp
 * \brief The frequency-domain donor⊗FRET⊗anisotropy decay kernel.
 *
 * Physics and rationale are in DecayFitDFA.h. The transforms use the pocketfft
 * header already vendored for `Pda.cpp` and `CLSMISM.cpp`, so this adds no
 * dependency.
 *
 * The model this implements is due to Oleg Opanasyuk, in work with Nicolaas van
 * der Voort; it is written here from the physics rather than transliterated.
 */
#include "DecayFitDFA.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "pocketfft/pocketfft_hdronly.h"

#include "DecayConvolution.h"   // fconv_per_cs: the recursive backend

namespace {

constexpr double kTwoPi = 6.283185307179586476925286766559;

/*! Guard the shapes once, where the message can name what is wrong. */
void require_pairs(const std::vector<double> &rates,
                   const std::vector<double> &weights,
                   const char *what) {
    if (rates.size() != weights.size()) {
        throw std::invalid_argument(
            std::string(what) + ": rates and weights differ in length");
    }
    if (rates.empty()) {
        throw std::invalid_argument(std::string(what) + ": no rates given");
    }
}

}  // namespace


namespace dfa {

void periodic_spectrum(const std::vector<double> &rates,
                       const std::vector<double> &weights,
                       std::size_t n_bins,
                       std::vector<std::complex<double>> &spectrum) {
    require_pairs(rates, weights, "periodic_spectrum");
    if (n_bins == 0) throw std::invalid_argument("periodic_spectrum: n_bins is zero");

    const std::size_t n_freq = n_bins / 2 + 1;
    spectrum.assign(n_freq, std::complex<double>(0.0, 0.0));

    for (std::size_t r = 0; r < rates.size(); ++r) {
        const double k = rates[r];
        const double p = weights[r];
        if (p == 0.0) continue;
        // A non-positive rate would make the periodic sum diverge; treat it as a
        // constant offset over the period, which is its limit.
        if (!(k > 0.0)) {
            spectrum[0] += p * static_cast<double>(n_bins);
            continue;
        }
        const double e_k = std::exp(-k);
        for (std::size_t w = 0; w < n_freq; ++w) {
            const double angle = -kTwoPi * static_cast<double>(w) /
                                 static_cast<double>(n_bins);
            const std::complex<double> phase(std::cos(angle), std::sin(angle));
            // D(w) = 1 / (1 - e^{-k} e^{-2 pi i w / n}); the periodic repetition
            // is already contained in this, so no tail correction follows.
            spectrum[w] += p / (1.0 - e_k * phase);
        }
    }
}


void apply_timeshift(std::vector<std::complex<double>> &spectrum,
                     std::size_t n_bins,
                     double shift_bins) {
    if (shift_bins == 0.0 || n_bins == 0) return;
    for (std::size_t w = 0; w < spectrum.size(); ++w) {
        const double angle = -kTwoPi * static_cast<double>(w) * shift_bins /
                             static_cast<double>(n_bins);
        spectrum[w] *= std::complex<double>(std::cos(angle), std::sin(angle));
    }
}


void inverse(const std::vector<std::complex<double>> &spectrum,
             std::size_t n_bins,
             std::vector<double> &decay) {
    if (spectrum.size() != n_bins / 2 + 1) {
        throw std::invalid_argument("inverse: spectrum is not a half-spectrum of n_bins");
    }
    decay.assign(n_bins, 0.0);
    const pocketfft::shape_t shape{n_bins};
    const pocketfft::stride_t stride_in{
        static_cast<std::ptrdiff_t>(sizeof(std::complex<double>))};
    const pocketfft::stride_t stride_out{static_cast<std::ptrdiff_t>(sizeof(double))};
    const pocketfft::shape_t axes{0};
    // 1/n normalisation, so a round trip is the identity.
    pocketfft::c2r(shape, stride_in, stride_out, axes, /*forward=*/false,
                   spectrum.data(), decay.data(),
                   1.0 / static_cast<double>(n_bins));
}


void normalised_spectrum(const std::vector<double> &signal,
                         std::size_t n_bins,
                         std::vector<std::complex<double>> &spectrum) {
    if (signal.size() < n_bins) {
        throw std::invalid_argument("normalised_spectrum: signal shorter than n_bins");
    }
    std::vector<double> clipped(signal.begin(), signal.begin() + n_bins);
    double total = 0.0;
    for (double &v : clipped) {
        if (!(v > 0.0)) v = 0.0;   // a negative response is not physical
        total += v;
    }
    if (total > 0.0) {
        for (double &v : clipped) v /= total;
    }

    spectrum.assign(n_bins / 2 + 1, std::complex<double>(0.0, 0.0));
    const pocketfft::shape_t shape{n_bins};
    const pocketfft::stride_t stride_in{static_cast<std::ptrdiff_t>(sizeof(double))};
    const pocketfft::stride_t stride_out{
        static_cast<std::ptrdiff_t>(sizeof(std::complex<double>))};
    const pocketfft::shape_t axes{0};
    pocketfft::r2c(shape, stride_in, stride_out, axes, /*forward=*/true,
                   clipped.data(), spectrum.data(), 1.0);
}


void vv_vh_decay(const std::vector<double> &kd, const std::vector<double> &pd,
                 const std::vector<double> &kf, const std::vector<double> &pf,
                 const std::vector<double> &ka, const std::vector<double> &pa,
                 double r0, double g, std::size_t n_bins,
                 std::vector<double> &vv, std::vector<double> &vh) {
    require_pairs(kd, pd, "vv_vh_decay (donor)");
    require_pairs(kf, pf, "vv_vh_decay (FRET)");
    require_pairs(ka, pa, "vv_vh_decay (anisotropy)");

    // f(t): the fluorescence, over the donor x FRET rate product. The rate of a
    // pair is the *sum* of the rates — de-excitation and transfer compete, so
    // their rates add — and its weight is the product of the weights.
    std::vector<double> k_f, p_f;
    k_f.reserve(kd.size() * kf.size());
    p_f.reserve(kd.size() * kf.size());
    for (std::size_t d = 0; d < kd.size(); ++d) {
        for (std::size_t f = 0; f < kf.size(); ++f) {
            k_f.push_back(kd[d] + kf[f]);
            p_f.push_back(pd[d] * pf[f]);
        }
    }

    // f(t) r(t): the same, further multiplied by the depolarisation spectrum.
    std::vector<double> k_fr, p_fr;
    k_fr.reserve(k_f.size() * ka.size());
    p_fr.reserve(k_f.size() * ka.size());
    for (std::size_t i = 0; i < k_f.size(); ++i) {
        for (std::size_t a = 0; a < ka.size(); ++a) {
            k_fr.push_back(k_f[i] + ka[a]);
            p_fr.push_back(p_f[i] * pa[a]);
        }
    }

    std::vector<std::complex<double>> spec_f, spec_fr;
    periodic_spectrum(k_f, p_f, n_bins, spec_f);
    periodic_spectrum(k_fr, p_fr, n_bins, spec_fr);

    std::vector<double> f, fr;
    inverse(spec_f, n_bins, f);
    inverse(spec_fr, n_bins, fr);

    vv.assign(n_bins, 0.0);
    vh.assign(n_bins, 0.0);
    for (std::size_t i = 0; i < n_bins; ++i) {
        // The ideal polarisation factors: parallel sees +2r, perpendicular -r.
        vv[i] = f[i] + 2.0 * r0 * fr[i];
        vh[i] = g * (f[i] - r0 * fr[i]);
    }
}

}  // namespace dfa


namespace dfa {

void convolve(ConvolutionMethod method,
              const std::vector<double> &rates,
              const std::vector<double> &weights,
              const std::vector<double> &irf,
              std::size_t n_bins,
              double shift_bins,
              std::vector<double> &decay) {
    require_pairs(rates, weights, "convolve");
    if (irf.size() < n_bins) {
        throw std::invalid_argument("convolve: irf shorter than n_bins");
    }

    // Normalise unconditionally, and *before* any shift. A convolution must not
    // change the number of photons, so the amplitude of the result has to mean
    // the same thing whatever the response and whatever the shift. Normalising
    // only on the shifted path — which is what falls out if the spectral
    // transform is allowed to do it as a side effect — makes the amplitude jump
    // the moment a fit floats the shift off zero, which reads as a correlation
    // between shift and amplitude rather than as a bug.
    std::vector<double> response(irf.begin(), irf.begin() + n_bins);
    double total = 0.0;
    for (double &v : response) {
        if (!(v > 0.0)) v = 0.0;   // a negative response is not physical
        total += v;
    }
    if (total > 0.0) {
        for (double &v : response) v /= total;
    }

    // A fractional shift cannot be expressed by the recursion, so it is applied
    // to the response spectrally first. That costs one transform per call and is
    // independent of the number of rates, so it does not change which backend is
    // cheaper.
    if (shift_bins != 0.0) {
        std::vector<std::complex<double>> s;
        normalised_spectrum(response, n_bins, s);
        apply_timeshift(s, n_bins, shift_bins);
        inverse(s, n_bins, response);
    }

    if (method == ConvolutionMethod::Recursive) {
        // fconv_per_cs takes [amplitude, lifetime] pairs and works in the same
        // units as dt; rates here are per bin, so dt = 1 and tau = 1 / k.
        std::vector<double> x;
        x.reserve(2 * rates.size());
        for (std::size_t r = 0; r < rates.size(); ++r) {
            const double k = rates[r] > 0.0 ? rates[r] : 1.0e-12;
            x.push_back(weights[r]);
            x.push_back(1.0 / k);
        }
        decay.assign(n_bins, 0.0);
        fconv_per_cs(decay.data(), x.data(), response.data(),
                     static_cast<int>(rates.size()),
                     static_cast<int>(n_bins) - 1, static_cast<int>(n_bins),
                     static_cast<double>(n_bins),
                     static_cast<int>(n_bins) - 1, 1.0);
        return;
    }

    // Spectral. The two backends must be the same convolution, and they are made
    // so exactly rather than approximately.
    //
    // The recursion applies the trapezoid rule to the convolution integral. Work
    // out which kernel that leaves and it is e^{-kL} at every lag L >= 1 and
    // *one half* at L = 0 — the trapezoid rule evaluated across the exponential's
    // jump from 0 to 1. Nothing else changes. Halving one sample of the kernel is
    // subtracting half a delta, and a delta has a flat spectrum, so in frequency
    // space the whole difference is the constant 1/2 subtracted below.
    //
    // This matters more than it looks. Left uncorrected the two backends differ
    // by (1 + e^{-k})/2 — a factor that depends on the *rate*, so it does not
    // divide out of a rate spectrum but reweights it, 0.5% at k = 0.01 and 5% at
    // k = 0.1. Distorting the relative weights of a rate spectrum is precisely
    // the failure this file's header documents in a formula that circulates for
    // this model; it would be poor to reproduce it here by a different route.
    double weight_sum = 0.0;
    for (double p : weights) weight_sum += p;

    std::vector<std::complex<double>> si, sd;
    normalised_spectrum(response, n_bins, si);
    periodic_spectrum(rates, weights, n_bins, sd);
    for (std::size_t w = 0; w < si.size(); ++w) {
        sd[w] = (sd[w] - 0.5 * weight_sum) * si[w];
    }
    inverse(sd, n_bins, decay);
}


void vv_vh_convolved(ConvolutionMethod method,
                     const std::vector<double> &kd, const std::vector<double> &pd,
                     const std::vector<double> &kf, const std::vector<double> &pf,
                     const std::vector<double> &ka, const std::vector<double> &pa,
                     double r0, double g,
                     const std::vector<double> &irf,
                     std::size_t n_bins, double shift_bins,
                     std::vector<double> &vv, std::vector<double> &vh) {
    require_pairs(kd, pd, "vv_vh_convolved (donor)");
    require_pairs(kf, pf, "vv_vh_convolved (FRET)");
    require_pairs(ka, pa, "vv_vh_convolved (anisotropy)");

    std::vector<double> k_f, p_f;
    for (std::size_t d = 0; d < kd.size(); ++d)
        for (std::size_t f = 0; f < kf.size(); ++f) {
            k_f.push_back(kd[d] + kf[f]);
            p_f.push_back(pd[d] * pf[f]);
        }

    std::vector<double> k_fr, p_fr;
    for (std::size_t i = 0; i < k_f.size(); ++i)
        for (std::size_t a = 0; a < ka.size(); ++a) {
            k_fr.push_back(k_f[i] + ka[a]);
            p_fr.push_back(p_f[i] * pa[a]);
        }

    std::vector<double> f, fr;
    convolve(method, k_f, p_f, irf, n_bins, shift_bins, f);
    convolve(method, k_fr, p_fr, irf, n_bins, shift_bins, fr);

    vv.assign(n_bins, 0.0);
    vh.assign(n_bins, 0.0);
    for (std::size_t i = 0; i < n_bins; ++i) {
        vv[i] = f[i] + 2.0 * r0 * fr[i];
        vh[i] = g * (f[i] - r0 * fr[i]);
    }
}

}  // namespace dfa

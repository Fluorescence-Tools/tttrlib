// SPDX-License-Identifier: BSD-3-Clause
#include "BlindIRF.h"
#include "Registry.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>

namespace tttrlib {

namespace {

using cdouble = std::complex<double>;

// Radix-2 FFT (in-place, size must be power of 2). sign=-1 forward, +1 inverse.
void fft(cdouble* a, int n, double sign) {
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = sign * 2.0 * M_PI / len;
        cdouble wl(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            cdouble w(1.0);
            for (int j = 0; j < len / 2; ++j) {
                auto u = a[i + j];
                auto v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// Next power of 2 >= n
int next_pow2(int n) {
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

// CIRCULAR convolution of period n: result[i] = sum_j signal[j] * kernel[(i-j) mod n].
// The forward model of the reference (ChiSurf `irf_estimation.py`, from
// BrightEyes-FLISM / BIRFI, Gomez-Sanchez et al. 2024) is periodic: a TCSPC
// histogram spans one excitation period and the decay tail wraps into the next,
// so the convolution over the period is circular. Computed exactly for ANY n by
// a zero-padded linear FFT convolution folded back onto the period -- the
// former next_pow2(n) transform was circular only when n was a power of two and
// something in between otherwise (found by the known-answer A/B, 2026-08-17).
static std::vector<double> linear_fft(
    const std::vector<double>& signal, const std::vector<double>& kernel, bool correlate
) {
    int n = static_cast<int>(signal.size());
    int m = next_pow2(2 * n - 1);
    std::vector<cdouble> sf(m, cdouble(0.0)), kf(m, cdouble(0.0));
    for (int i = 0; i < n; ++i) { sf[i] = signal[i]; kf[i] = kernel[i]; }
    fft(sf.data(), m, -1.0);
    fft(kf.data(), m, -1.0);
    for (int i = 0; i < m; ++i) sf[i] *= correlate ? std::conj(kf[i]) : kf[i];
    fft(sf.data(), m, 1.0);
    std::vector<double> result(m);
    for (int i = 0; i < m; ++i) result[i] = sf[i].real() / m;
    return result;   // convolution: lags 0..2n-2; correlation: lags 0..n-1 then m-(n-1)..m-1 (negative)
}

std::vector<double> fft_convolve(
    const std::vector<double>& signal,
    const std::vector<double>& kernel
) {
    int n = static_cast<int>(signal.size());
    auto lin = linear_fft(signal, kernel, false);
    std::vector<double> result(n);
    for (int i = 0; i < n; ++i) {
        result[i] = lin[i];
        if (i + n < 2 * n - 1) result[i] += lin[i + n];   // wrap of the tail
    }
    return result;
}

// Circular cross-correlation: result[i] = sum_j signal[j] * kernel[(j-i) mod n] --
// the adjoint of fft_convolve, which is what the Richardson-Lucy back-projection
// needs. The reference convolves with the time-reversed kernel instead, which is
// this correlation shifted by one bin; measured to move nothing visible on the
// known answer (peak and shape identical), so the exact adjoint is kept.
std::vector<double> fft_correlate(
    const std::vector<double>& signal,
    const std::vector<double>& kernel
) {
    int n = static_cast<int>(signal.size());
    auto lin = linear_fft(signal, kernel, true);
    int m = static_cast<int>(lin.size());
    std::vector<double> result(n);
    for (int i = 0; i < n; ++i) {
        result[i] = lin[i];                                 // lag +i
        if (i > 0) result[i] += lin[m - (n - i)];           // lag i - n (negative), wrapped
    }
    return result;
}

// 1D median filter with reflect boundary
double median(std::vector<double>& window) {
    int n = static_cast<int>(window.size());
    int mid = n / 2;
    std::nth_element(window.begin(), window.begin() + mid, window.end());
    if (n % 2 == 1) return window[mid];
    double a = window[mid];
    std::nth_element(window.begin(), window.begin() + mid - 1, window.end());
    return 0.5 * (a + window[mid - 1]);
}

void median_filter_1d(
    const std::vector<double>& in, std::vector<double>& out,
    int n, int half_window
) {
    out.resize(n);
    int w = 2 * half_window + 1;
    for (int i = 0; i < n; ++i) {
        std::vector<double> window(w);
        for (int k = -half_window; k <= half_window; ++k) {
            int idx = i + k;
            if (idx < 0) idx = -idx;
            if (idx >= n) idx = 2 * (n - 1) - idx;
            window[k + half_window] = in[std::clamp(idx, 0, n - 1)];
        }
        out[i] = median(window);
    }
}

// Savitzky-Golay derivative via local polynomial fit.
// Computes smoothed first derivative at each point.
std::vector<double> sg_derivative(
    const std::vector<double>& y, int n, int window, int order, double dt
) {
    int half = window / 2;
    std::vector<double> deriv(n, 0.0);

    // Precompute the derivative row of the pseudo-inverse (V^T V)^-1 V^T
    // For the derivative, we evaluate d/dx of the polynomial at x=0
    // Using the design matrix V[i, j] = (i - half)^j for j=0..order
    // The derivative is the coefficient of x^1 (the linear term).

    // Solve normal equations for the derivative row
    // Build A = V^T V (order+1 x order+1) and the derivative weighting vector
    int p = order + 1;
    std::vector<double> ATA(p * p, 0.0);
    // Unitless abscissa (i - half) here AND in the weight vector below; the
    // derivative is scaled to per-time at the end. Until 2026-08-17 the normal
    // equations used (i - half)*dt while the weights used (i - half), so the
    // filter was not the SG derivative: on a TCSPC decay its minimum landed on
    // the RISING edge and the "tail" window was a handful of bins wide (tau
    // came out 0.2 ns for a 2.5 ns decay). Found by the known-answer A/B.
    for (int i = 0; i < window; ++i) {
        double xi = (i - half);
        double xj = 1.0;
        for (int r = 0; r < p; ++r) {
            double xk = 1.0;
            for (int c = 0; c < p; ++c) {
                ATA[r * p + c] += xj * xk;
                xk *= xi;
            }
            xj *= xi;
        }
    }

    // Invert ATA (small matrix, Gaussian elimination)
    std::vector<double> invATA(p * p);
    std::vector<double> work = ATA;
    std::vector<double> I(p * p, 0.0);
    for (int i = 0; i < p; ++i) I[i * p + i] = 1.0;
    for (int k = 0; k < p; ++k) {
        int piv = k; double best = std::abs(work[k * p + k]);
        for (int i = k + 1; i < p; ++i) {
            double v = std::abs(work[i * p + k]);
            if (v > best) { best = v; piv = i; }
        }
        if (piv != k) {
            for (int j = 0; j < p; ++j) {
                std::swap(work[k * p + j], work[piv * p + j]);
                std::swap(I[k * p + j], I[piv * p + j]);
            }
        }
        double diag = work[k * p + k];
        if (std::abs(diag) < 1e-30) continue;
        for (int j = 0; j < p; ++j) { work[k * p + j] /= diag; I[k * p + j] /= diag; }
        for (int i = 0; i < p; ++i) {
            if (i == k) continue;
            double f = work[i * p + k];
            for (int j = 0; j < p; ++j) {
                work[i * p + j] -= f * work[k * p + j];
                I[i * p + j] -= f * I[k * p + j];
            }
        }
    }
    invATA = I;

    // The derivative weights are: w[i] = sum_r invATA[1, r] * (i-half)^r
    std::vector<double> weights(window);
    for (int i = 0; i < window; ++i) {
        double xi = (i - half);
        double xj = 1.0;
        double s = 0.0;
        for (int r = 0; r < p; ++r) {
            s += invATA[1 * p + r] * xj;
            xj *= xi;
        }
        weights[i] = s;  // this gives the coefficient of the linear term
    }

    // Apply to each point with reflect boundary
    for (int i = 0; i < n; ++i) {
        double s = 0.0;
        for (int k = -half; k <= half; ++k) {
            int idx = i + k;
            if (idx < 0) idx = -idx;
            if (idx >= n) idx = 2 * (n - 1) - idx;
            idx = std::clamp(idx, 0, n - 1);
            s += weights[k + half] * y[idx];
        }
        deriv[i] = s / dt;  // per unit time
    }
    return deriv;
}

} // anonymous namespace

std::vector<double> blind_irf_estimate(
    const std::vector<double>& data_in,
    int n_samples, int n_channels,
    double dt,
    int rl_iterations,
    int regularization,
    int sg_window,
    int sg_order
) {
    // --- Step 1: Find decay start/end per channel ---
    std::vector<int> t0(n_channels, 0), t1(n_channels, n_samples - 1);
    std::vector<double> y_range(n_channels);

    for (int c = 0; c < n_channels; ++c) {
        std::vector<double> y(n_samples);
        for (int i = 0; i < n_samples; ++i)
            y[i] = data_in[i * n_channels + c];

        double ymin = *std::min_element(y.begin(), y.end());
        double ymax = *std::max_element(y.begin(), y.end());
        y_range[c] = ymax - ymin;

        auto dy = sg_derivative(y, n_samples, sg_window, sg_order, dt);

        // t0: global minimum of derivative
        int t0_c = 0;
        double dy_min = dy[0];
        for (int i = 1; i < n_samples; ++i) {
            if (dy[i] < dy_min) { dy_min = dy[i]; t0_c = i; }
        }
        t0[c] = t0_c;

        // t1: first point after t0 with persistent positive derivative
        int t1_c = n_samples - 1;
        int persistence = 5;
        for (int i = t0_c + 1; i < n_samples - persistence; ++i) {
            double avg = 0.0;
            for (int p = 0; p < persistence; ++p) avg += dy[i + p];
            avg /= persistence;
            double amplitude = std::max(0.0, y[std::min(i + persistence, n_samples - 1)] - ymin);
            if (avg > 0 && amplitude > 0.05 * y_range[c]) {
                t1_c = i;
                break;
            }
        }
        t1[c] = t1_c;
    }

    // --- Step 2: Fit truncated exponential ---
    // The reference model (birfi / ChiSurf): one shared decay rate k, per-channel
    // amplitude A and background C, least squares over each channel's decay
    // region. birfi minimises it with Adam (1000 steps, often not converged);
    // here it is solved: for a given k the (A, C) of every channel are a 2x2
    // weighted linear least-squares problem (variable projection), so only k is
    // searched -- golden section on log k around a centroid guess. Weights are
    // Poisson (1/max(y,1)). The region starts one SG window after the steepest
    // descent, past the IRF-broadened bins. C matters twice: it is subtracted
    // before the Richardson-Lucy step, and min(y) or a tail median (the two
    // shortcuts tried first) both mis-estimate it -- one sits ~3 sigma below a
    // Poisson floor and hands RL a pedestal it smears into the IRF, the other
    // overshoots when the decay has not reached the floor by the last bin.
    std::vector<double> A_opt(n_channels, 1.0), C_opt(n_channels, 0.0);
    double k_opt = 0.01;
    {
        std::vector<int> r0(n_channels), r1(n_channels);
        double k_guess_num = 0.0, k_guess_den = 0.0;
        for (int c = 0; c < n_channels; ++c) {
            int a = std::min(t0[c] + sg_window, n_samples - 1);
            int b = std::min(t1[c], n_samples - 1);
            if (b - a < 3) a = t0[c];
            r0[c] = a; r1[c] = b;
            // centroid guess of 1/tau on this channel's region
            double ymin = std::numeric_limits<double>::infinity();
            for (int i = a; i <= b; ++i) ymin = std::min(ymin, data_in[i * n_channels + c]);
            double sxy = 0.0, sy = 0.0;
            for (int i = a; i <= b; ++i) {
                double yv = std::max(data_in[i * n_channels + c] - ymin, 0.0);
                sxy += (i - a) * dt * yv; sy += yv;
            }
            if (sy > 0.0 && sxy > 0.0) { k_guess_num += sy * (sy / sxy); k_guess_den += sy; }
        }
        const double k_guess = (k_guess_den > 0.0) ? k_guess_num / k_guess_den : 0.01;

        // total weighted SSE at k, with the per-channel (A, C) that minimise it
        auto sse_at = [&](double k, std::vector<double>* A_out, std::vector<double>* C_out) {
            double total = 0.0;
            for (int c = 0; c < n_channels; ++c) {
                double sw = 0, swe = 0, swee = 0, swy = 0, swey = 0;
                for (int i = r0[c]; i <= r1[c]; ++i) {
                    const double yv = data_in[i * n_channels + c];
                    const double w = 1.0 / std::max(yv, 1.0);
                    const double e = std::exp(-k * (i - r0[c]) * dt);
                    sw += w; swe += w * e; swee += w * e * e; swy += w * yv; swey += w * e * yv;
                }
                const double det = swee * sw - swe * swe;
                double A = 0.0, C = 0.0;
                if (std::abs(det) > 1e-300) {
                    A = (swey * sw - swe * swy) / det;
                    C = (swee * swy - swe * swey) / det;
                }
                if (A_out) (*A_out)[c] = A;
                if (C_out) (*C_out)[c] = C;
                for (int i = r0[c]; i <= r1[c]; ++i) {
                    const double yv = data_in[i * n_channels + c];
                    const double w = 1.0 / std::max(yv, 1.0);
                    const double res = yv - A * std::exp(-k * (i - r0[c]) * dt) - C;
                    total += w * res * res;
                }
            }
            return total;
        };

        // golden-section search on log k in [k_guess/10, k_guess*10]
        const double gr = 0.6180339887498949;
        double lo = std::log(std::max(k_guess, 1e-12) / 10.0), hi = std::log(std::max(k_guess, 1e-12) * 10.0);
        double x1 = hi - gr * (hi - lo), x2 = lo + gr * (hi - lo);
        double f1 = sse_at(std::exp(x1), nullptr, nullptr), f2 = sse_at(std::exp(x2), nullptr, nullptr);
        for (int it = 0; it < 80 && (hi - lo) > 1e-7; ++it) {
            if (f1 < f2) { hi = x2; x2 = x1; f2 = f1; x1 = hi - gr * (hi - lo); f1 = sse_at(std::exp(x1), nullptr, nullptr); }
            else         { lo = x1; x1 = x2; f1 = f2; x2 = lo + gr * (hi - lo); f2 = sse_at(std::exp(x2), nullptr, nullptr); }
        }
        k_opt = std::exp(0.5 * (lo + hi));
        sse_at(k_opt, &A_opt, &C_opt);
        for (int c = 0; c < n_channels; ++c) {
            if (!(C_opt[c] > 0.0)) C_opt[c] = 0.0;
            if (!(A_opt[c] > 1e-6)) A_opt[c] = 1e-6;
        }
    }

    // --- Step 3: Build kernel ---
    std::vector<double> kernel(n_samples);
    double ksum = 0.0;
    for (int i = 0; i < n_samples; ++i) {
        kernel[i] = std::exp(-k_opt * i * dt);
        ksum += kernel[i];
    }
    if (ksum > 0) for (auto& v : kernel) v /= ksum;

    // --- Step 4: Richardson-Lucy deconvolution ---
    std::vector<double> irf(n_samples * n_channels);

    #pragma omp parallel for
    for (int c = 0; c < n_channels; ++c) {
        std::vector<double> y(n_samples), x_est(n_samples, 1.0);
        for (int i = 0; i < n_samples; ++i) {
            y[i] = std::max(data_in[i * n_channels + c] - C_opt[c], 0.0);
        }

        for (int iter = 0; iter < rl_iterations; ++iter) {
            auto conv = fft_convolve(x_est, kernel);
            for (int i = 0; i < n_samples; ++i)
                conv[i] = std::max(conv[i], 1e-4);
            std::vector<double> ratio(n_samples);
            for (int i = 0; i < n_samples; ++i)
                ratio[i] = y[i] / conv[i];
            auto correction = fft_correlate(ratio, kernel);
            for (int i = 0; i < n_samples; ++i) {
                x_est[i] *= correction[i];
                if (x_est[i] < 0) x_est[i] = 0;
            }

            if (regularization > 1) {
                median_filter_1d(x_est, x_est, n_samples, regularization / 2);
            }
        }

        // Remove DC offset from tail
        int tail_len = std::min(50, n_samples / 10);
        if (tail_len > 0) {
            std::vector<double> tail(x_est.end() - tail_len, x_est.end());
            std::sort(tail.begin(), tail.end());
            double dc = tail[tail_len / 2];
            for (int i = 0; i < n_samples; ++i) {
                x_est[i] = std::max(x_est[i] - dc, 0.0);
            }
        }

        for (int i = 0; i < n_samples; ++i)
            irf[i * n_channels + c] = x_est[i];
    }

    return irf;
}

} // namespace tttrlib

// ---- registry entries (Registry.h, core): declared next to the code, registered
// when this library loads; a static consumer links the archive whole.
namespace {
const char* const kBlindIrfEntry = R"JSON({
  "name": "blind_irf",
  "label": "Blind IRF estimation from a decay",
  "summary": "Recovers the instrument response function from a measured decay alone by regularised Richardson-Lucy deconvolution with Savitzky-Golay smoothing.",
  "description": "When no scatterer measurement exists the IRF is estimated blind: the decay is deconvolved iteratively (Richardson-Lucy) against a smooth kernel estimate, with entropy-type regularisation and Savitzky-Golay smoothing of the estimate between iterations. Reproduces birfi (Vicidomini lab). Assumes a mono- or few-exponential decay much longer than the IRF; the estimate carries no absolute time zero.",
  "operation_type": "calibration",
  "method": "blind_irf_estimate",
  "params_schema": {
    "type": "object",
    "properties": {
      "rl_iterations": {
        "type": "integer",
        "title": "RL iterations",
        "default": 500
      },
      "regularization": {
        "type": "number",
        "title": "Regularisation",
        "default": 3
      },
      "sg_window": {
        "type": "integer",
        "title": "SG window",
        "default": 11
      },
      "sg_order": {
        "type": "integer",
        "title": "SG order",
        "default": 3
      }
    }
  },
  "inputs": {
    "required": [
      "decay_histogram"
    ]
  },
  "outputs": {
    "columns": [
      "irf"
    ]
  },
  "row_grain": "curve_point",
  "references": [
    {
      "type": "journal",
      "authors": "Richardson, W. H.",
      "title": "Bayesian-based iterative method of image restoration",
      "journal": "J Opt Soc Am",
      "year": 1972,
      "volume": "62",
      "pages": "55-59"
    },
    {
      "type": "journal",
      "authors": "Lucy, L. B.",
      "title": "An iterative technique for the rectification of observed distributions",
      "journal": "Astron J",
      "year": 1974,
      "volume": "79",
      "pages": "745-754"
    }
  ],
  "api": [
    "blind_irf_estimate",
    "blind_irf_estimate_array"
  ],
  "can_replay": true
})JSON";
bool register_blindirf_entries() {
    tttrlib::register_algorithm_json("decay", "blind_irf", kBlindIrfEntry);
    return true;
}
const bool kBlindIRFRegistered = register_blindirf_entries();
}  // namespace

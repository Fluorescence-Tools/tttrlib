// SPDX-License-Identifier: BSD-3-Clause
#include "BlindIRF.h"

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

// Circular convolution via FFT: result[i] = sum_j signal[j] * kernel[(i-j) mod n]
std::vector<double> fft_convolve(
    const std::vector<double>& signal,
    const std::vector<double>& kernel
) {
    int n = static_cast<int>(signal.size());
    int m = next_pow2(n);
    std::vector<cdouble> sf(m, cdouble(0.0)), kf(m, cdouble(0.0));
    for (int i = 0; i < n; ++i) { sf[i] = signal[i]; kf[i] = kernel[i]; }
    fft(sf.data(), m, -1.0);
    fft(kf.data(), m, -1.0);
    for (int i = 0; i < m; ++i) sf[i] *= kf[i];
    fft(sf.data(), m, 1.0);
    std::vector<double> result(n);
    for (int i = 0; i < n; ++i) result[i] = sf[i].real() / m;
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
    for (int i = 0; i < window; ++i) {
        double xi = (i - half) * dt;
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
        deriv[i] = s;  // derivative in units of 1/dt
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
    // Estimate per-channel A, C and shared k via simple optimization
    std::vector<double> A_opt(n_channels), C_opt(n_channels);
    double k_opt = 0.01;

    for (int c = 0; c < n_channels; ++c) {
        std::vector<double> y(n_samples);
        for (int i = 0; i < n_samples; ++i)
            y[i] = data_in[i * n_channels + c];

        double ymin = *std::min_element(y.begin(), y.end());
        double ymax = *std::max_element(y.begin(), y.end());
        C_opt[c] = std::max(ymin, 0.0);
        A_opt[c] = std::max(ymax - C_opt[c], 1e-6);

        // Centroid-based lifetime from decay region
        if (t1[c] > t0[c] + 1) {
            double sum_xy = 0.0, sum_y = 0.0;
            for (int i = t0[c]; i <= t1[c] && i < n_samples; ++i) {
                double xv = (i - t0[c]) * dt;
                double yv = std::max(y[i] - ymin, 0.0);
                sum_xy += xv * yv;
                sum_y += yv;
            }
            if (sum_y > 0) k_opt = sum_y / sum_xy;  // 1/tau
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

    std::vector<double> kernel_t(kernel.rbegin(), kernel.rend());

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
            auto correction = fft_convolve(ratio, kernel_t);
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

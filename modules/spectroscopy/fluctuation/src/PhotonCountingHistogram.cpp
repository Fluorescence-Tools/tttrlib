// SPDX-License-Identifier: BSD-3-Clause
#include "PhotonCountingHistogram.h"
#include "Registry.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace tttrlib {

// ─── PCH ───────────────────────────────────────────────────────────────────

std::vector<double> pch_single_species(
    int k_max, double brightness,
    int n_grid, double x_max
) {
    const int n = k_max + 1;
    std::vector<double> p1(n, 0.0);
    if (brightness <= 0.0 || n_grid < 2) { p1[0] = 1.0; return p1; }

    const double dx = x_max / (n_grid - 1);

    for (int i = 1; i < n; ++i) {
        const int k = i;
        const double log_fact = std::lgamma(static_cast<double>(k) + 1.0);
        double total = 0.0;
        for (int gi = 0; gi < n_grid; ++gi) {
            const double xi = static_cast<double>(gi) * dx;
            const double lam = brightness * std::exp(-2.0 * xi * xi);
            if (lam <= 0.0) continue;
            total += xi * xi * std::exp(
                static_cast<double>(k) * std::log(lam) - log_fact - lam);
        }
        p1[i] = total * dx;
    }

    double s = 0.0;
    for (int i = 1; i < n; ++i) s += p1[i];
    p1[0] = std::max(0.0, 1.0 - s);
    return p1;
}

// Self-convolution: out[i+j] += a[i] * b[j], truncated to len
static std::vector<double> convolve(
    const std::vector<double>& a, const std::vector<double>& b, int len
) {
    std::vector<double> out(len, 0.0);
    int la = std::min(static_cast<int>(a.size()), len);
    int lb = std::min(static_cast<int>(b.size()), len);
    for (int i = 0; i < la; ++i) {
        if (a[i] == 0.0) continue;
        int mj = std::min(lb, len - i);
        for (int j = 0; j < mj; ++j)
            out[i + j] += a[i] * b[j];
    }
    return out;
}

// Poisson PMF
static double poisson_pmf(int k, double lam) {
    if (lam <= 0.0) return (k == 0) ? 1.0 : 0.0;
    return std::exp(static_cast<double>(k) * std::log(lam) - lam
                    - std::lgamma(static_cast<double>(k) + 1.0));
}

std::vector<double> pch_open_system(
    int k_max, double brightness, double avg_n, int max_n
) {
    const int len = k_max + 1;
    auto p1 = pch_single_species(k_max, brightness);
    std::vector<double> pk_tot(len, 0.0);
    avg_n = std::max(avg_n, 0.0);

    for (int N = 0; N <= max_n; ++N) {
        double w = poisson_pmf(N, avg_n);
        if (w <= 0.0) continue;
        std::vector<double> base(len, 0.0);
        if (N == 0) {
            base[0] = 1.0;
        } else {
            base = p1;
            for (int conv = 1; conv < N; ++conv)
                base = convolve(base, p1, len);
        }
        for (int i = 0; i < len; ++i) pk_tot[i] += w * base[i];
    }
    return pk_tot;
}

std::vector<double> pch_mixture(
    int k_max,
    const std::vector<double>& brightnesses,
    const std::vector<double>& avg_numbers
) {
    const int len = k_max + 1;
    // Two vectors of different lengths cannot describe a mixture. Rejecting
    // beats reading past the end, which happened to hit zeroed heap and
    // silently fitted a three-species argument list as one species.
    if (brightnesses.size() != avg_numbers.size())
        throw std::invalid_argument(
            "pch_mixture: brightnesses (" + std::to_string(brightnesses.size()) +
            ") and avg_numbers (" + std::to_string(avg_numbers.size()) +
            ") must have one entry per species");
    if (brightnesses.empty()) {
        std::vector<double> p(len, 1.0);
        return p;
    }
    std::vector<double> pk(len, 0.0);
    pk[0] = 1.0;
    for (size_t s = 0; s < brightnesses.size(); ++s) {
        if (brightnesses[s] <= 0.0 || avg_numbers[s] <= 0.0) continue;
        auto pj = pch_open_system(k_max, brightnesses[s], avg_numbers[s]);
        pk = convolve(pk, pj, len);
    }
    // check finite
    bool ok = false;
    for (auto& v : pk) if (std::isfinite(v)) { ok = true; break; }
    if (!ok) std::fill(pk.begin(), pk.end(), 1.0);
    return pk;
}

// ─── FIDA ──────────────────────────────────────────────────────────────────

std::vector<double> fida_dvdx_gaussian(int n_bins, double x_min) {
    // Returns flat [x0..xn, w0..wn] (2*n_bins entries)
    std::vector<double> result(2 * n_bins);
    double* x = result.data();
    double* w = result.data() + n_bins;
    const double dx = (1.0 - x_min) / (n_bins - 1);
    for (int i = 0; i < n_bins; ++i) {
        x[i] = x_min + static_cast<double>(i) * dx;
        double ln_x = -std::log(x[i]);
        if (ln_x > 0.0) w[i] = std::sqrt(ln_x) / x[i];
        else w[i] = 0.0;
    }
    double sum = 0.0;
    for (int i = 0; i < n_bins; ++i) sum += w[i];
    sum *= dx;
    if (sum > 0.0)
        for (int i = 0; i < n_bins; ++i) w[i] /= sum;
    return result;
}

// In-place inverse DFT of size m via naive O(m^2) for small m,
// or Cooley-Tukey radix-2 if m is a power of 2.
static void inverse_dft(const std::complex<double>* g, std::complex<double>* p, int m) {
    // Check if m is a power of 2
    bool pow2 = (m > 0) && ((m & (m - 1)) == 0);
    if (pow2) {
        // Cooley-Tukey IFFT
        // Bit-reversal
        for (int i = 1, j = 0; i < m; ++i) {
            int bit = m >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(const_cast<std::complex<double>&>(g[i]),
                                  const_cast<std::complex<double>&>(g[j]));
        }
        // Wait, we can't swap const input. Copy first.
        std::vector<std::complex<double>> work(g, g + m);
        for (int i = 1, j = 0; i < m; ++i) {
            int bit = m >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(work[i], work[j]);
        }
        for (int len = 2; len <= m; len <<= 1) {
            double ang = 2.0 * M_PI / len;  // positive for inverse
            std::complex<double> wlen(std::cos(ang), std::sin(ang));
            for (int i = 0; i < m; i += len) {
                std::complex<double> w(1.0);
                for (int j = 0; j < len / 2; ++j) {
                    auto u = work[i + j];
                    auto v = work[i + j + len / 2] * w;
                    work[i + j] = u + v;
                    work[i + j + len / 2] = u - v;
                    w *= wlen;
                }
            }
        }
        for (int i = 0; i < m; ++i) p[i] = work[i] / static_cast<double>(m);
    } else {
        // Naive O(m^2) IDFT
        for (int k = 0; k < m; ++k) {
            std::complex<double> s(0.0);
            for (int j = 0; j < m; ++j)
                s += g[j] * std::polar(1.0, 2.0 * M_PI * j * k / m);
            p[k] = s / static_cast<double>(m);
        }
    }
}

std::vector<double> fida_pch(
    int k_max,
    const std::vector<double>& species_flat,
    int n_species,
    double background,
    const std::vector<double>& profile_flat,
    int n_profile_bins,
    int oversample
) {
    // Profile
    std::vector<double> x, w;
    if (profile_flat.empty() || n_profile_bins < 2) {
        auto prof = fida_dvdx_gaussian(256, 1e-4);
        n_profile_bins = 256;
        x.assign(prof.data(), prof.data() + n_profile_bins);
        w.assign(prof.data() + n_profile_bins, prof.data() + 2 * n_profile_bins);
    } else {
        x.assign(profile_flat.data(), profile_flat.data() + n_profile_bins);
        w.assign(profile_flat.data() + n_profile_bins,
                 profile_flat.data() + 2 * n_profile_bins);
    }
    const double dx = x[1] - x[0];
    const int m = std::max(oversample, 1) * (k_max + 1);

    // Evaluate PGF on unit circle
    std::vector<std::complex<double>> exponent(m);
    std::vector<std::complex<double>> g(m);

    #pragma omp parallel for schedule(static)
    for (int j = 0; j < m; ++j) {
        std::complex<double> xi = std::polar(1.0, -2.0 * M_PI * j / m);
        std::complex<double> exp_val(0.0);
        for (int s = 0; s < n_species; ++s) {
            double q = species_flat[2 * s];
            double n = species_flat[2 * s + 1];
            std::complex<double> integ(0.0);
            for (int bi = 0; bi < n_profile_bins; ++bi) {
                integ += (std::exp((xi - 1.0) * (q * x[bi])) - 1.0) * w[bi];
            }
            exp_val += n * integ * dx;
        }
        exp_val += (xi - 1.0) * background;
        exponent[j] = exp_val;
        g[j] = std::exp(exp_val);
    }

    // Inverse FFT
    std::vector<std::complex<double>> p_cmplx(m);
    inverse_dft(g.data(), p_cmplx.data(), m);

    // Extract real part, clip negatives, normalize
    std::vector<double> p(k_max + 1);
    double total = 0.0;
    for (int k = 0; k <= k_max; ++k) {
        double val = p_cmplx[k].real();
        if (val < 0.0) val = 0.0;
        p[k] = val;
        total += val;
    }
    if (total > 0.0)
        for (int k = 0; k <= k_max; ++k) p[k] /= total;
    return p;
}

} // namespace tttrlib

// ---- registry entries (Registry.h, core): declared next to the code, registered
// when this library loads; a static consumer links the archive whole.
namespace {
const char* const kPchEntry = R"JSON({
  "name": "pch",
  "label": "Photon counting histogram (PCH / FIDA)",
  "summary": "Predicts the photon-count distribution of one or several species of given brightness and mean number in the observation volume, closed or open system, with the FIDA profile.",
  "description": "The PCH of Chen et al. for a 3-D Gaussian volume (single species, mixtures, open system with Poisson number fluctuations) and Kask et al.'s FIDA with an explicit brightness profile and background, so a measured count histogram can be fitted for molecular brightness and concentration. Validated against pysimfcs. Assumes stationarity over the binning time and a bin time short against diffusion.",
  "operation_type": "pch_histogram_computation",
  "method": "pch_mixture",
  "params_schema": {
    "type": "object",
    "properties": {
      "k_max": {
        "type": "integer",
        "title": "Max counts",
        "default": 30
      },
      "brightness": {
        "type": "number",
        "title": "Brightness (counts/bin)"
      },
      "avg_number": {
        "type": "number",
        "title": "Mean number in volume"
      }
    }
  },
  "inputs": {
    "required": [
      "count_histogram"
    ]
  },
  "outputs": {
    "columns": [
      "P(k)"
    ]
  },
  "row_grain": "curve_point",
  "references": [
    {
      "type": "journal",
      "authors": "Chen, Y., Müller, J. D., So, P. T. C., Gratton, E.",
      "title": "The photon counting histogram in fluorescence fluctuation spectroscopy",
      "journal": "Biophys J",
      "year": 1999,
      "volume": "77",
      "pages": "553-567"
    },
    {
      "type": "journal",
      "authors": "Kask, P., Palo, K., Ullmann, D., Gall, K.",
      "title": "Fluorescence-intensity distribution analysis and its application in biomolecular detection technology",
      "journal": "Proc Natl Acad Sci USA",
      "year": 1999,
      "volume": "96",
      "pages": "13756-13761"
    }
  ],
  "api": [
    "pch_single_species",
    "pch_mixture",
    "pch_open_system",
    "fida_pch",
    "fida_dvdx_gaussian"
  ],
  "can_replay": true
})JSON";
bool register_photoncountinghistogram_entries() {
    tttrlib::register_algorithm_json("fluctuation", "pch", kPchEntry);
    return true;
}
const bool kPhotonCountingHistogramRegistered = register_photoncountinghistogram_entries();
}  // namespace

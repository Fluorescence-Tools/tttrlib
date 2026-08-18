// SPDX-License-Identifier: BSD-3-Clause
#include "SpectralCrosstalk.h"
#include "Registry.h"

#include <cmath>
#include <algorithm>

namespace tttrlib {

std::vector<double> correct_three_cube(
    double i_dd, double i_da, double i_aa,
    double gamma, double alpha, double delta,
    double bg_dd, double bg_da, double bg_aa
) {
    double f_dd = i_dd - bg_dd;
    double f_aa = i_aa - bg_aa;
    double f_da = (i_da - bg_da) - alpha * f_dd - delta * f_aa;

    double fc = f_da;
    double denom = fc + gamma * f_dd;
    double E = (denom > 0.0) ? fc / denom : 0.0;

    // Stoichiometry: S = (gamma * F_dd + Fc) / (gamma * F_dd + Fc + F_aa / beta)
    // Without beta correction, use S = (gamma*Fdd + Fc) / (gamma*Fdd + Fc + Faa)
    // which assumes beta=1.
    double denom_s = denom + f_aa;
    double S = (denom_s > 0.0) ? denom / denom_s : 0.0;

    return {E, S, fc};
}

std::vector<double> correct_three_cube_batch(
    const std::vector<double>& i_dd,
    const std::vector<double>& i_da,
    const std::vector<double>& i_aa,
    double gamma, double alpha, double delta,
    double bg_dd, double bg_da, double bg_aa
) {
    int n = static_cast<int>(i_dd.size());
    std::vector<double> result(3 * n);

    #pragma omp parallel for schedule(static)
    for (int b = 0; b < n; ++b) {
        auto r = correct_three_cube(
            i_dd[b], i_da[b], i_aa[b],
            gamma, alpha, delta,
            bg_dd, bg_da, bg_aa
        );
        result[3 * b] = r[0];
        result[3 * b + 1] = r[1];
        result[3 * b + 2] = r[2];
    }
    return result;
}

std::vector<double> invert_mixing_ridge(
    const std::vector<double>& matrix,
    const std::vector<double>& measured,
    int n_sources, int n_detectors,
    double ridge
) {
    // Solve: sources = (M M^T + ridge*I)^-1 M measured
    // where M is n_sources x n_detectors (row-major)
    //
    // Actually the forward model is measured = M^T @ sources,
    // so the inverse is sources = (M M^T)^-1 M measured
    //
    // Normal equations: A = M M^T + ridge*I, b = M measured
    // sources = A^-1 b

    // Build A = M M^T (n_sources x n_sources)
    std::vector<double> A(n_sources * n_sources, 0.0);
    for (int i = 0; i < n_sources; ++i)
        for (int j = 0; j < n_sources; ++j) {
            double s = 0.0;
            for (int k = 0; k < n_detectors; ++k)
                s += matrix[i * n_detectors + k] * matrix[j * n_detectors + k];
            A[i * n_sources + j] = s + ((i == j) ? ridge : 0.0);
        }

    // Build b = M measured (n_sources)
    std::vector<double> b(n_sources, 0.0);
    for (int i = 0; i < n_sources; ++i) {
        double s = 0.0;
        for (int k = 0; k < n_detectors; ++k)
            s += matrix[i * n_detectors + k] * measured[k];
        b[i] = s;
    }

    // Solve A x = b via Gaussian elimination with partial pivoting
    std::vector<double> M = A;
    std::vector<double> x = b;
    for (int k = 0; k < n_sources; ++k) {
        int piv = k;
        double best = std::abs(M[k * n_sources + k]);
        for (int i = k + 1; i < n_sources; ++i) {
            double v = std::abs(M[i * n_sources + k]);
            if (v > best) { best = v; piv = i; }
        }
        if (piv != k) {
            for (int j = 0; j < n_sources; ++j)
                std::swap(M[k * n_sources + j], M[piv * n_sources + j]);
            std::swap(x[k], x[piv]);
        }
        if (std::abs(M[k * n_sources + k]) < 1e-300) continue;
        for (int i = k + 1; i < n_sources; ++i) {
            double f = M[i * n_sources + k] / M[k * n_sources + k];
            for (int j = k; j < n_sources; ++j)
                M[i * n_sources + j] -= f * M[k * n_sources + j];
            x[i] -= f * x[k];
        }
    }
    for (int i = n_sources - 1; i >= 0; --i) {
        double s = x[i];
        for (int j = i + 1; j < n_sources; ++j)
            s -= M[i * n_sources + j] * x[j];
        x[i] = (std::abs(M[i * n_sources + i]) > 1e-300)
            ? s / M[i * n_sources + i] : 0.0;
    }
    return x;
}

} // namespace tttrlib

// ---- registry entries (Registry.h, core): declared next to the code, registered
// when this library loads; a static consumer links the archive whole.
namespace {
const char* const kSpectralCrosstalkEntry = R"JSON({
  "name": "spectral_crosstalk",
  "label": "Spectral crosstalk / three-cube FRET correction",
  "summary": "Corrects multi-detector intensities for spectral crosstalk and direct excitation (gamma, alpha, delta), and inverts a general mixing matrix by ridge regression.",
  "description": "The three-cube (sensitised-emission) FRET correction of Lee et al.: donor bleed-through, acceptor direct excitation and detection-efficiency ratio applied to I_DD, I_DA, I_AA with background subtraction, per burst or per pixel. `invert_mixing_ridge` solves the general n-source/m-detector unmixing with Tikhonov damping for ill-conditioned matrices.",
  "operation_type": "background_correction",
  "method": "correct_three_cube",
  "params_schema": {
    "type": "object",
    "properties": {
      "gamma": {
        "type": "number",
        "title": "gamma",
        "default": 1.0
      },
      "alpha": {
        "type": "number",
        "title": "alpha (leakage)",
        "default": 0.0
      },
      "delta": {
        "type": "number",
        "title": "delta (direct excitation)",
        "default": 0.0
      },
      "ridge": {
        "type": "number",
        "title": "Ridge",
        "default": 0.0
      }
    }
  },
  "inputs": {
    "required": [
      "intensities"
    ]
  },
  "outputs": {
    "columns": [
      "corrected_intensities"
    ]
  },
  "row_grain": "burst",
  "references": [
    {
      "type": "journal",
      "authors": "Lee, N. K., Kapanidis, A. N., Wang, Y., Michalet, X., Mukhopadhyay, J., Ebright, R. H., Weiss, S.",
      "title": "Accurate FRET measurements within single diffusing biomolecules using alternating-laser excitation",
      "journal": "Biophys J",
      "year": 2005,
      "volume": "88",
      "pages": "2939-2953"
    }
  ],
  "api": [
    "correct_three_cube",
    "correct_three_cube_batch",
    "invert_mixing_ridge"
  ],
  "can_replay": true
})JSON";
bool register_spectralcrosstalk_entries() {
    tttrlib::register_algorithm_json("corrections", "spectral_crosstalk", kSpectralCrosstalkEntry);
    return true;
}
const bool kSpectralCrosstalkRegistered = register_spectralcrosstalk_entries();
}  // namespace

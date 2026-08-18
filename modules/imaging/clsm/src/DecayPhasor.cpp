// SPDX-License-Identifier: BSD-3-Clause
#include "DecayPhasor.h"
#include "Registry.h"
#include <cstdlib>

namespace {

/// Squared modulus below which the IRF phasor cannot be divided out without the
/// reciprocal overflowing. `(0, 0)` is the case that used to yield a silent nan.
constexpr double kMinIrfModulus2 = 1e-300;

/**
 * The IRF correction divides by |phasor_irf|^2, so a degenerate or non-finite
 * IRF phasor is not a data condition — it is a call that cannot be answered.
 * Reject it loudly rather than returning nan.
 */
void check_irf(double g_irf, double s_irf) {
    if (!std::isfinite(g_irf) || !std::isfinite(s_irf)) {
        throw std::invalid_argument(
            "DecayPhasor: the IRF phasor must be finite, got "
            "g_irf=" + std::to_string(g_irf) + ", s_irf=" + std::to_string(s_irf));
    }
    const double m2 = g_irf * g_irf + s_irf * s_irf;
    if (!(m2 > kMinIrfModulus2)) {
        throw std::invalid_argument(
            "DecayPhasor: the IRF phasor is degenerate (g_irf=" +
            std::to_string(g_irf) + ", s_irf=" + std::to_string(s_irf) +
            "). The correction divides by g_irf^2 + s_irf^2, so (0, 0) is not "
            "'no IRF' -- it is a division by zero. Use the identity (1, 0) for "
            "an ideal instrument response.");
    }
}

void check_frequency(double frequency) {
    if (!std::isfinite(frequency) || frequency <= 0.0) {
        throw std::invalid_argument(
            "DecayPhasor: frequency must be finite and positive, got " +
            std::to_string(frequency));
    }
}

}  // namespace


std::vector<double> DecayPhasor::compute_phasor(
        unsigned short* microtimes, int n_microtimes,
        double frequency,
        int minimum_number_of_photons,
        double g_irf,
        double s_irf,
        std::vector<int>* idxs
){
    check_frequency(frequency);
    check_irf(g_irf, s_irf);
    if(n_microtimes < 0){
        throw std::invalid_argument(
            "DecayPhasor: n_microtimes must not be negative, got " +
            std::to_string(n_microtimes));
    }
    if(microtimes == nullptr && n_microtimes > 0){
        throw std::invalid_argument(
            "DecayPhasor: microtimes is null but n_microtimes is " +
            std::to_string(n_microtimes));
    }

    double factor = (2. * frequency * M_PI);
    std::vector<double> re{-1, -1};
    double g_sum = 0.0;
    double s_sum = 0.0;
    double sum;
    if(idxs == nullptr){
        sum = n_microtimes;
        for(int i=0;i<n_microtimes;i++){
            g_sum += std::cos(microtimes[i] * factor);
            s_sum += std::sin(microtimes[i] * factor);
        }
    } else{
        sum = (double) idxs->size();
        for(auto &idx: *idxs){
            // Bounds-check the caller's selection: this used to read out of
            // bounds on a stale or mis-sized index vector.
            if(idx < 0 || idx >= n_microtimes){
                throw std::invalid_argument(
                    "DecayPhasor: selected index " + std::to_string(idx) +
                    " is outside [0, " + std::to_string(n_microtimes) + ")");
            }
            auto mt = microtimes[idx];
            g_sum += std::cos(mt * factor);
            s_sum += std::sin(mt * factor);
        }
    }
    if(sum > minimum_number_of_photons && sum > 0.0){
        double g_exp = g_sum / std::max(1., sum);
        double s_exp = s_sum / std::max(1., sum);
        re[0] = DecayPhasor::g(g_irf, s_irf, g_exp, s_exp);
        re[1] = DecayPhasor::s(g_irf, s_irf, g_exp, s_exp);
    }
    return re;
}


std::vector<double> DecayPhasor::compute_phasor_bincounts(
        std::vector<int> &bincounts,
        double frequency,
        int minimum_number_of_photons,
        double g_irf, double s_irf
){
    check_frequency(frequency);
    check_irf(g_irf, s_irf);

    double factor = (2. * frequency * M_PI);
    std::vector<double> re{-1, -1};
    double g_sum = 0.0;
    double s_sum = 0.0;
    double sum = (double) std::accumulate(bincounts.begin(), bincounts.end(), 0.0);
    int mt = 0;
    for(auto &count: bincounts){
        g_sum += count * std::cos(mt * factor);
        s_sum += count * std::sin(mt * factor);
        mt++;
    }
    // `sum > 0` matters independently of the photon minimum: a caller passing a
    // negative minimum would otherwise normalise an empty or net-negative
    // histogram and get a number back.
    if(sum > minimum_number_of_photons && sum > 0.0){
        double g_exp = g_sum / std::max(1., sum);
        double s_exp = s_sum / std::max(1., sum);
        re[0] = DecayPhasor::g(g_irf, s_irf, g_exp, s_exp);
        re[1] = DecayPhasor::s(g_irf, s_irf, g_exp, s_exp);
    }
    return re;
}



void DecayPhasor::compute_phasor_bincounts_batch(
        const int* bincounts2d, int n_decays, int n_bins,
        double** output, int* dim1, int* dim2,
        double frequency,
        int minimum_number_of_photons,
        double g_irf, double s_irf
){
    check_frequency(frequency);
    check_irf(g_irf, s_irf);
    if (!output || !dim1 || !dim2) throw std::invalid_argument("compute_phasor_bincounts_batch: null output");
    if (n_decays < 0 || n_bins < 0) throw std::invalid_argument("compute_phasor_bincounts_batch: negative shape");

    const double factor = (2. * frequency * M_PI);
    std::vector<double> ct(n_bins), st(n_bins);
    for (int mt = 0; mt < n_bins; ++mt) {          // the values the per-decay loop computes
        ct[mt] = std::cos(mt * factor);
        st[mt] = std::sin(mt * factor);
    }
    double* out = static_cast<double*>(std::malloc(std::max(1, n_decays * 2) * sizeof(double)));
    if (!out) throw std::bad_alloc();

    #pragma omp parallel for schedule(static) if(n_decays > 256)
    for (int r = 0; r < n_decays; ++r) {
        const int* row = bincounts2d + static_cast<size_t>(r) * n_bins;
        double g_sum = 0.0, s_sum = 0.0, sum = 0.0;
        for (int mt = 0; mt < n_bins; ++mt) {
            sum += row[mt];
            g_sum += row[mt] * ct[mt];
            s_sum += row[mt] * st[mt];
        }
        double gv = -1.0, sv = -1.0;
        if (sum > minimum_number_of_photons && sum > 0.0) {
            const double g_exp = g_sum / std::max(1., sum);
            const double s_exp = s_sum / std::max(1., sum);
            gv = DecayPhasor::g(g_irf, s_irf, g_exp, s_exp);
            sv = DecayPhasor::s(g_irf, s_irf, g_exp, s_exp);
        }
        out[2 * r] = gv;
        out[2 * r + 1] = sv;
    }
    *output = out;
    *dim1 = n_decays;
    *dim2 = 2;
}


double DecayPhasor::g(
            double g_irf, double s_irf,
            double g_exp, double s_exp
    ) {
        check_irf(g_irf, s_irf);
        return 1. / (g_irf * g_irf + s_irf * s_irf) * (g_irf * g_exp + s_irf * s_exp);
    }


double DecayPhasor::s(
        double g_irf, double s_irf,
        double g_exp, double s_exp
) {
    check_irf(g_irf, s_irf);
    return 1. / (g_irf * g_irf + s_irf * s_irf) * (g_irf * s_exp - s_irf * g_exp);
}

// ---- registry entries (Registry.h, core): declared next to the code, registered
// when this library loads; a static consumer links the archive whole.
namespace {
const char* const kPhasorEntry = R"JSON({
  "name": "phasor",
  "label": "Phasor analysis of decays and FLIM images",
  "summary": "Maps a decay or every pixel of a FLIM image to the phasor plane (g, s) at the laser frequency, with IRF calibration.",
  "description": "Digman et al.'s phasor: the first Fourier component of the micro-time histogram, normalised by the IRF's, so a single-exponential lies on the universal semicircle and mixtures fall inside it on straight lines. Model-free, one pass, and the standard way to look at a whole FLIM image at once; `CLSMImage.get_phasor` does it per pixel with a photon threshold and frame stacking. Assumes a periodic excitation of known frequency and an IRF measured under the same conditions.",
  "operation_type": "phasor_analysis",
  "params_schema": {
    "type": "object",
    "properties": {
      "frequency": {
        "type": "number",
        "title": "Frequency (MHz)"
      },
      "minimum_number_of_photons": {
        "type": "integer",
        "title": "Min photons",
        "default": 30
      },
      "stack_frames": {
        "type": "boolean",
        "title": "Stack frames",
        "default": false
      }
    }
  },
  "inputs": {
    "required": [
      "micro_time_histogram_or_clsm_image"
    ],
    "optional": [
      "irf"
    ]
  },
  "outputs": {
    "columns": [
      "g",
      "s"
    ]
  },
  "row_grain": "pixel",
  "references": [
    {
      "type": "journal",
      "authors": "Digman, M. A., Caiolfa, V. R., Zamai, M., Gratton, E.",
      "title": "The phasor approach to fluorescence lifetime imaging analysis",
      "journal": "Biophys J",
      "year": 2008,
      "volume": "94",
      "pages": "L14-L16"
    }
  ],
  "api": [
    "DecayPhasor",
    "CLSMImage.get_phasor",
    "CLSMImage.get_phasor_v",
    "StreamingPhasor"
  ],
  "can_replay": true
})JSON";
bool register_decayphasor_entries() {
    tttrlib::register_algorithm_json("clsm", "phasor", kPhasorEntry);
    return true;
}
const bool kDecayPhasorRegistered = register_decayphasor_entries();
}  // namespace

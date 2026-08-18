// SPDX-License-Identifier: BSD-3-Clause
#include "TwoCDE.h"

#include "Registry.h"
#include <cmath>

namespace tttrlib {

std::function<double(int, int64_t, int64_t)> TwoCDE::make_reducer(int variant, int kernel) const {
    const bool laplace = (kernel != GAUSSIAN);

    if (variant == ALEX_2CDE) {
        // Streams: Dex (donor excitation, DexDem+DexAem), Aex (acceptor exc., AexAem).
        // BR_Dex = <KDE_Aex / KDE_Dex> over Dex photons, normalised by N_Aex.
        // BR_Aex = <KDE_Dex / KDE_Aex> over Aex photons, normalised by N_Dex.
        // ALEX-2CDE = 100 - 50 (BR_Dex - BR_Aex).  Raw KDE (no nbKDE).
        const std::vector<uint8_t>& is_dex = membership(DONOR_EXC);
        const std::vector<uint8_t>& is_aex = membership(ACCEPTOR_EXC);
        const std::vector<double>& kde_dex = kde(DONOR_EXC);
        const std::vector<double>& kde_aex = kde(ACCEPTOR_EXC);
        return [&, laplace](int /*b*/, int64_t s, int64_t e) -> double {
            (void)laplace;
            int64_t n_dex = 0, n_aex = 0;
            for (int64_t i = s; i <= e; ++i) {
                n_dex += is_dex[static_cast<size_t>(i)];
                n_aex += is_aex[static_cast<size_t>(i)];
            }
            if (n_dex == 0 || n_aex == 0) return std::nan("");
            double sum_dex = 0.0, sum_aex = 0.0;
            for (int64_t i = s; i <= e; ++i) {
                const size_t u = static_cast<size_t>(i);
                if (is_dex[u]) {
                    const double d = kde_dex[u];
                    if (d != 0.0) sum_dex += kde_aex[u] / d;
                }
                if (is_aex[u]) {
                    const double a = kde_aex[u];
                    if (a != 0.0) sum_aex += kde_dex[u] / a;
                }
            }
            const double br_dex = sum_dex / static_cast<double>(n_aex);
            const double br_aex = sum_aex / static_cast<double>(n_dex);
            return 100.0 - 50.0 * (br_dex - br_aex);
        };
    }

    // FRET-2CDE. Streams: D (donor, DexDem), A (acceptor, DexAem).
    // (E)_D   = <KDE_A / (KDE_A + nbKDE_D)> over D photons
    // (1-E)_A = <KDE_D / (KDE_D + nbKDE_A)> over A photons
    // FRET-2CDE = 110 - 100 ((E)_D + (1-E)_A).
    // Laplace nbKDE removes the self term and applies (1 + 2/N); Gaussian raw.
    const std::vector<uint8_t>& is_d = membership(DONOR);
    const std::vector<uint8_t>& is_a = membership(ACCEPTOR);
    const std::vector<double>& kde_d = kde(DONOR);
    const std::vector<double>& kde_a = kde(ACCEPTOR);
    return [&, laplace](int /*b*/, int64_t s, int64_t e) -> double {
        int64_t n_chd = 0, n_cha = 0;
        for (int64_t i = s; i <= e; ++i) {
            n_chd += is_d[static_cast<size_t>(i)];
            n_cha += is_a[static_cast<size_t>(i)];
        }
        if (n_chd == 0 || n_cha == 0) return std::nan("");
        const double corr_d = laplace ? (1.0 + 2.0 / static_cast<double>(n_chd)) : 1.0;
        const double corr_a = laplace ? (1.0 + 2.0 / static_cast<double>(n_cha)) : 1.0;
        double sum_ed = 0.0, sum_ea = 0.0;
        for (int64_t i = s; i <= e; ++i) {
            const size_t u = static_cast<size_t>(i);
            if (is_d[u]) {
                const double kde_adi = kde_a[u];
                double kde_ddi = kde_d[u];
                if (laplace) kde_ddi = corr_d * (kde_ddi - 1.0);  // nbKDE
                const double denom = kde_adi + kde_ddi;
                if (denom != 0.0) sum_ed += kde_adi / denom;
            }
            if (is_a[u]) {
                const double kde_dai = kde_d[u];
                double kde_aai = kde_a[u];
                if (laplace) kde_aai = corr_a * (kde_aai - 1.0);  // nbKDE
                const double denom = kde_dai + kde_aai;
                if (denom != 0.0) sum_ea += kde_dai / denom;
            }
        }
        const double ed = sum_ed / static_cast<double>(n_chd);
        const double ea = sum_ea / static_cast<double>(n_cha);
        return 110.0 - 100.0 * (ed + ea);
    };
}

void TwoCDE::compute(
    long long* bursts, int n_bursts, int n_cols,
    double tau, int variant, int kernel
) {
    build_streams();
    build_kde(seconds_to_macro_ticks(tau), kernel);
    for_each_burst(bursts, n_bursts, n_cols, make_reducer(variant, kernel));
}

void TwoCDE::compute(double tau, int variant, int kernel) {
    build_streams();
    build_kde(seconds_to_macro_ticks(tau), kernel);
    for_each_burst_from_filter(make_reducer(variant, kernel));
}


// ---- registry("operation") entry ------------------------------------------
// FRET-2CDE / ALEX-2CDE as a pipeline step, next to the class that computes it.
namespace {
const char* const kKdeCdeEntry = R"JSON({
  "name": "kde_cde",
  "label": "KDE 2CDE / ALEX-2CDE",
  "summary": "Kernel-density estimate (Laplace or Gaussian) of donor/acceptor photon arrival times per burst (Tomov et al., 2012). A static burst scores ~100; dynamic bursts deviate.",
  "operation_type": "burst_2cde",
  "data_format": "dstore",
  "row_grain": "burst",
  "kind": "burst_table",
  "inputs": {
    "required": [
      "tttr_photon_stream",
      "burst_selection"
    ],
    "description": "Photon stream for per-photon arrival times and colours."
  },
  "outputs": {
    "columns": [
      "FRET-2CDE",
      "ALEX-2CDE"
    ]
  },
  "settings_schema": {
    "type": "object",
    "properties": {
      "kernel": {
        "type": "string",
        "default": "gaussian",
        "enum": [
          "gaussian",
          "laplace"
        ]
      },
      "bandwidth": {
        "type": "number",
        "default": 0.05,
        "minimum": 0.001
      }
    }
  },
  "can_replay": true
})JSON";
}  // namespace

/// Register this operation's registry entry. Idempotent (a duplicate key is refused).
void register_operation_kde_cde() {
    register_algorithm_json("operation", "kde_cde", kKdeCdeEntry);
}

} // namespace tttrlib

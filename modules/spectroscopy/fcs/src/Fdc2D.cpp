// SPDX-License-Identifier: BSD-3-Clause
#include "Fdc2D.h"
#include "Registry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace tttrlib {

namespace {

void require(bool ok, const char* what) {
    if (!ok) throw std::invalid_argument(std::string("2d-fdc: ") + what);
}

// The reference's axis, in integers. TK_Create2DFDC_04.m:44 builds the edges
// as REAL values (t_Imax^x - 1) and compares the integer micro-time tick
// against them directly, so the effective integer edge is floor(v), not the
// nearest integer. Quantizing with round instead moves pairs that sit between
// the two: measured against the original .m run in Octave (3000 photons,
// 3 lags, factors 1 and 8, 16 log bins), round disagreed in every matrix
// (352-457 of ~85k pairs, ~0.5%, at the 5-7 of 16 edges where the two
// conventions differ) and floor reproduced it exactly. Saturating rather than
// wrapping, because a caller can ask for a span that overflows int64 at the
// top edge and a wrapped edge would sort below the others and silently
// swallow every pair.
void build_log_ticks(long long t_imax, long long* out, int n_ticks) {
    require(n_ticks >= 2, "the log axis needs at least two edges");
    out[0] = -1;
    const double span = static_cast<double>(t_imax);
    for (int j = 1; j < n_ticks; ++j) {
        const double x = static_cast<double>(j) / static_cast<double>(n_ticks - 1);
        const double v = std::pow(span, x) - 1.0;
        if (v < -9.22e18)
            out[j] = std::numeric_limits<long long>::min();
        else if (v > 9.22e18)
            out[j] = std::numeric_limits<long long>::max();
        else
            out[j] = static_cast<long long>(std::floor(v));
    }
}

// TK_Create2DFDC_04.m:38-40. The span is padded by one linear bin's worth of
// ticks and then rounded up to a whole number of linear bins, and the LOG edges
// are built from the result -- which is why the log axis moves with a factor
// that names the linear matrix.
long long matlab_t_imax(long long span_ticks, long long f) {
    require(f >= 1, "lint_bin_factor must be at least 1");
    const long long padded = span_ticks + f;
    const long long lint_imax = (padded + f - 1) / f;      // ceil(padded / f)
    return lint_imax * f;
}

// `searchsorted(..., side="left") - 1`, with the out-of-range answers the
// callers rely on: below the first edge and at or above the last are both -1.
inline int log_bin(long long tau, const long long* ticks, int n_ticks) {
    const long long* lo = std::lower_bound(ticks, ticks + n_ticks, tau);
    const int idx = static_cast<int>(lo - ticks);
    if (idx <= 0 || idx >= n_ticks) return -1;
    return idx - 1;
}

void check_stream(const long long* macro, int n_macro, const long long* micro,
                  int n_micro, long long t_min, long long t_max) {
    require(n_macro > 0, "the photon stream is empty");
    require(n_micro == n_macro, "macro_times and micro_times must be the same length");
    require(t_max >= t_min, "t_max must be at least t_min");
    // Sorted, or the binary search below returns the wrong photons and nothing
    // says so. This is O(n) against an O(n log n) inner loop.
    for (int i = 1; i < n_macro; ++i)
        require(macro[i] >= macro[i - 1],
                "macro_times must be sorted ascending -- the lag window is found "
                "by binary search, which on unsorted input answers confidently "
                "and wrongly");
}

// The pass itself. `out` is zeroed and then filled; `n_chunks` partitions the
// reference photons and each chunk keeps a private matrix, so the sum is exact
// and independent of the partition.
// One micro-time axis and the matrix stack it fills.
struct Axis {
    const long long* ticks;
    int n_ticks;
    long long* out;
};

void scan(const long long* macro, int n, const long long* micro,
          const long long* lags, int n_lags, long long ddT_ticks,
          long long t_min, long long t_imax,
          const std::vector<Axis>& axes, int n_chunks) {

    const int nc = n_chunks >= 1 ? n_chunks : 1;
    const long long half = ddT_ticks / 2;
    const long long last = macro[n - 1];
    const size_t n_axes = axes.size();

    // One private accumulator per (chunk, axis). The axes share the photon
    // walk and the window search -- which is the entire point of taking more
    // than one: measured, a second pass over 1M photons costs 2.27x a single
    // one (145 -> 329 ms), and it is a full pass, not noise.
    std::vector<size_t> per_lag(n_axes), per_chunk(n_axes), base(n_axes);
    size_t stride_all = 0;
    for (size_t a = 0; a < n_axes; ++a) {
        per_lag[a] = static_cast<size_t>(axes[a].n_ticks) * axes[a].n_ticks;
        per_chunk[a] = static_cast<size_t>(n_lags) * per_lag[a];
        base[a] = stride_all;
        stride_all += per_chunk[a];
    }
    std::vector<long long> acc(stride_all * static_cast<size_t>(nc), 0);

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int c = 0; c < nc; ++c) {
        // Per chunk, therefore per thread: one shared scratch vector here would
        // be a data race that shows up as wrong counts under -fopenmp only.
        std::vector<int> bin_i(n_axes);
        long long* mine = acc.data() + static_cast<size_t>(c) * stride_all;
        const int i0 = static_cast<int>((static_cast<long long>(n) * c) / nc);
        const int i1 = static_cast<int>((static_cast<long long>(n) * (c + 1)) / nc);
        for (int i = i0; i < i1; ++i) {
            const long long tau_i = micro[i] - t_min;
            // Dropped, not clamped: an out-of-range micro-time in the edge bin
            // is indistinguishable from a real feature there.
            if (tau_i <= 0 || tau_i >= t_imax) continue;
            bool any = false;
            for (size_t a = 0; a < n_axes; ++a) {
                const int b = log_bin(tau_i, axes[a].ticks, axes[a].n_ticks);
                bin_i[a] = (b > 0 && b < axes[a].n_ticks) ? b : -1;
                any = any || bin_i[a] >= 0;
            }
            // An axis that cannot place this photon skips it; the others still
            // count it, so one coarse axis does not veto a fine one.
            if (!any) continue;

            const long long ti = macro[i];
            for (int li = 0; li < n_lags; ++li) {
                const long long start = ti + lags[li] - half;
                const long long stop = ti + lags[li] + half;
                // A window running past the last photon is not a window with
                // few pairs, it is a window the measurement does not cover.
                if (stop > last) continue;
                const long long* ks = std::lower_bound(macro, macro + n, start);
                const long long* ke = std::upper_bound(macro, macro + n, stop);
                for (const long long* k = ks; k < ke; ++k) {
                    const int kk = static_cast<int>(k - macro);
                    const long long tau_k = micro[kk] - t_min;
                    if (tau_k <= 0 || tau_k >= t_imax) continue;
                    for (size_t a = 0; a < n_axes; ++a) {
                        if (bin_i[a] < 0) continue;
                        const int nt = axes[a].n_ticks;
                        const int bk = log_bin(tau_k, axes[a].ticks, nt);
                        if (bk > 0 && bk < nt) {
                            mine[base[a] + static_cast<size_t>(li) * per_lag[a]
                                 + static_cast<size_t>(bin_i[a]) * nt + bk] += 1;
                        }
                    }
                }
            }
        }
    }

    // Reduce, then drop the last row and column: an axis carries one more edge
    // than it has bins.
    for (size_t ax = 0; ax < n_axes; ++ax) {
        const int nt = axes[ax].n_ticks;
        const size_t bins = static_cast<size_t>(nt - 1);
        for (int li = 0; li < n_lags; ++li) {
            for (size_t a = 0; a < bins; ++a) {
                for (size_t b = 0; b < bins; ++b) {
                    long long total = 0;
                    for (int c = 0; c < nc; ++c) {
                        total += acc[static_cast<size_t>(c) * stride_all + base[ax]
                                     + static_cast<size_t>(li) * per_lag[ax]
                                     + a * nt + b];
                    }
                    axes[ax].out[static_cast<size_t>(li) * bins * bins
                                 + a * bins + b] = total;
                }
            }
        }
    }
}

}  // namespace

long long fdc_t_imax(long long span_ticks, long long lint_bin_factor) {
    return matlab_t_imax(span_ticks, lint_bin_factor);
}

void fdc_log_ticks(long long t_imax, long long* out, int n_out) {
    require(out != nullptr, "out is null");
    build_log_ticks(t_imax, out, n_out);
}

int fdc_log_bin(long long tau_ticks, long long* logt_ticks, int n_ticks) {
    require(logt_ticks != nullptr && n_ticks >= 2, "the log axis is missing");
    return log_bin(tau_ticks, logt_ticks, n_ticks);
}

void fdc_scan_log(
        long long* macro_times, int n_macro,
        long long* micro_times, int n_micro,
        long long* lags, int n_lags,
        long long ddT_ticks,
        long long t_min, long long t_max,
        int logt_imax, int n_chunks,
        long long* out, int n_out,
        long long lint_bin_factor) {
    check_stream(macro_times, n_macro, micro_times, n_micro, t_min, t_max);
    require(n_lags > 0 && lags != nullptr, "at least one lag is needed");
    require(logt_imax >= 1, "logt_imax must be positive");
    require(ddT_ticks >= 0, "the lag window cannot be negative");
    require(n_out == n_lags * logt_imax * logt_imax,
            "out must be (n_lags, logt_imax, logt_imax)");
    // Gate and edges from the SAME t_Imax, which is the reference's rule
    // (TK_Create2DFDC_04.m:66 tests `tauI >= t_Imax`, not `tauI > tMax`). Since
    // t_Imax rounds the span up to whole linear bins, the method admits photons
    // above t_max whenever the span is not a whole number of bins.
    const long long t_imax = matlab_t_imax(t_max - t_min, lint_bin_factor);
    std::vector<long long> ticks(static_cast<size_t>(logt_imax) + 1);
    build_log_ticks(t_imax, ticks.data(), logt_imax + 1);
    scan(macro_times, n_macro, micro_times, lags, n_lags, ddT_ticks, t_min, t_imax,
         {Axis{ticks.data(), logt_imax + 1, out}}, n_chunks);
}

void fdc_scan_axis(
        long long* macro_times, int n_macro,
        long long* micro_times, int n_micro,
        long long* lags, int n_lags,
        long long ddT_ticks,
        long long t_min, long long t_max,
        long long* ticks, int n_ticks,
        int n_chunks,
        long long* out, int n_out,
        long long t_imax) {
    check_stream(macro_times, n_macro, micro_times, n_micro, t_min, t_max);
    require(n_lags > 0 && lags != nullptr, "at least one lag is needed");
    require(ticks != nullptr && n_ticks >= 2, "the axis needs at least two edges");
    require(ddT_ticks >= 0, "the lag window cannot be negative");
    for (int j = 1; j < n_ticks; ++j)
        require(ticks[j] >= ticks[j - 1],
                "the axis edges must be ascending -- the bin is found by binary "
                "search over them");
    const int bins = n_ticks - 1;
    require(n_out == n_lags * bins * bins,
            "out must be (n_lags, n_ticks - 1, n_ticks - 1)");
    require(t_imax >= 0, "t_imax cannot be negative");
    scan(macro_times, n_macro, micro_times, lags, n_lags, ddT_ticks, t_min,
         t_imax > 0 ? t_imax : (t_max - t_min + 1),
         {Axis{ticks, n_ticks, out}}, n_chunks);
}

void fdc_scan_two_axes(
        long long* macro_times, int n_macro,
        long long* micro_times, int n_micro,
        long long* lags, int n_lags,
        long long ddT_ticks,
        long long t_min, long long t_max,
        long long* ticks_a, int n_ticks_a,
        long long* ticks_b, int n_ticks_b,
        int n_chunks,
        long long* out_a, int n_out_a,
        long long* out_b, int n_out_b,
        long long t_imax) {
    check_stream(macro_times, n_macro, micro_times, n_micro, t_min, t_max);
    require(n_lags > 0 && lags != nullptr, "at least one lag is needed");
    require(ddT_ticks >= 0, "the lag window cannot be negative");
    require(ticks_a != nullptr && n_ticks_a >= 2 &&
            ticks_b != nullptr && n_ticks_b >= 2,
            "both axes need at least two edges");
    for (int j = 1; j < n_ticks_a; ++j)
        require(ticks_a[j] >= ticks_a[j - 1], "axis a must be ascending");
    for (int j = 1; j < n_ticks_b; ++j)
        require(ticks_b[j] >= ticks_b[j - 1], "axis b must be ascending");
    const int bins_a = n_ticks_a - 1, bins_b = n_ticks_b - 1;
    require(n_out_a == n_lags * bins_a * bins_a,
            "out_a must be (n_lags, n_ticks_a - 1, n_ticks_a - 1)");
    require(n_out_b == n_lags * bins_b * bins_b,
            "out_b must be (n_lags, n_ticks_b - 1, n_ticks_b - 1)");
    require(t_imax >= 0, "t_imax cannot be negative");
    scan(macro_times, n_macro, micro_times, lags, n_lags, ddT_ticks, t_min,
         t_imax > 0 ? t_imax : (t_max - t_min + 1),
         {Axis{ticks_a, n_ticks_a, out_a}, Axis{ticks_b, n_ticks_b, out_b}},
         n_chunks);
}

void fdc_log(
        long long* macro_times, int n_macro,
        long long* micro_times, int n_micro,
        long long dT_ticks, long long ddT_ticks,
        long long t_min, long long t_max,
        int logt_imax, int n_chunks,
        long long* out, int n_out,
        long long lint_bin_factor) {
    check_stream(macro_times, n_macro, micro_times, n_micro, t_min, t_max);
    require(logt_imax >= 1, "logt_imax must be positive");
    require(ddT_ticks >= 0, "the lag window cannot be negative");
    require(n_out == logt_imax * logt_imax, "out must be (logt_imax, logt_imax)");
    long long lag = dT_ticks;
    const long long t_imax = matlab_t_imax(t_max - t_min, lint_bin_factor);
    std::vector<long long> ticks(static_cast<size_t>(logt_imax) + 1);
    build_log_ticks(t_imax, ticks.data(), logt_imax + 1);
    scan(macro_times, n_macro, micro_times, &lag, 1, ddT_ticks, t_min, t_imax,
         {Axis{ticks.data(), logt_imax + 1, out}}, n_chunks);
}

}  // namespace tttrlib

// ---- registry entry (Registry.h, core): declared next to the code, registered
// when this library loads.
namespace {
const char* const kFdc2dEntry = R"JSON({
  "name": "fdc_2d",
  "label": "2D fluorescence-decay correlation (2D-FDC)",
  "summary": "Photon-pair histogram over micro-times at a set of macro-time lags: the matrix whose off-diagonal weight measures a molecule changing its decay between two photons.",
  "description": "For every reference photon, the photons whose macro-time falls in a lag window dT +/- ddT/2 are counted, each pair binned by its two micro-times on a logarithmic (or caller-supplied) axis, so M[a][b] at lag dT is the number of pairs whose earlier photon fell in bin a and later in b. The lag dependence of the cross-peaks measures the interconversion rate between decay states (Felekyan/Kalinin/Seidel's 2D-FDC). Reproduces the original TK_Create2DFDC_04.m matrices exactly. The matrix is the output; the inversion into lifetimes and rates is left to the caller.",
  "operation_type": "analysis",
  "method": "fdc_log",
  "params_schema": {
    "type": "object",
    "properties": {
      "dT_ticks": {
        "type": "array",
        "items": {
          "type": "number"
        },
        "title": "Lags (ticks)"
      },
      "ddT_ticks": {
        "type": "number",
        "title": "Lag window (ticks)"
      },
      "logt_imax": {
        "type": "integer",
        "title": "Log axis size"
      },
      "lint_bin_factor": {
        "type": "integer",
        "default": 1
      }
    }
  },
  "inputs": {
    "required": [
      "tttr_photon_stream"
    ]
  },
  "outputs": {
    "columns": [
      "M(a, b) per lag"
    ]
  },
  "row_grain": "curve_point",
  "references": [
    {
      "type": "journal",
      "authors": "Kalinin, S., Felekyan, S., Valeri, A., Seidel, C. A. M.",
      "title": "Characterizing multiple molecular states in single-molecule multiparameter fluorescence detection by probability distribution analysis",
      "journal": "J Phys Chem B",
      "year": 2008,
      "volume": "112",
      "pages": "8361-8374"
    },
    {
      "type": "journal",
      "authors": "Felekyan, S., Kalinin, S., Sanabria, H., Valeri, A., Seidel, C. A. M.",
      "title": "Filtered FCS: species auto- and cross-correlation functions highlight binding and dynamics in biomolecules",
      "journal": "ChemPhysChem",
      "year": 2012,
      "volume": "13",
      "pages": "1036-1053"
    }
  ],
  "api": [
    "fdc_log",
    "fdc_log_bin",
    "fdc_log_ticks",
    "fdc_scan_axis",
    "fdc_scan_log",
    "fdc_scan_two_axes",
    "fdc_t_imax"
  ],
  "can_replay": true
})JSON";
const bool kFdc2dRegistered = (tttrlib::register_algorithm_json("fcs", "fdc_2d", kFdc2dEntry), true);
}  // namespace

// SPDX-License-Identifier: BSD-3-Clause
#include "PdaBurstLikelihood.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace {

const double NEG_INF = -std::numeric_limits<double>::infinity();

/// Hard ceiling on a Poisson cutoff, so a pathological rate cannot spin.
const int MAX_CUTOFF = 10000;

inline double poisson_logpmf(double b, double rate) {
    if (!(rate > 0.0)) return b == 0.0 ? 0.0 : NEG_INF;
    return b * std::log(rate) - rate - std::lgamma(b + 1.0);
}

inline double logaddexp(double a, double b) {
    if (a == NEG_INF) return b;
    if (b == NEG_INF) return a;
    const double hi = a > b ? a : b, lo = a > b ? b : a;
    return hi + std::log1p(std::exp(lo - hi));
}

/// Smallest k with Poisson survival P(X > k) <= tolerance.
int poisson_tail_cutoff(double rate, double tolerance) {
    if (!(rate > 0.0)) return 0;
    // The effective rate is B_c * max(F_c/(N p_c)) and a near-zero p_c sends it
    // past 1e11. Casting that to int is undefined -- it wrapped to a negative
    // kmax, the walk below never ran, and the cutoff came back 0: the box was
    // truncated to nothing exactly where the background explains the burst.
    if (!std::isfinite(rate) || rate >= (double) MAX_CUTOFF) return MAX_CUTOFF;
    const double log_tol = std::log(std::max(tolerance, 1e-300));
    int kmax = (int) std::ceil(rate) + 1;
    while (kmax < MAX_CUTOFF && poisson_logpmf(kmax, rate) > log_tol - 40.0) kmax++;
    // Walk down accumulating the survival; sf(k-1) = sf(k) + pmf(k).
    double sf = 0.0;
    for (int k = kmax; k >= 0; k--) {
        if (sf > tolerance) return k + 1;
        sf += std::exp(poisson_logpmf((double) k, rate));
    }
    return 0;
}

/// log u(b) for b = 0..count, normalised so log u(0) = 0. Untruncated.
std::vector<double> log_background_series(int count, double rate, double p) {
    if (!(rate > 0.0) || count <= 0) return std::vector<double>(1, 0.0);
    std::vector<double> out((size_t) count + 1, NEG_INF);
    if (!(p > 0.0)) {
        // An impossible channel holds no signal, so every photon in it is
        // background and the series degenerates to that one term.
        out[count] = poisson_logpmf((double) count, rate);
        return out;
    }
    const double log_p = std::log(p);
    const double lg_count = std::lgamma((double) count + 1.0);
    for (int b = 0; b <= count; b++) {
        out[b] = poisson_logpmf((double) b, rate) + lg_count
                 - std::lgamma((double) (count - b) + 1.0) - b * log_p;
    }
    const double u0 = out[0];
    for (int b = 0; b <= count; b++) {
        if (out[b] != NEG_INF) out[b] -= u0;
    }
    return out;
}

/// Discrete convolution of two non-negative sequences, in log space.
std::vector<double> log_convolve(
        const std::vector<double>& a, const std::vector<double>& b
) {
    std::vector<double> out(a.size() + b.size() - 1, NEG_INF);
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i] == NEG_INF) continue;
        for (size_t j = 0; j < b.size(); j++) {
            if (b[j] == NEG_INF) continue;
            out[i + j] = logaddexp(out[i + j], a[i] + b[j]);
        }
    }
    return out;
}

/// log L for one burst, without factoring out Multinom(F;p).
///
/// Substituting Multinom(F;p) into L = Multinom(F;p) * sum_m w_m c_m cancels
/// every p_c^{-b} against a p_c^{F_c}, leaving
///     L = sum_m P(N-m) (N-m)! * (v_1 * ... * v_K)(m),
///     v_c(b) = Pois(b;B_c) p_c^{F_c-b} / (F_c-b)! .
/// Nothing divides by p, so this stays exact where the ratio form does not:
/// a channel with p_c == 0 that collected photons makes the leading term zero,
/// and the correction would have to be +inf to compensate.
double log_likelihood_exact(
        const double* counts, int K,
        const std::vector<double>& background,
        const double* p,
        const std::vector<double>& pn
) {
    std::vector<double> log_c(1, 0.0);
    double total = 0.0;
    for (int c = 0; c < K; c++) {
        const int f = (int) counts[c];
        total += counts[c];
        const double rate = c < (int) background.size() ? background[c] : 0.0;
        std::vector<double> v((size_t) f + 1, NEG_INF);
        for (int b = 0; b <= f; b++) {
            double t = poisson_logpmf((double) b, rate)
                       - std::lgamma((double) (f - b) + 1.0);
            if (p[c] > 0.0) t += (double) (f - b) * std::log(p[c]);
            else if (f - b > 0) t = NEG_INF;   // p_c^positive == 0
            v[b] = t;
        }
        log_c = log_convolve(log_c, v);
    }
    double peak = NEG_INF;
    std::vector<double> terms;
    terms.reserve(log_c.size());
    for (size_t m = 0; m < log_c.size(); m++) {
        if ((double) m > total || log_c[m] == NEG_INF) continue;
        const double n_signal = total - (double) m;
        double t = log_c[m] + std::lgamma(n_signal + 1.0);
        if (!pn.empty()) {
            const size_t ns = (size_t) n_signal;
            const double w = ns < pn.size() ? pn[ns] : 0.0;
            if (!(w > 0.0)) continue;
            t += std::log(w);
        }
        terms.push_back(t);
        if (t > peak) peak = t;
    }
    if (terms.empty()) return NEG_INF;
    double s = 0.0;
    for (double t : terms) s += std::exp(t - peak);
    return peak + std::log(s);
}

} // namespace


double PdaBurstLikelihood::log_multinomial_pmf(
        std::vector<double> counts, std::vector<double> p
) {
    // A negative entry is not a probability vector at all: floored to 1 below
    // it would score its photons for free and can push the result above zero,
    // letting an unphysical point outscore every valid one.
    for (size_t c = 0; c < p.size(); c++) if (p[c] < 0.0) return NEG_INF;
    double n = 0.0, out = 0.0;
    for (size_t c = 0; c < counts.size(); c++) {
        n += counts[c];
        out -= std::lgamma(counts[c] + 1.0);
    }
    out += std::lgamma(n + 1.0);
    for (size_t c = 0; c < counts.size() && c < p.size(); c++) {
        if (p[c] > 0.0) out += counts[c] * std::log(p[c]);
        else if (counts[c] > 0.0) return NEG_INF;   // photons in a dead channel
    }
    return out;
}


double PdaBurstLikelihood::log_background_correction(
        std::vector<double> counts,
        std::vector<double> background,
        std::vector<double> p,
        std::vector<double> photon_number_pmf
) {
    // c = convolution of the per-channel series, each normalised to start at 1.
    // Convolved in log space: the series is unbounded above and its dominant
    // terms overflow a double.
    std::vector<double> log_c(1, 0.0);
    double log_offset = 0.0;
    for (size_t k = 0; k < counts.size(); k++) {
        const double rate = k < background.size() ? background[k] : 0.0;
        const double pk = k < p.size() ? p[k] : 0.0;
        log_offset += -rate;   // Pois(0;rate) was divided out; put it back once
        log_c = log_convolve(log_c, log_background_series((int) counts[k], rate, pk));
    }
    double total = 0.0;
    for (size_t k = 0; k < counts.size(); k++) total += counts[k];

    const double lg_total = std::lgamma(total + 1.0);
    double peak = NEG_INF;
    std::vector<double> terms;
    terms.reserve(log_c.size());
    for (size_t m = 0; m < log_c.size(); m++) {
        if ((double) m > total) break;
        double w = std::lgamma(total - (double) m + 1.0) - lg_total;
        if (!photon_number_pmf.empty()) {
            const size_t n_signal = (size_t) (total - (double) m);
            const double weight = n_signal < photon_number_pmf.size()
                                  ? photon_number_pmf[n_signal] : 0.0;
            w += weight > 0.0 ? std::log(weight) : NEG_INF;
        }
        const double t = w + log_c[m];
        // Only exactly-zero terms are dropped. A +inf must NOT be masked away
        // -- that would silently return a finite sum over the sub-dominant tail.
        if (t == NEG_INF) continue;
        terms.push_back(t);
        if (t > peak) peak = t;
    }
    if (terms.empty()) return NEG_INF;
    if (peak == std::numeric_limits<double>::infinity()) return peak;
    double s = 0.0;
    for (double t : terms) s += std::exp(t - peak);
    return log_offset + peak + std::log(s);
}


PdaBurstLikelihood::PdaBurstLikelihood(
        int* counts, int n_bursts, int n_channels,
        std::vector<double> background,
        std::vector<double> photon_number_pmf,
        double tolerance
) {
    if (n_bursts < 0 || n_channels <= 0)
        throw std::invalid_argument(
            "PdaBurstLikelihood: need at least one channel and a "
            "non-negative burst count.");
    _n_bursts = n_bursts;
    _n_channels = n_channels;
    _tolerance = tolerance > 0.0 ? tolerance : 1e-12;
    _counts.resize((size_t) n_bursts * n_channels);
    for (size_t i = 0; i < _counts.size(); i++) {
        if (counts[i] < 0)
            throw std::invalid_argument(
                "PdaBurstLikelihood: photon counts must be non-negative.");
        _counts[i] = (double) counts[i];
    }
    _background = std::move(background);
    _background.resize((size_t) n_channels, 0.0);
    _photon_number_pmf = std::move(photon_number_pmf);
    _has_background = false;
    for (double b : _background) if (b > 0.0) _has_background = true;
    build_burst_tables();
}


void PdaBurstLikelihood::build_burst_tables() {
    const int K = _n_channels;
    _total.assign((size_t) _n_bursts, 0.0);
    _log_mult_const.assign((size_t) _n_bursts, 0.0);
    _max_ratio.assign((size_t) K, 0.0);
    _max_count.assign((size_t) K, 0);

    for (int j = 0; j < _n_bursts; j++) {
        const double* f = &_counts[(size_t) j * K];
        double n = 0.0, lg = 0.0;
        for (int c = 0; c < K; c++) { n += f[c]; lg += std::lgamma(f[c] + 1.0); }
        _total[j] = n;
        _log_mult_const[j] = std::lgamma(n + 1.0) - lg;
        const double denom = n > 0.0 ? n : 1.0;
        for (int c = 0; c < K; c++) {
            _max_ratio[c] = std::max(_max_ratio[c], f[c] / denom);
            _max_count[c] = std::max(_max_count[c], (int) f[c]);
        }
    }
    if (!_has_background && _photon_number_pmf.empty()) return;

    // log_a[j][c][b] = log[Pois(b;B_c) * F_jc!/(F_jc-b)!], padded to the widest
    // channel. Model-independent, so a fit pays for its lgamma calls once.
    _series_width = 0;
    for (int c = 0; c < K; c++) _series_width = std::max(_series_width, _max_count[c]);
    _series_width += 1;
    _log_a.assign((size_t) _n_bursts * K * _series_width, NEG_INF);
    for (int j = 0; j < _n_bursts; j++) {
        for (int c = 0; c < K; c++) {
            const double f = _counts[(size_t) j * K + c];
            const double lg_f = std::lgamma(f + 1.0);
            double* row = &_log_a[((size_t) j * K + c) * _series_width];
            for (int b = 0; b <= (int) f && b < _series_width; b++) {
                row[b] = poisson_logpmf((double) b, _background[c])
                         + lg_f - std::lgamma(f - (double) b + 1.0);
            }
        }
    }
}


void PdaBurstLikelihood::ensure_box(const double* p, int n_points) {
    const int K = _n_channels;
    std::vector<int> want((size_t) K, 1);
    for (int c = 0; c < K; c++) {
        if (!(_background[c] > 0.0) || _max_count[c] == 0) continue;
        double p_min = 1.0;
        bool any = false;
        for (int i = 0; i < n_points; i++) {
            const double v = p[(size_t) i * K + c];
            if (v > 0.0 && (!any || v < p_min)) { p_min = v; any = true; }
        }
        if (!any) p_min = 1.0;
        const double ratio = _max_ratio[c] / std::max(p_min, 1e-300);
        const double effective = _background[c] * std::max(ratio, 1.0);
        want[c] = std::min(_max_count[c],
                           poisson_tail_cutoff(effective, _tolerance)) + 1;
    }
    // Only ever grow: a bigger box is strictly more accurate, and keeping it
    // stable is what lets the channel tables survive a whole fit.
    if (_boxes.size() != (size_t) K) _boxes.assign((size_t) K, 1);
    for (int c = 0; c < K; c++) _boxes[c] = std::max(_boxes[c], want[c]);
}


void PdaBurstLikelihood::build_channel_tables() {
    // Per-channel exponentials of the burst factor, peak-shifted onto (0,1].
    // These are the only transcendentals in the whole background path, and
    // there are sum_c box_c of them per burst rather than prod_c box_c -- the
    // difference between 90 and 3375 at a 15-wide box in three channels.
    const int K = _n_channels;
    if (_ea_boxes == _boxes && !_ea.empty()) return;    // still current
    _box_offset.assign((size_t) K + 1, 0);
    for (int c = 0; c < K; c++) _box_offset[c + 1] = _box_offset[c] + _boxes[c];
    _box_m_max = 0;
    for (int c = 0; c < K; c++) _box_m_max += _boxes[c] - 1;

    const int width = _box_offset[K];
    _ea.assign((size_t) _n_bursts * width, 0.0);
    _log_a_shift.assign((size_t) _n_bursts, 0.0);
    _ew.assign((size_t) _n_bursts * (_box_m_max + 1), 0.0);
    _log_w_shift.assign((size_t) _n_bursts, 0.0);

    for (int j = 0; j < _n_bursts; j++) {
        const double total = _total[j];
        const double lg_total = std::lgamma(total + 1.0);
        double shift = 0.0;
        for (int c = 0; c < K; c++) {
            const double* src = &_log_a[((size_t) j * K + c) * _series_width];
            double peak = NEG_INF;
            for (int b = 0; b < _boxes[c]; b++)
                if (src[b] > peak) peak = src[b];
            // A channel that cannot supply even one background photon leaves
            // its whole row at -inf; shifting by 0 keeps the row at zero and
            // the burst's correction underflows to the exact path.
            if (!std::isfinite(peak)) peak = 0.0;
            shift += peak;
            double* dst = &_ea[(size_t) j * width + _box_offset[c]];
            for (int b = 0; b < _boxes[c]; b++)
                dst[b] = src[b] == NEG_INF ? 0.0 : std::exp(src[b] - peak);
        }
        _log_a_shift[j] = shift;

        // w_m = P(N-m) (N-m)! / N!, likewise peak-shifted.
        std::vector<double> lw((size_t) _box_m_max + 1, NEG_INF);
        double wpeak = NEG_INF;
        for (int m = 0; m <= _box_m_max; m++) {
            if ((double) m > total) break;
            double v = std::lgamma(total - (double) m + 1.0) - lg_total;
            if (!_photon_number_pmf.empty()) {
                const size_t ns = (size_t) (total - (double) m);
                const double pn = ns < _photon_number_pmf.size()
                                  ? _photon_number_pmf[ns] : 0.0;
                if (!(pn > 0.0)) continue;
                v += std::log(pn);
            }
            lw[m] = v;
            if (v > wpeak) wpeak = v;
        }
        if (!std::isfinite(wpeak)) wpeak = 0.0;
        _log_w_shift[j] = wpeak;
        double* dw = &_ew[(size_t) j * (_box_m_max + 1)];
        for (int m = 0; m <= _box_m_max; m++)
            dw[m] = lw[m] == NEG_INF ? 0.0 : std::exp(lw[m] - wpeak);
    }
    _ea_boxes = _boxes;
}


void PdaBurstLikelihood::log_likelihood_grid(
        double* input, int n_input1, int n_input2,
        double** output, int* n_output1, int* n_output2
) {
    if (n_input2 != _n_channels)
        throw std::invalid_argument(
            "PdaBurstLikelihood: probability vectors must have one entry per "
            "channel.");
    const int n_points = n_input1, K = _n_channels;
    *n_output1 = n_points;
    *n_output2 = _n_bursts;
    auto* out = (double*) malloc(
            std::max<size_t>(1, (size_t) n_points * _n_bursts) * sizeof(double));

    // zero-background term: lgamma part is cached, the rest is one dot product
    std::vector<double> log_p((size_t) n_points * K);
    std::vector<char> point_ok((size_t) n_points, 1);
    for (int i = 0; i < n_points; i++) {
        for (int c = 0; c < K; c++) {
            const double v = input[(size_t) i * K + c];
            if (v < 0.0) point_ok[i] = 0;
            log_p[(size_t) i * K + c] = v > 0.0 ? std::log(v) : NEG_INF;
        }
    }
    for (int i = 0; i < n_points; i++) {
        double* row = out + (size_t) i * _n_bursts;
        if (!point_ok[i]) {
            for (int j = 0; j < _n_bursts; j++) row[j] = NEG_INF;
            continue;
        }
        for (int j = 0; j < _n_bursts; j++) {
            const double* f = &_counts[(size_t) j * K];
            double v = _log_mult_const[j];
            for (int c = 0; c < K; c++) {
                if (f[c] == 0.0) continue;          // 0 * log(0) is 0 here
                const double lp = log_p[(size_t) i * K + c];
                if (lp == NEG_INF) { v = NEG_INF; break; }
                v += f[c] * lp;
            }
            row[j] = v;
        }
    }
    if (!_has_background && _photon_number_pmf.empty()) { *output = out; return; }

    ensure_box(input, n_points);
    build_channel_tables();

    std::vector<double> bg(_background);
    std::vector<char> point_exact((size_t) n_points, 0);
    for (int i = 0; i < n_points; i++) {
        for (int c = 0; c < K; c++)
            // p^-b is undefined there; hand the whole point to the exact path.
            if (!(input[(size_t) i * K + c] > 0.0)) point_exact[i] = 1;
        if (!point_ok[i]) point_exact[i] = 1;
    }

    // Exact path for the points the factorisation cannot represent. It computes
    // log L outright rather than correcting the leading term: where a channel
    // has p_c == 0 but collected photons that term is zero, and no finite
    // correction recovers the (perfectly possible) all-background answer.
    for (int i = 0; i < n_points; i++) {
        if (!point_exact[i] || !point_ok[i]) continue;
        double* row = out + (size_t) i * _n_bursts;
        for (int j = 0; j < _n_bursts; j++)
            row[j] = log_likelihood_exact(&_counts[(size_t) j * K], K, bg,
                                          &input[(size_t) i * K],
                                          _photon_number_pmf);
    }

    // Rather than summing over the (b_1..b_K) box, convolve the per-channel
    // series and sum over the total background count m. Identical algebra --
    // c_m IS the m-th coefficient of the product of the per-channel
    // polynomials -- but O(K * box * m_max) instead of O(prod box_c), with no
    // box materialised and so no chunking. For K = 2 the two are the same work;
    // past that the box grows as the K-th power of the cutoff and this does not.
    const int m_max = _box_m_max;
    std::vector<double> g((size_t) _box_offset[K]);      // per-channel series
    std::vector<double> h((size_t) m_max + 1), acc((size_t) m_max + 1);
    std::vector<double> pw((size_t) _box_offset[K]);     // p_c^-b, shifted

    for (int i = 0; i < n_points; i++) {
        if (point_exact[i]) continue;
        double* row = out + (size_t) i * _n_bursts;
        // p_c^-b with the per-channel peak folded in, so every entry is <= 1
        // and nothing overflows. Built by repeated multiplication: b == box-1
        // is the peak, and each step down multiplies by p_c (which is < 1
        // wherever the peak is not at b == 0).
        double model_shift = 0.0;
        for (int c = 0; c < K; c++) {
            const double p_c = input[(size_t) i * K + c];
            const int nb = _boxes[c];
            double* t = &pw[_box_offset[c]];
            const double lp = log_p[(size_t) i * K + c];
            const bool falling = lp < 0.0;              // p < 1, so p^-b grows
            const int peak_b = falling ? nb - 1 : 0;
            model_shift += -(double) peak_b * lp;
            t[peak_b] = 1.0;
            for (int b = peak_b - 1; b >= 0; b--) t[b] = t[b + 1] * p_c;
            for (int b = peak_b + 1; b < nb; b++) t[b] = t[b - 1] / p_c;
        }
        for (int j = 0; j < _n_bursts; j++) {
            const double total = _total[j];
            // g_c(b) = A_jc(b) p_c^-b, both halves already shifted onto (0,1]
            double shift = model_shift + _log_a_shift[j];
            for (int c = 0; c < K; c++) {
                const double* a = &_ea[(size_t) j * _box_offset[K] + _box_offset[c]];
                const double* t = &pw[_box_offset[c]];
                double* dst = &g[_box_offset[c]];
                for (int b = 0; b < _boxes[c]; b++) dst[b] = a[b] * t[b];
            }
            // h = g_1 * g_2 * ... * g_K, truncated at the burst's photon total
            int len = 1;
            h[0] = g[0];
            for (int b = 1; b < _boxes[0]; b++) h[b] = g[b];
            len = _boxes[0];
            for (int c = 1; c < K; c++) {
                const int nb = _boxes[c];
                const int out_len = std::min(len + nb - 1, m_max + 1);
                std::fill(acc.begin(), acc.begin() + out_len, 0.0);
                for (int b = 0; b < nb; b++) {
                    const double gv = g[_box_offset[c] + b];
                    if (gv == 0.0) continue;
                    const int upto = std::min(len, out_len - b);
                    for (int a2 = 0; a2 < upto; a2++) acc[a2 + b] += h[a2] * gv;
                }
                std::copy(acc.begin(), acc.begin() + out_len, h.begin());
                len = out_len;
            }
            // sum_m w_jm h(m), with w already shifted
            const double* w = &_ew[(size_t) j * (_box_m_max + 1)];
            shift += _log_w_shift[j];
            double s = 0.0;
            const int upto = std::min(len, (int) total + 1);
            for (int m = 0; m < upto; m++) s += w[m] * h[m];
            const double corr = shift + std::log(s);
            // A shifted sum can still underflow where the peaks sit in
            // different corners. That is not evidence the correction
            // vanishes -- redo those on the exact path.
            if (!std::isfinite(corr)) {
                row[j] = log_likelihood_exact(&_counts[(size_t) j * K], K, bg,
                                              &input[(size_t) i * K],
                                              _photon_number_pmf);
            } else {
                row[j] += corr;
            }
        }
    }
    *output = out;
}


void PdaBurstLikelihood::log_likelihood(
        double* input, int n_input, double** output, int* n_output
) {
    double* grid = nullptr; int d1 = 0, d2 = 0;
    log_likelihood_grid(input, 1, n_input, &grid, &d1, &d2);
    *n_output = _n_bursts;
    *output = grid;   // one row: already exactly the per-burst vector
}


void PdaBurstLikelihood::total_log_likelihood(
        double* input, int n_input1, int n_input2,
        double** output, int* n_output
) {
    double* grid = nullptr; int d1 = 0, d2 = 0;
    log_likelihood_grid(input, n_input1, n_input2, &grid, &d1, &d2);
    *n_output = d1;
    auto* out = (double*) malloc(std::max(1, d1) * sizeof(double));
    for (int i = 0; i < d1; i++) {
        double s = 0.0;
        const double* row = grid + (size_t) i * d2;
        for (int j = 0; j < d2; j++) s += row[j];
        out[i] = s;
    }
    free(grid);
    *output = out;
}


void PdaBurstLikelihood::get_boxes(int** output, int* n_output) const {
    *n_output = (int) _boxes.size();
    auto* out = (int*) malloc(std::max<size_t>(1, _boxes.size()) * sizeof(int));
    for (size_t i = 0; i < _boxes.size(); i++) out[i] = _boxes[i];
    *output = out;
}

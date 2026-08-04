// SPDX-License-Identifier: BSD-3-Clause
#include "HMMBayes.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace tttrlib {
namespace hmm_rand {

namespace {
inline uint64_t mix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}
}  // namespace

double unit(uint64_t key, uint64_t counter) {
    // Counter-based rather than streaming: a draw is a pure function of
    // (key, counter), so the same sweep gives the same numbers no matter how
    // the bursts were split across threads.  A stateful generator per thread
    // would make results depend on the thread count, which is exactly the
    // property `TestSamplers` exists to protect.
    const uint64_t h = mix64(key ^ mix64(counter + 0x165667B19E3779F9ULL));
    return double(h >> 11) * (1.0 / 9007199254740992.0);   // 53-bit mantissa
}

double normal(uint64_t key, uint64_t& counter) {
    // Box-Muller. `u1` is pushed off zero because log(0) is not recoverable;
    // the shift is far below the resolution of the uniform itself.
    double u1 = unit(key, counter++);
    const double u2 = unit(key, counter++);
    if (u1 < 1e-300) u1 = 1e-300;
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
}

double gamma_variate(double shape, uint64_t key, uint64_t& counter) {
    if (!(shape > 0.0)) return 0.0;
    if (shape < 1.0) {
        // Boost: G_a = G_{a+1} * U^(1/a).  Cheaper and more accurate here than
        // rejection, and alpha < 1 is the interesting range (a sparse Dirichlet).
        const double g = gamma_variate(shape + 1.0, key, counter);
        double u = unit(key, counter++);
        if (u < 1e-300) u = 1e-300;
        return g * std::pow(u, 1.0 / shape);
    }
    const double d = shape - 1.0 / 3.0;
    const double c = 1.0 / std::sqrt(9.0 * d);
    for (int guard = 0; guard < 10000; ++guard) {
        double x, v;
        do {
            x = normal(key, counter);
            v = 1.0 + c * x;
        } while (v <= 0.0);
        v = v * v * v;
        const double u = unit(key, counter++);
        const double x2 = x * x;
        if (u < 1.0 - 0.0331 * x2 * x2) return d * v;
        if (std::log(u) < 0.5 * x2 + d * (1.0 - v + std::log(v))) return d * v;
    }
    return d;   // unreachable in practice; never spin forever on a bad key
}

void dirichlet(const double* alpha, int n, double* out,
               uint64_t key, uint64_t& counter) {
    double total = 0.0;
    for (int i = 0; i < n; ++i) {
        out[i] = gamma_variate(alpha[i] > 0.0 ? alpha[i] : 0.0, key, counter);
        total += out[i];
    }
    if (total > 0.0) {
        for (int i = 0; i < n; ++i) out[i] /= total;
    } else {
        // Every Gamma underflowed -- possible when all concentrations are tiny.
        // Uniform is the honest fallback: it is the Dirichlet's own mean, and
        // leaving zeros would hand the next forward pass an impossible model.
        for (int i = 0; i < n; ++i) out[i] = 1.0 / n;
    }
}

}  // namespace hmm_rand

// ---------------------------------------------------------------------------
// Summaries and diagnostics
// ---------------------------------------------------------------------------

namespace {

/*!
 * Canonical state order: descending emission of symbol 0.
 *
 * Any deterministic, permutation-equivariant rule works; this one matches
 * `canonical_order` in HMMSurrogate.cpp so the two never disagree about which
 * state is "state 0".
 */
std::vector<int> canonical_order(const double* par, int n, int n_symbols) {
    const double* obs = par + n + n * n;
    std::vector<int> ord(n);
    std::iota(ord.begin(), ord.end(), 0);
    std::stable_sort(ord.begin(), ord.end(), [&](int a, int b) {
        return obs[size_t(a) * n_symbols] > obs[size_t(b) * n_symbols];
    });
    return ord;
}

/// Rewrite one draw under a state permutation (prior, trans and obs together).
void relabel(const double* in, double* out, const std::vector<int>& ord,
             int n, int n_symbols) {
    for (int i = 0; i < n; ++i) out[i] = in[ord[i]];
    const double* t_in = in + n;
    double* t_out = out + n;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            t_out[i * n + j] = t_in[ord[i] * n + ord[j]];
    const double* o_in = in + n + n * n;
    double* o_out = out + n + n * n;
    for (int i = 0; i < n; ++i)
        for (int y = 0; y < n_symbols; ++y)
            o_out[size_t(i) * n_symbols + y] = o_in[size_t(ord[i]) * n_symbols + y];
}

/// All draws, relabelled canonically, flattened to `[draw][par]`.
std::vector<double> relabelled(const HmmPosterior& post) {
    const int nd = post.n_draws(), np = post.n_par;
    std::vector<double> out(size_t(post.n_chains) * nd * np, 0.0);
    std::vector<double> tmp(np);
    size_t w = 0;
    for (int c = 0; c < post.n_chains; ++c)
        for (int d = 0; d < nd; ++d, w += np) {
            const double* src = post.draw(c, d);
            relabel(src, tmp.data(),
                    canonical_order(src, post.n_states, post.n_symbols),
                    post.n_states, post.n_symbols);
            std::copy(tmp.begin(), tmp.end(), out.begin() + w);
        }
    return out;
}

}  // namespace

std::vector<double> HmmPosterior::mean() const {
    const std::vector<double> r = relabelled(*this);
    const size_t rows = n_par > 0 ? r.size() / n_par : 0;
    std::vector<double> out(n_par, 0.0);
    for (size_t i = 0; i < rows; ++i)
        for (int k = 0; k < n_par; ++k) out[k] += r[i * n_par + k];
    if (rows) for (int k = 0; k < n_par; ++k) out[k] /= double(rows);
    return out;
}

std::vector<double> HmmPosterior::sd() const {
    const std::vector<double> r = relabelled(*this);
    const size_t rows = n_par > 0 ? r.size() / n_par : 0;
    const std::vector<double> m = mean();
    std::vector<double> out(n_par, 0.0);
    if (rows < 2) return out;
    for (size_t i = 0; i < rows; ++i)
        for (int k = 0; k < n_par; ++k) {
            const double d = r[i * n_par + k] - m[k];
            out[k] += d * d;
        }
    for (int k = 0; k < n_par; ++k) out[k] = std::sqrt(out[k] / double(rows - 1));
    return out;
}

std::vector<double> HmmPosterior::quantile(double q) const {
    const std::vector<double> r = relabelled(*this);
    const size_t rows = n_par > 0 ? r.size() / n_par : 0;
    std::vector<double> out(n_par, 0.0);
    if (!rows) return out;
    std::vector<double> col(rows);
    for (int k = 0; k < n_par; ++k) {
        for (size_t i = 0; i < rows; ++i) col[i] = r[i * n_par + k];
        std::sort(col.begin(), col.end());
        double pos = q * double(rows - 1);
        if (pos < 0.0) pos = 0.0;
        if (pos > double(rows - 1)) pos = double(rows - 1);
        const size_t lo = size_t(pos);
        const size_t hi = std::min(lo + 1, rows - 1);
        out[k] = col[lo] + (pos - double(lo)) * (col[hi] - col[lo]);
    }
    return out;
}

std::vector<double> HmmPosterior::rhat() const {
    const int nd = n_draws();
    std::vector<double> out(n_par, std::numeric_limits<double>::quiet_NaN());
    const int half = nd / 2;
    if (half < 2) return out;
    const int m = n_chains * 2;             // split: two halves per chain
    const std::vector<double> r = relabelled(*this);
    auto at = [&](int c, int d) { return r.data() + (size_t(c) * nd + d) * n_par; };

    for (int k = 0; k < n_par; ++k) {
        std::vector<double> means(m), vars(m);
        for (int c = 0; c < n_chains; ++c)
            for (int h = 0; h < 2; ++h) {
                const int start = h == 0 ? 0 : nd - half;
                double s = 0.0;
                for (int d = 0; d < half; ++d) s += at(c, start + d)[k];
                const double mu = s / half;
                double v = 0.0;
                for (int d = 0; d < half; ++d) {
                    const double e = at(c, start + d)[k] - mu;
                    v += e * e;
                }
                means[c * 2 + h] = mu;
                vars[c * 2 + h] = v / (half - 1);
            }
        const double gm = std::accumulate(means.begin(), means.end(), 0.0) / m;
        double B = 0.0;
        for (double mu : means) B += (mu - gm) * (mu - gm);
        B *= double(half) / double(m - 1);
        const double W = std::accumulate(vars.begin(), vars.end(), 0.0) / m;
        // A parameter that never moved is not "unconverged" -- it is pinned
        // (a fixed emission, say).  Report 1 rather than 0/0.
        if (W <= 0.0) { out[k] = (B <= 0.0) ? 1.0 : std::numeric_limits<double>::infinity(); continue; }
        const double var_hat = (double(half - 1) / double(half)) * W + B / double(half);
        out[k] = std::sqrt(var_hat / W);
    }
    return out;
}

std::vector<double> HmmPosterior::ess() const {
    const int nd = n_draws();
    std::vector<double> out(n_par, 0.0);
    if (nd < 4) return out;
    const double total = double(n_chains) * nd;
    const std::vector<double> r = relabelled(*this);
    auto at = [&](int c, int d) { return r.data() + (size_t(c) * nd + d) * n_par; };

    std::vector<double> x(nd);
    for (int k = 0; k < n_par; ++k) {
        // Autocorrelations are pooled over chains at each lag, which is what
        // makes this usable with a single chain as well as many.
        double var = 0.0;
        std::vector<double> rho_sum(size_t(nd / 2), 0.0);
        int used = 0;
        for (int c = 0; c < n_chains; ++c) {
            double mu = 0.0;
            for (int d = 0; d < nd; ++d) { x[d] = at(c, d)[k]; mu += x[d]; }
            mu /= nd;
            double v = 0.0;
            for (int d = 0; d < nd; ++d) { x[d] -= mu; v += x[d] * x[d]; }
            if (v <= 0.0) continue;
            var += v / nd;
            for (size_t lag = 1; lag < rho_sum.size(); ++lag) {
                double s = 0.0;
                for (int d = 0; d + int(lag) < nd; ++d) s += x[d] * x[d + lag];
                rho_sum[lag] += (s / nd);
            }
            ++used;
        }
        if (!used || var <= 0.0) { out[k] = total; continue; }
        var /= used;
        // Geyer's initial positive sequence: sum consecutive *pairs* of
        // autocorrelations and stop at the first non-positive pair.  Truncating
        // on individual lags instead stops early on noise and inflates ESS.
        double sum = 0.0;
        for (size_t lag = 1; lag + 1 < rho_sum.size(); lag += 2) {
            const double pair = (rho_sum[lag] + rho_sum[lag + 1]) / (used * var);
            if (pair <= 0.0) break;
            sum += pair;
        }
        const double tau = 1.0 + 2.0 * sum;
        out[k] = tau > 0.0 ? total / tau : total;
    }
    return out;
}

}  // namespace tttrlib

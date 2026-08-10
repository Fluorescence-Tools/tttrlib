// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file HMMEmission.h
 * \brief Build an HMM emission table from per-state lifetime spectra.
 *
 * The engine's core reads emission through exactly one expression,
 * `obs[i * p + y]`, and neither knows nor cares what the symbol `y` means. With
 * a micro-time axis the alphabet is the **product** `stream * n_micro_bins +
 * bin`, and this header is the one place that fills such a table:
 *
 * ```
 *   obs[state][stream, bin] = P(stream | state) * f_{state,stream}(bin)
 * ```
 *
 * `f` is a multi-exponential decay convolved with an IRF pattern, evaluated as
 * a `SimDecay` — the same object the simulator draws micro-times from, so a
 * decay that generates photons and a decay that scores them cannot disagree.
 *
 * \par What this deliberately does not know
 * There is no Förster radius here, no linker width, no crosstalk matrix. A
 * state is described by what *scoring* needs — a lifetime spectrum and a stream
 * split — and the map from a structure onto those quantities is physics that
 * lives outside the library and enters as a prior. That boundary is what keeps
 * this file short: it is arithmetic over an axis, not a model of a molecule.
 */
#ifndef TTTRLIB_HMMEMISSION_H
#define TTTRLIB_HMMEMISSION_H

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "SimDecay.h"
#include "HMMBayes.h"   // slice_sample / dirichlet for the Gibbs counterpart

namespace tttrlib {

/// One state's decay in one stream: @f$ \sum_c a_c \exp(-t/\tau_c) @f$.
struct HmmLifetimeSpectrum {
    std::vector<double> amplitudes;
    std::vector<double> lifetimes;   ///< ns; entries <= 0 are ignored

    HmmLifetimeSpectrum() = default;
    HmmLifetimeSpectrum(std::vector<double> a, std::vector<double> tau)
        : amplitudes(std::move(a)), lifetimes(std::move(tau)) {}
    /// The mono-exponential case, which is most of what callers want.
    explicit HmmLifetimeSpectrum(double tau)
        : amplitudes(1, 1.0), lifetimes(1, tau) {}
};

/*!
 * \brief A per-state, per-stream emission specification over a micro-time axis.
 *
 * Row-major throughout, `[state * n_streams + stream]`, matching the `obs`
 * layout the engine already uses. `build()` returns an
 * `n_states * (n_streams * n_micro_bins)` row-stochastic table ready to drop
 * into `HmmModel::obs`.
 *
 * The parameter count is what makes this worth having: a free categorical over
 * a 4-stream, 1024-bin alphabet carries 4095 numbers per state, and EM does not
 * find its good optimum. A spectrum carries a handful, and does.
 */
struct HmmEmissionSpec {
    int n_states = 0;
    int n_streams = 0;
    int n_micro_bins = 1;

    /// Micro-time axis of the *bins* (ns): bin `k` covers `t0 + k*dt`.
    double dt = 0.008;
    double t0 = 0.0;

    /// IRF pattern on the same axis; empty means no convolution (delta IRF).
    std::vector<double> irf;

    /*!
     * \brief Gaussian IRF as `(centre, FWHM)` in ns — the *analytic* path.
     *
     * Set `irf_fwhm > 0` and each bin's probability is computed in closed form
     * instead of by convolving a sampled IRF pattern. A photon's micro-time is
     * the sum of two independent things — the memoryless excited-state time and
     * everything the instrument adds (finite pulse width, detector jitter) — so
     * its density is an exponential convolved with a Gaussian, the
     * exponentially-modified Gaussian. That has a closed form, and so does its
     * CDF, so the bin probability is a *difference of CDFs*: exact at any
     * resolution, with no discrete convolution and no sampled IRF to alias.
     *
     * Mutually exclusive with `irf`, which stays the route for a *measured*
     * IRF of arbitrary shape. `irf_fwhm <= 0` disables this path.
     *
     * After Tavakoli *et al.*, whose Eq. 7 is this density; `sigma = FWHM/2.355`.
     */
    double irf_center = 0.0;
    double irf_fwhm = 0.0;

    /// `P(stream | state)`, `n_states * n_streams`; rows are normalised in build().
    std::vector<double> stream_probability;

    /// Decay per (state, stream), `n_states * n_streams`.
    std::vector<HmmLifetimeSpectrum> spectrum;

    /*!
     * \brief Measured decay **patterns**, one per (state, stream); empty = unused.
     *
     * This is how a decay usually arrives. `SimDecay` treats a pattern — an
     * arbitrary array over micro-time — as the first-class representation, and
     * the multi-exponential helpers as optional constructors, because what an
     * experiment yields is a *measured* donor-only decay, a scatter pattern, an
     * IRF. Not a list of amplitudes and lifetimes.
     *
     * A non-empty entry overrides `spectrum[ik]` completely: the shape is taken
     * as given, and neither `fit_counts` nor `sample_counts` touches it. Only
     * the stream split remains free, which is *more* robust than fitting a
     * lifetime, not less — a fixed pattern has no decay parameters at all, so
     * the findability problem a free emission suffers cannot arise.
     *
     * Patterns are aggregated onto the emission axis if supplied at a different
     * length, which is exact: summing the source channels that fall in a bin is
     * precisely that bin's probability. Supply them on the instrument's own TAC
     * axis and let `build()` coarsen.
     *
     * \warning **No IRF is applied to a supplied pattern**, deliberately: a
     * *measured* decay already contains the instrument response, so convolving
     * again would broaden it twice. `irf` and `irf_fwhm` therefore affect only
     * the cells still described by a `spectrum`, and the two can be mixed within
     * one spec — a measured donor reference alongside an analytic acceptor decay
     * — without the IRF being applied inconsistently.
     */
    std::vector<std::vector<double>> pattern;

    /*!
     * \brief Optional background shape per stream, `n_streams * n_micro_bins`.
     *
     * Mixed in at weight `background_fraction`, identically for every state —
     * which is the whole content of "background": photons whose micro-time
     * carries no information about which state emitted them. Leave empty for a
     * uniform shape.
     */
    std::vector<double> background;
    double background_fraction = 0.0;

    /// A spec with flat stream splits and one mono-exponential decay everywhere.
    static HmmEmissionSpec uniform(
        int n_states, int n_streams, int n_micro_bins, double dt, double tau
    ) {
        HmmEmissionSpec s;
        s.n_states = n_states;
        s.n_streams = n_streams;
        s.n_micro_bins = n_micro_bins;
        s.dt = dt;
        s.stream_probability.assign(size_t(n_states) * n_streams,
                                    n_streams > 0 ? 1.0 / n_streams : 0.0);
        s.spectrum.assign(size_t(n_states) * n_streams, HmmLifetimeSpectrum(tau));
        return s;
    }

    /// Search box for the fitted lifetime, ns. A lifetime is positive and
    /// physically bounded, so a box is the honest form -- and it is where a
    /// prior would attach if one is supplied later.
    double tau_min = 0.05;
    double tau_max = 20.0;

    /*!
     * \brief M-step: re-fit the parameters from expected counts, and rebuild.
     *
     * `gamma_obs` is the E-step's expected per-(state, symbol) count matrix,
     * `n_states * (n_streams * n_micro_bins)`, row-major. Maximises the same
     * `Q` the free M-step does, but over the decay parameters rather than over
     * every column -- which is the whole point, since a free column can go to
     * exactly zero in the *interior* of a decay and no exponential can.
     *
     * It factorises exactly, so almost all of it stays closed-form:
     *
     * ```
     *   Q = sum_i sum_k (sum_b g[i][k,b]) log p_ik      <- closed form
     *     + sum_i sum_k sum_b g[i][k,b] log f_ik(b)     <- 1-D search per (i,k)
     * ```
     *
     * The stream split keeps the categorical M-step's closed form; only the
     * lifetime needs a bounded search, one scalar per (state, stream), by
     * golden section.
     *
     * **Mono-exponential components only.** A (state, stream) whose spectrum
     * has exactly one positive lifetime has that lifetime re-fitted; anything
     * else keeps its spectrum and contributes only through the stream split.
     * Fitting a full spectrum is a separate problem -- the components are not
     * separable and a 1-D search does not reach it -- and is deliberately left
     * for a later increment rather than half-done here.
     *
     * @return the rebuilt emission table, ready for `HmmModel::obs`.
     */
    std::vector<double> fit_counts(const std::vector<double>& gamma_obs) {
        validate();
        const int p = n_streams * n_micro_bins;
        if (gamma_obs.size() != size_t(n_states) * p)
            throw std::invalid_argument(
                "HmmEmissionSpec::fit_counts: gamma_obs must be n_states * n_symbols");

        for (int i = 0; i < n_states; ++i) {
            for (int k = 0; k < n_streams; ++k) {
                const double* counts = &gamma_obs[size_t(i) * p + size_t(k) * n_micro_bins];
                double w = 0.0;
                for (int b = 0; b < n_micro_bins; ++b) w += counts[b] > 0.0 ? counts[b] : 0.0;
                // build() normalises each state row, so the raw weight is
                // already the closed-form split; no separate division needed.
                stream_probability[index(i, k)] = w;

                HmmLifetimeSpectrum& sp = spectrum[index(i, k)];
                if (w <= 0.0 || !is_mono(sp)) continue;
                sp.lifetimes[0] = golden_max(
                    [&](double tau) { return q_of_tau(tau, counts); }, tau_min, tau_max);
            }
        }
        return build();
    }

    /*!
     * \brief Gibbs counterpart of `fit_counts`: *draw* the parameters, and rebuild.
     *
     * Same factorisation, same objective, one step instead of a maximisation:
     * the stream split stays conjugate and is drawn from its Dirichlet, and each
     * mono-exponential lifetime gets one univariate slice update against the
     * very `q_of_tau` the M-step maximises. Scoring through one kernel is the
     * point — a sampler and an optimiser that evaluated the decay differently
     * would disagree in a way no test on either alone would catch.
     *
     * \param gamma_obs Per-(state, symbol) counts from the *sampled* path, not
     *        the expected counts: this is a Gibbs step, so it conditions on a
     *        drawn path rather than averaging over paths.
     * \param alpha_obs Dirichlet concentrations for the stream split,
     *        `n_states * n_streams`, or empty for flat.
     *
     * \return the rebuilt emission table.
     */
    std::vector<double> sample_counts(
        const std::vector<double>& gamma_obs,
        const std::vector<double>& alpha_obs,
        uint64_t key, uint64_t& counter
    ) {
        validate();
        const int p = n_streams * n_micro_bins;
        if (gamma_obs.size() != size_t(n_states) * p)
            throw std::invalid_argument(
                "HmmEmissionSpec::sample_counts: gamma_obs must be n_states * n_symbols");

        // Two-argument form deliberately: `conc(size_t(n_streams))` is a
        // most-vexing-parse and declares a function.
        std::vector<double> conc(n_streams, 0.0), split(n_streams, 0.0);
        for (int i = 0; i < n_states; ++i) {
            for (int k = 0; k < n_streams; ++k) {
                const double* counts =
                    &gamma_obs[size_t(i) * p + size_t(k) * n_micro_bins];
                double w = 0.0;
                for (int b = 0; b < n_micro_bins; ++b)
                    w += counts[b] > 0.0 ? counts[b] : 0.0;
                const size_t ik = index(i, k);
                conc[k] = w + (alpha_obs.empty() ? 1.0 : alpha_obs[ik]);

                if (ik < pattern.size() && !pattern[ik].empty()) continue;
                HmmLifetimeSpectrum& sp = spectrum[ik];
                if (w <= 0.0 || !is_mono(sp)) continue;
                // Interval width from the current value, so the scale follows
                // the parameter as the chain moves; slice is insensitive to it.
                const double width = std::max(0.05 * sp.lifetimes[0], 1e-3);
                sp.lifetimes[0] = hmm_rand::slice_sample(
                    [&](double tau) { return q_of_tau(tau, counts); },
                    sp.lifetimes[0], tau_min, tau_max, width, key, counter);
            }
            hmm_rand::dirichlet(conc.data(), n_streams, split.data(), key, counter);
            for (int k = 0; k < n_streams; ++k)
                stream_probability[index(i, k)] = split[k];
        }
        return build();
    }

    /// Set one (state, stream) decay; grows nothing, so the spec must be sized.
    void set_spectrum(int state, int stream, const HmmLifetimeSpectrum& s) {
        spectrum.at(index(state, stream)) = s;
    }
    /*!
     * \brief Use a measured decay **pattern** for one (state, stream).
     *
     * Overrides that cell's spectrum; the shape is then fixed and only the
     * stream split is fitted or sampled. Any length is accepted and aggregated
     * onto the emission axis, so an instrument-resolution decay can be passed
     * straight in.
     */
    void set_pattern(int state, int stream, const std::vector<double>& p) {
        if (pattern.empty())
            pattern.assign(size_t(n_states) * n_streams, std::vector<double>());
        pattern.at(index(state, stream)) = p;
    }

    void set_stream_probability(int state, int stream, double p) {
        stream_probability.at(index(state, stream)) = p;
    }

    /*!
     * \brief The emission table, `n_states * (n_streams * n_micro_bins)`.
     *
     * Each state's row is `P(stream|state) * f_{state,stream}(bin)` with the
     * background mixed in, then normalised. At `n_micro_bins == 1` the decay
     * integrates to 1 in its single bin and the result is exactly the
     * categorical stream table — so the classic, micro-time-free model is the
     * degenerate case of this one rather than a separate path.
     */
    std::vector<double> build() const {
        validate();
        const int p = n_streams * n_micro_bins;
        std::vector<double> obs(size_t(n_states) * p, 0.0);

        // One SimDecay per (state, stream). Built here rather than cached on the
        // spec so a caller that edits a lifetime cannot leave a stale table
        // behind -- the M-step rebuilds this every candidate anyway, and the
        // cost is n_bins * n_components, not per photon.
        for (int i = 0; i < n_states; ++i) {
            double row_sum = 0.0;
            for (int k = 0; k < n_streams; ++k) {
                const size_t ik = index(i, k);
                const double pk = stream_probability[ik] > 0.0
                    ? stream_probability[ik] : 0.0;
                if (pk <= 0.0) continue;
                const bool has_pat = ik < pattern.size() && !pattern[ik].empty();
                const SimDecay d = SimDecay::from_pattern(
                    has_pat ? rebin(pattern[ik]) : spectrum_bins(spectrum[ik]), dt, t0);
                for (int b = 0; b < n_micro_bins; ++b) {
                    const double v = pk * d.pdf(b);
                    obs[size_t(i) * p + k * n_micro_bins + b] = v;
                    row_sum += v;
                }
            }
            // A state whose every stream probability is zero would otherwise be
            // an all-zero emission row, i.e. log(0) for every photon. Spread it
            // uniformly instead: an uninformative state, not a poisoned one.
            if (row_sum <= 0.0) {
                for (int y = 0; y < p; ++y) obs[size_t(i) * p + y] = 1.0 / p;
                row_sum = 1.0;
            }
            for (int y = 0; y < p; ++y) obs[size_t(i) * p + y] /= row_sum;
        }

        mix_background(obs, p);
        return obs;
    }

private:
    bool analytic_irf() const { return irf_fwhm > 0.0; }
    double irf_sigma() const { return irf_fwhm / 2.3548200450309493; }  // FWHM -> sigma

    /*!
     * \brief CDF of the exponentially-modified Gaussian at `x`.
     *
     * With `u = (x - mu)/sigma` and `v = sigma/tau`,
     *
     * ```
     *   F(x) = Phi(u) - 0.5 * exp(-u*v + v*v/2) * erfc((v - u)/sqrt(2))
     * ```
     *
     * Written in two branches for range, not for speed. The exponential and the
     * `erfc` pull hard in opposite directions, so evaluating them separately
     * overflows one and underflows the other long before the *product* — which
     * is always in [0,1] — is in any trouble. Using the identity
     * `exp(-u*v + v*v/2) * erfc(z) = exp(-u*u/2) * exp(z*z) * erfc(z)` keeps
     * both factors representable on the side where the naive form breaks.
     * Beyond `z > 26` the whole term is below any double's resolution against
     * `Phi(u)`, which is itself ~0 there: that is the far pre-pulse tail, where
     * no photon can be.
     */
    static double emg_cdf(double x, double mu, double sigma, double tau) {
        if (sigma <= 0.0 || tau <= 0.0) return 0.0;
        const double inv_sqrt2 = 0.7071067811865476;
        const double u = (x - mu) / sigma;
        const double v = sigma / tau;
        const double z = (v - u) * inv_sqrt2;
        const double phi = 0.5 * std::erfc(-u * inv_sqrt2);
        double term;
        if (z <= 0.0)        term = 0.5 * std::exp(-u * v + 0.5 * v * v) * std::erfc(z);
        else if (z < 26.0)   term = 0.5 * std::exp(-0.5 * u * u) * std::exp(z * z) * std::erfc(z);
        else                 term = 0.0;
        const double f = phi - term;
        return f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
    }

    /*!
     * \brief Per-bin integral of one component, `exp(-t/tau)`, IRF included.
     *
     * The single place a decay is turned into bin probabilities, so the table
     * that gets scored and the objective the M-step maximises cannot drift
     * apart -- they call this.
     *
     * Both branches return the *integral over the bin*, never the density
     * sampled at an edge. Analytically that is a CDF difference; without an
     * IRF it is `tau*(1 - exp(-dt/tau))*exp(-t_b/tau)`, the same quantity in
     * closed form. Scaled by `tau` in the analytic branch so the two agree in
     * normalisation and a spectrum's components keep their relative weights.
     */
    std::vector<double> component_bins(double tau) const {
        std::vector<double> f(size_t(n_micro_bins > 0 ? n_micro_bins : 0), 0.0);
        if (tau <= 0.0) return f;
        if (analytic_irf()) {
            const double s = irf_sigma();
            double prev = emg_cdf(t0, irf_center, s, tau);
            for (int b = 0; b < n_micro_bins; ++b) {
                const double next = emg_cdf(t0 + (b + 1) * dt, irf_center, s, tau);
                f[b] = next > prev ? tau * (next - prev) : 0.0;
                prev = next;
            }
        } else {
            const double scale = tau * (1.0 - std::exp(-dt / tau));
            for (int b = 0; b < n_micro_bins; ++b) {
                const double t = t0 + b * dt;
                if (t >= 0.0) f[b] = scale * std::exp(-t / tau);
            }
        }
        return f;
    }

    /*!
     * \brief Aggregate a supplied pattern onto the emission axis.
     *
     * Exact when the source length is a multiple of `n_micro_bins`: a bin's
     * probability *is* the sum of the source channels inside it. Otherwise the
     * source is distributed proportionally, which is the right thing for a
     * histogram and the only sensible reading of a non-commensurate axis.
     */
    std::vector<double> rebin(const std::vector<double>& src) const {
        std::vector<double> out(size_t(n_micro_bins), 0.0);
        if (src.empty() || n_micro_bins <= 0) return out;
        const size_t m = src.size();
        if (m == size_t(n_micro_bins)) {
            for (int b = 0; b < n_micro_bins; ++b)
                out[b] = src[b] > 0.0 ? src[b] : 0.0;
            return out;
        }
        // Fractional overlap of source channel j with target bin b.
        const double scale = double(n_micro_bins) / double(m);
        for (size_t j = 0; j < m; ++j) {
            const double v = src[j] > 0.0 ? src[j] : 0.0;
            if (v <= 0.0) continue;
            double lo = j * scale, hi = (j + 1) * scale;
            int b0 = int(lo), b1 = int(hi);
            if (b1 >= n_micro_bins) b1 = n_micro_bins - 1;
            if (b0 == b1) { out[b0] += v; continue; }
            for (int b = b0; b <= b1; ++b) {
                const double a = std::max(lo, double(b));
                const double c = std::min(hi, double(b + 1));
                if (c > a) out[b] += v * (c - a) / (hi - lo);
            }
        }
        return out;
    }

    /// A whole spectrum's per-bin pattern, with a measured IRF convolved in.
    std::vector<double> spectrum_bins(const HmmLifetimeSpectrum& sp) const {
        std::vector<double> out(size_t(n_micro_bins > 0 ? n_micro_bins : 0), 0.0);
        const size_t nc = std::min(sp.amplitudes.size(), sp.lifetimes.size());
        for (size_t c = 0; c < nc; ++c) {
            if (sp.lifetimes[c] <= 0.0) continue;
            const std::vector<double> f = component_bins(sp.lifetimes[c]);
            for (int b = 0; b < n_micro_bins; ++b) out[b] += sp.amplitudes[c] * f[b];
        }
        // A *measured* IRF is a sampled pattern, so it can only be applied by
        // discrete convolution; the analytic branch has already folded its
        // Gaussian in, and `validate()` forbids asking for both.
        if (!irf.empty()) out = SimDecay::convolve(out, irf);
        return out;
    }

    /// A single positive component -- the only case a 1-D search can re-fit.
    static bool is_mono(const HmmLifetimeSpectrum& sp) {
        return sp.lifetimes.size() == 1 && !sp.amplitudes.empty() && sp.lifetimes[0] > 0.0;
    }

    /// `sum_b counts[b] * log f(b; tau)` for a mono-exponential `f`, on this axis.
    double q_of_tau(double tau, const double* counts) const {
        if (tau <= 0.0) return -std::numeric_limits<double>::infinity();
        // Through the same kernel build() uses -- including the IRF -- so the
        // objective and the table that ends up being scored cannot disagree.
        // Fitting against a no-IRF shape and then scoring photons with an
        // IRF-convolved one would bias every lifetime by the IRF's offset.
        std::vector<double> f = component_bins(tau);
        if (!irf.empty()) f = SimDecay::convolve(f, irf);
        double total = 0.0;
        for (int b = 0; b < n_micro_bins; ++b) total += f[b];
        if (total <= 0.0) return -std::numeric_limits<double>::infinity();
        double q = 0.0;
        for (int b = 0; b < n_micro_bins; ++b) {
            if (counts[b] <= 0.0) continue;
            const double prob = f[b] / total;
            q += counts[b] * std::log(prob > 0.0 ? prob : 1e-300);
        }
        return q;
    }

    /*!
     * \brief Maximise a unimodal `f` on `[lo, hi]` by golden-section search.
     *
     * Written out rather than pulled in: the constraint here is std-only C++,
     * and a bounded search over one scalar needs no more than this.
     */
    template <class F>
    static double golden_max(F f, double lo, double hi,
                             double tol = 1e-6, int max_iter = 200) {
        const double g = 0.6180339887498949;   // (sqrt(5) - 1) / 2
        double a = lo, b = hi;
        double c = b - g * (b - a), d = a + g * (b - a);
        double fc = f(c), fd = f(d);
        for (int i = 0; i < max_iter && (b - a) >= tol; ++i) {
            if (fc > fd) { b = d; d = c; fd = fc; c = b - g * (b - a); fc = f(c); }
            else         { a = c; c = d; fc = fd; d = a + g * (b - a); fd = f(d); }
        }
        return 0.5 * (a + b);
    }


    size_t index(int state, int stream) const {
        if (state < 0 || state >= n_states || stream < 0 || stream >= n_streams)
            throw std::out_of_range("HmmEmissionSpec: (state, stream) out of range");
        return size_t(state) * n_streams + stream;
    }

    void validate() const {
        if (n_states <= 0 || n_streams <= 0 || n_micro_bins <= 0)
            throw std::invalid_argument(
                "HmmEmissionSpec: n_states, n_streams and n_micro_bins must be > 0");
        const size_t need = size_t(n_states) * n_streams;
        if (stream_probability.size() != need)
            throw std::invalid_argument(
                "HmmEmissionSpec: stream_probability must be n_states * n_streams");
        if (spectrum.size() != need)
            throw std::invalid_argument(
                "HmmEmissionSpec: spectrum must be n_states * n_streams");
        if (!pattern.empty() && pattern.size() != need)
            throw std::invalid_argument(
                "HmmEmissionSpec: pattern must be empty or n_states * n_streams");
        if (background_fraction < 0.0 || background_fraction > 1.0)
            throw std::invalid_argument(
                "HmmEmissionSpec: background_fraction must lie in [0, 1]");
        if (!background.empty()
            && background.size() != size_t(n_streams) * n_micro_bins)
            throw std::invalid_argument(
                "HmmEmissionSpec: background must be n_streams * n_micro_bins");
        if (!irf.empty() && int(irf.size()) > n_micro_bins)
            throw std::invalid_argument(
                "HmmEmissionSpec: irf must not be longer than n_micro_bins");
        // Applying both would convolve the Gaussian in twice -- once
        // analytically and once by pattern -- which broadens every decay
        // silently rather than failing.
        if (!irf.empty() && irf_fwhm > 0.0)
            throw std::invalid_argument(
                "HmmEmissionSpec: set either an `irf` pattern or `irf_fwhm`, not both");
    }

    void mix_background(std::vector<double>& obs, int p) const {
        if (background_fraction <= 0.0) return;
        std::vector<double> bg(size_t(p), 1.0 / p);
        if (!background.empty()) {
            double s = 0.0;
            for (double v : background) s += v > 0.0 ? v : 0.0;
            if (s > 0.0)
                for (int y = 0; y < p; ++y)
                    bg[y] = (background[y] > 0.0 ? background[y] : 0.0) / s;
        }
        const double f = background_fraction;
        for (int i = 0; i < n_states; ++i)
            for (int y = 0; y < p; ++y) {
                double& v = obs[size_t(i) * p + y];
                v = (1.0 - f) * v + f * bg[y];
            }
    }
};

} // namespace tttrlib

#endif // TTTRLIB_HMMEMISSION_H

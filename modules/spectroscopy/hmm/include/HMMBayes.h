/*!
 * \file HMMBayes.h
 * \brief Posterior sampling for the photon-by-photon HMM (blocked Gibbs).
 *
 * EM returns one model. This returns a *distribution* over models, which is
 * what a credible interval needs and what a point estimate cannot supply at
 * any amount of post-processing.
 *
 * The sweep is conjugate throughout, so there is no tuning and no accept step:
 *
 * ```
 *   1. draw a photon-level state path      s ~ P(s | y, theta)      (FFBS)
 *   2. draw the tick-level bridge in each gap                       (see below)
 *   3. count one-tick transitions, emissions, initial states
 *   4. draw theta ~ Dirichlet(counts + alpha)
 * ```
 *
 * \par Why step 2 exists
 * FFBS gives the state *at each photon*, but `trans` is the **one-tick** matrix,
 * and between two photons the chain takes @f$\Delta t@f$ unobserved steps. So
 * each gap needs an endpoint-conditioned draw of the path through it, or the
 * transition counts would be counts of photon-to-photon jumps rather than of
 * one-tick transitions, and `trans` would come out systematically wrong. This
 * is the step an early draft of the design missed, and it is the sweep's
 * dominant cost — a sweep is *not* cheaper than an EM iteration, which was the
 * original claim; measured on the prototype it is about 1.17x.
 */
#ifndef TTTRLIB_HMMBAYES_H
#define TTTRLIB_HMMBAYES_H

// Validation: KNOWN-ANSWER-TESTED 2026-08-17 -- blocked Gibbs started at the truth stays there, an EM seed
//   converges and covers the truth, draws are valid simplices, relabelling invariance.
//   test/python/hmm/test_gibbs.py.
//   Register: okf/testing/algorithm-validation.md

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace tttrlib {

/*!
 * \brief Draws from the posterior over @f$(\pi, A, B)@f$, plus diagnostics.
 *
 * `draws` is packed `[chain][draw][prior | trans | obs]` and left **exactly as
 * sampled**. Everything else — summaries *and* diagnostics — re-labels first,
 * because states are exchangeable: a chain that swaps state 0 and state 1 is
 * sampling the same posterior, but averaging its raw draws component-wise
 * averages two different states together and returns something that is not a
 * posterior mean of anything.
 *
 * \par Diagnostics must relabel too
 * This is not obvious, and getting it wrong looks like a broken sampler.
 * Measured here: two chains that each held a *stable* labelling — but opposite
 * ones — gave a raw split-@f$\hat R@f$ of **15.2** and a relabelled one of
 * **1.75**. Both chains were correct; the raw statistic was comparing "state 0"
 * in one against a different state in the other. For an exchangeable model,
 * chains sitting in different label permutations is the expected outcome, not
 * a failure, so relabelling first is what makes @f$\hat R@f$ mean anything.
 *
 * \warning What relabelling cannot fix is an *ambiguous* canonical order. The
 * rule here sorts by the emission of symbol 0, so two states with nearly equal
 * values there get sorted by noise, and the diagnostics become noisy with them.
 * If that happens the states are barely identified and the posterior is
 * genuinely multimodal — a wide interval is the honest answer, not a bug.
 */
struct HmmPosterior {
    int n_states = 0;
    int n_symbols = 0;
    int n_chains = 1;
    /// Parameters per draw: `n + n*n + n*n_symbols`.
    int n_par = 0;
    /// `[chain][draw][par]`, row-major, unrelabelled.
    std::vector<double> draws;
    /// Log-likelihood of each draw, `[chain][draw]`.
    std::vector<double> loglik;

    int n_draws() const {
        const int per = n_chains * n_par;
        return per > 0 ? int(draws.size()) / per : 0;
    }

    /// Posterior mean of every parameter, after canonical relabelling.
    std::vector<double> mean() const;
    /// Posterior standard deviation, after canonical relabelling.
    std::vector<double> sd() const;
    /// Posterior quantile `q` in [0,1], after canonical relabelling.
    std::vector<double> quantile(double q) const;

    /*!
     * \brief Split-@f$\hat R@f$ per parameter, on the raw draws.
     *
     * Each chain is halved first, so a single chain still yields a comparison
     * and a slowly drifting one is caught — plain between-chain R-hat cannot
     * see drift that every chain shares. Values near 1 indicate mixing;
     * > 1.01 is the usual line for "not converged".
     */
    std::vector<double> rhat() const;

    /*!
     * \brief Effective sample size per parameter, on the raw draws.
     *
     * Autocorrelation-corrected, summing the autocorrelations until a pair of
     * successive lags sums negative (Geyer's initial-positive-sequence rule).
     * Report this, not the draw count: 2000 draws at ESS 40 carry the
     * information of 40, and a credible interval quoted from the former is
     * about five times narrower than the data support.
     */
    std::vector<double> ess() const;

    /// One draw's parameter block (unrelabelled).
    const double* draw(int chain, int index) const {
        return draws.data() + (size_t(chain) * n_draws() + index) * n_par;
    }
};

namespace hmm_rand {

/// Uniform in [0,1) from a counter-based hash — thread-count independent.
double unit(uint64_t key, uint64_t counter);

/// Standard normal (Box-Muller), consuming two counter positions.
double normal(uint64_t key, uint64_t& counter);

/*!
 * \brief Gamma(shape, 1) variate, Marsaglia-Tsang.
 *
 * Shapes below 1 are handled by the standard boost @f$G_a = G_{a+1}U^{1/a}@f$
 * rather than by rejection from a bounding density, because the Dirichlet
 * concentrations that matter here — a sparsity-inducing @f$\alpha/M@f$ — sit
 * exactly in that range, where naive samplers are slowest and least accurate.
 */
double gamma_variate(double shape, uint64_t key, uint64_t& counter);

/// Dirichlet draw in place: `out[i] ~ Dir(alpha)`, normalised.
void dirichlet(const double* alpha, int n, double* out,
               uint64_t key, uint64_t& counter);

/*!
 * \brief One univariate slice-sampling update of `x0` under `log_f` on `[lo,hi]`.
 *
 * Neal (2003): draw a height under the density, widen an interval around `x0`
 * until it brackets the slice, then sample within it, shrinking on rejection.
 * The returned point is always accepted — there is no acceptance ratio and no
 * step size whose mis-setting silently wrecks the chain.
 *
 * \par Why this and not random-walk Metropolis
 * Chosen on measurement, not principle. On the lifetime conditional this
 * samples, benchmarked as **effective samples per log-density evaluation**
 * (the fair unit, since a slice update costs several calls):
 *
 * | sampler                   | best | worst | spread |
 * |---------------------------|------|-------|--------|
 * | Metropolis, step 0.02–1.5 | 168  | 17    | 10x    |
 * | slice, w 0.02–1.5         | 128  | 62    | 2x     |
 *
 * A well-tuned Metropolis beats it by ~30%, and a badly-tuned one loses by
 * 3.6x. Inside a Gibbs sweep the conditional's scale is not known in advance,
 * differs per (state, stream) and drifts as the fit moves, so the *untuned*
 * column is the one that applies. Matching Metropolis's best would mean
 * carrying dual-averaging adaptation and warm-up windows per cell; this needs
 * none, and is correct at any `w`.
 *
 * \param w Initial interval width. A scale hint only — being wrong costs
 *        evaluations, never correctness.
 * \param max_step Cap on stepping-out expansions per side.
 */
template <class F>
double slice_sample(F log_f, double x0, double lo, double hi, double w,
                    uint64_t key, uint64_t& counter, int max_step = 50) {
    if (!(hi > lo)) return x0;
    if (x0 < lo || x0 > hi) x0 = 0.5 * (lo + hi);
    if (!(w > 0.0)) w = 0.1 * (hi - lo);

    // Height of the slice: log_f(x0) + log(u), u ~ U(0,1).
    double u = unit(key, counter++);
    if (u < 1e-300) u = 1e-300;
    const double y = log_f(x0) + std::log(u);
    if (!(y > -std::numeric_limits<double>::infinity())) return x0;

    // Step out until both ends are below the slice (or hit the bounds).
    double a = x0 - w * unit(key, counter++);
    if (a < lo) a = lo;
    double b = a + w;
    if (b > hi) b = hi;
    for (int i = 0; i < max_step && a > lo && log_f(a) > y; ++i)
        a = (a - w < lo) ? lo : a - w;
    for (int i = 0; i < max_step && b < hi && log_f(b) > y; ++i)
        b = (b + w > hi) ? hi : b + w;

    // Sample within, shrinking toward x0 on rejection. Shrinking is what makes
    // the update valid whatever the interval started as -- so a poor `w` costs
    // a few more evaluations and nothing else.
    for (int i = 0; i < 100; ++i) {
        const double z = a + (b - a) * unit(key, counter++);
        if (log_f(z) >= y) return z;
        if (z < x0) a = z; else b = z;
    }
    return x0;
}

} // namespace hmm_rand

} // namespace tttrlib

#endif // TTTRLIB_HMMBAYES_H

// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file BurstSignificance.h
 * \brief Exact low-count detection statistics for burst searches.
 *
 * A burst search ultimately answers one question per candidate: *are there more
 * photons here than the background can explain?* The usual answer is the
 * Gaussian z-score \f$(k - \mu)/\sqrt{\mu}\f$, which is what
 * BurstSearchMaxTree.h used originally. That approximation assumes the Poisson
 * distribution is already normal, and single-molecule data is squarely in the
 * regime where it is not: a 20-photon transit against 2 expected background
 * photons has \f$\mu = 2\f$, where the Poisson distribution is visibly skewed
 * and discrete. The consequence is not merely imprecision — a threshold of
 * "4 sigma" does not correspond to the false-positive rate the user believes
 * they asked for, and the discrepancy is worst for the dim bursts near the
 * detection limit that one actually cares about.
 *
 * Gamma-ray and X-ray astronomy hit this problem first and solved it properly,
 * because their sources are photon-starved by construction. This header ports
 * the two standard tools:
 *
 *  - The **exact Poisson tail probability**, for when the background rate is
 *    known. Via the identity \f$P(K \ge k\,|\,\mu) = P(k, \mu)\f$ — the
 *    regularized lower incomplete gamma function — this is exact for all
 *    \f$k \ge 1\f$, with no normal approximation and no factorial overflow.
 *
 *  - The **Li & Ma (1983) on/off statistic**, for when the background is
 *    *estimated* rather than known. This is our actual situation: the
 *    rolling-ball baseline in BurstSearchMaxTree.cpp is measured from a finite
 *    stretch of data and carries its own Poisson error. Treating a measured
 *    background as exact overstates significance, and Li & Ma's likelihood-ratio
 *    form is the standard correction — it is how every gamma-ray observatory
 *    quotes a detection.
 *
 * Everything is computed in **log space**. This is not defensive style but a
 * hard requirement: a 40-photon burst on 2 expected counts has a tail
 * probability near \f$10^{-40}\f$, and a bright transit underflows a `double`
 * outright. Only the final sigma conversion leaves log space.
 *
 * The header is inline-only so the functions can be inlined into the max-tree's
 * per-node filter loop, where they are called once per candidate component.
 * The incomplete gamma function is not in `<cmath>`, so it is implemented here
 * rather than pulled in as a dependency.
 *
 * ### References
 *  - Li, T.-P. & Ma, Y.-Q. (1983), *Analysis methods for results in gamma-ray
 *    astronomy*, ApJ **272**, 317. DOI: 10.1086/161295. Equation (17) is the
 *    on/off significance implemented by li_ma_significance().
 *  - Press, W. H. et al., *Numerical Recipes*, 3rd ed., §6.2 — the `gser` /
 *    `gcf` series and continued-fraction split used by log_gamma_p().
 *  - Wichura, M. J. (1988), *Algorithm AS 241: The percentage points of the
 *    normal distribution*, Appl. Statist. **37**, 477 — the classical reference
 *    for the normal quantile inverted by log_p_to_sigma().
 *  - Ofek, E. O. & Zackay, B. (2018), *Optimal matched filter in the
 *    low-number-count Poisson noise regime*, A&A **616**, A170,
 *    arXiv:1709.01524 — background on why Gaussian filtering underperforms at
 *    low counts.
 */
#ifndef TTTRLIB_BURSTSIGNIFICANCE_H
#define TTTRLIB_BURSTSIGNIFICANCE_H

#include <cmath>
#include <cstdint>
#include <limits>

namespace tttrlib {

/// Two-pi, and 1/sqrt(2), spelled out because M_PI and M_SQRT1_2 are POSIX
/// rather than standard C++ and are absent from MSVC without _USE_MATH_DEFINES.
namespace detail {
constexpr double kPi = 3.14159265358979323846;
constexpr double kInvSqrt2 = 0.70710678118654752440;
/// Below this log-probability std::erfc() has underflowed to zero and the
/// Newton refinement in log_p_to_sigma() can no longer be evaluated.
constexpr double kErfcUnderflowLogP = -700.0;
}

/*!
 * \brief Natural log of the regularized lower incomplete gamma \f$P(a, x)\f$.
 *
 * Numerical Recipes §6.2: the ascending series converges quickly for
 * \f$x < a+1\f$, the Lentz continued fraction for \f$Q = 1 - P\f$ elsewhere.
 * Both are evaluated so that the huge prefactor
 * \f$e^{-x + a\ln x - \ln\Gamma(a)}\f$ stays in the exponent and never
 * materializes as a number — which is the whole point of returning a log.
 *
 * \param a shape, must be > 0.
 * \param x argument, must be >= 0.
 * \return \f$\ln P(a,x)\f$; \f$-\infty\f$ when \f$P\f$ underflows to zero.
 */
inline double log_gamma_p(double a, double x) {
    if (!(a > 0.0) || x < 0.0) return std::numeric_limits<double>::quiet_NaN();
    if (x == 0.0) return -std::numeric_limits<double>::infinity();

    // log of the common prefactor exp(-x + a*log(x) - lgamma(a)).
    const double log_prefactor = -x + a * std::log(x) - std::lgamma(a);

    if (x < a + 1.0) {
        // Ascending series: P(a,x) = prefactor * sum, sum = 1/a + x/(a(a+1)) + ...
        // `sum` is O(1/a) and strictly positive, so log() of it is safe.
        double ap = a;
        double del = 1.0 / a;
        double sum = del;
        for (int n = 0; n < 1000; ++n) {
            ap += 1.0;
            del *= x / ap;
            sum += del;
            if (std::fabs(del) < std::fabs(sum) * 1e-16) break;
        }
        return log_prefactor + std::log(sum);
    }

    // Modified Lentz continued fraction for Q(a,x), then P = 1 - Q.
    const double kTiny = 1e-300;
    double b = x + 1.0 - a;
    double c = 1.0 / kTiny;
    double d = 1.0 / b;
    double h = d;
    for (int i = 1; i < 1000; ++i) {
        const double an = -double(i) * (double(i) - a);
        b += 2.0;
        d = an * d + b;
        if (std::fabs(d) < kTiny) d = kTiny;
        c = b + an / c;
        if (std::fabs(c) < kTiny) c = kTiny;
        d = 1.0 / d;
        const double del = d * c;
        h *= del;
        if (std::fabs(del - 1.0) < 1e-16) break;
    }
    const double log_q = log_prefactor + std::log(h);
    if (log_q >= 0.0) return -std::numeric_limits<double>::infinity();  // Q ~ 1, P ~ 0
    // log(1 - exp(log_q)), written to keep precision when log_q is near 0 or very negative.
    return std::log(-std::expm1(log_q));
}

/*!
 * \brief \f$\ln P(K \ge k)\f$ for \f$K \sim \mathrm{Poisson}(\mu)\f$.
 *
 * Uses the exact identity \f$P(K \ge k\,|\,\mu) = P(k, \mu)\f$, the regularized
 * lower incomplete gamma. No normal approximation, no factorial, and no
 * summation over \f$k\f$ terms — so it is equally cheap and equally exact for
 * \f$k = 3\f$ and \f$k = 30000\f$.
 *
 * \param k observed counts (>= 0).
 * \param mu expected counts (> 0).
 * \return log of the upper-tail probability; 0 for k <= 0 (the event is certain).
 */
inline double log_poisson_upper_tail(int64_t k, double mu) {
    if (k <= 0) return 0.0;
    if (!(mu > 0.0)) return -std::numeric_limits<double>::infinity();
    return log_gamma_p(static_cast<double>(k), mu);
}

/*!
 * \brief Convert a log p-value to an equivalent one-sided Gaussian sigma.
 *
 * Solves \f$\tfrac{1}{2}\,\mathrm{erfc}(s/\sqrt2) = p\f$ for \f$s\f$. Sigma is
 * used purely as a familiar *unit* here — the underlying statistic is exact
 * Poisson, and this converts it to the scale users expect to set thresholds on.
 *
 * Two regimes. Above \ref detail::kErfcUnderflowLogP the equation is solved by
 * Newton iteration on \f$\ln\tfrac12\mathrm{erfc}\f$, which is smooth and
 * converges in a handful of steps. Below it `std::erfc` has underflowed to
 * zero, so the asymptotic inversion of \f$Q(s) \sim \phi(s)/s\f$,
 * \f$s^2 = 2L - \ln(2\pi s^2)\f$ with \f$L = -\ln p\f$, is used instead. That
 * branch is accurate to well under \f$10^{-3}\,\sigma\f$ at the handoff — and
 * the asymptotic form is also the *starting point* for the Newton branch, so
 * the two agree continuously across it rather than meeting at a seam.
 *
 * Negative sigma is returned for \f$p > 0.5\f$ (a deficit rather than an
 * excess), which is meaningful and must not be clamped away.
 *
 * \param log_p natural log of the upper-tail probability.
 * \return equivalent Gaussian sigma; 0 when log_p >= 0.
 */
inline double log_p_to_sigma(double log_p) {
    if (log_p >= 0.0) return 0.0;
    if (std::isnan(log_p)) return std::numeric_limits<double>::quiet_NaN();
    if (std::isinf(log_p)) return std::numeric_limits<double>::infinity();

    const double L = -log_p;

    // Starting guess. The asymptotic tail inversion is excellent for small p and
    // useless near p = 0.5, so only use it once it is in its domain.
    double s;
    if (L > 2.0) {
        double s2 = 2.0 * L;
        for (int i = 0; i < 8; ++i) {
            const double v = 2.0 * L - std::log(2.0 * detail::kPi * s2);
            s2 = (v > 1e-12) ? v : 1e-12;
        }
        s = std::sqrt(s2);
    } else {
        s = 0.0;
    }

    if (log_p < detail::kErfcUnderflowLogP) return s;  // erfc has underflowed

    // Newton on f(s) = log(0.5*erfc(s/sqrt2)) - log_p, with
    // d/ds log Q(s) = -phi(s)/Q(s).
    for (int i = 0; i < 8; ++i) {
        const double q = 0.5 * std::erfc(s * detail::kInvSqrt2);
        if (!(q > 0.0)) break;  // underflowed mid-iteration; keep what we have
        const double f = std::log(q) - log_p;
        const double phi = std::exp(-0.5 * s * s) / std::sqrt(2.0 * detail::kPi);
        const double deriv = -phi / q;
        if (!(std::fabs(deriv) > 0.0)) break;
        double step = f / deriv;
        // Damp so a bad start cannot throw the iterate into a flat region.
        if (step > 2.0) step = 2.0;
        if (step < -2.0) step = -2.0;
        s -= step;
        if (std::fabs(step) < 1e-12) break;
    }
    return s;
}

/*!
 * \brief Exact Poisson significance of \a k counts against \a mu expected.
 *
 * The drop-in replacement for \f$(k-\mu)/\sqrt{\mu}\f$. Use when the background
 * rate is known a priori; prefer li_ma_significance() when it was measured from
 * the data, which is the common case.
 */
inline double poisson_significance(int64_t k, double mu) {
    if (!(mu > 0.0)) return std::numeric_limits<double>::infinity();
    if (static_cast<double>(k) <= mu) {
        // Deficit or exactly at expectation: report the (negative) Gaussian
        // z-score rather than inverting an upper tail above 0.5, which is both
        // meaningless as a detection and numerically pointless.
        return (static_cast<double>(k) - mu) / std::sqrt(mu);
    }
    return log_p_to_sigma(log_poisson_upper_tail(k, mu));
}

/*!
 * \brief Li & Ma (1983) equation (17) on/off significance.
 *
 * The likelihood-ratio significance of \a n_on counts in the signal region
 * against \a n_off counts measured in a background region \f$1/\alpha\f$ times
 * longer. Unlike poisson_significance() this propagates the statistical error
 * of the *measured* background, so it does not overstate a detection when the
 * baseline itself is noisy.
 *
 * \f[
 *   S = \sqrt{2}\left\{ N_\mathrm{on}\ln\left[\frac{1+\alpha}{\alpha}
 *       \frac{N_\mathrm{on}}{N_\mathrm{on}+N_\mathrm{off}}\right]
 *     + N_\mathrm{off}\ln\left[(1+\alpha)
 *       \frac{N_\mathrm{off}}{N_\mathrm{on}+N_\mathrm{off}}\right]\right\}^{1/2}
 * \f]
 *
 * \param n_on counts in the burst.
 * \param n_off counts in the background region.
 * \param alpha exposure ratio \f$t_\mathrm{on}/t_\mathrm{off}\f$ (> 0).
 * \return significance in sigma, signed by \f$N_\mathrm{on} - \alpha
 *         N_\mathrm{off}\f$ so a deficit comes back negative.
 *
 * \see Li, T.-P. & Ma, Y.-Q. (1983), ApJ 272, 317, eq. (17).
 */
inline double li_ma_significance(double n_on, double n_off, double alpha) {
    if (!(alpha > 0.0)) return std::numeric_limits<double>::quiet_NaN();
    if (n_on < 0.0) n_on = 0.0;
    if (n_off < 0.0) n_off = 0.0;
    const double total = n_on + n_off;
    if (!(total > 0.0)) return 0.0;

    // Each term is x*log(...) with x -> 0 giving 0, so guard the logs rather
    // than the sum.
    double term_on = 0.0;
    if (n_on > 0.0) {
        term_on = n_on * std::log(((1.0 + alpha) / alpha) * (n_on / total));
    }
    double term_off = 0.0;
    if (n_off > 0.0) {
        term_off = n_off * std::log((1.0 + alpha) * (n_off / total));
    }
    double s2 = 2.0 * (term_on + term_off);
    if (s2 < 0.0) s2 = 0.0;  // rounding only; the expression is >= 0 analytically
    const double s = std::sqrt(s2);
    return (n_on - alpha * n_off >= 0.0) ? s : -s;
}

/*!
 * \brief How the trials factor is estimated for calibrated thresholds.
 */
enum class TrialsModel {
    /*! Number of non-overlapping rate windows, \f$N_\mathrm{photons}/m\f$.
     *  Conservative and stable, and the sane default: overlapping windows are
     *  strongly correlated, so counting them all would badly over-correct. */
    kIndependentWindows = 0,
    /*! The literal number of candidates that reached the significance test.
     *  Correct only for a *flat* candidate set. For the max-tree this
     *  over-counts severely, because its nodes are nested and therefore highly
     *  correlated — a single burst contributes a whole chain of them. */
    kTestedComponents = 1
};

/*!
 * \brief Estimate the number of independent trials a search performed.
 * \see TrialsModel for what each estimator assumes.
 */
inline double estimate_n_trials(TrialsModel model, int64_t n_photons, int m,
                                int64_t n_tested) {
    double n = 1.0;
    switch (model) {
        case TrialsModel::kTestedComponents:
            n = static_cast<double>(n_tested);
            break;
        case TrialsModel::kIndependentWindows:
        default:
            n = (m > 0) ? static_cast<double>(n_photons) / static_cast<double>(m)
                        : static_cast<double>(n_photons);
            break;
    }
    return (n >= 1.0) ? n : 1.0;
}

/*!
 * \brief Detection threshold in sigma for a target false-alarm rate.
 *
 * Astronomy quotes *post-trials* significance: a 4-sigma excursion is
 * unremarkable if you looked in a million places. This converts a rate the user
 * actually cares about — spurious bursts per second of acquisition — into the
 * per-candidate sigma threshold that delivers it, by dividing the tolerated
 * false-positive budget across the trials performed:
 * \f$p = f\,T_\mathrm{acq}/N_\mathrm{trials}\f$.
 *
 * The expected number of false bursts over the whole measurement is then
 * \f$f\,T_\mathrm{acq}\f$ regardless of how trials are counted, so the setting
 * transfers between a 10 s and a 1 h acquisition — which a bare sigma does not.
 *
 * \warning This is *approximately* calibrated. The exact trials factor for a
 *          scale-free, multi-level search is not analytically available;
 *          astronomy resolves this with background-only Monte Carlo. See
 *          estimate_n_trials() for the assumptions, and the background-only run
 *          in `examples/single_molecule/plot_burst_search_comparison.py` for the
 *          measured achieved rate.
 *
 * \param far_per_second tolerated spurious bursts per second (> 0).
 * \param acquisition_seconds total measurement duration (> 0).
 * \param n_trials number of independent tests (>= 1).
 * \return sigma threshold; 0 when the budget is so loose that any excess passes.
 */
inline double sigma_for_false_alarm_rate(double far_per_second,
                                         double acquisition_seconds,
                                         double n_trials) {
    if (!(far_per_second > 0.0) || !(acquisition_seconds > 0.0)) {
        return std::numeric_limits<double>::infinity();  // reject everything
    }
    if (!(n_trials >= 1.0)) n_trials = 1.0;
    const double log_p = std::log(far_per_second) + std::log(acquisition_seconds)
                       - std::log(n_trials);
    return log_p_to_sigma(log_p);
}

/*!
 * \brief Which significance statistic a burst search should apply.
 */
enum class SignificanceMode {
    /*! \f$(k-\mu)/\sqrt{\mu}\f$. Retained as the default so existing results
     *  are reproduced bit-exactly; unreliable at the low counts this library
     *  operates at. */
    kGaussian = 0,
    /*! Exact Poisson upper tail. Correct when the background rate is known. */
    kPoisson = 1,
    /*! Li & Ma (1983). Correct when the background was measured from the data,
     *  which is what a rolling-ball baseline gives us. */
    kLiMa = 2
};

}  // namespace tttrlib

#endif  // TTTRLIB_BURSTSIGNIFICANCE_H

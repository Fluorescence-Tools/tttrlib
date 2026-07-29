// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file DecayFitPrior.h
 * \brief Prior beliefs about a fitted parameter — including its bounds.
 *
 * **A bound is a prior.** A hard box \f$lb \le \theta \le ub\f$ is the
 * degenerate uniform prior: flat log-density inside the interval and
 * \f$-\infty\f$ outside. So a parameter needs one concept, not two, and a fit
 * that wants "positive, and probably near 2 ns" says so directly instead of
 * clamping at a boundary the optimiser then rails against.
 *
 * Two consumers read a prior, and both must see the same distribution:
 *
 *  - **Least-squares / Levenberg–Marquardt, giving MAP estimation.** A prior
 *    contributes extra *residuals* appended to the data residuals. Because the
 *    optimiser minimises a sum of squares, appending
 *    \f$r = \mathrm{sign}(\theta-\theta^*)\sqrt{-2[\ln p(\theta) - \ln p(\theta^*)]}\f$
 *    makes it minimise \f$\chi^2 - 2\ln p(\theta)\f$ — the negative
 *    log-posterior — with no change to the optimiser itself. For a Gaussian this
 *    reduces to the familiar \f$(\theta-\mu)/\sigma\f$. It is the same device
 *    the Poisson \f$2I^*\f$ deviance residuals already use.
 *  - **Posterior evaluation and sampling**, which reads `lnpdf` directly.
 *
 * Keeping those two in step is not hypothetical bookkeeping. In the Python
 * implementation these once disagreed — bounds suppressed the parameter priors
 * for the sampler while the optimiser still honoured them, so the two targeted
 * *different distributions* and only the sampler was wrong. One object serving
 * both paths is what prevents that.
 *
 * \par Deliberately a second implementation
 * These distributions also exist in Python, in chisurf's `fitting/priors.py`,
 * and that is not duplication to be removed: that module serves models this
 * library has never heard of, and one of its kinds is a live Python callback
 * that cannot cross into C++ at all. The two are pinned together instead by a
 * shared serialisation contract — the `kind` tag and state keys written by
 * `to_json()` here are byte-identical to the Python `get_state()` payload, so a
 * prior round-trips losslessly in both directions — and by a cross-language test
 * that evaluates every shared kind on a grid and asserts the two agree. Drift
 * fails a test rather than silently changing somebody's posterior.
 *
 * The `callable` kind is Python-only by nature and is reported as unsupported
 * rather than being silently dropped or approximated.
 */
#ifndef TTTRLIB_DECAYFITPRIOR_H
#define TTTRLIB_DECAYFITPRIOR_H

#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

using json = nlohmann::json;


/*!
 * \brief Log-density of one scalar parameter, and the bounds it implies.
 *
 * Subclasses supply `lnpdf`, `kind` and `to_json`; `mode`, `support` and
 * `residuals` have workable defaults that a distribution overrides when it has
 * a closed form (a Gaussian's residual is exactly \f$(x-\mu)/\sigma\f$, so it
 * says so rather than paying for the generic deviance square root).
 */
class DecayFitPrior {

public:

    virtual ~DecayFitPrior() = default;

    /*! Serialisation tag; must match the Python implementation's `kind`. */
    virtual const char *kind() const = 0;

    /*! Log prior density at \p x, or `-inf` outside the support. */
    virtual double lnpdf(double x) const = 0;

    /*!
     * Value maximising the density — the reference point of the generic
     * deviance residual. Defaults to 0 for distributions without a useful mode.
     */
    virtual double mode() const { return 0.0; }

    /*!
     * Hard `(lower, upper)` support, handed to the optimiser as box bounds.
     * Smooth unbounded priors return `(-inf, +inf)` and act through `residuals`
     * instead.
     */
    virtual std::pair<double, double> support() const {
        return {-std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::infinity()};
    }

    /*!
     * \brief Residual entries appended to the data residuals for MAP fitting.
     *
     * The generic form is the signed deviance
     * \f$\mathrm{sign}(x-\theta^*)\sqrt{-2[\ln p(x) - \ln p(\theta^*)]}\f$.
     * Outside the support it returns one large but *finite* penalty rather than
     * a NaN, so the optimiser is pushed back toward the feasible region instead
     * of losing its search direction entirely.
     */
    virtual std::vector<double> residuals(double x) const {
        const double lp = lnpdf(x);
        if (!std::isfinite(lp)) return {kOutsideSupportResidual};
        const double lp0 = lnpdf(mode());
        double dev = -2.0 * (lp - lp0);
        if (dev < 0.0) dev = 0.0;
        const double sign = (x >= mode()) ? 1.0 : -1.0;
        return {sign * std::sqrt(dev)};
    }

    /*! State dict, `kind` plus the distribution parameters. */
    virtual json to_json() const = 0;

    /*! Rebuild a prior from a `to_json()` / Python `get_state()` payload. */
    static std::shared_ptr<DecayFitPrior> from_json(const json &state);

    /*! Penalty standing in for an infinite residual outside the support. */
    static constexpr double kOutsideSupportResidual = 1e12;
};


/*!
 * \brief Flat inside `[lb, ub]`, impossible outside — a bound, expressed as a prior.
 *
 * Contributes no residual at all: the constraint is carried entirely by
 * `support()`, which the optimiser enforces as a hard box. Adding a residual
 * too would penalise the interior of a region the prior considers uniform.
 */
class UniformPrior : public DecayFitPrior {
    double lb_, ub_;

public:

    UniformPrior(double lb, double ub) : lb_(lb), ub_(ub) {
        if (ub_ < lb_) std::swap(lb_, ub_);
    }

    const char *kind() const override { return "uniform"; }

    double lnpdf(double x) const override {
        if (x < lb_ || x > ub_) return -std::numeric_limits<double>::infinity();
        const double width = ub_ - lb_;
        if (std::isfinite(width) && width > 0.0) return -std::log(width);
        return 0.0;
    }

    std::pair<double, double> support() const override { return {lb_, ub_}; }

    double mode() const override {
        if (std::isfinite(lb_) && std::isfinite(ub_)) return 0.5 * (lb_ + ub_);
        if (std::isfinite(lb_)) return lb_;
        if (std::isfinite(ub_)) return ub_;
        return 0.0;
    }

    /*! Empty — the box is enforced through `support()`. */
    std::vector<double> residuals(double) const override { return {}; }

    json to_json() const override {
        return json{{"kind", kind()}, {"lb", lb_}, {"ub", ub_}};
    }
};


/*! \brief Gaussian \f$\mathcal{N}(\mu, \sigma^2)\f$ — the Tikhonov/ridge prior. */
class NormalPrior : public DecayFitPrior {

protected:
    double mu_, sigma_;

public:

    NormalPrior(double mu, double sigma) : mu_(mu), sigma_(sigma) {
        if (!(sigma_ > 0.0)) throw std::invalid_argument("NormalPrior sigma must be > 0");
    }

    const char *kind() const override { return "normal"; }

    double lnpdf(double x) const override {
        const double z = (x - mu_) / sigma_;
        return -0.5 * z * z - std::log(sigma_) - 0.5 * std::log(2.0 * M_PI);
    }

    double mode() const override { return mu_; }

    /*! Exactly \f$(x-\mu)/\sigma\f$ — the closed form of the generic deviance. */
    std::vector<double> residuals(double x) const override {
        return {(x - mu_) / sigma_};
    }

    json to_json() const override {
        return json{{"kind", kind()}, {"mu", mu_}, {"sigma", sigma_}};
    }
};


/*!
 * \brief Gaussian restricted to `[lb, ub]`.
 *
 * Inherits the Gaussian residual: the truncation is carried by `support()`,
 * matching the Python implementation exactly.
 */
class TruncatedNormalPrior : public NormalPrior {
    double lb_, ub_;

public:

    TruncatedNormalPrior(double mu, double sigma, double lb, double ub)
        : NormalPrior(mu, sigma), lb_(lb), ub_(ub) {
        if (ub_ < lb_) std::swap(lb_, ub_);
    }

    const char *kind() const override { return "truncated_normal"; }

    double lnpdf(double x) const override {
        if (x < lb_ || x > ub_) return -std::numeric_limits<double>::infinity();
        return NormalPrior::lnpdf(x);
    }

    std::pair<double, double> support() const override { return {lb_, ub_}; }

    double mode() const override {
        double m = mu_;
        if (m < lb_) m = lb_;
        if (m > ub_) m = ub_;
        return m;
    }

    json to_json() const override {
        return json{{"kind", kind()}, {"mu", mu_}, {"sigma", sigma_},
                    {"lb", lb_}, {"ub", ub_}};
    }
};


/*! \brief Half-normal on \f$x \ge loc\f$ — a soft positivity prior. */
class HalfNormalPrior : public DecayFitPrior {
    double sigma_, loc_;

public:

    HalfNormalPrior(double sigma, double loc = 0.0) : sigma_(sigma), loc_(loc) {
        if (!(sigma_ > 0.0)) throw std::invalid_argument("HalfNormalPrior sigma must be > 0");
    }

    const char *kind() const override { return "half_normal"; }

    double lnpdf(double x) const override {
        if (x < loc_) return -std::numeric_limits<double>::infinity();
        const double z = (x - loc_) / sigma_;
        return -0.5 * z * z - std::log(sigma_) + 0.5 * std::log(2.0 / M_PI);
    }

    double mode() const override { return loc_; }

    std::pair<double, double> support() const override {
        return {loc_, std::numeric_limits<double>::infinity()};
    }

    std::vector<double> residuals(double x) const override {
        if (x < loc_) return {kOutsideSupportResidual};
        return {(x - loc_) / sigma_};
    }

    json to_json() const override {
        return json{{"kind", kind()}, {"sigma", sigma_}, {"loc", loc_}};
    }
};


/*!
 * \brief Log-normal on \f$x > 0\f$ — the natural prior for a rate or a lifetime.
 *
 * Scale-free in the sense that it treats a factor-of-two error the same whether
 * the parameter is 0.1 ns or 10 ns, which a Gaussian does not.
 */
class LogNormalPrior : public DecayFitPrior {
    double mu_, sigma_;

public:

    LogNormalPrior(double mu, double sigma) : mu_(mu), sigma_(sigma) {
        if (!(sigma_ > 0.0)) throw std::invalid_argument("LogNormalPrior sigma must be > 0");
    }

    const char *kind() const override { return "lognormal"; }

    double lnpdf(double x) const override {
        if (x <= 0.0) return -std::numeric_limits<double>::infinity();
        const double lx = std::log(x);
        const double z = (lx - mu_) / sigma_;
        return -0.5 * z * z - lx - std::log(sigma_) - 0.5 * std::log(2.0 * M_PI);
    }

    double mode() const override { return std::exp(mu_ - sigma_ * sigma_); }

    std::pair<double, double> support() const override {
        return {0.0, std::numeric_limits<double>::infinity()};
    }

    std::vector<double> residuals(double x) const override {
        if (x <= 0.0) return {kOutsideSupportResidual};
        return {(std::log(x) - mu_) / sigma_};
    }

    json to_json() const override {
        return json{{"kind", kind()}, {"mu", mu_}, {"sigma", sigma_}};
    }
};


/*! \brief Exponential on \f$x \ge loc\f$ — a decaying preference for small values. */
class ExponentialPrior : public DecayFitPrior {
    double scale_, loc_;

public:

    ExponentialPrior(double scale, double loc = 0.0) : scale_(scale), loc_(loc) {
        if (!(scale_ > 0.0)) throw std::invalid_argument("ExponentialPrior scale must be > 0");
    }

    const char *kind() const override { return "exponential"; }

    double lnpdf(double x) const override {
        if (x < loc_) return -std::numeric_limits<double>::infinity();
        return -(x - loc_) / scale_ - std::log(scale_);
    }

    double mode() const override { return loc_; }

    std::pair<double, double> support() const override {
        return {loc_, std::numeric_limits<double>::infinity()};
    }

    json to_json() const override {
        return json{{"kind", kind()}, {"scale", scale_}, {"loc", loc_}};
    }
};


/*! \brief Gamma with shape `alpha` and **rate** `beta`, shifted by `loc`. */
class GammaPrior : public DecayFitPrior {
    double alpha_, beta_, loc_;

public:

    GammaPrior(double alpha, double beta, double loc = 0.0)
        : alpha_(alpha), beta_(beta), loc_(loc) {
        if (!(alpha_ > 0.0 && beta_ > 0.0))
            throw std::invalid_argument("GammaPrior alpha and beta must be > 0");
    }

    const char *kind() const override { return "gamma"; }

    double lnpdf(double x) const override {
        const double t = x - loc_;
        if (t <= 0.0) return -std::numeric_limits<double>::infinity();
        return (alpha_ - 1.0) * std::log(t) - beta_ * t + alpha_ * std::log(beta_) -
               std::lgamma(alpha_);
    }

    double mode() const override {
        if (alpha_ >= 1.0) return loc_ + (alpha_ - 1.0) / beta_;
        return loc_;
    }

    std::pair<double, double> support() const override {
        return {loc_, std::numeric_limits<double>::infinity()};
    }

    json to_json() const override {
        return json{{"kind", kind()}, {"alpha", alpha_}, {"beta", beta_}, {"loc", loc_}};
    }
};


/*! \brief Beta on the open unit interval — for fractions and mixing ratios. */
class BetaPrior : public DecayFitPrior {
    double alpha_, beta_;

public:

    BetaPrior(double alpha, double beta) : alpha_(alpha), beta_(beta) {
        if (!(alpha_ > 0.0 && beta_ > 0.0))
            throw std::invalid_argument("BetaPrior alpha and beta must be > 0");
    }

    const char *kind() const override { return "beta"; }

    double lnpdf(double x) const override {
        if (x <= 0.0 || x >= 1.0) return -std::numeric_limits<double>::infinity();
        const double log_b =
            std::lgamma(alpha_) + std::lgamma(beta_) - std::lgamma(alpha_ + beta_);
        return (alpha_ - 1.0) * std::log(x) + (beta_ - 1.0) * std::log1p(-x) - log_b;
    }

    double mode() const override {
        if (alpha_ > 1.0 && beta_ > 1.0) return (alpha_ - 1.0) / (alpha_ + beta_ - 2.0);
        return 0.5;
    }

    std::pair<double, double> support() const override { return {0.0, 1.0}; }

    json to_json() const override {
        return json{{"kind", kind()}, {"alpha", alpha_}, {"beta", beta_}};
    }
};


/*!
 * \brief Product of independent priors on the same parameter.
 *
 * How two separate beliefs about one parameter are merged: the log density is
 * the sum, the support is the intersection, and the residuals are the
 * concatenation — so a parameter can be simultaneously bounded and softly
 * pulled toward a measured value.
 */
class ProductPrior : public DecayFitPrior {
    std::vector<std::shared_ptr<DecayFitPrior>> priors_;

public:

    explicit ProductPrior(std::vector<std::shared_ptr<DecayFitPrior>> priors)
        : priors_(std::move(priors)) {}

    const char *kind() const override { return "product"; }

    double lnpdf(double x) const override {
        double lp = 0.0;
        for (const auto &p : priors_) {
            if (!p) continue;
            const double v = p->lnpdf(x);
            if (!std::isfinite(v)) return -std::numeric_limits<double>::infinity();
            lp += v;
        }
        return lp;
    }

    std::pair<double, double> support() const override {
        double lo = -std::numeric_limits<double>::infinity();
        double hi = std::numeric_limits<double>::infinity();
        for (const auto &p : priors_) {
            if (!p) continue;
            const auto s = p->support();
            if (s.first > lo) lo = s.first;
            if (s.second < hi) hi = s.second;
        }
        return {lo, hi};
    }

    std::vector<double> residuals(double x) const override {
        std::vector<double> out;
        for (const auto &p : priors_) {
            if (!p) continue;
            const auto r = p->residuals(x);
            out.insert(out.end(), r.begin(), r.end());
        }
        return out;
    }

    const std::vector<std::shared_ptr<DecayFitPrior>> &components() const { return priors_; }

    json to_json() const override {
        json parts = json::array();
        for (const auto &p : priors_) {
            if (p) parts.push_back(p->to_json());
        }
        return json{{"kind", kind()}, {"priors", parts}};
    }
};


inline std::shared_ptr<DecayFitPrior> DecayFitPrior::from_json(const json &state) {
    if (state.is_null()) return nullptr;
    if (!state.contains("kind")) {
        throw std::invalid_argument("prior state has no 'kind' key");
    }
    const std::string kind = state.at("kind").get<std::string>();

    auto num = [&state](const char *key, double fallback) -> double {
        return state.contains(key) ? state.at(key).get<double>() : fallback;
    };
    const double inf = std::numeric_limits<double>::infinity();

    if (kind == "uniform") {
        return std::make_shared<UniformPrior>(num("lb", -inf), num("ub", inf));
    }
    if (kind == "normal") {
        return std::make_shared<NormalPrior>(num("mu", 0.0), num("sigma", 1.0));
    }
    if (kind == "truncated_normal") {
        return std::make_shared<TruncatedNormalPrior>(
            num("mu", 0.0), num("sigma", 1.0), num("lb", -inf), num("ub", inf));
    }
    if (kind == "half_normal") {
        return std::make_shared<HalfNormalPrior>(num("sigma", 1.0), num("loc", 0.0));
    }
    if (kind == "lognormal") {
        return std::make_shared<LogNormalPrior>(num("mu", 0.0), num("sigma", 1.0));
    }
    if (kind == "exponential") {
        return std::make_shared<ExponentialPrior>(num("scale", 1.0), num("loc", 0.0));
    }
    if (kind == "gamma") {
        return std::make_shared<GammaPrior>(num("alpha", 1.0), num("beta", 1.0), num("loc", 0.0));
    }
    if (kind == "beta") {
        return std::make_shared<BetaPrior>(num("alpha", 1.0), num("beta", 1.0));
    }
    if (kind == "product") {
        std::vector<std::shared_ptr<DecayFitPrior>> parts;
        if (state.contains("priors")) {
            for (const auto &sub : state.at("priors")) parts.push_back(from_json(sub));
        }
        return std::make_shared<ProductPrior>(std::move(parts));
    }
    if (kind == "callable") {
        // A live Python callback. Reported rather than approximated: silently
        // substituting a flat prior would change the posterior without saying so.
        throw std::invalid_argument(
            "prior kind 'callable' is a Python callback and cannot be evaluated in C++; "
            "replace it with an analytic kind to use it in a native fit");
    }
    throw std::invalid_argument("unknown prior kind '" + kind + "'");
}

#endif // TTTRLIB_DECAYFITPRIOR_H

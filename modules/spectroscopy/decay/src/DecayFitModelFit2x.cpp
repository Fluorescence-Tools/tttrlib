// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file DecayFitModelFit2x.cpp
 * \brief The Fit2x estimators, presented through the common fit interface.
 *
 * Each class here is a thin wrapper: it decodes the flat, registry-described
 * setup vector, packs the caller's parameters into the layout the numerical
 * kernel expects, runs the kernel unchanged, and unpacks the results into the
 * flat vector `results_schema` describes.
 *
 * That packing is the whole point. The kernels use one array holding optimised
 * parameters, two setup flags and two outputs together — a workable convention
 * inside a function, and a persistent hazard at an interface, where callers had
 * to know that a "4-parameter" model wanted an 8-element vector. Confining it to
 * these wrappers means the hazard exists in one place that is read together with
 * the schema it implements, instead of in every caller.
 *
 * The wrappers are immutable and hold no fitting state: the kernels keep theirs
 * in `thread_local` storage, so one model instance is safe to share across every
 * worker of a batch.
 */
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "DecayFit23.h"
#include "DecayFit24.h"
#include "DecayFit25.h"
#include "DecayFit26.h"
#include "DecayFitModel.h"

namespace {

/*!
 * \brief Slots of the shared `fit2x` setup block.
 *
 * Must match the declaration order of `fit_setup/fit2x` in FitRegistry.cpp,
 * which is what the flattening rule makes normative. A test compares the two so
 * a reordering of the JSON cannot silently mean something else here.
 */
enum Fit2xSetup {
    kDt = 0,
    kPeriod,
    kGFactor,
    kL1,
    kL2,
    kConvolutionStop,
    kSoftBifl,
    kObjective,
    kFitStart,
    kFitStop,
    kFit2xSetupSize
};

/*! Objective indices, in the declared order of the `objective` category. */
enum Objective { kPoissonMle = 0, kP2sMle, kNeymanLsq, kGehrelsLsq };

/*! Read setup slot \p slot, or \p fallback when the vector is short. */
double setup_at(const std::vector<double> &setup, int slot, double fallback) {
    if (slot < 0 || static_cast<std::size_t>(slot) >= setup.size()) return fallback;
    const double v = setup[static_cast<std::size_t>(slot)];
    return std::isfinite(v) ? v : fallback;
}


/*!
 * \brief What the four Fit2x estimators share.
 *
 * Holds the immutable setup and builds the `[period, g, l1, l2,
 * convolution_stop]` corrections array the kernels take, once, at construction.
 */
class Fit2xModelBase : public DecayFitModel {

protected:
    std::vector<double> setup_;
    std::vector<double> corrections_;   // as the kernels expect them
    double bifl_flag_;                  // -1 discounts background photons, else 0
    double p2s_flag_;                   // 1 scores the P+2S sum
    int objective_ = kPoissonMle;       // registry objective, handed to the kernels via the context

public:

    Fit2xModelBase(const std::vector<double> &setup, const std::vector<double> & /*irf*/)
        : setup_(setup) {
        const double objective = setup_at(setup_, kObjective, kPoissonMle);
        corrections_ = {
            setup_at(setup_, kPeriod, 0.0),
            setup_at(setup_, kGFactor, 1.0),
            setup_at(setup_, kL1, 0.0),
            setup_at(setup_, kL2, 0.0),
            setup_at(setup_, kConvolutionStop, -1.0),
        };
        // The kernels read these two as sign/magnitude flags out of the packed
        // parameter vector; the interface carries them as named setup instead.
        bifl_flag_ = setup_at(setup_, kSoftBifl, 1.0) != 0.0 ? -1.0 : 0.0;
        p2s_flag_ = (static_cast<int>(objective) == kP2sMle) ? 1.0 : 0.0;
        objective_ = static_cast<int>(objective);
        if (objective_ < kPoissonMle || objective_ > kGehrelsLsq) objective_ = kPoissonMle;
    }

    /*!
     * \brief Fill \p ctx and the integer counts for one evaluation.
     *
     * The counts conversion happens here, once per fit, rather than in the
     * statistics: `DecayFitProblem` stores `double` because pooled or rebinned
     * data is not integral, while the Poisson statistics index a factorial table
     * by count. Converting at the boundary keeps both true.
     */
    void bind(DecayFitProblem &problem, std::vector<int> &counts,
              std::vector<double> &corrections, DecayFitContext &ctx) const {
        problem.require_valid();
        ctx.objective = objective_;   // 0 poisson, 1 p2s, 2 neyman, 3 gehrels
        init_fact();   // idempotent (call_once); wcm_p2s needs the table
        const std::size_t n = problem.data.size();
        counts.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            counts[i] = static_cast<int>(problem.data[i]);
        }
        if (problem.model.size() != n) problem.model.assign(n, 0.0);

        // `convolution_stop = -1` means "to the end of the IRF". The kernels
        // never understood that sentinel — the Python constructor resolved it
        // before they saw it — and an unresolved -1 stops the convolution
        // immediately, yielding an identically zero model rather than an error.
        // Resolving it here is what makes -1 mean the same thing to every caller.
        corrections = corrections_;
        if (corrections.size() > 4 && corrections[4] < 0.0) {
            corrections[4] = static_cast<double>(problem.n_bins - 1);
        }

        ctx.problem = &problem;
        ctx.counts = counts.data();
        ctx.corrections = corrections.data();
        // The kernels' "Nchannels" is bins per polarization, which is exactly
        // n_bins; the old container inferred it as size()/2 and so only ever
        // worked for two-channel data.
        ctx.n_bins = problem.n_bins;
        ctx.dt = setup_at(setup_, kDt, problem.dt);
        problem.dt = ctx.dt;
    }

    /*!
     * \brief Hard bounds per parameter, from the priors on the constraints.
     *
     * A bound *is* a uniform prior here, so the box the optimiser sees is the
     * prior's support and there is no second bounds concept that could disagree
     * with it. Returns false when no prior bounds anything, so the caller can
     * leave the context's pointers null and the kernels skip the work entirely.
     */
    static bool bounds_from_priors(const DecayFitConstraints &constraints,
                                   int n_params,
                                   std::vector<double> &lower,
                                   std::vector<double> &upper) {
        const double inf = std::numeric_limits<double>::infinity();
        lower.assign(static_cast<std::size_t>(n_params), -inf);
        upper.assign(static_cast<std::size_t>(n_params), inf);
        bool any = false;
        for (int i = 0; i < n_params; ++i) {
            const auto prior = constraints.prior(i);
            if (!prior) continue;
            const auto support = prior->support();
            lower[static_cast<std::size_t>(i)] = support.first;
            upper[static_cast<std::size_t>(i)] = support.second;
            if (std::isfinite(support.first) || std::isfinite(support.second)) any = true;
        }
        return any;
    }

    /*! Point \p ctx at \p lower / \p upper when any prior actually bounds. */
    static void bind_bounds(const DecayFitConstraints &constraints, int n_params,
                            std::vector<double> &lower, std::vector<double> &upper,
                            DecayFitContext &ctx) {
        if (bounds_from_priors(constraints, n_params, lower, upper)) {
            ctx.lower = lower.data();
            ctx.upper = upper.data();
        }
    }

    /*! Legacy `fixed` mask for the kernels, derived from the link vector. */
    static std::vector<short> fixed_mask(const DecayFitConstraints &constraints,
                                         int n_params) {
        std::vector<short> fixed(static_cast<std::size_t>(n_params), 0);
        for (int i = 0; i < n_params; ++i) {
            // A linked slot is not free to this kernel: the shared value is
            // driven from outside, so the kernel must hold it where it is.
            fixed[static_cast<std::size_t>(i)] = constraints.code(i) != 0 ? 1 : 0;
        }
        return fixed;
    }

    /*! `corrections` for \p problem, with the -1 sentinel resolved. */
    std::vector<double> corrections_for(const DecayFitProblem &problem) const {
        std::vector<double> corrections = corrections_;
        if (corrections.size() > 4 && corrections[4] < 0.0) {
            corrections[4] = static_cast<double>(problem.n_bins - 1);
        }
        return corrections;
    }

    bool supports_lnprob() const override { return true; }
};


/*! \brief One lifetime plus a time-resolved anisotropy. */
class Fit23Model : public Fit2xModelBase {
public:
    using Fit2xModelBase::Fit2xModelBase;

    const char *name() const override { return "fit23"; }
    int n_parameters(const DecayFitProblem &) const override { return 4; }
    int n_results(const DecayFitProblem &) const override { return 5; }

    double evaluate(const double *x, DecayFitProblem &problem) const override {
        std::vector<int> counts;
        std::vector<double> corrections;
        DecayFitContext ctx;
        bind(problem, counts, corrections, ctx);
        double packed[8] = {x[0], x[1], x[2], x[3], bifl_flag_, p2s_flag_, 0.0, 0.0};
        // Everything held: an evaluation asks for the score *at these values*,
        // so nothing may be re-derived from the data.
        short fixed[4] = {1, 1, 1, 1};
        return DecayFit23::evaluate(packed, fixed, &ctx);
    }

    bool model_curve(const double *x, const DecayFitProblem &problem,
                     double *curve) const override {
        if (problem.irf.empty() || problem.background.empty()) return false;
        std::vector<double> corrections = corrections_for(problem);
        // The parameters go to the model as given: `correct_input` re-derives rho
        // from integrated signals, which do not exist without data.
        double param[4] = {x[0], x[1], x[2], x[3]};
        DecayFit23::modelf(param,
                           const_cast<double *>(problem.irf.data()),
                           const_cast<double *>(problem.background.data()),
                           problem.n_bins, problem.dt, corrections.data(), curve);
        return true;
    }

    double fit(double *x, const DecayFitConstraints &constraints,
               DecayFitProblem &problem, double *results) const override {
        std::vector<int> counts;
        std::vector<double> corrections;
        DecayFitContext ctx;
        bind(problem, counts, corrections, ctx);

        double packed[8] = {x[0], x[1], x[2], x[3], bifl_flag_, p2s_flag_, 0.0, 0.0};
        std::vector<short> fixed = fixed_mask(constraints, 4);
        std::vector<double> lower, upper;
        bind_bounds(constraints, 4, lower, upper, ctx);

        double two_istar;
        double row[7] = {0, 0, 0, 0, 0, 0, 0};
        // The unpolarized tau-only specialisation is an internal optimisation,
        // not a separate entry point: a caller asks for a fit and gets whichever
        // path applies.
        if (DecayFit23::fit_tau_only_unpolarized_row(
                problem.data.data(), static_cast<int>(problem.data.size()),
                packed, 4, fixed.data(), static_cast<int>(fixed.size()),
                bifl_flag_, p2s_flag_, &ctx, row, 7, true)) {
            for (int i = 0; i < 4; ++i) x[i] = row[i];
            two_istar = row[4];
            if (results != nullptr) {
                results[0] = two_istar;
                results[1] = (x[0] >= 0.0) ? 1.0 : 0.0;
                results[2] = 0.0;      // the closed-form path does not iterate
                results[3] = row[5];
                results[4] = row[6];
            }
            return two_istar;
        }

        two_istar = DecayFit23::fit(packed, fixed.data(), &ctx);
        for (int i = 0; i < 4; ++i) x[i] = packed[i];
        if (results != nullptr) {
            results[0] = two_istar;
            results[1] = (packed[0] >= 0.0) ? 1.0 : 0.0;
            results[2] = static_cast<double>(ctx.iterations);
            results[3] = packed[6];
            results[4] = packed[7];
        }
        return two_istar;
    }
};


/*! \brief Two lifetimes with a mixing fraction, scatter and an offset. */
class Fit24Model : public Fit2xModelBase {
public:
    using Fit2xModelBase::Fit2xModelBase;

    const char *name() const override { return "fit24"; }
    int n_parameters(const DecayFitProblem &) const override { return 5; }
    int n_results(const DecayFitProblem &) const override { return 5; }

    double evaluate(const double *x, DecayFitProblem &problem) const override {
        std::vector<int> counts;
        std::vector<double> corrections;
        DecayFitContext ctx;
        bind(problem, counts, corrections, ctx);
        double packed[8] = {x[0], x[1], x[2], x[3], x[4], bifl_flag_, 0.0, 0.0};
        short fixed[5] = {1, 1, 1, 1, 1};
        return DecayFit24::evaluate(packed, fixed, &ctx);
    }

    bool model_curve(const double *x, const DecayFitProblem &problem,
                     double *curve) const override {
        if (problem.irf.empty() || problem.background.empty()) return false;
        std::vector<double> corrections = corrections_for(problem);
        double param[5] = {x[0], x[1], x[2], x[3], x[4]};
        DecayFit24::modelf(param,
                           const_cast<double *>(problem.irf.data()),
                           const_cast<double *>(problem.background.data()),
                           problem.n_bins, problem.dt, corrections.data(), curve);
        return true;
    }

    double fit(double *x, const DecayFitConstraints &constraints,
               DecayFitProblem &problem, double *results) const override {
        std::vector<int> counts;
        std::vector<double> corrections;
        DecayFitContext ctx;
        bind(problem, counts, corrections, ctx);
        double packed[8] = {x[0], x[1], x[2], x[3], x[4], bifl_flag_, 0.0, 0.0};
        std::vector<short> fixed = fixed_mask(constraints, 5);
        std::vector<double> lower, upper;
        bind_bounds(constraints, 5, lower, upper, ctx);
        const double two_istar = DecayFit24::fit(packed, fixed.data(), &ctx);
        for (int i = 0; i < 5; ++i) x[i] = packed[i];
        if (results != nullptr) {
            results[0] = two_istar;
            results[1] = (packed[0] >= 0.0) ? 1.0 : 0.0;
            results[2] = static_cast<double>(ctx.iterations);
            results[3] = packed[6];
            results[4] = packed[7];
        }
        return two_istar;
    }
};


/*! \brief Picks whichever of four fixed lifetimes best describes the decay. */
class Fit25Model : public Fit2xModelBase {
public:
    using Fit2xModelBase::Fit2xModelBase;

    const char *name() const override { return "fit25"; }
    int n_parameters(const DecayFitProblem &) const override { return 6; }
    int n_results(const DecayFitProblem &) const override { return 6; }

    double evaluate(const double *x, DecayFitProblem &problem) const override {
        std::vector<int> counts;
        std::vector<double> corrections;
        DecayFitContext ctx;
        bind(problem, counts, corrections, ctx);
        double packed[9] = {x[0], x[1], x[2], x[3], x[4], x[5], bifl_flag_, 0.0, 0.0};
        short fixed[6] = {1, 1, 1, 1, 1, 1};
        return DecayFit25::evaluate(packed, fixed, &ctx);
    }

    double fit(double *x, const DecayFitConstraints &constraints,
               DecayFitProblem &problem, double *results) const override {
        std::vector<int> counts;
        std::vector<double> corrections;
        DecayFitContext ctx;
        bind(problem, counts, corrections, ctx);
        const double candidates[4] = {x[0], x[1], x[2], x[3]};
        double packed[9] = {x[0], x[1], x[2], x[3], x[4], x[5], bifl_flag_, 0.0, 0.0};
        std::vector<short> fixed = fixed_mask(constraints, 6);
        std::vector<double> lower, upper;
        bind_bounds(constraints, 6, lower, upper, ctx);
        const double two_istar = DecayFit25::fit(packed, fixed.data(), &ctx);
        for (int i = 0; i < 6; ++i) x[i] = packed[i];
        if (results != nullptr) {
            results[0] = two_istar;
            results[1] = (packed[0] >= 0.0) ? 1.0 : 0.0;
            results[2] = static_cast<double>(ctx.iterations);
            // This model classifies rather than measures, so which candidate won
            // is the answer. It used to be recoverable only by comparing the
            // returned lifetime back against the four inputs.
            results[3] = -1.0;
            for (int i = 0; i < 4; ++i) {
                if (candidates[i] == packed[0]) { results[3] = i; break; }
            }
            results[4] = packed[7];
            results[5] = packed[8];
        }
        return two_istar;
    }
};


/*! \brief Mixing fraction between two fixed reference patterns. */
class Fit26Model : public Fit2xModelBase {
public:
    using Fit2xModelBase::Fit2xModelBase;

    const char *name() const override { return "fit26"; }
    int n_parameters(const DecayFitProblem &) const override { return 1; }
    int n_results(const DecayFitProblem &) const override { return 3; }

    double evaluate(const double *x, DecayFitProblem &problem) const override {
        std::vector<int> counts;
        std::vector<double> corrections;
        DecayFitContext ctx;
        bind(problem, counts, corrections, ctx);
        double packed[2] = {x[0], 0.0};
        short fixed[1] = {1};
        return DecayFit26::evaluate(packed, fixed, &ctx);
    }

    bool model_curve(const double *x, const DecayFitProblem &problem,
                     double *curve) const override {
        if (problem.irf.empty() || problem.background.empty()) return false;
        // This model *is* a mixture of the two reference patterns, which the
        // problem already carries as irf and background.
        const double f = x[0];
        const std::size_t n = problem.total_size();
        for (std::size_t i = 0; i < n; ++i) {
            curve[i] = f * problem.irf[i] + (1.0 - f) * problem.background[i];
        }
        return true;
    }

    double fit(double *x, const DecayFitConstraints &constraints,
               DecayFitProblem &problem, double *results) const override {
        std::vector<int> counts;
        std::vector<double> corrections;
        DecayFitContext ctx;
        bind(problem, counts, corrections, ctx);
        double packed[2] = {x[0], 0.0};
        std::vector<short> fixed = fixed_mask(constraints, 1);
        std::vector<double> lower, upper;
        bind_bounds(constraints, 1, lower, upper, ctx);
        const double two_istar = DecayFit26::fit(packed, fixed.data(), &ctx);
        x[0] = packed[0];
        if (results != nullptr) {
            results[0] = two_istar;
            results[1] = std::isfinite(two_istar) ? 1.0 : 0.0;
            results[2] = static_cast<double>(ctx.iterations);
        }
        return two_istar;
    }
};


}  // namespace


/*!
 * \brief Register the Fit2x family.
 *
 * Named and called explicitly rather than run from a static initialiser: an
 * unreferenced initialiser lets the linker drop this whole object file out of
 * libtttrlib_static.a. See src/DecayFitModelRegistration.h.
 */
void register_decay_fit_models_fit2x() {
    register_decay_fit("fit23", [](const std::vector<double> &s, const std::vector<double> &irf) {
        return std::make_shared<const Fit23Model>(s, irf);
    });
    register_decay_fit("fit24", [](const std::vector<double> &s, const std::vector<double> &irf) {
        return std::make_shared<const Fit24Model>(s, irf);
    });
    register_decay_fit("fit25", [](const std::vector<double> &s, const std::vector<double> &irf) {
        return std::make_shared<const Fit25Model>(s, irf);
    });
    register_decay_fit("fit26", [](const std::vector<double> &s, const std::vector<double> &irf) {
        return std::make_shared<const Fit26Model>(s, irf);
    });
}

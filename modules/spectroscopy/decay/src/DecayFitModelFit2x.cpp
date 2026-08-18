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

#include "Registry.h"

#include "DecayFit23.h"
#include "DecayFit24.h"
#include "DecayFit25.h"
#include "DecayFit26.h"
#include "DecayFitModel.h"

namespace {

/*!
 * \brief Slots of the shared `fit2x` setup block.
 *
 * Must match the declaration order of the `fit_setup/fit2x` entry registered
 * at the end of this file (kFit2xEntry),
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
namespace {

// What registry("fit") / registry("fit_setup") say about this family. Declared
// here, next to the models that implement it, and registered through the one
// registry (register_algorithm_json) -- there is no registry literal to keep
// in step with the code any more. The parameter order in `params_schema` IS
// the flat initial_values layout (DecayFitSetup.cpp derives it from here), so
// these blocks are normative, not documentation.
const char* const kFit23Entry = R"JSON({
  "name": "fit23",
  "operation_type": "tcspc_fitting",
  "api": ["DecayFit2", "DecayFit23", "Fit23"],
  "n_patterns": 0,
  "label": "Single lifetime + anisotropy (Fit23)",
  "summary": "Poisson MLE of one fluorescence lifetime with time-resolved anisotropy.",
  "description": "The single-molecule burst-MLE workhorse. Fits one lifetime to a polarization-resolved (Jordi VV|VH) decay by maximum likelihood, jointly modelling the anisotropy decay so the parallel and perpendicular channels are described together. Scatter (gamma) and anisotropy (r0, rho) are barely identifiable from a short burst decay against an auto-extracted background, so only tau is free by default; free the others when a measured IRF/background makes them meaningful. tau, gamma, r0, rho map to initial_values in that order.",
  "setup": {
    "category": "fit_setup",
    "name": "fit2x"
  },
  "params_schema": {
    "type": "object",
    "required": [
      "tau",
      "gamma",
      "r0",
      "rho"
    ],
    "properties": {
      "tau": {
        "type": "number",
        "title": "Lifetime tau (ns)",
        "default": 2.0,
        "minimum": 0.01,
        "maximum": 20.0,
        "unit": "ns",
        "fixed_default": false,
        "description": "Fluorescence lifetime. The one parameter fit by default."
      },
      "gamma": {
        "type": "number",
        "title": "Scatter fraction",
        "default": 0.1,
        "minimum": 0.0,
        "maximum": 1.0,
        "fixed_default": true,
        "description": "Fraction of the signal explained by the background/scatter pattern rather than the decay. Not identifiable against an auto-extracted (scatter-shaped) background, so fixed by default."
      },
      "r0": {
        "type": "number",
        "title": "Fundamental anisotropy r0",
        "default": 0.38,
        "minimum": -0.2,
        "maximum": 0.4,
        "fixed_default": true,
        "description": "Anisotropy at time zero, set by the dye's absorption/emission dipole angle (0.4 for parallel dipoles). A known photophysical constant, so fixed by default."
      },
      "rho": {
        "type": "number",
        "title": "Rotational correlation time rho (ns)",
        "default": 1.22,
        "minimum": 0.01,
        "maximum": 100.0,
        "unit": "ns",
        "fixed_default": true,
        "description": "Rotational correlation time of the anisotropy decay. Left free against a short burst decay it rails to zero, so fixed by default; free it with a measured IRF/background to fit anisotropy."
      }
    }
  },
  "supports_lnprob": true,
  "supports_gradient": false,
  "results_schema": {
    "type": "object",
    "properties": {
      "twoIstar": {
        "type": "number",
        "title": "2I*",
        "description": "Goodness of the optimised fit: -2 ln(L(C|M)/L(C|C)), the Poisson deviance against a perfectly fitting model. Around 1 for a good fit to counting data; large values mean the model cannot describe the decay."
      },
      "converged": {
        "type": "boolean",
        "title": "Converged",
        "description": "Whether the optimiser reached its tolerance rather than stopping on the iteration limit. Carried as 0.0 or 1.0 in the flat result vector."
      },
      "iterations": {
        "type": "integer",
        "title": "Iterations",
        "description": "Objective evaluations the optimiser needed. A row that ran to the limit is worth looking at even when its 2I* looks acceptable."
      },
      "r_scatter": {
        "type": "number",
        "title": "Anisotropy (scatter-corrected)",
        "description": "Steady-state anisotropy computed from the integrated signals after removing the background/scatter contribution."
      },
      "r_experimental": {
        "type": "number",
        "title": "Anisotropy (uncorrected)",
        "description": "Steady-state anisotropy computed from the raw integrated signals, without background subtraction."
      }
    }
  }
})JSON";

const char* const kFit24Entry = R"JSON({
  "name": "fit24",
  "operation_type": "tcspc_fitting",
  "api": ["DecayFit24", "Fit24"],
  "n_patterns": 0,
  "label": "Bi-exponential (Fit24)",
  "summary": "Poisson MLE of two lifetimes with a mixing fraction, scatter and a constant offset.",
  "description": "Two-lifetime maximum-likelihood fit for a decay that a single exponential cannot describe — a mixture of two states (e.g. FRET and no-FRET donor populations) with distinct lifetimes. The second component's amplitude fraction A2, a scatter fraction gamma and a constant offset are fit alongside the two lifetimes. Parameters map to initial_values as [tau1, gamma, tau2, A2, offset].",
  "setup": {
    "category": "fit_setup",
    "name": "fit2x"
  },
  "params_schema": {
    "type": "object",
    "required": [
      "tau1",
      "gamma",
      "tau2",
      "A2",
      "offset"
    ],
    "properties": {
      "tau1": {
        "type": "number",
        "title": "Lifetime 1 tau1 (ns)",
        "default": 1.0,
        "minimum": 0.01,
        "maximum": 20.0,
        "unit": "ns",
        "fixed_default": false,
        "description": "First (shorter) fluorescence lifetime."
      },
      "gamma": {
        "type": "number",
        "title": "Scatter fraction",
        "default": 0.0,
        "minimum": 0.0,
        "maximum": 1.0,
        "fixed_default": false,
        "description": "Fraction of the signal explained by the background/scatter pattern."
      },
      "tau2": {
        "type": "number",
        "title": "Lifetime 2 tau2 (ns)",
        "default": 4.0,
        "minimum": 0.01,
        "maximum": 20.0,
        "unit": "ns",
        "fixed_default": false,
        "description": "Second (longer) fluorescence lifetime."
      },
      "A2": {
        "type": "number",
        "title": "Fraction of tau2",
        "default": 0.5,
        "minimum": 0.0,
        "maximum": 1.0,
        "fixed_default": false,
        "description": "Amplitude fraction of the second lifetime component."
      },
      "offset": {
        "type": "number",
        "title": "Constant offset",
        "default": 0.0,
        "minimum": 0.0,
        "maximum": 1000000.0,
        "fixed_default": true,
        "description": "Flat baseline added to every channel (uncorrelated dark counts)."
      }
    }
  },
  "supports_lnprob": true,
  "supports_gradient": false,
  "results_schema": {
    "type": "object",
    "properties": {
      "twoIstar": {
        "type": "number",
        "title": "2I*",
        "description": "Poisson deviance of the optimised fit against a perfectly fitting model."
      },
      "converged": {
        "type": "boolean",
        "title": "Converged",
        "description": "Whether the optimiser reached its tolerance. Carried as 0.0 or 1.0."
      },
      "iterations": {
        "type": "integer",
        "title": "Iterations",
        "description": "Objective evaluations the optimiser needed."
      },
      "r_scatter": {
        "type": "number",
        "title": "Anisotropy (scatter-corrected)",
        "description": "Steady-state anisotropy from the integrated signals with the background/scatter contribution removed."
      },
      "r_experimental": {
        "type": "number",
        "title": "Anisotropy (uncorrected)",
        "description": "Steady-state anisotropy from the raw integrated signals."
      }
    }
  }
})JSON";

const char* const kFit25Entry = R"JSON({
  "name": "fit25",
  "operation_type": "tcspc_fitting",
  "api": ["DecayFit25", "Fit25"],
  "n_patterns": 0,
  "label": "Best of four fixed lifetimes (Fit25)",
  "summary": "Selects which of four fixed lifetimes best describes the decay.",
  "description": "A discrete selection rather than a continuous fit: the four lifetimes are held fixed, each is scored against the data, and the one best describing the decay is returned (with its scatter fraction). Useful when the sample is known to occupy one of a few discrete states and the goal is to classify rather than to measure a continuous lifetime. Parameters map to initial_values as [tau1, tau2, tau3, tau4, gamma, r0]; the four lifetimes are always fixed.",
  "setup": {
    "category": "fit_setup",
    "name": "fit2x"
  },
  "params_schema": {
    "type": "object",
    "required": [
      "tau1",
      "tau2",
      "tau3",
      "tau4",
      "gamma",
      "r0"
    ],
    "properties": {
      "tau1": {
        "type": "number",
        "title": "Candidate lifetime 1 (ns)",
        "default": 0.5,
        "minimum": 0.01,
        "maximum": 20.0,
        "unit": "ns",
        "fixed_default": true,
        "description": "First candidate lifetime (always fixed)."
      },
      "tau2": {
        "type": "number",
        "title": "Candidate lifetime 2 (ns)",
        "default": 1.5,
        "minimum": 0.01,
        "maximum": 20.0,
        "unit": "ns",
        "fixed_default": true,
        "description": "Second candidate lifetime (always fixed)."
      },
      "tau3": {
        "type": "number",
        "title": "Candidate lifetime 3 (ns)",
        "default": 2.5,
        "minimum": 0.01,
        "maximum": 20.0,
        "unit": "ns",
        "fixed_default": true,
        "description": "Third candidate lifetime (always fixed)."
      },
      "tau4": {
        "type": "number",
        "title": "Candidate lifetime 4 (ns)",
        "default": 4.0,
        "minimum": 0.01,
        "maximum": 20.0,
        "unit": "ns",
        "fixed_default": true,
        "description": "Fourth candidate lifetime (always fixed)."
      },
      "gamma": {
        "type": "number",
        "title": "Scatter fraction",
        "default": 0.0,
        "minimum": 0.0,
        "maximum": 1.0,
        "fixed_default": false,
        "description": "Fraction of the signal explained by the background/scatter pattern."
      },
      "r0": {
        "type": "number",
        "title": "Fundamental anisotropy r0",
        "default": 0.38,
        "minimum": 0.0,
        "maximum": 0.4,
        "fixed_default": true,
        "description": "Fundamental anisotropy (fixed input; part of the initial_values vector)."
      }
    }
  },
  "supports_lnprob": true,
  "supports_gradient": false,
  "results_schema": {
    "type": "object",
    "properties": {
      "twoIstar": {
        "type": "number",
        "title": "2I*",
        "description": "Poisson deviance of the winning candidate against a perfectly fitting model."
      },
      "converged": {
        "type": "boolean",
        "title": "Converged",
        "description": "Whether the scatter optimisation for the winning candidate reached its tolerance. Carried as 0.0 or 1.0."
      },
      "iterations": {
        "type": "integer",
        "title": "Iterations",
        "description": "Objective evaluations summed over the candidates scored."
      },
      "selected_index": {
        "type": "integer",
        "title": "Winning candidate",
        "description": "Which of the four candidate lifetimes best described the decay, as a 0-based index. This model classifies rather than measures, so the index is the answer; previously it could only be recovered by comparing the returned lifetime against the four inputs."
      },
      "r_scatter": {
        "type": "number",
        "title": "Anisotropy (scatter-corrected)",
        "description": "Steady-state anisotropy with the background/scatter contribution removed."
      },
      "r_experimental": {
        "type": "number",
        "title": "Anisotropy (uncorrected)",
        "description": "Steady-state anisotropy from the raw integrated signals."
      }
    }
  }
})JSON";

const char* const kFit26Entry = R"JSON({
  "name": "fit26",
  "operation_type": "tcspc_fitting",
  "api": ["DecayFit26", "Fit26"],
  "n_patterns": 2,
  "label": "Two-pattern mixture (Fit26)",
  "summary": "Fits the mixing fraction between two fixed reference patterns.",
  "description": "A one-parameter mixture: the model is a linear combination of two fixed reference decay patterns and only their mixing fraction x1 is fit. Used for species fractioning when the pure-component decays are known (e.g. two conformational states measured separately). The single parameter maps to initial_values as [x1].",
  "setup": {
    "category": "fit_setup",
    "name": "fit2x"
  },
  "params_schema": {
    "type": "object",
    "required": [
      "x1"
    ],
    "properties": {
      "x1": {
        "type": "number",
        "title": "Fraction of pattern 1",
        "default": 0.5,
        "minimum": 0.0,
        "maximum": 1.0,
        "fixed_default": false,
        "description": "Amplitude fraction of the first reference pattern; the second is 1 - x1."
      }
    }
  },
  "supports_lnprob": true,
  "supports_gradient": false,
  "results_schema": {
    "type": "object",
    "properties": {
      "twoIstar": {
        "type": "number",
        "title": "2I*",
        "description": "Poisson deviance of the optimised mixture against a perfectly fitting model."
      },
      "converged": {
        "type": "boolean",
        "title": "Converged",
        "description": "Whether the optimiser reached its tolerance. Carried as 0.0 or 1.0."
      },
      "iterations": {
        "type": "integer",
        "title": "Iterations",
        "description": "Objective evaluations the optimiser needed."
      }
    }
  }
})JSON";

const char* const kFit2xEntry = R"JSON({
  "name": "fit2x",
  "operation_type": "tcspc_fitting",
  "api": ["DecayFitProblem", "DecayFitConstraints", "DecayFitOutcome", "DecayFitBatchOutcome", "DecayFitLinkedOutcome", "fit_linked", "fit_names", "result_names", "results_as_dict", "parameter_vector", "setup_vector", "default_links", "decay_fit_names", "decay_fit_parameter_names", "decay_fit_result_names", "decay_fit_setup_names", "decay_fit_setup_vector", "decay_fit_default_links", "decay_fit_is_registered", "install_plugin_decay_fits"],
  "label": "Fit2x construction inputs",
  "summary": "Inputs set once when a Fit2x model is built (not optimised).",
  "description": "The instrument description and correction factors shared by every Fit2x model (Fit23/24/25/26). Supplied to the model constructor; the IRF and background are Jordi (VV|VH) histograms the same length as the data. Referenced from each fit entry's 'setup' link.",
  "params_schema": {
    "type": "object",
    "required": [
      "dt",
      "period",
      "g_factor",
      "l1",
      "l2",
      "convolution_stop",
      "soft_bifl_scatter_flag",
      "p2s_twoIstar_flag"
    ],
    "properties": {
      "dt": {
        "type": "number",
        "title": "Micro-time bin width (ns)",
        "default": 0.032,
        "minimum": 1e-06,
        "maximum": 1000.0,
        "unit": "ns",
        "description": "Time width of one micro-time channel. tau is reported in these units, so it must be in nanoseconds for the lifetime to be in nanoseconds."
      },
      "period": {
        "type": "number",
        "title": "Excitation period (ns)",
        "default": 13.5,
        "minimum": 0.001,
        "maximum": 10000.0,
        "unit": "ns",
        "description": "Time between excitation pulses. The decay is convolved over one period (wrap-around), so a lifetime longer than the period cannot be measured."
      },
      "g_factor": {
        "type": "number",
        "title": "G-factor",
        "default": 1.0,
        "minimum": 0.0,
        "maximum": 100.0,
        "description": "Detection-efficiency ratio between the parallel and perpendicular channels, used only for the anisotropy."
      },
      "l1": {
        "type": "number",
        "title": "Mixing l1",
        "default": 0.0,
        "minimum": 0.0,
        "maximum": 1.0,
        "advanced": true,
        "description": "Depolarisation/mixing correction between the parallel and perpendicular detection channels."
      },
      "l2": {
        "type": "number",
        "title": "Mixing l2",
        "default": 0.0,
        "minimum": 0.0,
        "maximum": 1.0,
        "advanced": true,
        "description": "Second depolarisation/mixing correction factor."
      },
      "convolution_stop": {
        "type": "integer",
        "title": "Convolution stop (channel)",
        "default": -1,
        "minimum": -1,
        "maximum": 1000000,
        "advanced": true,
        "description": "Last micro-time channel included in the convolution. -1 uses the full IRF length."
      },
      "soft_bifl_scatter_flag": {
        "type": "boolean",
        "title": "Discount background photons",
        "default": true,
        "description": "When true the reported score is reduced by the background photon contribution (background photons carry no lifetime information)."
      },
      "objective": {
        "type": "string",
        "title": "Objective",
        "default": "poisson_mle",
        "enum": [
          "poisson_mle",
          "p2s_mle",
          "neyman_lsq",
          "gehrels_lsq"
        ],
        "entries_of": {
          "category": "objective"
        },
        "description": "Which statistic the fit minimises, by name from the 'objective' category. 'p2s_mle' scores the anisotropy-free sum P + 2S; the default scores the parallel and perpendicular channels individually in a global fit. Replaces the former p2s_twoIstar_flag, so that adding a statistic does not mean adding a flag to every model."
      },
      "fit_start": {
        "type": "integer",
        "title": "First fitted channel",
        "default": 0,
        "minimum": 0,
        "maximum": 1000000,
        "advanced": true,
        "description": "First micro-time channel included in the objective. Non-zero makes this a tail fit."
      },
      "fit_stop": {
        "type": "integer",
        "title": "Last fitted channel",
        "default": -1,
        "minimum": -1,
        "maximum": 1000000,
        "advanced": true,
        "description": "One past the last micro-time channel included in the objective; -1 fits to the end."
      }
    }
  }
})JSON";


}  // namespace

/// Register the Fit2x registry entries (fit23..fit26 and the fit2x setup block). Idempotent.
void register_fit_descriptors_fit2x() {
    tttrlib::register_algorithm_json("fit", "fit23", kFit23Entry);
    tttrlib::register_algorithm_json("fit", "fit24", kFit24Entry);
    tttrlib::register_algorithm_json("fit", "fit25", kFit25Entry);
    tttrlib::register_algorithm_json("fit", "fit26", kFit26Entry);
    tttrlib::register_algorithm_json("fit_setup", "fit2x", kFit2xEntry);
}

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

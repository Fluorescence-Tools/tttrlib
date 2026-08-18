// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file DecayFitModelNExp.cpp
 * \brief The multi-exponential reconvolution fit, behind the common interface.
 *
 * This is the model with a **variable** parameter count: `n_exponentials`
 * lifetimes followed by that many amplitudes. It is therefore the one that
 * exercises the registry's `count_from` link, and the reason the flattening rule
 * has to say what an array property does to the flat vector rather than leaving
 * it to each caller to work out.
 *
 * Parameter layout, per the rule (declaration order, arrays expanded
 * contiguously): `[lifetime_0 … lifetime_{n-1}, amplitude_0 … amplitude_{n-1}]`.
 *
 * Unlike the Fit2x family this model does not search over amplitudes: at every
 * lifetime trial the nonnegative amplitudes and the background fraction are
 * profiled by expectation-maximisation. The amplitudes in the parameter vector
 * are therefore starting values on the way in and profiled results on the way
 * out — which is exactly what the registry's `fixed_default: true` on them says.
 */
#include <algorithm>
#include <cmath>
#include <vector>

#include "AlgorithmRegistry.h"
#include "DecayFitModel.h"
#include "DecayFitNExp.h"
#include "DecayStatistics.h"

namespace {

/*!
 * \brief Slots of the `nexp` setup block.
 *
 * Must match the declaration order of the `fit_setup/nexp` entry registered
 * at the end of this file (kNexpEntry).
 */
enum NExpSetup {
    kDt = 0,
    kNExponentials,
    kPeriod,
    kConvolutionStop,
    kTailStart,
    kTauMin,
    kTauMax,
    kGridIntervals,
    kMaxOuterIterations,
    kMaxEmIterations,
    kInitialBackgroundFraction,
    kFitStart,
    kFitStop,
    kNExpSetupSize
};

double setup_at(const std::vector<double> &setup, int slot, double fallback) {
    if (slot < 0 || static_cast<std::size_t>(slot) >= setup.size()) return fallback;
    const double v = setup[static_cast<std::size_t>(slot)];
    return std::isfinite(v) ? v : fallback;
}


/*! \brief Any number of lifetimes, amplitudes profiled by EM. */
class NExpModel : public DecayFitModel {

    std::vector<double> setup_;
    DecayFitNExpOptions options_;
    int n_exponentials_;

public:

    NExpModel(const std::vector<double> &setup, const std::vector<double> & /*irf*/)
        : setup_(setup) {
        n_exponentials_ = std::max(1, static_cast<int>(setup_at(setup_, kNExponentials, 1.0)));
        options_.dt = setup_at(setup_, kDt, 1.0);
        options_.period = setup_at(setup_, kPeriod, 0.0);
        options_.convolution_stop = static_cast<int>(setup_at(setup_, kConvolutionStop, -1.0));
        options_.tail_start = static_cast<int>(setup_at(setup_, kTailStart, -1.0));
        options_.tau_min = setup_at(setup_, kTauMin, 1.0e-3);
        options_.tau_max = setup_at(setup_, kTauMax, 100.0);
        options_.coordinate_grid_intervals =
            static_cast<int>(setup_at(setup_, kGridIntervals, 24.0));
        options_.max_outer_iterations =
            static_cast<int>(setup_at(setup_, kMaxOuterIterations, 20.0));
        options_.max_em_iterations =
            static_cast<int>(setup_at(setup_, kMaxEmIterations, 500.0));
        options_.initial_background_fraction =
            setup_at(setup_, kInitialBackgroundFraction, 0.01);
        options_.include_model = true;
    }

    const char *name() const override { return "fit_nexp"; }

    /*! Two arrays of `n_exponentials`, per the `count_from` link. */
    int n_parameters(const DecayFitProblem &) const override { return 2 * n_exponentials_; }

    int n_results(const DecayFitProblem &) const override { return 7; }

    double evaluate(const double *x, DecayFitProblem &problem) const override {
        // Holding every lifetime turns the fit into a pure amplitude profile at
        // exactly these values, which is what an evaluation means here.
        DecayFitConstraints held(std::vector<int>(
            static_cast<std::size_t>(2 * n_exponentials_), -1));
        std::vector<double> scratch(x, x + 2 * n_exponentials_);
        return const_cast<NExpModel *>(this)->run(scratch.data(), held, problem, nullptr, true);
    }

    double fit(double *x, const DecayFitConstraints &constraints,
               DecayFitProblem &problem, double *results) const override {
        return const_cast<NExpModel *>(this)->run(x, constraints, problem, results, false);
    }

private:

    /*!
     * \brief Shared body of `fit` and `evaluate`.
     *
     * \param evaluate_only hold every lifetime, so only the amplitudes profile.
     */
    double run(double *x, const DecayFitConstraints &constraints,
               DecayFitProblem &problem, double *results, bool evaluate_only) const {
        problem.require_valid();
        init_fact();

        const int n = n_exponentials_;
        std::vector<double> lifetimes(x, x + n);
        std::vector<double> amplitudes(x + n, x + 2 * n);
        std::vector<int> lifetime_fixed(static_cast<std::size_t>(n), 0);
        for (int i = 0; i < n; ++i) {
            // A linked lifetime is driven from outside this fit, so it is held
            // here just as a fixed one is.
            lifetime_fixed[static_cast<std::size_t>(i)] =
                (evaluate_only || constraints.code(i) != 0) ? 1 : 0;
        }

        DecayFitNExpOptions options = options_;
        // The problem's fit range is the one place a tail fit is expressed; keep
        // the model's own tail_start when the problem does not override it.
        if (problem.fit_start > 0) options.tail_start = problem.fit_start;

        const DecayFitNExpResult r = DecayFitNExp::fit(
            problem.data, problem.irf, problem.background,
            lifetimes, amplitudes, lifetime_fixed, options);

        for (int i = 0; i < n && i < static_cast<int>(r.lifetimes.size()); ++i) x[i] = r.lifetimes[i];
        for (int i = 0; i < n && i < static_cast<int>(r.amplitudes.size()); ++i) x[n + i] = r.amplitudes[i];
        if (!r.model.empty() && r.model.size() == problem.model.size()) problem.model = r.model;

        // The reported goodness is the Poisson deviance against a perfectly
        // fitting model, computed here from the fitted curve, so that every
        // model's first result column means the same thing. The optimiser's own
        // profile likelihood omits data-only terms and so is not comparable
        // across models; it is reported separately.
        double two_istar = std::numeric_limits<double>::infinity();
        if (!problem.model.empty()) {
            std::vector<int> counts(problem.data.size());
            for (std::size_t i = 0; i < problem.data.size(); ++i)
                counts[i] = static_cast<int>(problem.data[i]);
            const int half = static_cast<int>(problem.data.size() / 2);
            if (half > 0) two_istar = twoIstar(counts.data(), problem.model.data(), half);
        }

        if (results != nullptr) {
            results[0] = two_istar;
            results[1] = r.converged ? 1.0 : 0.0;
            results[2] = static_cast<double>(r.outer_iterations);
            results[3] = r.negative_log_likelihood;
            results[4] = r.photon_count;
            results[5] = r.background_amplitude;
            results[6] = static_cast<double>(r.em_iterations);
        }
        return two_istar;
    }

public:
    bool supports_lnprob() const override { return true; }
};


}  // namespace


/*!
 * \brief Register the n-exponential model.
 *
 * Named and called explicitly; see src/DecayFitModelRegistration.h for why a
 * static initialiser is not enough.
 */
namespace {

// registry("fit")["fit_nexp"] and registry("fit_setup")["nexp"], next to the
// model; see the note in DecayFitModelFit2x.cpp.
const char* const kFitNexpEntry = R"JSON({
  "name": "fit_nexp",
  "n_patterns": 0,
  "label": "Multi-exponential reconvolution (N-exp)",
  "summary": "Poisson MLE of any number of lifetimes, with amplitudes profiled by EM.",
  "description": "General one- or multi-exponential reconvolution fit. Lifetimes are found by deterministic coordinate-wise Brent minimisation over a log-spaced grid of starting points; at every lifetime trial the nonnegative amplitudes and the background fraction are profiled by expectation-maximisation, so they are never searched over directly. Data may be one channel or a polarisation-resolved pair sharing one temporal shape, in which case the channels are pooled as exact sufficient statistics while each keeps its own profiled total. Set 'tail_start' in the setup to fit the tail without reconvolution, which is the usual treatment of a sensitised-emission decay whose rise is not a simple instrument response.",
  "setup": {
    "category": "fit_setup",
    "name": "nexp"
  },
  "params_schema": {
    "type": "object",
    "required": [
      "lifetimes",
      "amplitudes"
    ],
    "properties": {
      "lifetimes": {
        "type": "array",
        "title": "Lifetimes (ns)",
        "count_from": "n_exponentials",
        "items": {
          "type": "number",
          "default": 2.0,
          "minimum": 0.001,
          "maximum": 100.0,
          "unit": "ns",
          "fixed_default": false,
          "description": "One fluorescence lifetime. Fix a lifetime to fit a known component's amplitude only."
        },
        "description": "The exponential lifetimes, as many as 'n_exponentials' in the setup."
      },
      "amplitudes": {
        "type": "array",
        "title": "Amplitudes",
        "count_from": "n_exponentials",
        "items": {
          "type": "number",
          "default": 1.0,
          "minimum": 0.0,
          "maximum": 1000000000000.0,
          "fixed_default": true,
          "description": "Starting amplitude of the matching lifetime. Profiled by EM rather than searched, so these are starting values, not free parameters in the usual sense."
        },
        "description": "Starting amplitudes, one per lifetime."
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
        "description": "Whether the outer lifetime search met its tolerance. Carried as 0.0 or 1.0."
      },
      "iterations": {
        "type": "integer",
        "title": "Outer iterations",
        "description": "Coordinate sweeps over the lifetimes."
      },
      "negative_log_likelihood": {
        "type": "number",
        "title": "-ln L",
        "description": "Profile shape negative log likelihood. Terms depending only on the data are omitted, so it is comparable between fits of the same data and not otherwise."
      },
      "photon_count": {
        "type": "number",
        "title": "Photons",
        "description": "Total counts the fit was scored against."
      },
      "background_amplitude": {
        "type": "number",
        "title": "Background amplitude",
        "description": "Profiled amplitude of the background pattern."
      },
      "em_iterations": {
        "type": "integer",
        "title": "EM iterations",
        "description": "Amplitude-profiling iterations at the final lifetimes."
      }
    }
  }
})JSON";

const char* const kNexpEntry = R"JSON({
  "name": "nexp",
  "label": "Multi-exponential construction inputs",
  "summary": "Inputs set once when a multi-exponential model is built.",
  "description": "Instrument description, component count and search controls for the N-exponential reconvolution fit. 'n_exponentials' is what the model's variable-length lifetime and amplitude arrays take their length from, via their 'count_from' link.",
  "params_schema": {
    "type": "object",
    "required": [
      "dt",
      "n_exponentials"
    ],
    "properties": {
      "dt": {
        "type": "number",
        "title": "Micro-time bin width (ns)",
        "default": 0.032,
        "minimum": 1e-06,
        "maximum": 1000.0,
        "unit": "ns",
        "description": "Time width of one micro-time channel; lifetimes are reported in these units."
      },
      "n_exponentials": {
        "type": "integer",
        "title": "Number of components",
        "default": 1,
        "minimum": 1,
        "maximum": 32,
        "description": "How many exponential components the model has. The lifetime and amplitude parameter arrays each hold this many entries — this is the property their 'count_from' names."
      },
      "period": {
        "type": "number",
        "title": "Excitation period (ns)",
        "default": 0.0,
        "minimum": 0.0,
        "maximum": 10000.0,
        "unit": "ns",
        "description": "Time between excitation pulses; 0 disables the periodic wrap-around."
      },
      "convolution_stop": {
        "type": "integer",
        "title": "Convolution stop (channel)",
        "default": -1,
        "minimum": -1,
        "maximum": 1000000,
        "advanced": true,
        "description": "Last channel included in the convolution; -1 uses the full IRF length."
      },
      "tail_start": {
        "type": "integer",
        "title": "Tail-fit start (channel)",
        "default": -1,
        "minimum": -1,
        "maximum": 1000000,
        "description": "When >= 0 the fit is a tail fit: each component is a pure decay from this channel with no IRF reconvolution, and earlier channels are excluded. The standard treatment of a sensitised-emission decay, whose rise is not a simple instrument response. -1 keeps the normal reconvolution fit."
      },
      "tau_min": {
        "type": "number",
        "title": "Shortest allowed lifetime (ns)",
        "default": 0.001,
        "minimum": 1e-09,
        "maximum": 1000.0,
        "unit": "ns",
        "advanced": true,
        "description": "Lower bound of the lifetime search."
      },
      "tau_max": {
        "type": "number",
        "title": "Longest allowed lifetime (ns)",
        "default": 100.0,
        "minimum": 1e-06,
        "maximum": 1000000.0,
        "unit": "ns",
        "advanced": true,
        "description": "Upper bound of the lifetime search."
      },
      "coordinate_grid_intervals": {
        "type": "integer",
        "title": "Search grid intervals",
        "default": 24,
        "minimum": 1,
        "maximum": 1024,
        "advanced": true,
        "description": "Log-spaced starting points per lifetime. Higher is more robust against local minima and linearly slower; a well-conditioned 1-2 component fit with a decent initial guess is usually fine at 8."
      },
      "max_outer_iterations": {
        "type": "integer",
        "title": "Max lifetime sweeps",
        "default": 20,
        "minimum": 1,
        "maximum": 10000,
        "advanced": true,
        "description": "Cap on coordinate sweeps over the lifetimes."
      },
      "max_em_iterations": {
        "type": "integer",
        "title": "Max EM iterations",
        "default": 500,
        "minimum": 1,
        "maximum": 100000,
        "advanced": true,
        "description": "Cap on amplitude-profiling iterations per lifetime trial."
      },
      "initial_background_fraction": {
        "type": "number",
        "title": "Initial background fraction",
        "default": 0.01,
        "minimum": 0.0,
        "maximum": 1.0,
        "advanced": true,
        "description": "Starting share of the counts attributed to the background pattern."
      },
      "fit_start": {
        "type": "integer",
        "title": "First fitted channel",
        "default": 0,
        "minimum": 0,
        "maximum": 1000000,
        "advanced": true,
        "description": "First micro-time channel included in the objective."
      },
      "fit_stop": {
        "type": "integer",
        "title": "Last fitted channel",
        "default": -1,
        "minimum": -1,
        "maximum": 1000000,
        "advanced": true,
        "description": "One past the last channel included in the objective; -1 fits to the end."
      }
    }
  }
})JSON";


}  // namespace

/// Register the n-exponential registry entries. Idempotent.
void register_fit_descriptors_nexp() {
    tttrlib::register_algorithm_json("fit", "fit_nexp", kFitNexpEntry);
    tttrlib::register_algorithm_json("fit_setup", "nexp", kNexpEntry);
}

void register_decay_fit_models_nexp() {
    register_decay_fit("fit_nexp", [](const std::vector<double> &s, const std::vector<double> &irf) {
        return std::make_shared<const NExpModel>(s, irf);
    });
}

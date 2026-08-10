// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file DecayFitPlugin.cpp
 * \brief Makes a plugin's decay fit model into an ordinary DecayFitModel.
 *
 * The plugin host sits *below* the fitting code, which is what lets a plugin
 * contribute a file format without the format table depending on the fitting
 * stack. The price is that the host cannot construct a `DecayFitModel` itself.
 * So this file hands the means down: it installs a registrar with the host, and
 * that registrar wraps each C table in an adapter which is, from every other
 * point of view in the library, just another model.
 *
 * The same direction of dependency as the content sniffers, and for the same
 * reason. A lower layer that reached up for what it needs would put the whole
 * fitting stack underneath the format table.
 *
 * Once wrapped, `make_decay_fit("mymodel", …)` works, the batch fitter works,
 * the constrained fitter works, and the model appears in `registry("fit")`
 * alongside the built-ins -- none of which knows a plugin is involved.
 */

#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "DecayFitModel.h"
#include "DecayFitProblem.h"
#include "i_lbfgs.h"
#include "PluginHost.h"

// DecayFitModel and its neighbours live at global scope, so the adapter does
// too; only the plugin host is namespaced, and it is qualified below.
using tttrlib::PluginHost;

namespace {

/*!
 * Builds the C view of a problem.
 *
 * A view, not a copy: the arrays are the host's and stay the host's, which is
 * what keeps a fit that runs thousands of times from copying its data on every
 * iteration. `problem.model` is the one buffer the plugin writes to, and it is
 * already sized to match the data.
 */
tttrlib_fit_problem_v1 view_of(DecayFitProblem& problem,
                               std::vector<const double*>& pattern_pointers) {
    pattern_pointers.clear();
    pattern_pointers.reserve(problem.patterns.size());
    for (const auto& p : problem.patterns) pattern_pointers.push_back(p.data());

    tttrlib_fit_problem_v1 v{};
    v.struct_size = sizeof(v);
    v.data = problem.data.empty() ? nullptr : problem.data.data();
    v.irf = problem.irf.empty() ? nullptr : problem.irf.data();
    v.background = problem.background.empty() ? nullptr : problem.background.data();
    v.patterns = pattern_pointers.empty() ? nullptr : pattern_pointers.data();
    v.n_patterns = static_cast<uint32_t>(pattern_pointers.size());
    v.model = problem.model.empty() ? nullptr : problem.model.data();
    v.n_channels = problem.n_channels;
    v.n_bins = problem.n_bins;
    v.dt = problem.dt;
    v.setup = problem.setup.empty() ? nullptr : problem.setup.data();
    v.n_setup = static_cast<uint32_t>(problem.setup.size());
    v.fit_start = problem.fit_start;
    v.fit_stop = problem.fit_stop;
    v.irf_is_per_channel = problem.irf_is_per_channel() ? 1 : 0;
    return v;
}

/*!
 * \brief A plugin's fit model, wearing the library's own interface.
 *
 * Every override does the same three things: build the view, call through the
 * C table, and turn a non-OK status into the exception the surrounding code
 * already expects. The plugin's own error message is carried along, because
 * "the fit failed" without saying whose fit or why is not a diagnosis.
 */
class PluginDecayFitModel : public DecayFitModel {
public:
    PluginDecayFitModel(const tttrlib_decay_fit_v1* table, void* instance)
        : table_(table), instance_(instance) {}

    ~PluginDecayFitModel() override {
        if (table_ != nullptr && table_->destroy != nullptr && instance_ != nullptr) {
            table_->destroy(table_->ctx, instance_);
        }
    }

    PluginDecayFitModel(const PluginDecayFitModel&) = delete;
    PluginDecayFitModel& operator=(const PluginDecayFitModel&) = delete;

    const char* name() const override { return table_->name; }

    int n_parameters(const DecayFitProblem& problem) const override {
        return count(problem, table_->n_parameters, "n_parameters");
    }

    int n_results(const DecayFitProblem& problem) const override {
        if (table_->n_results == nullptr) return 0;
        return count(problem, table_->n_results, "n_results");
    }

    double evaluate(const double* x, DecayFitProblem& problem) const override {
        std::vector<const double*> patterns;
        tttrlib_fit_problem_v1 v = view_of(problem, patterns);
        double out = 0.0;
        const int status = call([&] {
            return table_->evaluate(table_->ctx, instance_, x, &v, &out);
        });
        if (status != TTTRLIB_OK) throw_failure("evaluate", status);
        return out;
    }

    /*!
     * \brief Optimise with the library's own minimiser.
     *
     * The ABI asks a plugin for `evaluate` and nothing else, and this is why:
     * writing a bounded, constrained, link-aware optimiser is most of the work
     * of writing a fit model, it has nothing to do with the photophysics the
     * plugin author actually knows, and every plugin doing it separately would
     * mean every plugin doing it slightly differently.
     *
     * So a plugin model gets the same L-BFGS, the same constraint handling and
     * the same treatment of priors-as-bounds as a built-in — a plugin fit and a
     * built-in fit differ in the objective and in nothing else. A model that
     * wants its own optimiser can still profile it inside `evaluate`, exactly
     * as the built-ins with closed-form amplitude steps do.
     */
    double fit(double* x, const DecayFitConstraints& constraints,
               DecayFitProblem& problem, double* results) const override {
        const int n = n_parameters(problem);
        const DecayFitLinkMap map(constraints, n);

        Target target{this, &map, &problem, n, std::vector<double>(static_cast<std::size_t>(n), 0.0)};
        if (map.n_variables() > 0) {
            std::vector<double> variables(map.n_variables(), 0.0);
            map.contract(x, variables.data());

            bfgs optimiser(&Target::evaluate_for_optimiser, map.n_variables());

            // Bounds come only from the priors: a bound *is* a uniform prior,
            // so there is no second array of them to disagree with.
            std::vector<double> lower, upper;
            map.bounds(lower, upper);
            for (int v = 0; v < map.n_variables(); ++v) {
                if (std::isfinite(lower[v]) && std::isfinite(upper[v])) {
                    optimiser.set_bounds(v, lower[v], upper[v]);
                }
            }
            optimiser.minimize(variables.data(), &target);
            map.expand(variables.data(), x);
        }

        // Evaluate once more at the answer, so problem.model corresponds to the
        // parameters returned rather than to the optimiser's last probe.
        const double objective = evaluate(x, problem);
        if (results != nullptr) {
            const int n_res = n_results(problem);
            // The plugin reports no results of its own through this ABI, so the
            // one thing that is always meaningful — the objective it just
            // returned — goes in the first slot, and the rest are zeroed rather
            // than left as whatever the caller's buffer held.
            for (int i = 0; i < n_res; ++i) results[i] = 0.0;
            if (n_res > 0) results[0] = objective;
        }
        return objective;
    }

private:
    /// What the optimiser is handed: everything one objective call needs, and a
    /// plain function to call, because `bfgs` takes a C-style callback.
    struct Target {
        const PluginDecayFitModel* model;
        const DecayFitLinkMap* map;
        DecayFitProblem* problem;
        int n_parameters;
        std::vector<double> full;

        static double evaluate_for_optimiser(double* variables, void* pv) {
            auto* t = static_cast<Target*>(pv);
            if (t->full.size() != static_cast<std::size_t>(t->n_parameters)) {
                t->full.assign(static_cast<std::size_t>(t->n_parameters), 0.0);
            }
            t->map->expand(variables, t->full.data());

            const double lp = t->map->lnprior(t->full.data());
            if (!std::isfinite(lp)) return std::numeric_limits<double>::max();

            double value;
            try {
                value = t->model->evaluate(t->full.data(), *t->problem);
            } catch (...) {
                // A plugin that fails at some point in parameter space should
                // steer the optimiser away from it, not abort the fit.
                return std::numeric_limits<double>::max();
            }
            if (!std::isfinite(value)) return std::numeric_limits<double>::max();
            // Minimising -2 ln posterior: the objective is already a deviance,
            // so the log prior enters with the same factor.
            return value - 2.0 * lp;
        }
    };

    using CountFn = int (*)(void*, void*, const tttrlib_fit_problem_v1*, int32_t*);

    int count(const DecayFitProblem& problem, CountFn fn, const char* what) const {
        // The counting calls do not write to the problem, but the C view is
        // built from a mutable one -- a const_cast here rather than a second,
        // read-only view type, because the plugin sees exactly the same struct
        // either way and two of them would be two things to keep in step.
        DecayFitProblem& mutable_problem = const_cast<DecayFitProblem&>(problem);
        std::vector<const double*> patterns;
        tttrlib_fit_problem_v1 v = view_of(mutable_problem, patterns);
        int32_t out = 0;
        const int status = call([&] { return fn(table_->ctx, instance_, &v, &out); });
        if (status != TTTRLIB_OK) throw_failure(what, status);
        return static_cast<int>(out);
    }

    /// No exception crosses the boundary in either direction -- so if one comes
    /// back out of a plugin anyway, it is caught here rather than unwinding
    /// through C frames, which is undefined.
    template <typename F>
    static int call(F&& f) {
        try {
            return f();
        } catch (...) {
            return TTTRLIB_ERROR;
        }
    }

    [[noreturn]] void throw_failure(const char* what, int status) const {
        const std::string detail = PluginHost::last_error();
        throw std::runtime_error(
            std::string("plugin fit model '") + table_->name + "': " + what +
            " failed" + (detail.empty() ? std::string() : " (" + detail + ")") +
            " [status " + std::to_string(status) + "]");
    }

    const tttrlib_decay_fit_v1* table_;
    void* instance_;
};

/*!
 * Called by the host, once per model, while a plugin is initialising.
 *
 * Registers a factory rather than an instance: a model is built per fit, from
 * that fit's setup and IRF, and may precompute from them.
 */
bool register_plugin_fit(const tttrlib_decay_fit_v1* table) {
    // Not decay_fit_names(): see decay_fit_is_registered. This runs inside the
    // built-in registration's call_once, and going back through it deadlocks.
    if (decay_fit_is_registered(table->name)) return false;  // refused, not shadowed
    register_decay_fit(table->name, [table](const std::vector<double>& setup,
                                            const std::vector<double>& irf)
                       -> std::shared_ptr<const DecayFitModel> {
        void* instance = nullptr;
        int status = TTTRLIB_ERROR;
        try {
            status = table->create(table->ctx,
                                   setup.empty() ? nullptr : setup.data(),
                                   static_cast<uint32_t>(setup.size()),
                                   irf.empty() ? nullptr : irf.data(),
                                   static_cast<uint32_t>(irf.size()),
                                   &instance);
        } catch (...) {
            status = TTTRLIB_ERROR;
        }
        if (status != TTTRLIB_OK) {
            const std::string detail = PluginHost::last_error();
            throw std::runtime_error(
                std::string("plugin fit model '") + table->name +
                "': could not be constructed" +
                (detail.empty() ? std::string() : " (" + detail + ")"));
        }
        return std::make_shared<PluginDecayFitModel>(table, instance);
    });
    return true;
}

}  // namespace

void install_plugin_decay_fits() {
    // Installed before the host is asked to load anything: the registrar has to
    // be in place when a plugin's init calls register_decay_fit, and loading is
    // what triggers that init.
    static std::once_flag once;
    std::call_once(once, [] {
        PluginHost::set_decay_fit_registrar(&register_plugin_fit);
        PluginHost::ensure_loaded();
    });
}

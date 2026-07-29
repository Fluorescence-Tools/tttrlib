// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file DecayFitModel.h
 * \brief One interface every decay fit is reached through.
 *
 * Previously each estimator was its own shape. `Fit23`/`Fit24`/`Fit25`/`Fit26`
 * shared `static double fit(double*, short*, DecayFitData*)`; `DecayFitNExp`
 * introduced options and result structs of its own; a batched
 * polarisation-resolved model would have needed a third. The registry described
 * all of them uniformly in JSON, but the *call* was not uniform, so every caller
 * hard-coded a per-model path and reached around any facade that tried to hide
 * it — visibly so in the Python layer, where `FitNExp` inherited from `Fit2x`
 * without calling its constructor and re-implemented every property.
 *
 * The contract here is deliberately narrow, and everything wide is described in
 * JSON rather than in C++ types:
 *
 *  - **Parameters are a flat `double*`.** Names, defaults, ranges, units and
 *    the order of the vector come from the model's `params_schema` in the
 *    registry. That keeps the hot path free of string work while making the
 *    layout discoverable from every language binding — and removes the class of
 *    bug where a parameter vector held optimised values, setup flags and
 *    outputs at once with only a comment to say which was which.
 *  - **Results are a flat `double*`**, described by `results_schema` the same
 *    way, so a batch result is a matrix whose columns mean what the JSON says.
 *    Bulk arrays are excluded by construction: the fitted curve stays in
 *    `DecayFitProblem::model`, because a batch that carried it per row would be
 *    thousands of columns wide.
 *  - **Constraints are a flat `int` link vector** plus optional priors, so
 *    fixed, free and shared parameters are one concept (see
 *    \ref DecayFitConstraints).
 *
 * \par Immutable, and therefore shareable
 * A model is built once from its registry name plus the setup and IRF, and at
 * that moment caches whatever it derives from them — a normalised response, its
 * transform, a frequency grid. It is `const` thereafter, so one instance serves
 * every thread with no cloning and no locking; only the `DecayFitProblem` is
 * copied per worker. The corollary is deliberate: **changing the IRF means
 * building a new model.** A cached transform that silently outlived its IRF was
 * a real bug in a downstream tool, guarded there by a comment. Here the type
 * system enforces what the comment asked for.
 */
#ifndef TTTRLIB_DECAYFITMODEL_H
#define TTTRLIB_DECAYFITMODEL_H

#include <algorithm>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "DecayFitPrior.h"
#include "DecayFitProblem.h"

using json = nlohmann::json;


/*!
 * \brief Which parameters are fitted, which are held, and which move together.
 *
 * One integer per parameter slot expresses all three states that used to need a
 * boolean mask plus a convention:
 *
 *  - `link[i] < 0`  — **fixed**: held at its supplied value.
 *  - `link[i] == 0` — **free**: optimised on its own.
 *  - `link[i] == k` (`k > 0`) — **linked**: every slot carrying group `k` is
 *    optimised as a single shared value.
 *
 * Linking is what makes a batch more than parallel independent fits. Fitting
 * many measurements together is worthwhile precisely because some parameters are
 * properties of the *instrument* and must be common — a g-factor, a fundamental
 * anisotropy, a timeshift — while others are properties of each *sample* and
 * must not be. Without shared parameters a batch buys only threading.
 *
 * \par Group ids are global slot ids
 * A group is defined over the whole concatenated parameter vector, not as a
 * `(row, parameter)` pair. That costs nothing today, when a batch holds entries
 * of one model, and means a batch of *different* models later needs no change
 * here: entries simply carry different lengths and offsets while the resolution
 * below is untouched.
 *
 * Priors are optional and per slot; see \ref DecayFitPrior. Where several linked
 * slots each carry a prior, the shared value's prior is their product — the
 * beliefs are about one quantity, so they combine rather than compete.
 */
class DecayFitConstraints {

public:

    /*! One entry per parameter slot; empty means "everything free". */
    std::vector<int> link;

    /*! Optional prior per parameter slot; entries may be null. */
    std::vector<std::shared_ptr<DecayFitPrior>> priors;

    DecayFitConstraints() = default;

    explicit DecayFitConstraints(std::vector<int> link_) : link(std::move(link_)) {}

    /*! All parameters free. */
    static DecayFitConstraints all_free(int n) {
        return DecayFitConstraints(std::vector<int>(static_cast<std::size_t>(n), 0));
    }

    /*! Translate a legacy boolean mask (non-zero meaning fixed). */
    static DecayFitConstraints from_fixed_mask(const short *fixed, int n) {
        std::vector<int> l(static_cast<std::size_t>(n), 0);
        if (fixed != nullptr) {
            for (int i = 0; i < n; ++i) l[i] = fixed[i] ? -1 : 0;
        }
        return DecayFitConstraints(std::move(l));
    }

    /*! Link code of slot \p i, treating an absent vector as all-free. */
    int code(int i) const {
        if (link.empty()) return 0;
        if (i < 0 || static_cast<std::size_t>(i) >= link.size()) return 0;
        return link[static_cast<std::size_t>(i)];
    }

    bool is_fixed(int i) const { return code(i) < 0; }

    /*! Prior on slot \p i, or null. */
    std::shared_ptr<DecayFitPrior> prior(int i) const {
        if (i < 0 || static_cast<std::size_t>(i) >= priors.size()) return nullptr;
        return priors[static_cast<std::size_t>(i)];
    }

    json to_json() const {
        json j;
        j["link"] = link;
        json ps = json::array();
        for (const auto &p : priors) ps.push_back(p ? p->to_json() : json());
        j["priors"] = ps;
        return j;
    }

    static DecayFitConstraints from_json(const json &j) {
        DecayFitConstraints c;
        if (j.contains("link")) c.link = j.at("link").get<std::vector<int>>();
        if (j.contains("priors")) {
            for (const auto &sub : j.at("priors")) {
                c.priors.push_back(sub.is_null() ? nullptr : DecayFitPrior::from_json(sub));
            }
        }
        return c;
    }
};


/*!
 * \brief The link vector resolved into optimiser variables.
 *
 * Built once per fit, before any objective evaluation. It answers the two
 * questions the optimiser keeps asking — how many variables are there, and where
 * does variable \a v write in the full parameter vector — without re-scanning
 * the link codes on every iteration.
 */
class DecayFitLinkMap {

public:

    /*! Slots driven by each optimiser variable. */
    std::vector<std::vector<int>> slots_of_variable;

    /*! Variable driving each slot, or -1 when the slot is fixed. */
    std::vector<int> variable_of_slot;

    /*! Combined prior per variable (product over its slots'), possibly null. */
    std::vector<std::shared_ptr<DecayFitPrior>> variable_prior;

    DecayFitLinkMap() = default;

    /*!
     * \brief Resolve \p constraints over \p n_slots parameters.
     *
     * Free slots take one variable each; every slot sharing a positive group id
     * takes one variable between them; fixed slots take none. Group ids need not
     * be contiguous or ordered — they are labels, not indices — so a caller can
     * assign them meaningfully without renumbering.
     */
    DecayFitLinkMap(const DecayFitConstraints &constraints, int n_slots) {
        variable_of_slot.assign(static_cast<std::size_t>(std::max(n_slots, 0)), -1);
        std::vector<int> group_id;   // group label of each variable, 0 for a free slot

        for (int i = 0; i < n_slots; ++i) {
            const int c = constraints.code(i);
            if (c < 0) continue;                    // fixed: no variable
            if (c == 0) {                           // free: its own variable
                variable_of_slot[static_cast<std::size_t>(i)] =
                    static_cast<int>(slots_of_variable.size());
                slots_of_variable.push_back({i});
                group_id.push_back(0);
                continue;
            }
            // linked: join the variable already opened for this group, if any
            int v = -1;
            for (std::size_t k = 0; k < group_id.size(); ++k) {
                if (group_id[k] == c) { v = static_cast<int>(k); break; }
            }
            if (v < 0) {
                v = static_cast<int>(slots_of_variable.size());
                slots_of_variable.push_back({});
                group_id.push_back(c);
            }
            slots_of_variable[static_cast<std::size_t>(v)].push_back(i);
            variable_of_slot[static_cast<std::size_t>(i)] = v;
        }

        // A variable's prior is the product of the priors on the slots it drives.
        variable_prior.assign(slots_of_variable.size(), nullptr);
        for (std::size_t v = 0; v < slots_of_variable.size(); ++v) {
            std::vector<std::shared_ptr<DecayFitPrior>> parts;
            for (int slot : slots_of_variable[v]) {
                auto p = constraints.prior(slot);
                if (p) parts.push_back(p);
            }
            if (parts.size() == 1) {
                variable_prior[v] = parts.front();
            } else if (parts.size() > 1) {
                variable_prior[v] = std::make_shared<ProductPrior>(std::move(parts));
            }
        }
    }

    /*! Number of values the optimiser actually searches over. */
    int n_variables() const { return static_cast<int>(slots_of_variable.size()); }

    /*!
     * \brief Write the variables into the full parameter vector.
     *
     * Fixed slots are left untouched, so \p x must already hold their values.
     */
    void expand(const double *variables, double *x) const {
        for (std::size_t v = 0; v < slots_of_variable.size(); ++v) {
            for (int slot : slots_of_variable[v]) x[slot] = variables[v];
        }
    }

    /*! Read starting values for the variables out of a full parameter vector. */
    void contract(const double *x, double *variables) const {
        for (std::size_t v = 0; v < slots_of_variable.size(); ++v) {
            variables[v] = slots_of_variable[v].empty()
                               ? 0.0
                               : x[slots_of_variable[v].front()];
        }
    }

    /*!
     * \brief Hard box bounds per variable, taken from its prior's support.
     *
     * A variable with no prior is unbounded. This is the only place bounds come
     * from: there is no separate bounds array, because a bound *is* a uniform
     * prior.
     */
    void bounds(std::vector<double> &lower, std::vector<double> &upper) const {
        const double inf = std::numeric_limits<double>::infinity();
        lower.assign(slots_of_variable.size(), -inf);
        upper.assign(slots_of_variable.size(), inf);
        for (std::size_t v = 0; v < variable_prior.size(); ++v) {
            if (!variable_prior[v]) continue;
            const auto s = variable_prior[v]->support();
            lower[v] = s.first;
            upper[v] = s.second;
        }
    }

    /*! Sum of log prior densities over the variables, at full-vector \p x. */
    double lnprior(const double *x) const {
        double lp = 0.0;
        for (std::size_t v = 0; v < slots_of_variable.size(); ++v) {
            if (!variable_prior[v] || slots_of_variable[v].empty()) continue;
            const double value = x[slots_of_variable[v].front()];
            const double c = variable_prior[v]->lnpdf(value);
            if (!std::isfinite(c)) return -std::numeric_limits<double>::infinity();
            lp += c;
        }
        return lp;
    }

    /*!
     * \brief Prior residuals of every variable, concatenated.
     *
     * Appended to the data residuals so a least-squares optimiser minimises the
     * negative log-posterior rather than \f$\chi^2\f$ alone.
     */
    std::vector<double> prior_residuals(const double *x) const {
        std::vector<double> out;
        for (std::size_t v = 0; v < slots_of_variable.size(); ++v) {
            if (!variable_prior[v] || slots_of_variable[v].empty()) continue;
            const auto r = variable_prior[v]->residuals(x[slots_of_variable[v].front()]);
            out.insert(out.end(), r.begin(), r.end());
        }
        return out;
    }
};


/*!
 * \brief A fittable decay model.
 *
 * Every method is `const`: the model holds no mutable state, so a single
 * instance is safe to share across threads and `fit_batch` copies only the
 * problem. Anything a model needs to precompute is derived in its constructor
 * from the setup and IRF it was built with.
 */
class DecayFitModel {

public:

    virtual ~DecayFitModel() = default;

    /*! Registry key of this model, e.g. `"fit23"`. */
    virtual const char *name() const = 0;

    /*!
     * Length of the parameter vector for \p problem.
     *
     * It takes the problem because arity is not always fixed: a
     * multi-exponential model's length follows the number of components, and a
     * batched model's follows the number of entries. Both are declared in the
     * registry as arrays whose length is read from a sibling setup value.
     */
    virtual int n_parameters(const DecayFitProblem &problem) const = 0;

    /*! Length of the result vector for \p problem, per `results_schema`. */
    virtual int n_results(const DecayFitProblem &problem) const = 0;

    /*!
     * \brief Evaluate the model at \p x, filling `problem.model`.
     *
     * \return the objective (goodness) at \p x — the same quantity `fit`
     *         minimises, so a caller can score parameters without fitting.
     *
     * \note This scores the model *against the data*, and for models that
     *       profile their amplitude against the observed counts the returned
     *       curve is scaled to them — so on empty data it is identically zero.
     *       Use `model_curve` to obtain the shape itself.
     */
    virtual double evaluate(const double *x, DecayFitProblem &problem) const = 0;

    /*!
     * \brief The model curve at \p x, independent of any data.
     *
     * `evaluate` cannot serve this purpose: several models profile their
     * amplitude against the observed counts, so evaluating against empty data
     * returns zeros rather than a shape. Simulating a decay, plotting a model
     * before any measurement exists, or generating test data all need the curve
     * on its own.
     *
     * Writes `n_channels * n_bins` values into `curve`. The normalisation is the
     * model's own — callers that need a particular photon budget should scale
     * the result themselves.
     *
     * \return false when the model cannot produce a data-free curve, leaving the
     *         caller to fall back on `evaluate`.
     */
    virtual bool model_curve(const double *x, const DecayFitProblem &problem,
                             double *curve) const {
        (void)x; (void)problem; (void)curve;
        return false;
    }

    /*!
     * \brief Optimise \p x subject to \p constraints.
     *
     * \param x  In: starting values, full length `n_parameters`. Out: the
     *           optimum. Fixed slots are returned unchanged.
     * \param constraints Which slots are fixed, free or linked, and their priors.
     * \param problem The measurement; `problem.model` is left evaluated at the
     *        optimum.
     * \param results Optional buffer of `n_results` doubles, filled per
     *        `results_schema`.
     * \return the objective at the optimum.
     */
    virtual double fit(double *x,
                       const DecayFitConstraints &constraints,
                       DecayFitProblem &problem,
                       double *results = nullptr) const = 0;

    /*! Whether `lnprob` is implemented; advertised in the registry. */
    virtual bool supports_lnprob() const { return false; }

    /*! Whether `gradient` is implemented; advertised in the registry. */
    virtual bool supports_gradient() const { return false; }

    /*!
     * \brief Log posterior at \p x — log likelihood plus log prior.
     *
     * Lets a sampler drive the model without leaving native code. The default
     * derives it from the objective for models whose objective is a Poisson
     * deviance, which is \f$-2\ln L\f$ up to a constant.
     */
    virtual double lnprob(const double *x,
                          const DecayFitConstraints &constraints,
                          DecayFitProblem &problem) const {
        const DecayFitLinkMap map(constraints, n_parameters(problem));
        const double lp = map.lnprior(x);
        if (!std::isfinite(lp)) return -std::numeric_limits<double>::infinity();
        return -0.5 * evaluate(x, problem) + lp;
    }

    /*!
     * \brief Gradient of the objective with respect to the free variables.
     *
     * \return false when the model has no analytic gradient, leaving the caller
     *         to fall back on finite differences.
     */
    virtual bool gradient(const double * /*x*/,
                          const DecayFitConstraints & /*constraints*/,
                          DecayFitProblem & /*problem*/,
                          double * /*grad_out*/) const {
        return false;
    }
};


/*!
 * \brief Builds one model from its setup and IRF.
 *
 * The signature every model's factory has, so the registry can create any of
 * them without knowing which it is creating.
 */
using DecayFitFactory = std::function<std::shared_ptr<const DecayFitModel>(
    const std::vector<double> &setup, const std::vector<double> &irf)>;

/*!
 * \brief Make \p name constructible through `make_decay_fit`.
 *
 * Called from a static initialiser in each model's translation unit, so adding
 * a model is a self-contained change: no central list to edit, and nothing else
 * has to know the model exists.
 */
void register_decay_fit(const std::string &name, DecayFitFactory factory);

/*!
 * \brief Build the model registered under \p name.
 *
 * The one way a fit is constructed. \p setup and \p irf are what the model
 * precomputes from, and are fixed for its lifetime.
 *
 * \throws std::invalid_argument when \p name is not a registered fit.
 */
std::shared_ptr<const DecayFitModel> make_decay_fit(
    const std::string &name,
    const std::vector<double> &setup = {},
    const std::vector<double> &irf = {});

/*! Names of every registered fit model. */
std::vector<std::string> decay_fit_names();

/*!
 * \brief Build \p name's flat setup vector from named values.
 *
 * Every slot takes its documented default unless \p values_json overrides it, so
 * a caller supplies only what it cares about and cannot get the *order* wrong.
 * This lives in C++ rather than in one binding's helper module because a caller
 * assembling the vector by hand — which R and Java would otherwise have to do —
 * is exactly the positional hazard the registry exists to remove.
 *
 * \param name Registry key of the fit, e.g. `"fit23"`.
 * \param values_json JSON object of setup values by name, e.g.
 *        `{"dt": 0.032, "period": 13.5, "objective": "p2s_mle"}`. A string is
 *        accepted for an enumerated property and converted to its index.
 * \throws std::invalid_argument for an unknown fit, an unknown setup name, or an
 *         enum value that is not one of the declared choices.
 */
std::vector<double> decay_fit_setup_vector(const std::string &name,
                                           const std::string &values_json = "{}");

/*! Setup-slot names of \p name, in flattened order. */
std::vector<std::string> decay_fit_setup_names(const std::string &name);

/*!
 * \brief Parameter-slot names of \p name, in flattened order.
 * \param n entry count for variable-length blocks (see `count_from`).
 */
std::vector<std::string> decay_fit_parameter_names(const std::string &name, int n = 0);

/*! Result-column names of \p name, in flattened order. */
std::vector<std::string> decay_fit_result_names(const std::string &name, int n = 0);

/*!
 * \brief Link vector holding every parameter the registry marks `fixed_default`.
 *
 * Those defaults exist because some parameters are not identifiable from a short
 * decay, so freeing them by default produces confident nonsense.
 */
std::vector<int> decay_fit_default_links(const std::string &name, int n = 0);


/*!
 * \brief Fit many measurements with one model.
 *
 * Replaces the per-model `fit_matrix` / `fit_many` / `fit_map` copies with a
 * single implementation: rows are partitioned across workers, each holding a
 * private copy of \p prototype so their working state cannot collide, while the
 * model itself is shared because it is immutable.
 *
 * \param model      The (shared, immutable) model.
 * \param prototype  Problem describing the shape of every row; its `data` is
 *                   replaced per row.
 * \param data_matrix `n_rows * n_cols` row-major measurements.
 * \param x0         Starting parameters, either one vector reused for every row
 *                   or `n_rows` vectors laid out row-major.
 * \param constraints Applied to every row.
 * \param x_out      `n_rows * n_parameters` fitted parameters, row-major.
 * \param results_out `n_rows * n_results` output, row-major.
 * \return objective per row.
 */
std::vector<double> fit_batch(
    const DecayFitModel &model,
    const DecayFitProblem &prototype,
    const std::vector<double> &data_matrix,
    std::size_t n_rows,
    std::size_t n_cols,
    const std::vector<double> &x0,
    const DecayFitConstraints &constraints,
    std::vector<double> &x_out,
    std::vector<double> &results_out);


/*!
 * \brief What one fit produced.
 *
 * Returned by value rather than written through output parameters: a `double&`
 * or `vector&` out-argument needs a typemap in every language binding, and gets
 * one wrong somewhere. A plain struct wraps identically in all of them.
 */
struct DecayFitOutcome {
    /*! Fitted parameters, `n_parameters` long. */
    std::vector<double> parameters;
    /*! Results per `results_schema`, `n_results` long. */
    std::vector<double> results;
    /*! The objective at the optimum — also `results[0]` for every model. */
    double objective = 0.0;
};


/*!
 * \brief What a *linked* batch produced — one joint fit over many measurements.
 *
 * Unlike an independent batch this has a single `objective`, because the rows
 * were fitted together against one shared parameter vector.
 */
struct DecayFitLinkedOutcome {
    /*! `n_rows * n_parameters` fitted parameters, row-major and expanded, so a
     *  linked value appears in every row that shares it. */
    std::vector<double> parameters;
    /*! `n_rows * n_results` results, harvested per row at the joint optimum. */
    std::vector<double> results;
    /*! Objective of each row at the optimum. */
    std::vector<double> row_objective;
    /*! The joint objective actually minimised: the sum over rows, plus priors. */
    double objective = 0.0;
    /*! Whether the optimiser met its tolerance rather than hitting the limit. */
    bool converged = false;
    /*! Joint objective evaluations. */
    int iterations = 0;
    /*! Number of values actually searched over, after links and fixing. */
    int n_variables = 0;
};


/*! \brief What a batch of fits produced; all arrays are row-major. */
struct DecayFitBatchOutcome {
    /*! `n_rows * n_parameters` fitted parameters. */
    std::vector<double> parameters;
    /*! `n_rows * n_results` results. */
    std::vector<double> results;
    /*! One objective per row. */
    std::vector<double> objective;
};


/*!
 * \brief Fit many measurements **together**, with parameters shared between them.
 *
 * `fit_batch` fits each row on its own, so a link group spanning rows does
 * nothing there — the rows never meet. This is the other thing: one parameter
 * vector of `n_rows * n_parameters` slots, link groups resolved across the whole
 * of it, and a single objective (the sum over rows) minimised jointly.
 *
 * That is what makes fitting several measurements together worth the cost. Some
 * parameters are properties of the *instrument* and must be common to every
 * measurement — a g-factor, a fundamental anisotropy, a timeshift — while others
 * are properties of each *sample* and must not be. Tie the first kind with a
 * link group and every row informs it; leave the second kind free per row.
 *
 * \param model      The (shared, immutable) model.
 * \param prototype  Problem describing the shape of every row.
 * \param data_matrix `n_rows * n_cols` row-major measurements.
 * \param x0         Starting parameters: one vector of `n_parameters` reused for
 *                   every row, or `n_rows * n_parameters` laid out row-major.
 * \param constraints Defined over the **concatenated** vector: slot
 *                   `row * n_parameters + i`. Group ids are global, so a group
 *                   naturally spans rows.
 * \param max_iterations Cap on joint objective evaluations (0 = the default).
 *
 * \note Every entry is the same model. Mixed models in one linked fit are
 *       rejected rather than silently mishandled; the parameter layout is
 *       already general enough to lift that later.
 */
DecayFitLinkedOutcome fit_linked(
    const DecayFitModel &model,
    const DecayFitProblem &prototype,
    const std::vector<double> &data_matrix,
    std::size_t n_rows,
    std::size_t n_cols,
    const std::vector<double> &x0,
    const DecayFitConstraints &constraints,
    int max_iterations = 0);


/*!
 * \brief An owning, value-semantic handle on a fit model.
 *
 * The language bindings' entry point. `make_decay_fit` returns a
 * `shared_ptr<const DecayFitModel>`, which is the right thing in C++ and an
 * awkward thing to wrap: SWIG has to be taught the smart pointer, the const
 * qualifier and the abstract base separately, in four languages. This is a
 * plain class instead — construct it with a name, call methods on it — so every
 * binding gets the same natural object with no smart-pointer machinery.
 *
 * Copying is cheap and shares the underlying model, which is safe because the
 * model is immutable.
 */
class DecayFit2 {

    std::shared_ptr<const DecayFitModel> model_;

public:

    /*!
     * \brief Build the fit registered under \p name.
     *
     * \param name Registry key, e.g. `"fit23"`; see `decay_fit_names()`.
     * \param setup Flat setup vector, laid out by the model's `fit_setup` entry.
     * \param irf Instrument response the model precomputes from.
     * \throws std::invalid_argument when \p name is not registered.
     */
    DecayFit2(const std::string &name,
              const std::vector<double> &setup = {},
              const std::vector<double> &irf = {})
        : model_(make_decay_fit(name, setup, irf)) {}

    /*! Registry key of the wrapped model. */
    std::string name() const { return model_ ? model_->name() : std::string(); }

    int n_parameters(const DecayFitProblem &problem) const {
        return model_->n_parameters(problem);
    }

    int n_results(const DecayFitProblem &problem) const {
        return model_->n_results(problem);
    }

    bool supports_lnprob() const { return model_ && model_->supports_lnprob(); }
    bool supports_gradient() const { return model_ && model_->supports_gradient(); }

    /*!
     * \brief Score \p parameters without optimising.
     * \return the objective; `problem.model` is left evaluated there.
     */
    double evaluate(const std::vector<double> &parameters,
                    DecayFitProblem &problem) const {
        std::vector<double> x = parameters;
        return model_->evaluate(x.data(), problem);
    }

    /*!
     * \brief The model curve at \p parameters, with no reference to the data.
     *
     * What simulation and plotting need, and what `evaluate` cannot give for a
     * model whose amplitude is profiled against the observed counts.
     *
     * \throws std::runtime_error when this model has no data-free curve.
     */
    std::vector<double> model_curve(const std::vector<double> &parameters,
                                    const DecayFitProblem &problem) const {
        // Validate before handing raw pointers to a kernel. `fit()` reaches
        // this check through `bind()`/`is_usable()`; `model_curve` used to skip
        // it entirely and wrote/read straight out of `problem.irf.data()` and
        // `background.data()`. An undersized response therefore produced a
        // plausible-looking curve *and* read past the end of the heap buffer,
        // which surfaces as a crash somewhere else entirely, one call later.
        problem.require_valid();
        std::vector<double> curve(problem.total_size(), 0.0);
        std::vector<double> x = parameters;
        x.resize(static_cast<std::size_t>(model_->n_parameters(problem)), 0.0);
        if (!model_->model_curve(x.data(), problem, curve.data())) {
            throw std::runtime_error(
                std::string("fit '") + name() +
                "' cannot produce a model curve independent of the data");
        }
        return curve;
    }

    /*! Whether `model_curve` is available for this model. */
    bool supports_model_curve() const {
        DecayFitProblem probe(1, 1, 1.0);
        probe.irf.assign(1, 0.0);
        probe.background.assign(1, 0.0);
        std::vector<double> x(static_cast<std::size_t>(
            model_->n_parameters(probe)), 0.0);
        std::vector<double> curve(1, 0.0);
        return model_->model_curve(x.data(), probe, curve.data());
    }

    /*!
     * \brief Optimise from \p parameters.
     *
     * `problem.model` is left evaluated at the optimum.
     */
    DecayFitOutcome fit(const std::vector<double> &parameters,
                        const DecayFitConstraints &constraints,
                        DecayFitProblem &problem) const {
        DecayFitOutcome out;
        out.parameters = parameters;
        out.parameters.resize(static_cast<std::size_t>(model_->n_parameters(problem)), 0.0);
        out.results.assign(static_cast<std::size_t>(model_->n_results(problem)), 0.0);
        out.objective = model_->fit(out.parameters.data(), constraints, problem,
                                    out.results.data());
        return out;
    }

    /*! Log posterior at \p parameters — log likelihood plus log prior. */
    double lnprob(const std::vector<double> &parameters,
                  const DecayFitConstraints &constraints,
                  DecayFitProblem &problem) const {
        return model_->lnprob(parameters.data(), constraints, problem);
    }

    /*!
     * \brief Fit every row **together**, with parameters shared between rows.
     *
     * See ::fit_linked. Constraints are defined over the concatenated vector,
     * so a link group spanning rows ties those rows to one shared value —
     * unlike `fit_many`, where the rows never meet.
     */
    DecayFitLinkedOutcome fit_linked(const DecayFitProblem &prototype,
                                     const std::vector<double> &data_matrix,
                                     std::size_t n_rows,
                                     std::size_t n_cols,
                                     const std::vector<double> &x0,
                                     const DecayFitConstraints &constraints,
                                     int max_iterations = 0) const {
        return ::fit_linked(*model_, prototype, data_matrix, n_rows, n_cols, x0,
                            constraints, max_iterations);
    }

    /*! \brief Fit every row of \p data_matrix, each independently. */
    DecayFitBatchOutcome fit_many(const DecayFitProblem &prototype,
                                  const std::vector<double> &data_matrix,
                                  std::size_t n_rows,
                                  std::size_t n_cols,
                                  const std::vector<double> &x0,
                                  const DecayFitConstraints &constraints) const {
        DecayFitBatchOutcome out;
        out.objective = fit_batch(*model_, prototype, data_matrix, n_rows, n_cols, x0,
                                  constraints, out.parameters, out.results);
        return out;
    }
};

#endif // TTTRLIB_DECAYFITMODEL_H

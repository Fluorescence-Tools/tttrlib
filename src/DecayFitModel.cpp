// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file DecayFitModel.cpp
 * \brief The fit-model factory and the one batch loop shared by every model.
 */
#include "DecayFitModel.h"

#include <cstdlib>
#include <map>
#include <mutex>

#include "ParallelFor.h"
#include "i_lbfgs.h"


namespace {

/*!
 * \brief How many workers a batch of \p n_rows should use.
 *
 * `parallel_for` would otherwise take every hardware thread, which ignores the
 * controls the batch fits have always honoured: `TTTRLIB_USE_OPENMP=0` to force
 * serial, and `TTTRLIB_NUM_THREADS` / `OMP_NUM_THREADS` to cap the count. Those
 * are documented and used, so the heuristic moved here with the batch loop
 * rather than being dropped with the per-model loops it used to live in.
 *
 * Small batches stay serial because thread setup would dominate, and the count
 * is capped at roughly one worker per 512 rows so a short batch does not spawn
 * more threads than it has work for.
 */
unsigned int batch_threads(std::size_t n_rows) {
    if (n_rows < 1024) return 1;
    const char *disabled = std::getenv("TTTRLIB_USE_OPENMP");
    if (disabled != nullptr &&
        (disabled[0] == '0' || disabled[0] == 'f' || disabled[0] == 'F' ||
         disabled[0] == 'n' || disabled[0] == 'N')) {
        return 1;
    }
    unsigned int requested = 0;
    for (const char *name : {"TTTRLIB_NUM_THREADS", "OMP_NUM_THREADS"}) {
        const char *value = std::getenv(name);
        if (value != nullptr) {
            const int parsed = std::atoi(value);
            if (parsed > 0) { requested = static_cast<unsigned int>(parsed); break; }
        }
    }
    if (requested == 0) requested = 4;
    return std::max(1u, std::min(requested, static_cast<unsigned int>(n_rows / 512)));
}

/*!
 * \brief The name → factory table.
 *
 * A function-local static rather than a namespace-scope object: models register
 * from static initialisers in their own translation units, and a namespace-scope
 * map could still be unconstructed when the first of them runs. Reaching the
 * table only through this accessor makes it exist on first use, whenever that is.
 */
std::map<std::string, DecayFitFactory> &factory_table() {
    static std::map<std::string, DecayFitFactory> table;
    return table;
}

std::mutex &factory_mutex() {
    static std::mutex m;
    return m;
}

}  // namespace


void register_decay_fit(const std::string &name, DecayFitFactory factory) {
    std::lock_guard<std::mutex> guard(factory_mutex());
    factory_table()[name] = std::move(factory);
}


std::shared_ptr<const DecayFitModel> make_decay_fit(
    const std::string &name,
    const std::vector<double> &setup,
    const std::vector<double> &irf) {
    DecayFitFactory factory;
    {
        std::lock_guard<std::mutex> guard(factory_mutex());
        auto &table = factory_table();
        auto it = table.find(name);
        if (it == table.end()) {
            std::string known;
            for (const auto &entry : table) {
                if (!known.empty()) known += ", ";
                known += entry.first;
            }
            throw std::invalid_argument(
                "unknown decay fit '" + name + "'; available: " +
                (known.empty() ? std::string("(none registered)") : known));
        }
        factory = it->second;
    }
    return factory(setup, irf);
}


std::vector<std::string> decay_fit_names() {
    std::lock_guard<std::mutex> guard(factory_mutex());
    std::vector<std::string> names;
    names.reserve(factory_table().size());
    for (const auto &entry : factory_table()) names.push_back(entry.first);
    return names;
}


std::vector<double> fit_batch(
    const DecayFitModel &model,
    const DecayFitProblem &prototype,
    const std::vector<double> &data_matrix,
    std::size_t n_rows,
    std::size_t n_cols,
    const std::vector<double> &x0,
    const DecayFitConstraints &constraints,
    std::vector<double> &x_out,
    std::vector<double> &results_out) {

    prototype.require_valid();

    if (n_cols != prototype.total_size()) {
        throw std::invalid_argument(
            "fit_batch: n_cols does not match the prototype's n_channels * n_bins");
    }
    if (data_matrix.size() < n_rows * n_cols) {
        throw std::invalid_argument("fit_batch: data_matrix is shorter than n_rows * n_cols");
    }

    const int n_par = model.n_parameters(prototype);
    const int n_res = model.n_results(prototype);

    // x0 is either one starting vector reused for every row, or one per row.
    // Anything else is a caller mistake worth naming rather than silently
    // reading past the end of the vector.
    const bool per_row_start = x0.size() == n_rows * static_cast<std::size_t>(n_par);
    if (!per_row_start && x0.size() != static_cast<std::size_t>(n_par)) {
        throw std::invalid_argument(
            "fit_batch: x0 must hold n_parameters values (shared) or "
            "n_rows * n_parameters (one start per row)");
    }

    std::vector<double> objective(n_rows, 0.0);
    x_out.assign(n_rows * static_cast<std::size_t>(std::max(n_par, 0)), 0.0);
    results_out.assign(n_rows * static_cast<std::size_t>(std::max(n_res, 0)), 0.0);

    if (n_rows == 0) return objective;

    // Work is split into chunks rather than individual rows so each worker copies
    // the problem once instead of per row. Rows of a batch are the same length and
    // cost roughly the same, so the balance lost against per-row stealing is
    // slight, while the copies saved are proportional to the batch size. The model
    // itself is never copied: it is immutable, which is what lets every worker
    // share one instance.
    // Exactly one chunk per worker, not more: `parallel_for` derives its thread
    // count from the number of items, so handing it more chunks than workers
    // would quietly reinstate the parallelism `TTTRLIB_USE_OPENMP=0` just asked
    // to switch off. Equal chunks balance well here because the rows of a batch
    // are the same length and cost about the same.
    const unsigned int n_workers = batch_threads(n_rows);
    const std::size_t target_chunks = std::min<std::size_t>(n_rows, n_workers);
    const std::size_t chunk = (n_rows + target_chunks - 1) / target_chunks;
    const int n_chunks = static_cast<int>((n_rows + chunk - 1) / chunk);

    tttrlib::parallel_for(n_chunks, [&](int c) {
        DecayFitProblem local = prototype;   // private working state per worker
        std::vector<double> x(static_cast<std::size_t>(n_par), 0.0);
        std::vector<double> res(static_cast<std::size_t>(std::max(n_res, 0)), 0.0);

        const std::size_t begin = static_cast<std::size_t>(c) * chunk;
        const std::size_t end = std::min(begin + chunk, n_rows);

        for (std::size_t row = begin; row < end; ++row) {
            std::copy(data_matrix.begin() + static_cast<std::ptrdiff_t>(row * n_cols),
                      data_matrix.begin() + static_cast<std::ptrdiff_t>((row + 1) * n_cols),
                      local.data.begin());
            local.reset_model();

            const double *start = per_row_start
                                      ? x0.data() + row * static_cast<std::size_t>(n_par)
                                      : x0.data();
            std::copy(start, start + n_par, x.begin());

            objective[row] = model.fit(x.data(), constraints, local,
                                       n_res > 0 ? res.data() : nullptr);

            // The fitted parameters are the point of the batch, so they travel
            // back per row alongside the results rather than being discarded
            // with the worker's scratch vector.
            std::copy(x.begin(), x.end(),
                      x_out.begin() +
                          static_cast<std::ptrdiff_t>(row * static_cast<std::size_t>(n_par)));

            if (n_res > 0) {
                std::copy(res.begin(), res.end(),
                          results_out.begin() +
                              static_cast<std::ptrdiff_t>(row * static_cast<std::size_t>(n_res)));
            }
        }
    });

    return objective;
}


namespace {

/*!
 * \brief What the joint objective needs on every evaluation.
 *
 * The optimiser takes a plain function pointer and a `void*`, so this is what
 * that pointer carries. It owns nothing.
 */
struct LinkedContext {
    const DecayFitModel *model = nullptr;
    const DecayFitLinkMap *map = nullptr;
    std::vector<DecayFitProblem> *problems = nullptr;
    std::vector<double> *full = nullptr;      // the concatenated parameter vector
    int n_parameters = 0;
    std::size_t n_rows = 0;
    int evaluations = 0;
};

/*!
 * \brief The joint objective: the summed row objectives, plus the priors.
 *
 * Rows are evaluated in parallel — each has its own problem, and the kernels
 * keep their working state in thread-local storage, so nothing is shared but the
 * immutable model.
 */
double linked_target(double *variables, void *pv) {
    auto *ctx = static_cast<LinkedContext *>(pv);
    ctx->evaluations++;

    // Expand the searched variables into the full vector. Fixed slots keep the
    // values already there; every slot of a link group receives the same value.
    ctx->map->expand(variables, ctx->full->data());

    const double lp = ctx->map->lnprior(ctx->full->data());
    if (!std::isfinite(lp)) return std::numeric_limits<double>::max();

    std::vector<double> per_row(ctx->n_rows, 0.0);
    tttrlib::parallel_for(static_cast<int>(ctx->n_rows), [&](int row) {
        const double *x = ctx->full->data() +
                          static_cast<std::size_t>(row) * ctx->n_parameters;
        per_row[row] = ctx->model->evaluate(x, (*ctx->problems)[row]);
    });

    double total = 0.0;
    for (double v : per_row) {
        if (!std::isfinite(v)) return std::numeric_limits<double>::max();
        total += v;
    }
    // Minimising -2 ln posterior: the objective is already a deviance, so the
    // log prior enters with the same factor rather than being added raw.
    return total - 2.0 * lp;
}

}  // namespace


DecayFitLinkedOutcome fit_linked(
    const DecayFitModel &model,
    const DecayFitProblem &prototype,
    const std::vector<double> &data_matrix,
    std::size_t n_rows,
    std::size_t n_cols,
    const std::vector<double> &x0,
    const DecayFitConstraints &constraints,
    int max_iterations) {

    prototype.require_valid();
    if (n_cols != prototype.total_size()) {
        throw std::invalid_argument(
            "fit_linked: n_cols does not match the prototype's n_channels * n_bins");
    }
    if (data_matrix.size() < n_rows * n_cols) {
        throw std::invalid_argument("fit_linked: data_matrix is shorter than n_rows * n_cols");
    }
    if (n_rows == 0) return DecayFitLinkedOutcome();

    const int n_par = model.n_parameters(prototype);
    const int n_res = model.n_results(prototype);
    const std::size_t n_slots = n_rows * static_cast<std::size_t>(n_par);

    const bool per_row_start = x0.size() == n_slots;
    if (!per_row_start && x0.size() != static_cast<std::size_t>(n_par)) {
        throw std::invalid_argument(
            "fit_linked: x0 must hold n_parameters values (shared) or "
            "n_rows * n_parameters (one start per row)");
    }

    // One problem per row: the rows are fitted together but each keeps its own
    // data and its own model curve.
    std::vector<DecayFitProblem> problems(n_rows, prototype);
    for (std::size_t row = 0; row < n_rows; ++row) {
        std::copy(data_matrix.begin() + static_cast<std::ptrdiff_t>(row * n_cols),
                  data_matrix.begin() + static_cast<std::ptrdiff_t>((row + 1) * n_cols),
                  problems[row].data.begin());
        problems[row].reset_model();
    }

    std::vector<double> full(n_slots, 0.0);
    for (std::size_t row = 0; row < n_rows; ++row) {
        const double *start = per_row_start
                                  ? x0.data() + row * static_cast<std::size_t>(n_par)
                                  : x0.data();
        std::copy(start, start + n_par,
                  full.begin() + static_cast<std::ptrdiff_t>(row * n_par));
    }

    const DecayFitLinkMap map(constraints, static_cast<int>(n_slots));

    DecayFitLinkedOutcome out;
    out.n_variables = map.n_variables();

    LinkedContext ctx;
    ctx.model = &model;
    ctx.map = &map;
    ctx.problems = &problems;
    ctx.full = &full;
    ctx.n_parameters = n_par;
    ctx.n_rows = n_rows;

    if (map.n_variables() > 0) {
        std::vector<double> variables(map.n_variables(), 0.0);
        map.contract(full.data(), variables.data());

        bfgs optimiser(linked_target, map.n_variables());
        if (max_iterations > 0) optimiser.maxiter = max_iterations;

        // Bounds come only from the priors: a bound *is* a uniform prior, so
        // there is no separate bounds array to disagree with them.
        std::vector<double> lower, upper;
        map.bounds(lower, upper);
        for (int v = 0; v < map.n_variables(); ++v) {
            if (std::isfinite(lower[v]) && std::isfinite(upper[v])) {
                optimiser.set_bounds(v, lower[v], upper[v]);
            }
        }

        const int info = optimiser.minimize(variables.data(), &ctx);
        map.expand(variables.data(), full.data());
        out.converged = (info != 5);      // 5 is the iteration-limit code
    } else {
        // Nothing to search: still evaluate once so the model curves and the
        // reported objective correspond to the parameters supplied.
        out.converged = true;
    }

    out.iterations = ctx.evaluations;
    out.parameters = full;

    // Harvest per-row results at the joint optimum. Everything is held, so this
    // scores the answer rather than re-fitting it row by row — which would undo
    // the linking that was the point.
    DecayFitConstraints all_held(std::vector<int>(static_cast<std::size_t>(n_par), -1));
    out.results.assign(n_rows * static_cast<std::size_t>(std::max(n_res, 0)), 0.0);
    out.row_objective.assign(n_rows, 0.0);

    std::vector<double> row_results(static_cast<std::size_t>(std::max(n_res, 0)), 0.0);
    for (std::size_t row = 0; row < n_rows; ++row) {
        std::vector<double> x(full.begin() + static_cast<std::ptrdiff_t>(row * n_par),
                              full.begin() + static_cast<std::ptrdiff_t>((row + 1) * n_par));
        out.row_objective[row] = model.fit(x.data(), all_held, problems[row],
                                           n_res > 0 ? row_results.data() : nullptr);
        if (n_res > 0) {
            std::copy(row_results.begin(), row_results.end(),
                      out.results.begin() +
                          static_cast<std::ptrdiff_t>(row * static_cast<std::size_t>(n_res)));
        }
        out.objective += out.row_objective[row];
    }
    return out;
}

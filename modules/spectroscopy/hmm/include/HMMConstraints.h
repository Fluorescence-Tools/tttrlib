// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file HMMConstraints.h
 * \brief **Constraints** — hard conditions imposed on an HMM fit, never scored.
 *
 * A constraint is asserted, not believed. `P(acceptor | state 3) = 0` for a
 * donor-only dark state contributes nothing to the objective; it is re-imposed
 * after every M-step and holds exactly. That is the whole distinction from a
 * \ref tttrlib::HmmRestraints "restraint", which *is* scored and can be traded
 * off against the likelihood.
 *
 * The two are separate types because IMP — the consumer of this interface —
 * keeps `Restraint` and `Constraint` separate, and because they arrive from
 * different places: restraints come from an external physics model as
 * serialised priors, constraints are asserted locally by whoever knows the
 * experiment.
 *
 * \par Why not express a constraint as a very sharp prior
 * It would be slower, only approximately satisfied, and it would put a large
 * term in the objective that swamps the comparison between models. A pinned
 * value should be pinned.
 *
 * \par The other reason constraints matter here
 * Fixing **anchors state identity**. States are exchangeable, so an index-keyed
 * restraint otherwise attaches to whichever state EM happened to place at that
 * index — the practical face of label switching. Pinning one entry of a state
 * removes the ambiguity for that state.
 */
#ifndef TTTRLIB_HMMCONSTRAINTS_H
#define TTTRLIB_HMMCONSTRAINTS_H

// Validation: KNOWN-ANSWER-TESTED 2026-08-17 -- unconstrained path bit-identical to the plain fit, fixed
//   emission stays exact, a constraint is never scored. test/python/hmm/test_constraints.py.
//   Register: okf/testing/algorithm-validation.md

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

// Constraints carry no priors, so this header no longer reaches DecayFitPrior.h
// for the alias -- declare it here rather than depend on an include order.
using json = nlohmann::json;

namespace tttrlib {

/*!
 * \brief Fixed entries of π, A and B for an `(n_states, p)` model.
 *
 * NaN means "free", so the mask and the values are one object rather than two
 * that can disagree.
 */
class HmmConstraints {

public:

    HmmConstraints() = default;

    HmmConstraints(int n_states, int p)
        : n_states_(n_states), p_(p),
          fixed_prior_(n_states, nan()),
          fixed_trans_(std::size_t(n_states) * n_states, nan()),
          fixed_obs_(std::size_t(n_states) * p, nan()) {}

    int n_states() const { return n_states_; }
    int n_symbols() const { return p_; }

    /// Pin one emission probability, e.g. a donor-only (dark acceptor) state.
    void fix_emission(int state, int symbol, double value) {
        bounds(state, n_states_, "state");
        bounds(symbol, p_, "symbol");
        fixed_obs_[std::size_t(state) * p_ + symbol] = value;
    }

    void fix_transition(int from, int to, double value) {
        bounds(from, n_states_, "from");
        bounds(to, n_states_, "to");
        fixed_trans_[std::size_t(from) * n_states_ + to] = value;
    }

    void fix_initial(int state, double value) {
        bounds(state, n_states_, "state");
        fixed_prior_[state] = value;
    }

    const std::vector<double>& fixed_prior() const { return fixed_prior_; }
    const std::vector<double>& fixed_trans() const { return fixed_trans_; }
    const std::vector<double>& fixed_obs() const { return fixed_obs_; }

    /*! True when nothing is pinned. */
    bool is_empty() const {
        for (double f : fixed_prior_) if (!std::isnan(f)) return false;
        for (double f : fixed_trans_) if (!std::isnan(f)) return false;
        for (double f : fixed_obs_) if (!std::isnan(f)) return false;
        return true;
    }

    /*!
     * \brief Impose the fixed entries on an already-normalised block.
     *
     * Pinned entries take their exact values and each row's *free* remainder is
     * rescaled to whatever budget is left, so the row still sums to 1.
     *
     * Static and standalone because it is needed in two places: after the M-step,
     * and after SQUAREM's extrapolation — which projects a parameter vector back
     * onto the simplex without running an M-step and would otherwise let an
     * accepted candidate silently violate the constraint.
     */
    static void impose(std::vector<double>& value, int n_rows, int n_cols,
                       const std::vector<double>& fixed);

    void validate() const;

    json to_json() const;
    static HmmConstraints from_json(const json& state);
    std::string to_json_string(int indent = -1) const;
    static HmmConstraints from_json_string(const std::string& text);

private:

    static double nan() { return std::numeric_limits<double>::quiet_NaN(); }

    static void bounds(int i, int n, const char* what) {
        if (i < 0 || i >= n) throw std::invalid_argument(std::string(what) + " index out of range");
    }

    int n_states_ = 0;
    int p_ = 0;
    std::vector<double> fixed_prior_, fixed_trans_, fixed_obs_;
};

} // namespace tttrlib

#endif // TTTRLIB_HMMCONSTRAINTS_H

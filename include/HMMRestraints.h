// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file HMMRestraints.h
 * \brief **Restraints** — soft, scored terms that bias an HMM fit.
 *
 * A restraint contributes to the objective and *can* be violated: the fit
 * trades it off against the likelihood. That is the whole distinction from a
 * \ref tttrlib::HmmConstraints "constraint", which is imposed and never scored.
 * The two are kept apart deliberately, because IMP — the consumer of this
 * interface — keeps `Restraint` and `Constraint` apart, and because they enter
 * from different places: restraints arrive from an external physics model as
 * serialised priors, while constraints are asserted locally by the user.
 *
 * Two kinds live here.
 *
 * **Dirichlet concentrations on π, A and B.** These are *exactly conjugate* to
 * what the E-step accumulates. The E-step produces raw expected counts and the
 * M-step is `row_normalize` over them, so a Dirichlet restraint is literally
 * `counts + alpha - 1` before normalising — no approximation, no blending, no
 * tuning knob. Beta is the two-column case, so "this state's acceptor fraction
 * is near 0.7" is expressible directly.
 *
 * **Arbitrary priors on decay parameters**, via the existing \ref DecayFitPrior
 * taxonomy. These are the non-conjugate kinds (Normal, LogNormal, …). They have
 * no exact simplex M-step, which is exactly why they belong on *decay*
 * parameters — a lifetime or a stream split, where the emission row is generated
 * from a handful of numbers and a bounded search is honest and cheap.
 *
 * \par Why non-conjugate priors are refused on the simplex
 * There is no exact M-step for them there, and the "blend the estimate toward
 * the prior mode" trick that looks like an answer optimises no objective at all
 * — it breaks EM's monotonicity and makes the reported log-likelihood
 * meaningless. Better to reject than to approximate silently.
 *
 * \par No physics here
 * This header knows nothing about Förster radii, linker widths or crosstalk.
 * Physics lives outside the engine and enters *through* these restraints: a
 * structure constrains a distance, an external model maps that to a prior on a
 * lifetime and a stream split, and the engine sees only the prior.
 */
#ifndef TTTRLIB_HMMRESTRAINTS_H
#define TTTRLIB_HMMRESTRAINTS_H

#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "DecayFitPrior.h"

namespace tttrlib {

/*!
 * \brief Scored priors for an `(n_states, p)` model.
 *
 * All arrays are row-major. A concentration of 1 everywhere restrains nothing:
 * `is_flat()` reports that, and MAP is then required to be *bit-identical* to
 * plain EM.
 */
class HmmRestraints {

public:

    HmmRestraints() = default;

    HmmRestraints(int n_states, int p)
        : n_states_(n_states), p_(p),
          alpha_prior_(n_states, 1.0),
          alpha_trans_(std::size_t(n_states) * n_states, 1.0),
          alpha_obs_(std::size_t(n_states) * p, 1.0) {}

    int n_states() const { return n_states_; }
    int n_symbols() const { return p_; }

    void set_alpha_prior(std::vector<double> a) { alpha_prior_ = check(std::move(a), n_states_, "alpha_prior"); }
    void set_alpha_trans(std::vector<double> a) { alpha_trans_ = check(std::move(a), std::size_t(n_states_) * n_states_, "alpha_trans"); }
    void set_alpha_obs(std::vector<double> a)   { alpha_obs_   = check(std::move(a), std::size_t(n_states_) * p_, "alpha_obs"); }

    const std::vector<double>& alpha_prior() const { return alpha_prior_; }
    const std::vector<double>& alpha_trans() const { return alpha_trans_; }
    const std::vector<double>& alpha_obs() const { return alpha_obs_; }

    /*!
     * \brief Diagonal-dominant transition restraint — states persist.
     *
     * The "sticky" idea from the sticky HDP-HMM as a user knob: `strength`
     * pseudo-counts on each self-transition against `off` on each escape. It
     * earns its keep exactly when the data are too short for EM to resolve slow
     * dynamics, which is when EM invents spurious fast switching.
     */
    static HmmRestraints sticky(int n_states, int p, double strength = 100.0, double off = 1.0) {
        HmmRestraints r(n_states, p);
        std::vector<double> a(std::size_t(n_states) * n_states, off);
        for (int i = 0; i < n_states; ++i) a[std::size_t(i) * n_states + i] = strength;
        r.set_alpha_trans(std::move(a));
        return r;
    }

    /*! Prior on a decay parameter, indexed by `(state, parameter)`. */
    void set_decay_prior(int state, int index, std::shared_ptr<DecayFitPrior> prior) {
        if (state < 0 || state >= n_states_) throw std::invalid_argument("state index out of range");
        if (index < 0) throw std::invalid_argument("decay prior index must be >= 0");
        auto& row = decay_priors_[state];
        if (int(row.size()) <= index) row.resize(index + 1);
        row[index] = std::move(prior);
    }

    /*! Log-density of the decay priors at `values` for one state; 0 when none set. */
    double decay_log_prior(int state, const std::vector<double>& values) const {
        auto it = decay_priors_.find(state);
        if (it == decay_priors_.end()) return 0.0;
        double lp = 0.0;
        for (std::size_t i = 0; i < it->second.size() && i < values.size(); ++i)
            if (it->second[i]) lp += it->second[i]->lnpdf(values[i]);
        return lp;
    }

    /*! True when nothing is restrained; MAP is then identical to MLE, bit for bit. */
    bool is_flat() const {
        for (double a : alpha_prior_) if (a != 1.0) return false;
        for (double a : alpha_trans_) if (a != 1.0) return false;
        for (double a : alpha_obs_) if (a != 1.0) return false;
        return decay_priors_.empty();
    }

    /*!
     * \brief Add the Dirichlet pseudo-counts to one block of raw expected counts.
     *
     * Leaves the block **unnormalised** — normalisation and any hard constraint
     * are applied by the caller afterwards, in that order, so a pinned entry is
     * never perturbed by a later rescale.
     */
    void add_pseudocounts(std::vector<double>& counts, const std::vector<double>& alpha) const;

    /*!
     * \brief `sum (alpha - 1) log theta` — the parameter-dependent log prior.
     *
     * Dirichlet normalising constants do not depend on the parameters, so they
     * cannot change which model is preferred and are dropped. **This is what the
     * convergence test and the SQUAREM accept test must compare**; using the
     * marginal log-likelihood instead climbs a different hill.
     */
    double log_prior(const std::vector<double>& prior,
                     const std::vector<double>& trans,
                     const std::vector<double>& obs) const;

    json to_json() const;
    static HmmRestraints from_json(const json& state);
    std::string to_json_string(int indent = -1) const;
    static HmmRestraints from_json_string(const std::string& text);

private:

    static std::vector<double> check(std::vector<double> a, std::size_t want, const char* what) {
        if (a.size() != want) throw std::invalid_argument(std::string(what) + " has the wrong size");
        for (double v : a) if (!(v > 0.0)) throw std::invalid_argument(std::string(what) + " must be > 0");
        return a;
    }

    int n_states_ = 0;
    int p_ = 0;
    std::vector<double> alpha_prior_, alpha_trans_, alpha_obs_;
    std::map<int, std::vector<std::shared_ptr<DecayFitPrior>>> decay_priors_;
};

} // namespace tttrlib

#endif // TTTRLIB_HMMRESTRAINTS_H

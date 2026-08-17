// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file HmmVB.h
 * \brief Variational Bayes for the photon-stream HMM.
 *
 * Mean-field VB with Dirichlet factors over \f$(\pi, A, B)\f$. The E-step is
 * the existing forward-backward with geometric-mean weights
 * \f$\tilde\theta = \exp(E_q[\log\theta])\f$ substituted for the point
 * estimates, so no new kernel is required — `HMM::evaluate` is called verbatim.
 *
 * The ELBO is the standard conjugate-exponential form:
 *
 * \f[
 *   \mathcal{L} = \log\tilde Z
 *     - \mathrm{KL}(q(\pi)\|p(\pi))
 *     - \sum_i \mathrm{KL}(q(A_i)\|p(A_i))
 *     - \sum_i \mathrm{KL}(q(B_i)\|p(B_i))
 * \f]
 *
 * where \f$\log\tilde Z\f$ is the log-normaliser of the forward pass under the
 * sub-stochastic weights (\f$\tilde A^{\Delta t}\f$ over a gap, no row
 * normalisation) — Beal's bound, `HmmVB::elbo`. The ELBO doubles as the
 * model-selection criterion, filling the gap that priors open up under BIC.
 * It is conservative: ln K! and the mean-field gap are not recovered.
 *
 * The *iteration* runs on `HMM::evaluate`, whose \f$A^{\Delta t}\f$ cache
 * row-normalises \f$\tilde A\f$ before every composition. Its fixed point is
 * the VB one to ~1e-4 relative (hmmlearn A/B), and its data term is exactly
 * K(K−1)/2 nat above the sub-stochastic one; that value is kept as
 * `HmmVB::loglik` / `elbo_normalised`, the bound is computed once at the
 * returned posterior (one extra forward pass).
 *
 * \par Why the E-step reuses unchanged
 * Standard VB-HMM assumes one transition per observation. Here photons are
 * separated by \f$\Delta t\f$ ticks and the chain propagates as \f$A^{\Delta
 * t}\f$, so it is not obvious that the usual mean-field update applies. It
 * does, because the latent variable is the full tick-level path, not the
 * photon-level one. The complete-data likelihood is a product over ticks, so
 * \f$E_q[\log p(z|A)] = \sum_{ij} n_{ij} E_q[\log A_{ij}]\f$ with \f$n_{ij}\f$
 * the one-tick transition counts. The variational weight per tick is therefore
 * exactly \f$\tilde A_{ij} = \exp(E_q[\log A_{ij}])\f$, and marginalising the
 * unobserved intermediate ticks of a chain weighted by \f$\tilde A\f$ gives
 * exactly \f$\tilde A^{\Delta t}\f$. So the VB E-step *is* the existing
 * forward-backward with \f$\tilde A\f$ substituted for \f$A\f$.
 */
#ifndef TTTRLIB_HMMVB_H
#define TTTRLIB_HMMVB_H
// Validation: A/B-TESTED 2026-08-17 -- hmmlearn 0.3.3 VariationalCategoricalHMM on dense
//   (dt = 1) streams: posterior alpha 1e-4 rel, its lower bound at our posterior = elbo to
//   2e-10, elbo_normalised - elbo = K(K-1)/2 (K = 2, 3); digamma vs scipy 1e-11; Dirichlet
//   KL closed form. test/python/hmm/test_ab_hmm_reference.py::TestVariationalBayes*.
//   Register: okf/testing/algorithm-validation.md

#include <cmath>
#include <limits>
#include <vector>

namespace tttrlib {

struct HmmModel;
// class, not struct: MSVC puts the class key in the mangled name, so a
// mismatched forward declaration renames every function taking one.
class HmmRestraints;
class HMM;

/*!
 * \brief Digamma \f$\psi(x)\f$ for strictly positive \f$x\f$.
 *
 * Uses the standard recurrence \f$\psi(x) = \psi(x+1) - 1/x\f$ up to the
 * asymptotic region, then the Stirling-type series
 * \f$\psi(x) \sim \ln x - \frac{1}{2x} - \frac{1}{12x^2} + \frac{1}{120x^4}
 * - \cdots\f$ which is accurate to ~1e-14 once \f$x \ge 6\f$.
 */
double digamma(double x);

/*!
 * \brief \f$\mathrm{KL}(\mathrm{Dir}(\mathbf{a}) \| \mathrm{Dir}(\mathbf{b}))\f$
 * for a single row of length \p n.
 *
 * Both \p a and \p b are concentration vectors of the same length. Used by the
 * ELBO computation and exposed for testing.
 */
double dirichlet_kl(const double* a, const double* b, int n);

/*!
 * \brief A variational posterior over \f$(\pi, A, B)\f$.
 *
 * Unlike a point estimate this carries uncertainty (`std`, `mean`) and a
 * model-selection score (`elbo`). The posterior is factorised as three
 * independent Dirichlet distributions, one per parameter row.
 */
struct HmmVB {
    /// Posterior concentration for the initial distribution \f$\pi\f$.
    std::vector<double> alpha_prior;
    /// Posterior concentrations for transition rows \f$A_i\f$, row-major.
    std::vector<double> alpha_trans;
    /// Posterior concentrations for emission rows \f$B_i\f$, row-major.
    std::vector<double> alpha_obs;
    /*!
     * \brief Evidence lower bound at the returned posterior — Beal's bound,
     *        the model-selection criterion.
     *
     * `loglik_beal − ΣKL(q‖p)`: the forward pass under the sub-stochastic
     * geometric-mean weights (\f$\tilde A^{\Delta t}\f$ marginalising the
     * unobserved ticks) minus the Dirichlet KL terms. This is the quantity the
     * header derives and what `hmmlearn`'s VB-HMM reports (A/B to 2e-10 on
     * dense streams). Conservative: ln K! (label switching) and the mean-field
     * gap are not recovered, so it trails the exact evidence by a few nat
     * growing with K — compare models by it, do not read it as log p(y).
     */
    double elbo = -std::numeric_limits<double>::infinity();
    /*!
     * \brief Data term of `elbo`: log-normaliser of the sub-stochastic
     *        forward pass at the returned posterior.
     */
    double loglik_beal = -std::numeric_limits<double>::infinity();
    /*!
     * \brief Log-normaliser of the last E-step as the engine evaluates it —
     *        the geometric-mean weights with \f$\tilde A\f$'s rows normalised
     *        before the tick power (`HMM::evaluate`).
     *
     * This is the fit's iteration variable, not a bound: it sits exactly
     * K(K−1)/2 nat above `loglik_beal` (½ nat per free transition parameter,
     * data-independent once every state is populated). Kept as the E-step's
     * own number; `elbo_normalised` is this minus the KL terms.
     */
    double loglik = -std::numeric_limits<double>::infinity();
    /*!
     * \brief `loglik − ΣKL`, the value `elbo` reported before 2026-08-17 and
     *        the convergence variable of the iteration (`history` holds it per
     *        iteration). Equals `elbo + K(K−1)/2` nat up to the last update.
     */
    double elbo_normalised = -std::numeric_limits<double>::infinity();
    int n_iter = 0;
    bool converged = false;
    /// `elbo_normalised` per iteration (the convergence variable), for diagnostics.
    std::vector<double> history;
    int n_micro_bins = 1;

    /// Posterior mean parameters, packaged as a model.
    HmmModel mean() const;

    /*!
     * \brief Marginal posterior standard deviations, packed as
     *        `[prior | trans | obs]`.
     *
     * Each entry's marginal is \f$\mathrm{Beta}(a_k, a_0 - a_k)\f$, so
     * \f$\mathrm{sd} = \sqrt{a_k(a_0 - a_k) / (a_0^2 (a_0+1))}\f$.
     */
    std::vector<double> std() const;
};

/*!
 * \brief Mean-field VB over \f$(\pi, A, B)\f$ with Dirichlet factors.
 *
 * \param hmm The fitted HMM engine (photon data already loaded).
 * \param init Initial model — one E-step at its plain parameters seeds the
 *        first posterior concentrations.
 * \param restraints Dirichlet prior concentrations; flat (Dir(1) everywhere)
 *        if null.
 * \param max_iter Maximum VB iterations.
 * \param tol Convergence tolerance on `elbo_normalised` between iterations.
 *
 * Fixed entries are not supported: a pinned parameter is a point mass, not a
 * Dirichlet. Use `HMM::optimize` for constrained point estimates, or a sharp
 * prior if a soft version of the constraint is what is actually meant.
 */
HmmVB fit_vb(
    const HMM& hmm, const HmmModel& init,
    const HmmRestraints* restraints = nullptr,
    int max_iter = 300, double tol = 1e-7
);

} // namespace tttrlib

#endif // TTTRLIB_HMMVB_H

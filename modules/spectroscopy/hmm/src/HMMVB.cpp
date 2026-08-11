// SPDX-License-Identifier: BSD-3-Clause
//
// Variational Bayes for the photon-stream HMM. See HMMVB.h for the math.

#include "HMMVB.h"
#include "HMM.h"
#include "HMMRestraints.h"

#include <cmath>
#include <stdexcept>

namespace tttrlib {

// -- special functions --------------------------------------------------------

double digamma(double x) {
    if (x <= 0.0 || !std::isfinite(x))
        throw std::invalid_argument("digamma requires x > 0");
    double result = 0.0;
    // psi(x) = psi(x+1) - 1/x, applied until x >= 6
    while (x < 6.0) {
        result -= 1.0 / x;
        x += 1.0;
    }
    // Stirling-type asymptotic series: accurate to ~1e-14 for x >= 6
    static const double c[5] = {1.0/12.0, 1.0/120.0, 1.0/252.0, 1.0/240.0, 1.0/132.0};
    double f = 1.0 / (x * x);
    double series = c[0] - f * (c[1] - f * (c[2] - f * (c[3] - f * c[4])));
    result += std::log(x) - 0.5 / x - f * series;
    return result;
}

double dirichlet_kl(const double* a, const double* b, int n) {
    double a0 = 0.0, b0 = 0.0;
    for (int i = 0; i < n; ++i) { a0 += a[i]; b0 += b[i]; }
    double kl = std::lgamma(a0) - std::lgamma(b0);
    double psi_a0 = digamma(a0);
    for (int i = 0; i < n; ++i) {
        kl -= std::lgamma(a[i]);
        kl += std::lgamma(b[i]);
        kl += (a[i] - b[i]) * (digamma(a[i]) - psi_a0);
    }
    return kl;
}

// -- helpers ------------------------------------------------------------------

namespace {

// exp(E_q[log theta]) for a Dirichlet row: geometric-mean weights.
// Rows sum to < 1 — these are not probabilities; the forward pass's scaling
// handles the deficit.
void tilde_row(const double* alpha, int n, double* out) {
    double a0 = 0.0;
    for (int i = 0; i < n; ++i) a0 += alpha[i];
    double psi_a0 = digamma(a0);
    for (int i = 0; i < n; ++i)
        out[i] = std::exp(digamma(alpha[i]) - psi_a0);
}

// Posterior mean of each Dirichlet row.
void mean_row(const double* alpha, int n, double* out) {
    double a0 = 0.0;
    for (int i = 0; i < n; ++i) a0 += alpha[i];
    for (int i = 0; i < n; ++i) out[i] = alpha[i] / a0;
}

// Marginal posterior sd: each entry ~ Beta(a_k, a0 - a_k).
void std_row(const double* alpha, int n, double* out) {
    double a0 = 0.0;
    for (int i = 0; i < n; ++i) a0 += alpha[i];
    for (int i = 0; i < n; ++i)
        out[i] = std::sqrt(alpha[i] * (a0 - alpha[i]) /
                           (a0 * a0 * (a0 + 1.0)));
}

double sum_kl(const std::vector<double>& post, const std::vector<double>& prior,
              int row_size) {
    int n_rows = static_cast<int>(post.size()) / row_size;
    double total = 0.0;
    for (int r = 0; r < n_rows; ++r)
        total += dirichlet_kl(post.data() + r * row_size,
                              prior.data() + r * row_size, row_size);
    return total;
}

} // anonymous namespace

// -- HmmVB methods ------------------------------------------------------------

HmmModel HmmVB::mean() const {
    int n = static_cast<int>(alpha_prior.size());
    int p = n > 0 ? static_cast<int>(alpha_obs.size()) / n : 0;
    std::vector<double> mp(n), mt(n * n), mo(n * p);
    mean_row(alpha_prior.data(), n, mp.data());
    for (int i = 0; i < n; ++i)
        mean_row(alpha_trans.data() + i * n, n, mt.data() + i * n);
    for (int i = 0; i < n; ++i)
        mean_row(alpha_obs.data() + i * p, p, mo.data() + i * p);
    HmmModel m(std::move(mp), std::move(mt), std::move(mo));
    m.loglik = loglik;
    m.logpost = elbo;
    m.n_iter = n_iter;
    m.converged = converged;
    m.n_micro_bins = n_micro_bins;
    return m;
}

std::vector<double> HmmVB::std() const {
    int n = static_cast<int>(alpha_prior.size());
    int p = n > 0 ? static_cast<int>(alpha_obs.size()) / n : 0;
    std::vector<double> result(alpha_prior.size() + alpha_trans.size() +
                               alpha_obs.size());
    double* ptr = result.data();
    std_row(alpha_prior.data(), n, ptr); ptr += n;
    for (int i = 0; i < n; ++i) {
        std_row(alpha_trans.data() + i * n, n, ptr);
        ptr += n;
    }
    for (int i = 0; i < n; ++i) {
        std_row(alpha_obs.data() + i * p, p, ptr);
        ptr += p;
    }
    return result;
}

// -- fit_vb -------------------------------------------------------------------

// Exported explicitly: the WINDOWS_EXPORT_ALL_SYMBOLS .def generation drops
// exactly this symbol on MSVC (both generators; every other hmm export
// survives), and the extension then fails to link. dllexport on the
// definition costs nothing elsewhere -- MinGW accepts it, and in a STATIC
// module build it is ignored.
#ifdef _WIN32
__declspec(dllexport)
#endif
HmmVB fit_vb(
    const HMM& hmm, const HmmModel& init,
    const HmmRestraints* restraints,
    int max_iter, double tol
) {
    int n = init.n_states();
    int p = init.n_symbols();
    if (n < 1 || p < 1)
        throw std::invalid_argument("fit_vb: model must have >= 1 state and symbol");

    // Prior concentrations (Dir(1) everywhere if no restraints supplied).
    std::vector<double> a0_prior(n, 1.0);
    std::vector<double> a0_trans(static_cast<size_t>(n) * n, 1.0);
    std::vector<double> a0_obs(static_cast<size_t>(n) * p, 1.0);
    if (restraints) {
        a0_prior = restraints->alpha_prior();
        a0_trans = restraints->alpha_trans();
        a0_obs = restraints->alpha_obs();
    }

    // Seed q(theta) from the initial point model: one E-step at its plain
    // parameters gives counts, and those counts define the first posterior.
    HmmEval seed = hmm.evaluate(init);
    std::vector<double> a_prior = a0_prior;
    std::vector<double> a_trans = a0_trans;
    std::vector<double> a_obs = a0_obs;
    for (int i = 0; i < n; ++i) a_prior[i] += seed.prior_counts[i];
    for (size_t i = 0; i < a_trans.size(); ++i) a_trans[i] += seed.xi[i];
    for (size_t i = 0; i < a_obs.size(); ++i) a_obs[i] += seed.gamma_obs[i];

    HmmVB result;
    result.n_micro_bins = init.n_micro_bins;

    double prev = -std::numeric_limits<double>::infinity();
    double elbo = -std::numeric_limits<double>::infinity();
    double log_z = -std::numeric_limits<double>::infinity();

    for (int it = 1; it <= max_iter; ++it) {
        // Geometric-mean weights from the current posterior.
        std::vector<double> pi_t(n), A_t(static_cast<size_t>(n) * n),
            B_t(static_cast<size_t>(n) * p);
        tilde_row(a_prior.data(), n, pi_t.data());
        for (int i = 0; i < n; ++i)
            tilde_row(a_trans.data() + i * n, n, A_t.data() + i * n);
        for (int i = 0; i < n; ++i)
            tilde_row(a_obs.data() + i * p, p, B_t.data() + i * p);

        HmmModel model(pi_t, A_t, B_t);
        model.n_micro_bins = init.n_micro_bins;
        HmmEval ev = hmm.evaluate(model);
        log_z = ev.loglik;

        // ELBO belongs to the q(theta) that produced these tilde weights.
        double kl = dirichlet_kl(a_prior.data(), a0_prior.data(), n);
        kl += sum_kl(a_trans, a0_trans, n);
        kl += sum_kl(a_obs, a0_obs, p);
        elbo = log_z - kl;
        result.history.push_back(elbo);

        // Update posterior concentrations from the E-step counts.
        for (int i = 0; i < n; ++i) a_prior[i] = a0_prior[i] + ev.prior_counts[i];
        for (size_t i = 0; i < a_trans.size(); ++i) a_trans[i] = a0_trans[i] + ev.xi[i];
        for (size_t i = 0; i < a_obs.size(); ++i) a_obs[i] = a0_obs[i] + ev.gamma_obs[i];

        result.n_iter = it;
        if (it > 1 && std::abs(elbo - prev) < tol) {
            result.converged = true;
            break;
        }
        prev = elbo;
    }

    result.alpha_prior = std::move(a_prior);
    result.alpha_trans = std::move(a_trans);
    result.alpha_obs = std::move(a_obs);
    result.elbo = elbo;
    result.loglik = log_z;
    return result;
}

} // namespace tttrlib

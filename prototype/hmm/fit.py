"""Point estimation: maximum likelihood (classic H2MM) and MAP.

``fit`` with no constraints is Baum-Welch and reproduces the C++ engine's
``optimize``.  ``fit`` with constraints is MAP, and the only difference is that
the M-step adds Dirichlet pseudo-counts and honours fixed entries -- both exact,
neither an approximation.

The convergence test is on the **penalised** objective ``loglik + log p(theta)``,
not on the marginal log-likelihood.  Under a prior the EM fixed point belongs to
the penalised map, so testing the wrong one would stop in the wrong place and,
worse, would make a monotonicity check pass while the algorithm climbed a
different hill.  With a flat prior the two coincide exactly.
"""
from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from .constraints import HmmConstraints, row_normalize
from .core import PhotonData, e_step, matrix_powers

__all__ = ["HmmModel", "fit", "random_model"]


@dataclass
class HmmModel:
    """``lambda = {pi, A, B}`` plus what the fit reports about itself."""
    prior: np.ndarray
    trans: np.ndarray
    obs: np.ndarray
    loglik: float = -np.inf
    logpost: float = -np.inf
    n_iter: int = 0
    converged: bool = False
    history: list = field(default_factory=list)

    @property
    def n_states(self):
        return len(self.prior)

    @property
    def p(self):
        return self.obs.shape[1]

    def n_free(self, n_micro_bins=1):
        """Free parameters, for BIC.

        Valid for the **free categorical** emission only.  A physically
        parameterised emission has far fewer -- one FRET efficiency per state
        rather than ``p - 1`` -- so it must report its own count, and BIC must
        not be taken from here in that case.
        """
        n, p = self.n_states, self.p
        return n * n + (p - 1) * n - 1

    def bic(self, n_photons):
        """``-2 logL + k ln N``.

        Defined on the **marginal log-likelihood** only.  Under an informative
        prior the effective degrees of freedom shrink and this is no longer a
        valid criterion -- use the variational ELBO instead.
        """
        if not np.isfinite(self.loglik) or n_photons <= 0:
            return np.inf
        return -2.0 * self.loglik + self.n_free() * np.log(n_photons)

    def reorder(self, key=None):
        """Return a copy with states sorted, so index-keyed comparisons work.

        States are exchangeable, so a fit's state 0 is arbitrary.  ``key``
        defaults to the probability of the last symbol (acceptor fraction for a
        two-stream model), giving a canonical low-to-high-FRET ordering.
        """
        k = self.obs[:, -1] if key is None else np.asarray(key)
        order = np.argsort(k)
        return HmmModel(self.prior[order], self.trans[np.ix_(order, order)],
                        self.obs[order], self.loglik, self.logpost,
                        self.n_iter, self.converged, list(self.history))


def random_model(n_states, p, rng, trans_scale=1e-3):
    """A dispersed starting point, matching the engine's ``factory_model``."""
    rng = np.random.default_rng(rng) if not isinstance(rng, np.random.Generator) else rng
    prior = row_normalize(rng.random(n_states).reshape(1, -1)).ravel()
    trans = np.full((n_states, n_states), trans_scale)
    np.fill_diagonal(trans, 1.0 - trans_scale * (n_states - 1))
    obs = row_normalize(rng.random((n_states, p)) + 0.5)
    return HmmModel(prior, trans, obs)


def fit(data: PhotonData, init: HmmModel, constraints: HmmConstraints = None,
        max_iter=500, tol=1e-9, min_trans=1e-12, track=False) -> HmmModel:
    """EM to a (penalised) fixed point.

    Parameters
    ----------
    constraints : HmmConstraints, optional
        ``None`` (or a flat set) gives plain maximum likelihood.
    track : bool
        Record the objective at every iteration, so monotonicity can be
        asserted rather than assumed.
    """
    n = init.n_states
    if constraints is None:
        constraints = HmmConstraints.flat(n, data.p)
    if constraints.n_states != n or constraints.p != data.p:
        raise ValueError("constraints do not match the model / data alphabet")

    prior = row_normalize(np.asarray(init.prior, float).reshape(1, -1)).ravel()
    trans = row_normalize(np.asarray(init.trans, float))
    obs = row_normalize(np.asarray(init.obs, float))

    history = []
    prev = -np.inf
    converged = False
    it = 0
    loglik = -np.inf

    for it in range(1, max_iter + 1):
        apow = matrix_powers(trans, data.max_gap)
        prior_acc, gamma_obs, xi, loglik = e_step(data, prior, trans, obs, apow)

        # The objective belongs to the *input* model of this map.
        objective = loglik + constraints.log_prior(prior, trans, obs)
        if track:
            history.append(objective)

        prior = constraints.apply(prior_acc, "prior")
        trans = constraints.apply(xi, "trans")
        obs = constraints.apply(gamma_obs, "obs")

        if min_trans > 0.0:
            # Keep escape routes open: a state whose exit probability collapses
            # to exactly 0 can never be left again, and EM has no way back.
            off = ~np.eye(n, dtype=bool)
            free = np.isnan(constraints.fixed_trans)
            bump = off & free & (trans < min_trans)
            if bump.any():
                trans[bump] = min_trans
                trans = constraints.impose_fixed(row_normalize(trans), "trans")

        if it > 1 and objective - prev < tol:
            converged = True
            break
        prev = objective

    logpost = loglik + constraints.log_prior(prior, trans, obs)
    return HmmModel(prior, trans, obs, float(loglik), float(logpost),
                    it, converged, history)

"""Variational Bayes for the photon-stream HMM.

This is the intended default Bayesian engine.  It costs about one EM run, is
deterministic and reproducible, and yields the **ELBO** -- which doubles as the
model-selection criterion, filling the gap that priors open up under BIC.  Both
photon-based smFRET tools that went Bayesian took this route rather than MCMC
(Okamoto & Sako 2012 on time-stamp data; hFRET 2019 for hierarchical models,
selecting by ELBO).

Why it reuses the E-step *unchanged*
------------------------------------

Standard VB-HMM assumes one transition per observation.  Here photons are
separated by ``dt`` ticks and the chain propagates as ``A**dt``, so it is not
obvious that the usual mean-field update applies -- in general
``E_q[log (A**dt)_uv] != dt * E_q[log A]_uv``, and the natural worry is that
the variational E-step needs something intractable.

It does not, because the latent variable here is the **full tick-level path**,
not the photon-level one.  The complete-data likelihood is a product over
ticks, so

    E_q[log p(z | A)] = sum_ij n_ij * E_q[log A_ij]

with ``n_ij`` the one-tick transition counts.  The variational weight per tick
is therefore exactly ``Atilde_ij = exp(E_q[log A_ij])``, and marginalising the
unobserved intermediate ticks of a chain weighted by ``Atilde`` gives exactly
``Atilde**dt``.  So the VB E-step *is* the existing forward-backward with
``Atilde`` substituted for ``A``, and the expected one-tick counts it already
returns are precisely the ``E_q[n_ij]`` the M-step needs.

No approximation is introduced, and no new kernel is required -- ``e_step`` is
called verbatim.

The ELBO is the standard conjugate-exponential form

    L = log Ztilde - KL(q(pi) || p(pi)) - sum_i KL(q(A_i) || p(A_i))
                   - sum_i KL(q(B_i) || p(B_i))

where ``log Ztilde`` is the log-normaliser the forward pass already returns.
"""
from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from .constraints import HmmConstraints
from .core import PhotonData, e_step
from .fit import HmmModel
from .special import digamma, dirichlet_kl

__all__ = ["HmmVB", "fit_vb"]


def _tilde(alpha_hat):
    """``exp(E_q[log theta])`` for a Dirichlet with concentration ``alpha_hat``.

    Rows sum to less than 1 -- deliberately.  These are geometric-mean weights,
    not probabilities, and the forward pass's scaling handles the deficit.
    """
    a = np.atleast_2d(np.asarray(alpha_hat, dtype=np.float64))
    out = np.exp(digamma(a) - digamma(a.sum(axis=-1))[..., None])
    return out.reshape(np.shape(alpha_hat))


def _mean(alpha_hat):
    a = np.atleast_2d(np.asarray(alpha_hat, dtype=np.float64))
    out = a / a.sum(axis=-1, keepdims=True)
    return out.reshape(np.shape(alpha_hat))


def _std(alpha_hat):
    """Marginal posterior sd; each entry's marginal is Beta(a_k, a0 - a_k)."""
    a = np.atleast_2d(np.asarray(alpha_hat, dtype=np.float64))
    a0 = a.sum(axis=-1, keepdims=True)
    out = np.sqrt(a * (a0 - a) / (a0 * a0 * (a0 + 1.0)))
    return out.reshape(np.shape(alpha_hat))


@dataclass
class HmmVB:
    """A variational posterior over ``{pi, A, B}``.

    Unlike a point estimate this carries uncertainty (``std``, ``sample``,
    ``quantile``) and a model-selection score (``elbo``).
    """
    alpha_prior: np.ndarray
    alpha_trans: np.ndarray
    alpha_obs: np.ndarray
    elbo: float = -np.inf
    loglik_lb: float = -np.inf
    n_iter: int = 0
    converged: bool = False
    history: list = field(default_factory=list)

    @property
    def mean(self) -> HmmModel:
        """Posterior mean parameters, packaged as a model."""
        return HmmModel(_mean(self.alpha_prior), _mean(self.alpha_trans),
                        _mean(self.alpha_obs), self.loglik_lb, self.elbo,
                        self.n_iter, self.converged)

    @property
    def std(self):
        """``(prior, trans, obs)`` marginal posterior standard deviations."""
        return _std(self.alpha_prior), _std(self.alpha_trans), _std(self.alpha_obs)

    def sample(self, n_draws=1000, seed=0):
        """Draw models from the variational posterior.

        Exact for the factorised posterior (each row is an independent
        Dirichlet), and the honest way to get intervals without an incomplete
        beta inverse.
        """
        rng = np.random.default_rng(seed)
        draws = []
        for _ in range(n_draws):
            prior = rng.dirichlet(self.alpha_prior)
            trans = np.vstack([rng.dirichlet(r) for r in np.atleast_2d(self.alpha_trans)])
            obs = np.vstack([rng.dirichlet(r) for r in np.atleast_2d(self.alpha_obs)])
            draws.append(HmmModel(prior, trans, obs))
        return draws

    def quantile(self, q, n_draws=2000, seed=0):
        """``(prior, trans, obs)`` quantiles, by sampling the posterior."""
        draws = self.sample(n_draws, seed)
        return (np.quantile([d.prior for d in draws], q, axis=0),
                np.quantile([d.trans for d in draws], q, axis=0),
                np.quantile([d.obs for d in draws], q, axis=0))


def fit_vb(data: PhotonData, init: HmmModel, constraints: HmmConstraints = None,
           max_iter=300, tol=1e-7) -> HmmVB:
    """Mean-field VB over ``{pi, A, B}`` with Dirichlet factors.

    ``constraints`` supplies the *prior* concentrations.  A flat set gives the
    uninformative ``Dir(1)`` prior on every row.

    Fixed entries are not supported: a pinned parameter is a point mass, not a
    Dirichlet, and pretending otherwise would silently change the posterior.
    Use ``fit`` for constrained point estimates, or a sharp prior if a soft
    version of the constraint is what is actually meant.
    """
    n = init.n_states
    if constraints is None:
        constraints = HmmConstraints.flat(n, data.p)
    if constraints.n_states != n or constraints.p != data.p:
        raise ValueError("constraints do not match the model / data alphabet")
    if not (np.all(np.isnan(constraints.fixed_prior))
            and np.all(np.isnan(constraints.fixed_trans))
            and np.all(np.isnan(constraints.fixed_obs))):
        raise NotImplementedError(
            "fit_vb does not support fixed entries: a fixed parameter is a point "
            "mass, not a Dirichlet factor. Use fit() for constrained point "
            "estimates, or express the constraint as a sharp prior.")

    a0_prior = constraints.alpha_prior
    a0_trans = constraints.alpha_trans
    a0_obs = constraints.alpha_obs

    # Seed q(theta) from the initial point model: one E-step at its plain
    # parameters gives counts, and those counts define the first posterior.
    prior_acc, gamma_obs, xi, _ = e_step(
        data, init.prior, init.trans, init.obs)
    a_prior = a0_prior + prior_acc
    a_trans = a0_trans + xi
    a_obs = a0_obs + gamma_obs

    history = []
    prev = -np.inf
    converged = False
    it = 0
    log_z = -np.inf

    for it in range(1, max_iter + 1):
        pi_t = _tilde(a_prior)
        A_t = _tilde(a_trans)
        B_t = _tilde(a_obs)

        prior_acc, gamma_obs, xi, log_z = e_step(data, pi_t, A_t, B_t)

        # The ELBO belongs to the q(theta) that produced these tilde weights.
        kl = (float(dirichlet_kl(a_prior, a0_prior).sum())
              + float(dirichlet_kl(a_trans, a0_trans).sum())
              + float(dirichlet_kl(a_obs, a0_obs).sum()))
        elbo = log_z - kl
        history.append(elbo)

        a_prior = a0_prior + prior_acc
        a_trans = a0_trans + xi
        a_obs = a0_obs + gamma_obs

        if it > 1 and abs(elbo - prev) < tol:
            converged = True
            break
        prev = elbo

    return HmmVB(a_prior, a_trans, a_obs, float(elbo), float(log_z),
                 it, converged, history)

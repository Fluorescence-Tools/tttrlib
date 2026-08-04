"""Priors and hard constraints on an HMM's parameters.

.. note::

   **The shipped C++ splits this class in two, and its names are the right
   ones.**  ``tttrlib.HmmRestraints`` holds the soft, *scored* half (the
   Dirichlet concentrations) and ``tttrlib.HmmConstraints`` the hard, *imposed*
   half (the fixed entries).  A restraint contributes ``log p`` to the objective
   and can be violated; a constraint is asserted and never scored.  This module
   keeps them in one object because it predates that distinction -- it is the
   prototype the port was written from, not the naming authority.  Read
   ``include/HMMRestraints.h`` and ``include/HMMConstraints.h`` for the
   interface that actually ships.

Two mechanisms, deliberately distinct:

**Priors (soft).**  Dirichlet concentration parameters on each row of π, A and
B.  These are *exactly* conjugate to the E-step's sufficient statistics -- the
engine's M-step is ``row_normalize(xi_acc)`` over raw expected counts, so a
Dirichlet prior is literally ``counts + alpha - 1`` before normalising.  No
approximation, no blending, no tuning knob.  Beta is the two-column case of
Dirichlet, so a "FRET efficiency in [0.6, 0.9]" belief is expressible directly.

**Fixing (hard).**  A boolean mask plus values.  ``P(acceptor | state 3) = 0``
for a donor-only dark state is a constraint, not a belief, and expressing it as
a very sharp prior would be both slower and less exact.  Fixing also *anchors
state identity*, which is the practical mitigation for label switching: an
index-keyed prior otherwise attaches to whichever state EM happened to put at
that index.

Non-conjugate priors (Normal on a distance, LogNormal on a lifetime) are
deliberately **not** supported here.  On simplex entries there is no exact
M-step for them, and the blend-toward-the-mode trick that looks like an answer
optimises no objective at all.  They belong on *physical* parameters, where the
emission row is generated from one or two numbers and a bounded search is both
honest and cheap.
"""
from __future__ import annotations

import numpy as np

__all__ = ["HmmConstraints", "row_normalize"]


def row_normalize(x, axis=-1):
    """Scale so each row sums to 1, leaving all-zero rows uniform."""
    x = np.asarray(x, dtype=np.float64)
    total = x.sum(axis=axis, keepdims=True)
    out = np.where(total > 0, x / np.where(total > 0, total, 1.0),
                   1.0 / x.shape[axis])
    return out


class HmmConstraints:
    """Dirichlet priors and fixed entries for an ``(n_states, p)`` model.

    Parameters
    ----------
    alpha_trans : (n, n) array, optional
        Dirichlet concentration on each row of the transition matrix.  ``1``
        everywhere is the flat prior and reproduces plain EM exactly.
    alpha_obs : (n, p) array, optional
        Dirichlet concentration on each emission row.
    alpha_prior : (n,) array, optional
        Dirichlet concentration on the initial-state distribution.
    fixed_trans, fixed_obs, fixed_prior : arrays, optional
        Values to hold constant.  ``np.nan`` marks an entry as free, so the
        mask and the values are one object rather than two that can disagree.
    """

    def __init__(self, n_states, p,
                 alpha_trans=None, alpha_obs=None, alpha_prior=None,
                 fixed_trans=None, fixed_obs=None, fixed_prior=None):
        self.n_states = int(n_states)
        self.p = int(p)
        n = self.n_states

        self.alpha_trans = self._alpha(alpha_trans, (n, n))
        self.alpha_obs = self._alpha(alpha_obs, (n, self.p))
        self.alpha_prior = self._alpha(alpha_prior, (n,))

        self.fixed_trans = self._fixed(fixed_trans, (n, n))
        self.fixed_obs = self._fixed(fixed_obs, (n, self.p))
        self.fixed_prior = self._fixed(fixed_prior, (n,))
        self.validate()

    # -- construction helpers -------------------------------------------------

    @staticmethod
    def _alpha(a, shape):
        if a is None:
            return np.ones(shape, dtype=np.float64)
        a = np.asarray(a, dtype=np.float64)
        if a.shape != shape:
            raise ValueError(f"expected concentration of shape {shape}, got {a.shape}")
        return a

    @staticmethod
    def _fixed(f, shape):
        if f is None:
            return np.full(shape, np.nan, dtype=np.float64)
        f = np.asarray(f, dtype=np.float64)
        if f.shape != shape:
            raise ValueError(f"expected fixed array of shape {shape}, got {f.shape}")
        return f

    @classmethod
    def flat(cls, n_states, p):
        """The no-op constraint set: MAP under it is identical to MLE."""
        return cls(n_states, p)

    @classmethod
    def sticky(cls, n_states, p, strength=100.0, off=1.0):
        """Diagonal-dominant transition prior -- states persist.

        This is the "sticky" idea from the sticky HDP-HMM expressed as a user
        knob: ``strength`` pseudo-counts on each self-transition against ``off``
        on each escape.  Useful when the data are too short for EM to resolve
        slow dynamics, which is exactly when it invents spurious fast switching.
        """
        alpha = np.full((n_states, n_states), float(off))
        np.fill_diagonal(alpha, float(strength))
        return cls(n_states, p, alpha_trans=alpha)

    def with_dark_state(self, state, stream, value=0.0):
        """Pin one emission probability -- e.g. a donor-only (dark acceptor) state.

        Returns a new object; the receiver is unchanged.
        """
        fixed = self.fixed_obs.copy()
        fixed[state, stream] = float(value)
        return HmmConstraints(self.n_states, self.p,
                              self.alpha_trans, self.alpha_obs, self.alpha_prior,
                              self.fixed_trans, fixed, self.fixed_prior)

    # -- use ------------------------------------------------------------------

    def validate(self):
        for name, a in (("alpha_trans", self.alpha_trans),
                        ("alpha_obs", self.alpha_obs),
                        ("alpha_prior", self.alpha_prior)):
            if not np.all(a > 0):
                raise ValueError(f"{name} must be strictly positive")
        for name, f in (("fixed_trans", self.fixed_trans),
                        ("fixed_obs", self.fixed_obs),
                        ("fixed_prior", self.fixed_prior)):
            free = ~np.isnan(f)
            if np.any(f[free] < 0) or np.any(f[free] > 1):
                raise ValueError(f"{name} entries must lie in [0, 1]")
            if f.ndim == 2:
                rows = np.nansum(np.where(np.isnan(f), 0.0, f), axis=1)
            else:
                rows = np.array([np.where(np.isnan(f), 0.0, f).sum()])
            if np.any(rows > 1.0 + 1e-12):
                raise ValueError(f"{name} fixed entries exceed 1 in a row")

    @property
    def is_flat(self):
        """True when this imposes nothing -- MAP then equals MLE bit for bit."""
        return (np.all(self.alpha_trans == 1.0)
                and np.all(self.alpha_obs == 1.0)
                and np.all(self.alpha_prior == 1.0)
                and np.all(np.isnan(self.fixed_trans))
                and np.all(np.isnan(self.fixed_obs))
                and np.all(np.isnan(self.fixed_prior)))

    def apply(self, counts, kind):
        """MAP M-step for one parameter block.

        ``counts`` are the raw expected counts from the E-step.  Adds the
        Dirichlet pseudo-counts, normalises, then overwrites fixed entries and
        rescales the free remainder of each row so the row still sums to 1.
        """
        alpha = {"trans": self.alpha_trans,
                 "obs": self.alpha_obs,
                 "prior": self.alpha_prior}[kind]
        fixed = {"trans": self.fixed_trans,
                 "obs": self.fixed_obs,
                 "prior": self.fixed_prior}[kind]

        counts = np.asarray(counts, dtype=np.float64)
        post = np.maximum(counts + alpha - 1.0, 0.0)
        out = row_normalize(post.reshape(1, -1) if post.ndim == 1 else post)
        if post.ndim == 1:
            out = out.ravel()

        return self.impose_fixed(out, kind)

    def impose_fixed(self, value, kind):
        """Overwrite fixed entries and rescale each row's free remainder to fit.

        Shared by the M-step and by the ``min_trans`` floor, so a fixed entry
        cannot be silently perturbed by whichever of them runs last.
        """
        fixed = {"trans": self.fixed_trans,
                 "obs": self.fixed_obs,
                 "prior": self.fixed_prior}[kind]
        mask = ~np.isnan(fixed)
        value = np.asarray(value, dtype=np.float64)
        if not mask.any():
            return value

        out = np.atleast_2d(value.copy())
        m2 = np.atleast_2d(mask)
        f2 = np.atleast_2d(fixed)
        for r in range(out.shape[0]):
            if not m2[r].any():
                continue
            out[r, m2[r]] = f2[r, m2[r]]
            budget = 1.0 - out[r, m2[r]].sum()
            free = ~m2[r]
            if not free.any():
                continue
            s = out[r, free].sum()
            out[r, free] = (out[r, free] / s * budget) if s > 0 else budget / free.sum()
        return out.reshape(fixed.shape)

    def log_prior(self, prior, trans, obs):
        """Log prior density of a model, up to an additive constant.

        Only the ``sum (alpha - 1) log theta`` part: the Dirichlet normalising
        constants do not depend on the parameters, so they cannot change which
        model is preferred and are dropped.  Fixed entries contribute nothing.
        """
        tiny = np.finfo(float).tiny
        total = 0.0
        for a, th in ((self.alpha_prior, np.asarray(prior, float)),
                      (self.alpha_trans, np.asarray(trans, float)),
                      (self.alpha_obs, np.asarray(obs, float))):
            total += float(np.sum((a - 1.0) * np.log(np.maximum(th, tiny))))
        return total

    def __repr__(self):
        bits = []
        if not np.all(self.alpha_trans == 1.0):
            bits.append("trans prior")
        if not np.all(self.alpha_obs == 1.0):
            bits.append("obs prior")
        if not np.all(self.alpha_prior == 1.0):
            bits.append("initial prior")
        n_fixed = int(np.sum(~np.isnan(self.fixed_obs))
                      + np.sum(~np.isnan(self.fixed_trans))
                      + np.sum(~np.isnan(self.fixed_prior)))
        if n_fixed:
            bits.append(f"{n_fixed} fixed")
        return f"HmmConstraints(n={self.n_states}, p={self.p}, {', '.join(bits) or 'flat'})"

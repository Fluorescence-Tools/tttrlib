"""Simulation-based calibration — are the error bars honest?

Every other test in this package checks *arithmetic*: that the numbers produced
are the numbers intended, against brute-force enumeration.  None of them can
tell whether a posterior's **width** is right.  A sampler with a subtly wrong
bridge, or an off-by-one between "prior concentration" and "prior mode", can
reproduce the correct posterior *mean* and still report intervals that are far
too tight -- and every existing test would pass.

SBC (Talts, Betancourt, Simpson, Vehtari & Gelman, 2018,
`arXiv:1804.06788 <https://arxiv.org/abs/1804.06788>`_) closes that gap.  The
argument is one line: if you draw parameters from the prior and then data from
those parameters, the *data-averaged posterior* is the prior again.  So for each
replicate

    theta~ ~ p(theta)          draw from the prior
    y     ~ p(y | theta~)      simulate
    {theta_l} ~ p(theta | y)   L posterior draws
    rank  = #{l : theta_l < theta~}

the rank is uniform on ``{0, ..., L}`` whenever the posterior is correct.  This
is exact, not asymptotic, and it needs no ground-truth posterior to compare
against.

**Read the shape, not just the p-value.**

===================================  ===========================================
rank histogram                       diagnosis
===================================  ===========================================
flat                                 calibrated
U-shaped (mass at both ends)         posterior too **narrow** -- over-confident
inverted-U (central hump)            posterior too **wide**
sloped                               biased
===================================  ===========================================

Two traps specific to this model, both handled by the caller:

* **Exchangeable states.**  Comparing "true state 0" with "the draw's state 0"
  is meaningless when labels are arbitrary, and would manufacture a failure.
  Truth and draws must be put in the same canonical order first.
* **Autocorrelated draws.**  The uniformity result assumes independent draws, so
  Gibbs output must be thinned.  Use ``gibbs.ess`` to choose the factor.
"""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from .special import chi2_sf

__all__ = ["sbc_ranks", "rank_histogram", "uniformity_pvalue",
           "outer_mass_ratio", "SbcResult", "summarize",
           "add_background_photons", "SbcPrior", "TruncatedNormalPrior1D"]


def add_background_photons(times, streams, frac, rng, n_streams=2):
    """Insert uncorrelated photons at uniformly random times.

    Used to make SBC a *misspecification* detector.  The perturbation has to
    break structure the model cannot absorb: relabelling existing photons to
    random symbols does **not** work, because a free categorical emission simply
    learns the contaminated row and stays correctly specified -- measured flat at
    0/5/15% contamination, no trend.  Adding photons changes the inter-photon gap
    distribution instead, which a categorical HMM has no way to represent.

    Measured dose-response (tail mass vs a flat rank histogram, emission
    parameters): 1.2 at 0%, 1.8 at 20%, 3.0 at 50%, with p -> 0.  Transition
    parameters stay calibrated -- extra photons dilute the emission rows but
    leave the state trajectory, and hence the one-tick counts, intact.
    """
    out_t, out_s = [], []
    for tt, ss in zip(times, streams):
        tt = np.asarray(tt, dtype=np.int64)
        ss = np.asarray(ss, dtype=np.int64)
        k = int(frac * len(tt))
        if k > 0:
            tt = np.concatenate([tt, rng.integers(tt[0], tt[-1] + 1, k)])
            ss = np.concatenate([ss, rng.integers(0, n_streams, k)])
            order = np.argsort(tt, kind="stable")
            tt, ss = tt[order], ss[order]
        out_t.append(tt.tolist())
        out_s.append(ss.tolist())
    return out_t, out_s


def sbc_ranks(draw_prior, simulate, posterior_draws, n_replicates=300, seed=0,
              progress=None):
    """Run the SBC loop.

    Parameters
    ----------
    draw_prior : callable ``(rng) -> (theta_true, aux)``
        Draw parameters from the prior.  ``theta_true`` is the 1-D vector of
        scalars to be ranked (already in canonical state order); ``aux`` is
        whatever ``simulate`` needs and is passed straight through.
    simulate : callable ``(theta_true, aux, rng) -> data``
    posterior_draws : callable ``(data, rng) -> (L, n_params) array``
        Posterior draws, in the same canonical order and the same parameter
        layout as ``theta_true``.
    n_replicates : int
        More replicates sharpen the test; 300 resolves gross miscalibration,
        and a few thousand is needed for subtle bias.

    Returns
    -------
    ranks : ``(n_replicates, n_params)`` integer array in ``0..L``
    n_draws : int
        ``L``, needed to interpret the ranks.
    """
    rng = np.random.default_rng(seed)
    ranks = []
    n_draws = None
    for r in range(n_replicates):
        theta_true, aux = draw_prior(rng)
        theta_true = np.atleast_1d(np.asarray(theta_true, dtype=np.float64))
        data = simulate(theta_true, aux, rng)
        draws = np.atleast_2d(np.asarray(posterior_draws(data, rng), dtype=np.float64))
        if draws.shape[1] != theta_true.shape[0]:
            raise ValueError(
                f"posterior_draws returned {draws.shape[1]} parameters, "
                f"draw_prior returned {theta_true.shape[0]}")
        if n_draws is None:
            n_draws = draws.shape[0]
        elif draws.shape[0] != n_draws:
            raise ValueError("every replicate must return the same number of draws")
        ranks.append((draws < theta_true[None, :]).sum(axis=0))
        if progress is not None:
            progress(r + 1, n_replicates)
    return np.asarray(ranks, dtype=np.int64), int(n_draws)


def rank_histogram(ranks, n_draws, n_bins=20):
    """Bin ranks from ``0..n_draws`` into ``n_bins`` equal-width bins."""
    ranks = np.asarray(ranks).ravel()
    edges = np.linspace(0, n_draws + 1, n_bins + 1)
    counts, _ = np.histogram(ranks, bins=edges)
    return counts


def uniformity_pvalue(ranks, n_draws, n_bins=20):
    """Chi-square goodness-of-fit against a flat rank histogram.

    Small p means "not uniform", i.e. miscalibrated.  It does **not** say how;
    pair it with :func:`outer_mass_ratio` for the direction.
    """
    counts = rank_histogram(ranks, n_draws, n_bins)
    n = counts.sum()
    if n == 0:
        return float("nan")
    expected = n / n_bins
    chi2 = float(((counts - expected) ** 2 / expected).sum())
    return float(chi2_sf(chi2, n_bins - 1))


def outer_mass_ratio(ranks, n_draws, n_bins=20, outer_frac=0.1):
    """How much rank mass sits in the tails, relative to a flat histogram.

    ``> 1`` means the extremes are over-populated -- the true value keeps
    falling outside the posterior, so the posterior is too **narrow**.
    ``< 1`` means it is too wide.  ``1`` is calibrated.

    This is the direction that a p-value alone cannot give.
    """
    counts = rank_histogram(ranks, n_draws, n_bins)
    n = counts.sum()
    if n == 0:
        return float("nan")
    k = max(1, int(round(outer_frac * n_bins)))
    outer = counts[:k].sum() + counts[-k:].sum()
    expected_frac = 2.0 * k / n_bins
    return float((outer / n) / expected_frac)


@dataclass
class SbcResult:
    """Per-parameter calibration verdict."""
    ranks: np.ndarray
    n_draws: int
    names: list

    def pvalues(self, n_bins=20):
        return np.array([uniformity_pvalue(self.ranks[:, j], self.n_draws, n_bins)
                         for j in range(self.ranks.shape[1])])

    def outer_ratios(self, n_bins=20):
        return np.array([outer_mass_ratio(self.ranks[:, j], self.n_draws, n_bins)
                         for j in range(self.ranks.shape[1])])

    def verdict(self, n_bins=20, alpha=0.01, tol=0.25):
        """``"calibrated"`` / ``"too narrow"`` / ``"too wide"`` per parameter."""
        out = []
        for p, r in zip(self.pvalues(n_bins), self.outer_ratios(n_bins)):
            if p >= alpha and abs(r - 1.0) <= tol:
                out.append("calibrated")
            elif r > 1.0:
                out.append("too narrow")
            else:
                out.append("too wide")
        return out

    def report(self, n_bins=20):
        lines = [f"{'parameter':>14s} {'p(uniform)':>11s} {'outer/flat':>11s}  verdict"]
        for name, p, r, v in zip(self.names, self.pvalues(n_bins),
                                 self.outer_ratios(n_bins), self.verdict(n_bins)):
            lines.append(f"{name:>14s} {p:11.4f} {r:11.2f}  {v}")
        return "\n".join(lines)


def summarize(ranks, n_draws, names=None):
    ranks = np.atleast_2d(np.asarray(ranks))
    if names is None:
        names = [f"p{j}" for j in range(ranks.shape[1])]
    return SbcResult(ranks, int(n_draws), list(names))


class SbcPrior:
    """A prior used by **both** the generator and the sampler.

    SBC is valid only when parameters are drawn from the same prior the sampler
    conditions on.  Supplying them as two separate callables is how they drift:
    a flat sampler prior against a Normal-drawn truth produces a **sloped** rank
    histogram that is indistinguishable, by eye, from genuine sampler bias.
    That mistake cost a full 120-replicate run here.

    Bundling both directions in one object makes the mismatch unrepresentable —
    ``sample`` and ``log_prob`` are the same distribution by construction.

    Note the per-state factorisation: ``log_prob`` is called per state, so a
    *joint* constraint across states (say "keep the two distances apart") cannot
    be expressed and must not be applied in ``sample`` either, or the two halves
    diverge again in a subtler way.
    """

    def sample(self, rng, n_states):
        raise NotImplementedError

    def log_prob(self, k, params):
        raise NotImplementedError

    def as_log_prior(self):
        """The callable ``gibbs_physical`` expects."""
        return lambda k, params: self.log_prob(k, params)


class TruncatedNormalPrior1D(SbcPrior):
    """Truncated Normal on a single scalar per state — e.g. a distance.

    The natural prior for a structural constraint: a structure gives a mean and
    an uncertainty on ``R``, and the bounds keep the sampler inside the
    parameterisation's valid range.
    """

    def __init__(self, mu, sigma, lo, hi):
        self.mu, self.sigma, self.lo, self.hi = float(mu), float(sigma), float(lo), float(hi)

    def sample(self, rng, n_states):
        out = np.empty((n_states, 1))
        for k in range(n_states):
            while True:
                r = rng.normal(self.mu, self.sigma)
                if self.lo < r < self.hi:
                    out[k, 0] = r
                    break
        return out

    def log_prob(self, k, params):
        r = float(np.atleast_1d(params)[0])
        if not (self.lo < r < self.hi):
            return -np.inf
        return -0.5 * ((r - self.mu) / self.sigma) ** 2

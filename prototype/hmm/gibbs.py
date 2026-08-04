"""Blocked Gibbs for the photon-stream HMM — the exact reference sampler.

VB is the workhorse; this is the gold standard that proves VB right.  It is
exact (no mean-field factorisation), reports multimodality that VB collapses,
and is what the calibration tests are run against.

The sweep
---------

::

    1. forward filter, then backward sample the state at each *photon*   (FFBS)
    2. sample the latent tick-level path inside each gap                 (bridge)
    3. count one-tick transitions, emissions, initial states
    4. draw theta ~ Dirichlet(counts + alpha)

**Step 2 is the part that is easy to miss.**  FFBS gives states at photons, but
``A`` is the *one-tick* transition matrix, so the transitions that happened
between two photons ``dt`` ticks apart are themselves latent and must be drawn.
Conditional on the endpoints this is an endpoint-conditioned Markov bridge,
sampled left to right with

    P(s_t = i | s_{t-1}, s_dt = v)  ∝  A[s_{t-1}, i] * (A**(dt-t))[i, v]

at ``O(dt * n)`` per gap.  That is the same order as the deferred-rho
contraction the E-step uses, so a Gibbs sweep is **comparable to an EM
iteration**, not cheaper -- an earlier version of this plan claimed otherwise on
the grounds that FFBS skips the rho cache.  It does skip it, and then pays a
similar price here instead.

Randomness
----------

Path and bridge draws use a **counter-based** generator keyed by
``(seed, sweep, burst)``, so a draw depends only on where it is in the
computation and not on evaluation order.  That is what makes the result
reproducible and independent of thread count once the C++ port parallelises
over bursts, and it mirrors the ``splitmix64`` the engine already uses in
``sample_states`` / ``sample_paths``.  The small per-sweep Dirichlet draws are
inherently serial and use a plain NumPy generator.
"""
from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from .constraints import HmmConstraints
from .core import PhotonData, matrix_powers, _forward
from .fit import HmmModel

try:
    from numba import njit
except ImportError:  # pragma: no cover
    def njit(*args, **kwargs):
        def wrap(fn):
            return fn
        return wrap(args[0]) if args and callable(args[0]) else wrap


__all__ = ["HmmPosterior", "HmmPhysicalPosterior", "gibbs", "gibbs_physical",
           "sample_paths_and_counts", "split_rhat", "ess"]


_GOLDEN = np.uint64(0x9E3779B97F4A7C15)
_M1 = np.uint64(0xBF58476D1CE4E5B9)
_M2 = np.uint64(0x94D049BB133111EB)


@njit(cache=True)
def _mix(z):
    """SplitMix64 finaliser -- the same bit mixer the C++ engine uses."""
    z = (z ^ (z >> np.uint64(30))) * _M1
    z = (z ^ (z >> np.uint64(27))) * _M2
    return z ^ (z >> np.uint64(31))


@njit(cache=True)
def _u01(key, counter):
    """Uniform in [0, 1) determined entirely by ``(key, counter)``."""
    z = _mix(key + _GOLDEN * (counter + np.uint64(1)))
    return (z >> np.uint64(11)) * (1.0 / 9007199254740992.0)


@njit(cache=True)
def _pick(w, n, u):
    """Sample an index from unnormalised weights ``w[:n]``."""
    tot = 0.0
    for i in range(n):
        tot += w[i]
    if tot <= 0.0:
        return np.int64(u * n) % n
    x = u * tot
    acc = 0.0
    for i in range(n - 1):
        acc += w[i]
        if x <= acc:
            return i
    return n - 1


@njit(cache=True)
def _sweep(symbols, gaps, offsets, prior, obs, apow, seed, sweep_index,
           prior_cnt, obs_cnt, trans_cnt, path_out, want_path):
    """One FFBS + bridge pass.  Fills the count arrays; returns the log-likelihood."""
    n = prior.shape[0]
    n_bursts = offsets.shape[0] - 1

    max_len = 0
    for b in range(n_bursts):
        m = offsets[b + 1] - offsets[b]
        if m > max_len:
            max_len = m

    alpha = np.empty((max_len, n), dtype=np.float64)
    scale = np.empty(max_len, dtype=np.float64)
    path = np.empty(max_len, dtype=np.int64)
    w = np.empty(n, dtype=np.float64)

    total_ll = 0.0
    for b in range(n_bursts):
        start = offsets[b]
        stop = offsets[b + 1]
        m = stop - start
        if m <= 0:
            continue

        key = _mix(np.uint64(seed) ^ (_GOLDEN * np.uint64(sweep_index + 1))
                   ^ _mix(np.uint64(b + 1)))
        counter = np.uint64(0)

        total_ll += _forward(symbols, gaps, start, stop, prior, obs, apow, alpha, scale)

        # --- backward sample the photon-level path -------------------------
        for i in range(n):
            w[i] = alpha[m - 1, i]
        path[m - 1] = _pick(w, n, _u01(key, counter))
        counter += np.uint64(1)

        for k in range(m - 2, -1, -1):
            dt = gaps[start + k + 1]
            v = path[k + 1]
            if dt <= 0:
                # Photons at the same tick cannot straddle a transition.
                for i in range(n):
                    w[i] = alpha[k, i] if i == v else 0.0
            else:
                for i in range(n):
                    w[i] = alpha[k, i] * apow[dt, i, v]
            path[k] = _pick(w, n, _u01(key, counter))
            counter += np.uint64(1)

        # --- counts that do not need the bridge ----------------------------
        prior_cnt[path[0]] += 1.0
        for k in range(m):
            obs_cnt[path[k], symbols[start + k]] += 1.0
        if want_path:
            for k in range(m):
                path_out[start + k] = path[k]

        # --- sample the tick-level bridge inside each gap ------------------
        for k in range(m - 1):
            dt = gaps[start + k + 1]
            if dt <= 0:
                continue
            prev = path[k]
            v = path[k + 1]
            for t in range(1, dt):
                for i in range(n):
                    w[i] = apow[1, prev, i] * apow[dt - t, i, v]
                s = _pick(w, n, _u01(key, counter))
                counter += np.uint64(1)
                trans_cnt[prev, s] += 1.0
                prev = s
            trans_cnt[prev, v] += 1.0
    return total_ll


def sample_paths_and_counts(data: PhotonData, model: HmmModel, seed=0,
                            sweep_index=0, want_path=False):
    """One FFBS + bridge draw.

    Returns ``(prior_cnt, obs_cnt, trans_cnt, loglik, path)`` -- the sampled
    analogues of the E-step's ``prior_acc`` / ``gamma_obs`` / ``xi``.  Averaged
    over many draws they converge to exactly those, which is how the bridge
    sampling is verified.
    """
    prior = np.ascontiguousarray(model.prior, dtype=np.float64)
    trans = np.ascontiguousarray(model.trans, dtype=np.float64)
    obs = np.ascontiguousarray(model.obs, dtype=np.float64)
    n = prior.shape[0]

    apow = matrix_powers(trans, max(1, data.max_gap))
    prior_cnt = np.zeros(n)
    obs_cnt = np.zeros((n, data.p))
    trans_cnt = np.zeros((n, n))
    path = np.zeros(data.n_photons if want_path else 1, dtype=np.int64)

    ll = _sweep(data.symbols, data.gaps, data.offsets, prior, obs, apow,
                np.uint64(seed), sweep_index,
                prior_cnt, obs_cnt, trans_cnt, path, want_path)
    return prior_cnt, obs_cnt, trans_cnt, float(ll), (path if want_path else None)


# ---------------------------------------------------------------------------
# diagnostics
# ---------------------------------------------------------------------------

def split_rhat(chains):
    """Split-Rhat over ``chains`` of shape ``(n_chains, n_draws)``.

    Each chain is split in half first, so a chain that has not mixed *within
    itself* is caught even when the chains happen to agree with each other.
    """
    x = np.asarray(chains, dtype=np.float64)
    n_chains, n_draws = x.shape
    half = n_draws // 2
    if half < 2:
        return np.nan
    s = np.concatenate([x[:, :half], x[:, half:2 * half]], axis=0)
    m, n = s.shape
    means = s.mean(axis=1)
    variances = s.var(axis=1, ddof=1)
    W = variances.mean()
    B = n * means.var(ddof=1)
    if W <= 0:
        return np.nan
    var_hat = (n - 1) / n * W + B / n
    return float(np.sqrt(var_hat / W))


def ess(chains):
    """Effective sample size, Geyer initial-positive-sequence style."""
    x = np.asarray(chains, dtype=np.float64)
    n_chains, n_draws = x.shape
    if n_draws < 4:
        return np.nan
    centred = x - x.mean(axis=1, keepdims=True)
    var = centred.var(axis=1, ddof=1).mean()
    if var <= 0:
        return float(n_chains * n_draws)

    max_lag = min(n_draws - 2, 500)
    rho = np.empty(max_lag + 1)
    for t in range(max_lag + 1):
        acov = np.mean([np.dot(c[:n_draws - t], c[t:]) / n_draws for c in centred])
        rho[t] = acov / var

    total = 0.0
    t = 1
    while t + 1 <= max_lag:
        pair = rho[t] + rho[t + 1]
        if pair < 0:
            break
        total += pair
        t += 2
    tau = 1.0 + 2.0 * total
    return float(n_chains * n_draws / max(tau, 1e-12))


@dataclass
class HmmPosterior:
    """Draws from the exact posterior, plus convergence diagnostics.

    ``draws`` are **unrelabeled**.  States are exchangeable, so the posterior is
    multimodal and a raw mean over draws is meaningless if the sampler moved
    between labellings; ``summary`` relabels each draw canonically first.  The
    unrelabeled draws are kept so a caller can impose its own identifiability
    constraint instead.
    """
    draws: list
    loglik: np.ndarray
    n_chains: int = 1
    n_burn: int = 0
    history: list = field(default_factory=list)

    def _stack(self, relabel=True):
        ds = [d.reorder() for d in self.draws] if relabel else self.draws
        return (np.array([d.prior for d in ds]),
                np.array([d.trans for d in ds]),
                np.array([d.obs for d in ds]))

    def mean(self, relabel=True):
        return [a.mean(axis=0) for a in self._stack(relabel)]

    def std(self, relabel=True):
        return [a.std(axis=0, ddof=1) for a in self._stack(relabel)]

    def quantile(self, q, relabel=True):
        return [np.quantile(a, q, axis=0) for a in self._stack(relabel)]

    def rhat(self, relabel=True):
        """Split-Rhat per parameter, reshaped like the parameter arrays."""
        out = []
        for a in self._stack(relabel):
            flat = a.reshape(a.shape[0], -1)
            per_chain = flat.reshape(self.n_chains, -1, flat.shape[1])
            out.append(np.array([split_rhat(per_chain[:, :, j])
                                 for j in range(flat.shape[1])]).reshape(a.shape[1:]))
        return out

    def ess(self, relabel=True):
        out = []
        for a in self._stack(relabel):
            flat = a.reshape(a.shape[0], -1)
            per_chain = flat.reshape(self.n_chains, -1, flat.shape[1])
            out.append(np.array([ess(per_chain[:, :, j])
                                 for j in range(flat.shape[1])]).reshape(a.shape[1:]))
        return out


def gibbs(data: PhotonData, init, constraints: HmmConstraints = None,
          n_draws=1000, n_burn=200, n_chains=4, seed=0, thin=1) -> HmmPosterior:
    """Blocked Gibbs over ``{pi, A, B}`` and the latent tick-level path.

    ``init`` is either one model (all chains start there, which defeats the
    point of Rhat) or a list of ``n_chains`` dispersed models.
    """
    n = init[0].n_states if isinstance(init, (list, tuple)) else init.n_states
    if constraints is None:
        constraints = HmmConstraints.flat(n, data.p)
    if constraints.n_states != n or constraints.p != data.p:
        raise ValueError("constraints do not match the model / data alphabet")
    if not (np.all(np.isnan(constraints.fixed_prior))
            and np.all(np.isnan(constraints.fixed_trans))
            and np.all(np.isnan(constraints.fixed_obs))):
        raise NotImplementedError(
            "gibbs does not support fixed entries: a fixed parameter has no "
            "conditional to draw from. Express it as a sharp Dirichlet prior.")

    inits = list(init) if isinstance(init, (list, tuple)) else [init] * n_chains
    if len(inits) != n_chains:
        raise ValueError(f"need {n_chains} initial models, got {len(inits)}")

    a0_prior = constraints.alpha_prior
    a0_trans = constraints.alpha_trans
    a0_obs = constraints.alpha_obs

    draws, logliks = [], []
    for chain in range(n_chains):
        rng = np.random.default_rng([seed, chain])
        cur = HmmModel(np.array(inits[chain].prior, dtype=float),
                       np.array(inits[chain].trans, dtype=float),
                       np.array(inits[chain].obs, dtype=float))
        kept = 0
        it = 0
        while kept < n_draws:
            pc, oc, tc, ll, _ = sample_paths_and_counts(
                data, cur, seed=seed + 1000 * (chain + 1), sweep_index=it)
            it += 1

            prior = rng.dirichlet(a0_prior + pc)
            trans = np.vstack([rng.dirichlet(r) for r in (a0_trans + tc)])
            obs = np.vstack([rng.dirichlet(r) for r in (a0_obs + oc)])
            cur = HmmModel(prior, trans, obs, loglik=ll)

            if it > n_burn and (it - n_burn) % thin == 0:
                draws.append(cur)
                logliks.append(ll)
                kept += 1

    return HmmPosterior(draws, np.array(logliks), n_chains, n_burn)


# ---------------------------------------------------------------------------
# physically parameterised emission: Metropolis within Gibbs
# ---------------------------------------------------------------------------

@dataclass
class HmmPhysicalPosterior(HmmPosterior):
    """Posterior over the *physical* parameters, not just the categorical table.

    ``params`` has shape ``(n_kept, n_states, n_params)`` -- distances for
    :class:`FretDistanceEmission`.  This is what the whole exercise is for: a
    credible interval on a physical quantity rather than on an emission row.
    """
    params: np.ndarray = None
    accept_rate: np.ndarray = None

    def param_mean(self, relabel=True):
        p = self._sorted_params() if relabel else self.params
        return p.mean(axis=0)

    def param_quantile(self, q, relabel=True):
        p = self._sorted_params() if relabel else self.params
        return np.quantile(p, q, axis=0)

    def _sorted_params(self):
        """States are exchangeable; order each draw by its first parameter."""
        out = np.array(self.params, copy=True)
        for i in range(out.shape[0]):
            out[i] = out[i][np.argsort(out[i, :, 0])]
        return out

    def param_ess(self):
        p = self._sorted_params()
        flat = p.reshape(p.shape[0], -1)
        per_chain = flat.reshape(self.n_chains, -1, flat.shape[1])
        return np.array([ess(per_chain[:, :, j])
                         for j in range(flat.shape[1])]).reshape(p.shape[1:])


def _param_bounds(emission, n_par):
    """Box for the random walk, from whatever the parameterisation declares."""
    b = getattr(emission, "distance_bounds", None)
    if b is not None and n_par == 1:
        return np.array([[b[0], b[1]]])
    tb = getattr(emission, "tau_bounds", None)
    if tb is not None and n_par == 2:      # LifetimeEmission: (tau, p_acceptor)
        return np.array([[tb[0], tb[1]], [1e-6, 1 - 1e-6]])
    raise ValueError("parameterisation declares no bounds for the sampler")


def _log_conditional(emission, params_k, counts_k, k, log_prior):
    """log p(params_k | sampled path, data), up to a constant.

    Same form as the M-step objective -- but with the **sampled** counts from
    this sweep's path rather than expected ones, which is exactly what makes it
    a conditional to draw from rather than a surface to maximise.
    """
    row = emission.state(params_k).row()
    q = float(np.dot(counts_k, np.log(np.maximum(row, np.finfo(float).tiny))))
    if log_prior is not None:
        q += float(log_prior(k, params_k))
    return q


def gibbs_physical(data: PhotonData, emission, params_init, init_models,
                   constraints: HmmConstraints = None, n_draws=500, n_burn=300,
                   n_chains=2, seed=0, thin=1, log_prior=None,
                   step0=None, target_accept=0.3):
    """Blocked Gibbs with a Metropolis step on the physical parameters.

    Steps 1-3 are unchanged from :func:`gibbs` -- FFBS path, tick-level bridge,
    exact Dirichlet draws for pi and A.  Only the emission draw differs: instead
    of a Dirichlet over a free row, the physical parameters get a bounded
    random-walk Metropolis step, because a generated emission row has no
    conjugate conditional.

    The step size adapts during **burn-in only** -- continuing to adapt would
    break the chain's stationarity, so it is frozen before any draw is kept.

    ``log_prior(k, params_k) -> float`` is where a structural prior enters, and
    it applies to the *sampled* coordinate: a structure constrains ``R``, so
    ``DecayFitPrior``'s Normal/LogNormal go straight on it.
    """
    n = emission.n_states
    if constraints is None:
        constraints = HmmConstraints.flat(n, data.p)
    a0_prior, a0_trans = constraints.alpha_prior, constraints.alpha_trans

    params_init = np.atleast_2d(np.asarray(params_init, dtype=float).reshape(n, -1))
    n_par = params_init.shape[1]
    bounds = _param_bounds(emission, n_par)
    if step0 is None:
        step0 = 0.05 * (bounds[:, 1] - bounds[:, 0])

    inits = list(init_models) if isinstance(init_models, (list, tuple)) \
        else [init_models] * n_chains

    draws, params_out, logliks, accepts = [], [], [], []
    for chain in range(n_chains):
        rng = np.random.default_rng([seed, chain, 7])
        par = np.array(params_init, copy=True)
        step = np.tile(np.asarray(step0, dtype=float), (n, 1))
        cur = HmmModel(np.array(inits[chain].prior, dtype=float),
                       np.array(inits[chain].trans, dtype=float),
                       emission.to_obs(par))
        n_acc = np.zeros(n)
        n_try = np.zeros(n)
        kept, it = 0, 0

        while kept < n_draws:
            pc, oc, tc, ll, _ = sample_paths_and_counts(
                data, cur, seed=seed + 1000 * (chain + 1), sweep_index=it)
            it += 1

            prior = rng.dirichlet(a0_prior + pc)
            trans = np.vstack([rng.dirichlet(r) for r in (a0_trans + tc)])

            # --- Metropolis on the physical parameters, one state at a time ---
            for k in range(n):
                cur_lp = _log_conditional(emission, par[k], oc[k], k, log_prior)
                prop = par[k] + step[k] * rng.normal(size=n_par)
                n_try[k] += 1
                if np.all(prop >= bounds[:, 0]) and np.all(prop <= bounds[:, 1]):
                    prop_lp = _log_conditional(emission, prop, oc[k], k, log_prior)
                    if np.log(max(rng.random(), 1e-300)) < prop_lp - cur_lp:
                        par[k] = prop
                        n_acc[k] += 1
                if it <= n_burn:      # adapt during burn-in only
                    rate = n_acc[k] / max(n_try[k], 1)
                    step[k] *= float(np.exp((rate - target_accept) * 0.1))

            cur = HmmModel(prior, trans, emission.to_obs(par), loglik=ll)

            if it > n_burn and (it - n_burn) % thin == 0:
                draws.append(cur)
                params_out.append(np.array(par, copy=True))
                logliks.append(ll)
                kept += 1
        accepts.append(n_acc / np.maximum(n_try, 1))

    return HmmPhysicalPosterior(
        draws=draws, loglik=np.array(logliks), n_chains=n_chains, n_burn=n_burn,
        params=np.array(params_out), accept_rate=np.array(accepts))

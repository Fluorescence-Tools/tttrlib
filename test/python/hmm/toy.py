#!/usr/bin/env python3
"""Dense NumPy reference for the photon-stream HMM — the toy harness.

Every phase of the HMM work opens here: the target behaviour is demonstrated on
a tiny problem with known ground truth *before* the fast C++ engine is touched,
and the engine is then held to this reference.  That is already the convention
in this suite — ``test_hmm_decoding.py`` carries a ``gamma_reference`` and
``test_surrogate.py`` a NumPy feature reference — generalised into one module.

Three layers, each checking the one below:

``enumerate_burst``
    Brute force over **every tick-level state path**.  No recursions, no
    caches, no algebra beyond a product of matrix entries.  For a burst
    spanning ``T`` ticks with ``n`` states it visits all ``n**T`` paths, so it
    is limited to toy inputs — which is the point.  It is the ground truth for
    the log-likelihood *and* for the expected one-tick transition counts, so it
    validates the engine's deferred-ρ contraction from first principles.

``forward_backward`` / ``expected_counts``
    The scaled recursions, written the slow obvious way.  Must agree with the
    enumeration on toy inputs, and is cheap enough to run on real ones.

``em``
    Baum-Welch built on the above; the reference the C++ ``fit`` is checked
    against.

Later phases extend this module rather than replacing it (MAP with Dirichlet
pseudo-counts, variational Bayes, and blocked Gibbs all reuse
``expected_counts``).

Nothing here is a shipping path.  Clarity beats speed everywhere.
"""
from __future__ import annotations

import itertools

import numpy as np

__all__ = [
    "normalize_model",
    "two_state",
    "three_state",
    "simulate",
    "propagate",
    "forward_backward",
    "expected_counts",
    "viterbi",
    "em",
    "enumerate_burst",
    "burst_loglik",
    "total_loglik",
]


# ---------------------------------------------------------------------------
# models and ground truth
# ---------------------------------------------------------------------------

def normalize_model(prior, A, B):
    """Return ``(prior, A, B)`` as float arrays with π and every row summing to 1."""
    prior = np.asarray(prior, float).ravel()
    A = np.asarray(A, float)
    B = np.asarray(B, float)
    if A.ndim == 1:
        A = A.reshape(len(prior), len(prior))
    if B.ndim == 1:
        B = B.reshape(len(prior), -1)
    prior = prior / prior.sum()
    A = A / A.sum(axis=1, keepdims=True)
    B = B / B.sum(axis=1, keepdims=True)
    return prior, A, B


def two_state(e_low=0.2, e_high=0.8, k_switch=1e-3):
    """Two FRET states with acceptor fractions ``e_low``/``e_high``.

    ``k_switch`` is the per-tick switching probability; the default is slow
    relative to typical inter-photon gaps, i.e. the dynamic-but-resolvable
    regime.  Lower ``e_high - e_low`` to make the states overlap.
    """
    prior = np.array([0.5, 0.5])
    A = np.array([[1.0 - k_switch, k_switch], [k_switch, 1.0 - k_switch]])
    B = np.array([[1.0 - e_low, e_low], [1.0 - e_high, e_high]])
    return normalize_model(prior, A, B)


def three_state(e=(0.2, 0.5, 0.8), k_switch=5e-4):
    """Three FRET states, matching the shape used by ``benchmarks/bench_h2mm.py``."""
    n = len(e)
    prior = np.full(n, 1.0 / n)
    A = np.full((n, n), k_switch)
    np.fill_diagonal(A, 1.0 - k_switch * (n - 1))
    e = np.asarray(e, float)
    B = np.column_stack([1.0 - e, e])
    return normalize_model(prior, A, B)


def simulate(prior, A, B, n_bursts=40, burst_len=200, mean_gap=30, seed=1):
    """Draw ``n_bursts`` photon streams from the model.

    Returns ``(times, streams)`` as lists of Python lists, the layout
    ``HMM.set_bursts`` takes.  The state path is drawn here rather than through
    the engine so the reference stays independent of the code under test.
    """
    rng = np.random.default_rng(seed)
    prior, A, B = normalize_model(prior, A, B)
    n = len(prior)
    times, streams = [], []
    for _ in range(n_bursts):
        gaps = rng.integers(1, 2 * mean_gap, size=burst_len)
        t = np.cumsum(gaps).astype(np.int64)
        s = np.empty(burst_len, dtype=np.int64)
        state = rng.choice(n, p=prior)
        s[0] = rng.choice(B.shape[1], p=B[state])
        for k in range(1, burst_len):
            # Draw the state after the whole gap in one step: the distribution
            # after dt ticks is exactly row `state` of A**dt, so this is not an
            # approximation of tick-by-tick propagation, it is the same law.
            state = rng.choice(n, p=propagate(A, t[k] - t[k - 1])[state])
            s[k] = rng.choice(B.shape[1], p=B[state])
        times.append(t.tolist())
        streams.append(s.tolist())
    return times, streams


# ---------------------------------------------------------------------------
# layer 2 — the scaled recursions
# ---------------------------------------------------------------------------

def propagate(A, dt):
    """``A**dt``.  ``matrix_power`` is the honest independent implementation of
    the engine's pair-power binary exponentiation."""
    dt = int(dt)
    return np.eye(A.shape[0]) if dt <= 0 else np.linalg.matrix_power(A, dt)


def forward_backward(prior, A, B, times, streams):
    """Scaled forward-backward for one burst.

    Returns ``dict`` with ``alpha``, ``beta``, ``scale``, ``gamma`` and
    ``loglik``.  ``alpha`` rows are normalised and ``scale`` holds the row sums,
    matching the engine's ``forward_burst``.
    """
    prior, A, B = normalize_model(prior, A, B)
    n = len(prior)
    m = len(streams)

    alpha = np.zeros((m, n))
    scale = np.zeros(m)
    a = prior * B[:, streams[0]]
    scale[0] = a.sum()
    alpha[0] = a / scale[0] if scale[0] > 0 else a
    for k in range(1, m):
        dt = times[k] - times[k - 1]
        a = alpha[k - 1] @ propagate(A, dt)
        a = a * B[:, streams[k]]
        scale[k] = a.sum()
        alpha[k] = a / scale[k] if scale[k] > 0 else a

    beta = np.zeros((m, n))
    beta[m - 1] = 1.0
    for k in range(m - 2, -1, -1):
        dt = times[k + 1] - times[k]
        w = B[:, streams[k + 1]] * beta[k + 1]
        beta[k] = (propagate(A, dt) @ w) / scale[k + 1]

    g = alpha * beta
    g = g / g.sum(axis=1, keepdims=True)
    return {
        "alpha": alpha,
        "beta": beta,
        "scale": scale,
        "gamma": g,
        "loglik": float(np.log(scale[scale > 0]).sum()),
    }


def expected_counts(prior, A, B, bursts):
    """E-step sufficient statistics over all bursts.

    ``bursts`` is an iterable of ``(times, streams)``.  Returns
    ``(prior_acc, gamma_obs, xi, loglik)`` — the same four quantities the C++
    E-step accumulates into ``prior_acc`` / ``gamma_obs_acc`` / ``xi_acc``.

    ``xi[i, j]`` is the expected number of **one-tick** ``i -> j`` transitions,
    not the photon-to-photon transition count.  Photons are separated by ``dt``
    ticks, so the count inside one gap sums over where in the gap the transition
    happened::

        E[n_ij] = sum_s (alpha_k @ A**s)[i] * A[i, j] * (A**(dt-1-s) @ z)[j]

    with ``z[v] = B[v, y_{k+1}] * beta_{k+1}[v] / scale_{k+1}``.  Writing it this
    way avoids dividing by ``(A**dt)[u, v]``, which can underflow.  This is the
    quantity the engine's deferred ρ contraction computes.
    """
    prior, A, B = normalize_model(prior, A, B)
    n, p = B.shape

    prior_acc = np.zeros(n)
    gamma_obs = np.zeros((n, p))
    xi = np.zeros((n, n))
    loglik = 0.0

    for times, streams in bursts:
        times = np.asarray(times, dtype=np.int64)
        streams = np.asarray(streams, dtype=np.int64)
        fb = forward_backward(prior, A, B, times, streams)
        alpha, beta, scale, gamma = fb["alpha"], fb["beta"], fb["scale"], fb["gamma"]
        loglik += fb["loglik"]

        prior_acc += gamma[0]
        for k, y in enumerate(streams):
            gamma_obs[:, y] += gamma[k]

        for k in range(len(streams) - 1):
            dt = int(times[k + 1] - times[k])
            if dt <= 0:
                continue
            z = B[:, streams[k + 1]] * beta[k + 1] / scale[k + 1]
            left = alpha[k].copy()          # alpha_k @ A**s, s = 0
            rights = [z]                    # A**r @ z for r = 0 .. dt-1
            for _ in range(dt - 1):
                rights.append(A @ rights[-1])
            for s in range(dt):
                xi += np.outer(left, rights[dt - 1 - s]) * A
                if s + 1 < dt:
                    left = left @ A

    return prior_acc, gamma_obs, xi, loglik


def viterbi(prior, A, B, times, streams):
    """Most likely state path for one burst, and its log-probability."""
    prior, A, B = normalize_model(prior, A, B)
    n = len(prior)
    m = len(streams)
    tiny = np.finfo(float).tiny
    log_B = np.log(np.maximum(B, tiny))

    delta = np.log(np.maximum(prior, tiny)) + log_B[:, streams[0]]
    back = np.zeros((m, n), dtype=int)
    for k in range(1, m):
        log_A = np.log(np.maximum(propagate(A, times[k] - times[k - 1]), tiny))
        cand = delta[:, None] + log_A
        back[k] = np.argmax(cand, axis=0)
        delta = cand[back[k], np.arange(n)] + log_B[:, streams[k]]

    path = np.zeros(m, dtype=int)
    path[-1] = int(np.argmax(delta))
    for k in range(m - 1, 0, -1):
        path[k - 1] = back[k, path[k]]
    return path, float(np.max(delta))


def em(prior, A, B, bursts, max_iter=300, tol=1e-9):
    """Baum-Welch.  Returns ``(prior, A, B, loglik, n_iter, converged)``."""
    prior, A, B = normalize_model(prior, A, B)
    bursts = list(bursts)
    n_bursts = max(1, len(bursts))
    prev = -np.inf
    converged = False
    it = 0
    loglik = -np.inf
    for it in range(1, max_iter + 1):
        prior_acc, gamma_obs, xi, loglik = expected_counts(prior, A, B, bursts)
        prior, A, B = normalize_model(prior_acc / n_bursts, xi, gamma_obs)
        if it > 1 and loglik - prev < tol:
            converged = True
            break
        prev = loglik
    return prior, A, B, loglik, it, converged


# ---------------------------------------------------------------------------
# layer 1 — brute force over every tick-level path
# ---------------------------------------------------------------------------

def enumerate_burst(prior, A, B, times, streams):
    """Exact statistics for one burst by enumerating every tick-level path.

    Visits all ``n ** T`` paths where ``T`` is the number of ticks the burst
    spans, so keep both small (``n=2``, ``T<=12`` is instant).  Returns
    ``(loglik, prior_acc, gamma_obs, xi)`` with the same meaning as
    ``expected_counts`` — including ``xi`` as **one-tick** transition counts,
    counted here by literally walking each path.

    This is the only function in the module that assumes nothing about HMM
    algebra, so it is what the recursions are proved against.
    """
    prior, A, B = normalize_model(prior, A, B)
    n, p = B.shape
    times = np.asarray(times, dtype=np.int64)
    streams = np.asarray(streams, dtype=np.int64)

    t0 = int(times[0])
    n_ticks = int(times[-1]) - t0 + 1
    # A list, not a dict: two photons may share a tick, and both must be scored.
    photons = [(int(t) - t0, int(y)) for t, y in zip(times, streams)]

    total = 0.0
    prior_acc = np.zeros(n)
    gamma_obs = np.zeros((n, p))
    xi = np.zeros((n, n))

    for path in itertools.product(range(n), repeat=n_ticks):
        w = prior[path[0]]
        for tick in range(1, n_ticks):
            w *= A[path[tick - 1], path[tick]]
        for tick, y in photons:
            w *= B[path[tick], y]
        if w == 0.0:
            continue
        total += w
        prior_acc[path[0]] += w
        for tick, y in photons:
            gamma_obs[path[tick], y] += w
        for tick in range(1, n_ticks):
            xi[path[tick - 1], path[tick]] += w

    if total <= 0.0:
        raise ValueError("burst has zero probability under this model")
    return float(np.log(total)), prior_acc / total, gamma_obs / total, xi / total


def burst_loglik(prior, A, B, times, streams):
    """Log-likelihood of one burst via the scaled forward pass."""
    return forward_backward(prior, A, B, times, streams)["loglik"]


def total_loglik(prior, A, B, bursts):
    """Log-likelihood of every burst, summed."""
    return sum(burst_loglik(prior, A, B, t, s) for t, s in bursts)

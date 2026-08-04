"""Photon-stream HMM core — the E-step, Numba-accelerated.

This is the development implementation: the physics and the inference are
worked out here in NumPy/Numba, validated against the brute-force oracle in
``test/python/hmm/toy.py``, and only then ported to C++.  The eSRRF prototype
took the same route (``prototype/esrrf`` -> ``src/CLSMSuperRes.cpp``).

Two design points carried deliberately from the C++ target so the port is a
transcription rather than a redesign:

**The photon layout is CSR.**  Bursts are concatenated into one symbol array
with an offsets array, exactly like the engine's ``streams_`` / ``offsets_``.

**The alphabet is opaque.**  Every emission read is ``obs[state, symbol]`` and
nothing in the recursions knows what a symbol *means*.  Classic H2MM passes
``symbol = stream``; the lifetime-resolved model passes
``symbol = stream * n_micro_bins + micro_bin``.  Same kernel, larger ``p``.

The expected one-tick transition counts use the identity validated in
``toy.expected_counts``::

    E[n_ij] = sum_s (alpha_k @ A**s)[i] * A[i, j] * (A**(dt-1-s) @ z)[j]

with ``z[v] = obs[v, y_{k+1}] * beta_{k+1}[v] / scale_{k+1}``.

Both routes to that quantity are implemented, and the tests assert they agree:

- the **direct sum** above, ``O(dt * n**2)`` per gap -- obviously correct, and
  what the fast path is checked against;
- the **rho cache** (``use_rho=True``, the default), which precomputes the same
  sum as a tensor indexed by gap length, giving ``O(n**4)`` per gap.  This is
  the deferred contraction the C++ engine uses, and it is why the engine is
  fast on sparse photon streams -- the direct sum measured ~69x slower than C++
  at benchmark scale, and the cache closes most of that.

Keeping the slow one is not redundancy: it is the only reason to believe the
fast one.
"""
from __future__ import annotations

import numpy as np

try:
    from numba import njit
    HAVE_NUMBA = True
except ImportError:  # pragma: no cover - numba is a dev dependency
    HAVE_NUMBA = False

    def njit(*args, **kwargs):
        def wrap(fn):
            return fn
        return wrap(args[0]) if args and callable(args[0]) else wrap


__all__ = ["PhotonData", "matrix_powers", "rho_powers", "e_step",
           "forward_backward_burst"]


class PhotonData:
    """Bursts of photons in the CSR layout the engine uses.

    Parameters
    ----------
    times, streams : sequences of sequences
        Per-burst macro times (monotonic, integer ticks) and stream indices.
    n_streams : int
        Size of the detection-stream alphabet.
    micro : sequences of sequences, optional
        Per-photon micro-time **bin** indices.  When given, the symbol alphabet
        becomes the product ``stream * n_micro_bins + micro_bin`` and ``p``
        grows accordingly -- this is the whole of the lifetime-resolved
        extension as far as the recursions are concerned.
    n_micro_bins : int
        Number of micro-time bins; 1 means no micro-time axis.
    """

    def __init__(self, times, streams, n_streams, micro=None, n_micro_bins=1):
        if n_micro_bins < 1:
            raise ValueError("n_micro_bins must be >= 1")
        if (micro is None) != (n_micro_bins == 1):
            if micro is None and n_micro_bins > 1:
                raise ValueError("n_micro_bins > 1 requires micro-time bins")

        self.n_streams = int(n_streams)
        self.n_micro_bins = int(n_micro_bins)
        self.p = self.n_streams * self.n_micro_bins

        lengths = [len(t) for t in times]
        if any(l != len(s) for l, s in zip(lengths, streams)):
            raise ValueError("times and streams must agree in length per burst")

        self.offsets = np.zeros(len(times) + 1, dtype=np.int64)
        self.offsets[1:] = np.cumsum(lengths)

        flat_t = np.concatenate([np.asarray(t, dtype=np.int64) for t in times])
        flat_s = np.concatenate([np.asarray(s, dtype=np.int64) for s in streams])
        if flat_s.min(initial=0) < 0 or flat_s.max(initial=0) >= self.n_streams:
            raise ValueError("stream index outside [0, n_streams)")

        if micro is None:
            symbols = flat_s
        else:
            flat_m = np.concatenate([np.asarray(m, dtype=np.int64) for m in micro])
            if flat_m.min(initial=0) < 0 or flat_m.max(initial=0) >= self.n_micro_bins:
                raise ValueError("micro-time bin outside [0, n_micro_bins)")
            symbols = flat_s * self.n_micro_bins + flat_m

        self.times = flat_t
        self.symbols = symbols.astype(np.int64)

        # Gap to the *previous* photon, 0 at every burst start (no propagation).
        gaps = np.zeros(len(flat_t), dtype=np.int64)
        for a, b in zip(self.offsets[:-1], self.offsets[1:]):
            if b - a > 1:
                d = np.diff(flat_t[a:b])
                if (d < 0).any():
                    raise ValueError("macro times must be non-decreasing in a burst")
                gaps[a + 1:b] = d
        self.gaps = gaps
        self.max_gap = int(gaps.max(initial=0))

    @property
    def n_bursts(self):
        return len(self.offsets) - 1

    @property
    def n_photons(self):
        return int(self.offsets[-1])

    def __repr__(self):
        return (f"PhotonData(n_bursts={self.n_bursts}, n_photons={self.n_photons}, "
                f"p={self.p}, max_gap={self.max_gap})")


def matrix_powers(A, max_power):
    """``[A**0, A**1, ..., A**max_power]`` stacked on axis 0.

    The engine uses binary exponentiation over the *unique* gaps; building the
    full ladder is wasteful but exact, and keeps the prototype's arithmetic
    obvious.  Cost is ``O(max_power * n**3)``, negligible for the state counts
    photon HMMs use.
    """
    A = np.asarray(A, dtype=np.float64)
    n = A.shape[0]
    out = np.empty((max_power + 1, n, n), dtype=np.float64)
    out[0] = np.eye(n)
    for d in range(1, max_power + 1):
        out[d] = out[d - 1] @ A
    return out


@njit(cache=True)
def _forward(symbols, gaps, start, stop, prior, obs, apow, alpha, scale):
    """Scaled forward pass over one burst.  Returns the log-likelihood."""
    n = prior.shape[0]
    ll = 0.0

    tot = 0.0
    y0 = symbols[start]
    for i in range(n):
        a = prior[i] * obs[i, y0]
        alpha[0, i] = a
        tot += a
    scale[0] = tot
    if tot > 0.0:
        for i in range(n):
            alpha[0, i] /= tot
        ll += np.log(tot)

    for k in range(1, stop - start):
        dt = gaps[start + k]
        y = symbols[start + k]
        tot = 0.0
        for i in range(n):
            v = 0.0
            if dt <= 0:
                v = alpha[k - 1, i]
            else:
                for j in range(n):
                    v += alpha[k - 1, j] * apow[dt, j, i]
            v *= obs[i, y]
            alpha[k, i] = v
            tot += v
        scale[k] = tot
        if tot > 0.0:
            for i in range(n):
                alpha[k, i] /= tot
            ll += np.log(tot)
    return ll


@njit(cache=True)
def _backward(symbols, gaps, start, stop, obs, apow, scale, beta):
    """Scaled backward pass over one burst."""
    n = beta.shape[1]
    m = stop - start
    for i in range(n):
        beta[m - 1, i] = 1.0
    for k in range(m - 2, -1, -1):
        dt = gaps[start + k + 1]
        y = symbols[start + k + 1]
        s = scale[k + 1]
        for i in range(n):
            v = 0.0
            if dt <= 0:
                v = obs[i, y] * beta[k + 1, i]
            else:
                for j in range(n):
                    v += apow[dt, i, j] * obs[j, y] * beta[k + 1, j]
            beta[k, i] = v / s if s > 0.0 else 0.0


def rho_powers(A, apow, max_power):
    """The deferred-contraction tensor the C++ engine calls the rho cache.

    ``rho[d, u, v, i, j]`` is the expected number of one-tick ``i -> j``
    transitions in a gap of ``d`` ticks that starts in ``u`` and ends in ``v``,
    *unnormalised* by ``(A**d)[u, v]``::

        rho[d, u, v, i, j] = sum_s (A**s)[u, i] * A[i, j] * (A**(d-1-s))[j, v]

    Precomputing it turns the per-gap cost from ``O(d * n**2)`` into
    ``O(n**4)``, which is the whole reason the engine is fast on sparse photon
    streams.  Built by the recursion

        rho[d] = rho[d-1] @ A   (contracting v)  +  (A**(d-1))[u,i] A[i,j] delta[j,v]

    at ``O(max_power * n**4)`` total -- negligible next to the E-step itself.
    Verified against the direct sum in ``test_prototype.py``.
    """
    A = np.asarray(A, dtype=np.float64)
    n = A.shape[0]
    rho = np.zeros((max_power + 1, n, n, n, n), dtype=np.float64)
    if max_power >= 1:
        # d = 1: delta[u,i] A[i,j] delta[j,v]
        for i in range(n):
            for j in range(n):
                rho[1, i, j, i, j] = A[i, j]
    for d in range(2, max_power + 1):
        # carry the previous gap one tick further towards v
        rho[d] = np.einsum("uwij,wv->uvij", rho[d - 1], A)
        # plus the transition happening on the final tick
        rho[d] += np.einsum("ui,ij,jv->uvij", apow[d - 1], A, np.eye(n))
    return rho


@njit(cache=True)
def _accumulate_rho(symbols, gaps, offsets, prior, obs, apow, rho,
                    prior_acc, gamma_obs, xi):
    """E-step using the precomputed rho cache -- O(n**4) per gap."""
    n = prior.shape[0]
    n_bursts = offsets.shape[0] - 1
    max_len = 0
    for b in range(n_bursts):
        m = offsets[b + 1] - offsets[b]
        if m > max_len:
            max_len = m

    alpha = np.empty((max_len, n), dtype=np.float64)
    beta = np.empty((max_len, n), dtype=np.float64)
    scale = np.empty(max_len, dtype=np.float64)
    z = np.empty(n, dtype=np.float64)

    total_ll = 0.0
    for b in range(n_bursts):
        start = offsets[b]
        stop = offsets[b + 1]
        m = stop - start
        if m <= 0:
            continue
        total_ll += _forward(symbols, gaps, start, stop, prior, obs, apow, alpha, scale)
        _backward(symbols, gaps, start, stop, obs, apow, scale, beta)

        for i in range(n):
            prior_acc[i] += alpha[0, i] * beta[0, i]
        for k in range(m):
            y = symbols[start + k]
            for i in range(n):
                gamma_obs[i, y] += alpha[k, i] * beta[k, i]

        for k in range(m - 1):
            dt = gaps[start + k + 1]
            if dt <= 0:
                continue
            y = symbols[start + k + 1]
            s = scale[k + 1]
            if s <= 0.0:
                continue
            for i in range(n):
                z[i] = obs[i, y] * beta[k + 1, i] / s
            for u in range(n):
                wu = alpha[k, u]
                if wu == 0.0:
                    continue
                for v in range(n):
                    c = wu * z[v]
                    if c == 0.0:
                        continue
                    for i in range(n):
                        for j in range(n):
                            xi[i, j] += c * rho[dt, u, v, i, j]
    return total_ll


@njit(cache=True)
def _accumulate(symbols, gaps, offsets, prior, obs, apow,
                prior_acc, gamma_obs, xi):
    """E-step over every burst.  Returns the total log-likelihood.

    ``xi`` accumulates expected **one-tick** transition counts, summed over
    where in each gap the transition could have happened.
    """
    n = prior.shape[0]
    n_bursts = offsets.shape[0] - 1
    max_len = 0
    for b in range(n_bursts):
        m = offsets[b + 1] - offsets[b]
        if m > max_len:
            max_len = m

    alpha = np.empty((max_len, n), dtype=np.float64)
    beta = np.empty((max_len, n), dtype=np.float64)
    scale = np.empty(max_len, dtype=np.float64)
    left = np.empty(n, dtype=np.float64)
    nxt = np.empty(n, dtype=np.float64)
    z = np.empty(n, dtype=np.float64)
    rights = np.empty((max(1, apow.shape[0]), n), dtype=np.float64)

    total_ll = 0.0
    for b in range(n_bursts):
        start = offsets[b]
        stop = offsets[b + 1]
        m = stop - start
        if m <= 0:
            continue
        total_ll += _forward(symbols, gaps, start, stop, prior, obs, apow, alpha, scale)
        _backward(symbols, gaps, start, stop, obs, apow, scale, beta)

        # gamma_k = alpha_k * beta_k (already normalised by the scaling)
        for i in range(n):
            prior_acc[i] += alpha[0, i] * beta[0, i]
        for k in range(m):
            y = symbols[start + k]
            for i in range(n):
                gamma_obs[i, y] += alpha[k, i] * beta[k, i]

        for k in range(m - 1):
            dt = gaps[start + k + 1]
            if dt <= 0:
                continue
            y = symbols[start + k + 1]
            s = scale[k + 1]
            if s <= 0.0:
                continue
            for i in range(n):
                z[i] = obs[i, y] * beta[k + 1, i] / s

            # rights[r] = A**r @ z, for r = 0 .. dt-1
            for i in range(n):
                rights[0, i] = z[i]
            for r in range(1, dt):
                for i in range(n):
                    acc = 0.0
                    for j in range(n):
                        acc += apow[1, i, j] * rights[r - 1, j]
                    rights[r, i] = acc

            # left = alpha_k @ A**s, walked forward one tick at a time
            for i in range(n):
                left[i] = alpha[k, i]
            for step in range(dt):
                r = dt - 1 - step
                for i in range(n):
                    li = left[i]
                    if li == 0.0:
                        continue
                    for j in range(n):
                        xi[i, j] += li * apow[1, i, j] * rights[r, j]
                if step + 1 < dt:
                    for j in range(n):
                        acc = 0.0
                        for i in range(n):
                            acc += left[i] * apow[1, i, j]
                        nxt[j] = acc
                    for j in range(n):
                        left[j] = nxt[j]
    return total_ll


def e_step(data: PhotonData, prior, A, obs, apow=None, rho=None, use_rho=True):
    """Sufficient statistics for one E-step.

    Returns ``(prior_acc, gamma_obs, xi, loglik)`` -- the same four quantities
    the C++ engine accumulates into ``prior_acc`` / ``gamma_obs_acc`` /
    ``xi_acc``, and the same four an external sampler needs (the planned
    ``HmmEval`` contract).

    ``use_rho=False`` selects the direct ``O(dt * n**2)`` summation instead of
    the rho cache.  The two are mathematically identical and are asserted equal
    in the tests; the slow one is kept because it is obviously correct, and it
    is what the fast one is checked against.
    """
    prior = np.ascontiguousarray(prior, dtype=np.float64)
    A = np.ascontiguousarray(A, dtype=np.float64)
    obs = np.ascontiguousarray(obs, dtype=np.float64)
    n = prior.shape[0]

    if apow is None:
        apow = matrix_powers(A, data.max_gap)

    prior_acc = np.zeros(n, dtype=np.float64)
    gamma_obs = np.zeros((n, data.p), dtype=np.float64)
    xi = np.zeros((n, n), dtype=np.float64)

    if use_rho:
        if rho is None:
            rho = rho_powers(A, apow, data.max_gap)
        ll = _accumulate_rho(data.symbols, data.gaps, data.offsets,
                             prior, obs, apow, rho, prior_acc, gamma_obs, xi)
    else:
        ll = _accumulate(data.symbols, data.gaps, data.offsets,
                         prior, obs, apow, prior_acc, gamma_obs, xi)
    return prior_acc, gamma_obs, xi, float(ll)


def forward_backward_burst(data: PhotonData, prior, A, obs, burst):
    """``(gamma, loglik)`` for a single burst -- a debugging convenience."""
    prior = np.ascontiguousarray(prior, dtype=np.float64)
    obs = np.ascontiguousarray(obs, dtype=np.float64)
    apow = matrix_powers(A, data.max_gap)
    start, stop = int(data.offsets[burst]), int(data.offsets[burst + 1])
    m = stop - start
    n = prior.shape[0]
    alpha = np.empty((m, n))
    beta = np.empty((m, n))
    scale = np.empty(m)
    ll = _forward(data.symbols, data.gaps, start, stop, prior, obs, apow, alpha, scale)
    _backward(data.symbols, data.gaps, start, stop, obs, apow, scale, beta)
    g = alpha * beta
    return g / g.sum(axis=1, keepdims=True), float(ll)

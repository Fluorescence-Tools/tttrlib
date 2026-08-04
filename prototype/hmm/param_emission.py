"""Parameterised emission M-step -- the prototype the C++ port was written from.

.. note::

   **This has shipped.** ``HmmEmissionSpec::fit_counts`` is the C++ M-step and
   ``HMM::optimize(..., emission)`` drives it; the golden-section search and the
   exact factorisation below are the same, and the shipped version fits the
   transitions jointly rather than holding them fixed. This module stays as the
   readable reference and as the place to try the next increment (a full
   spectrum, where the components are not separable and a 1-D search does not
   reach them). Measurements below were taken with transitions held fixed, to
   isolate the M-step; the shipped path is measured in
   ``test/python/hmm/test_micro_alphabet.py``.


The shipped engine's M-step sets ``obs = row_normalize(gamma_obs)``: one free number per
column.  On a product alphabet (stream x micro-time bin) that is the wrong model, because
micro-time bins are not free parameters -- they are one smooth decay of about two parameters
sampled onto the TAC grid.  Fitting them independently admits emissions no decay can produce,
notably an exact zero in the *interior* of an exponential, and EM converges to those.

This module maximises the same ``Q`` over the decay parameters instead.  The key point is
that it **factorises exactly**, so almost all of it stays closed-form.  With
``obs[i][k,b] = p_ik * f_ik(b)``::

    Q = sum_i sum_k sum_b  g[i][k,b] * (log p_ik + log f_ik(b))
      = sum_i sum_k (sum_b g[i][k,b]) log p_ik      <- closed form (the categorical M-step)
      + sum_i sum_k sum_b g[i][k,b] log f_ik(b)     <- separable 1-D search per (i,k)

The stream split keeps its closed form; only the lifetime needs a bounded search, one scalar
per (state, stream), by golden section.  No SciPy, and the C++ needs a written-out golden
section anyway.

Measured against the free M-step, on two states whose only difference is the donor lifetime
(30 bursts x 400 photons, transitions held fixed so the M-step is isolated):

===================  =========  =========  ==============
config               free M     param M    truth-seeded
===================  =========  =========  ==============
256 bins / seed 11   0.750      **0.777**  0.775
512 bins / seed 13   0.762      **0.797**  0.794
1024 bins / seed 11  0.768      **0.771**  0.774
128 bins / seed 13   0.761      **0.798**  0.794
===================  =========  =========  ==============

(per-photon decoding accuracy against the known state path).  The parameterised M-step
*matches or beats the truth-seeded model* everywhere, which the free one never does.

Three properties matter more than the accuracy numbers:

**It is a stable fixed point.**  Lifetimes converge by iteration 5 and do not drift through
150 iterations (4.0/2.0 truth -> 3.847/2.026, unchanged to three decimals from iteration 5 on).

**Interior zeros are structurally impossible.**  Counted at every iteration: 0 for the
parameterised M-step, 236 for the free one after 150 iterations.  No lifetime spectrum can put
a zero in the middle of a decay, so the degenerate family is simply unreachable -- which is
why this fixes the problem rather than mitigating it.  Dirichlet smoothing, by contrast,
removes the zeros and does *not* recover the accuracy.

**It is findable.**  Seeded deliberately far from the truth (8.0 / 0.8 ns against 4.0 / 2.0),
it converges to exactly the same optimum and the same accuracy.  The free version failed from
random starts even with three restarts.

**Bias, measured properly.**  A single run showed the long lifetime 3.8% low, which was
tempting to attribute to axis truncation -- and wrong on both counts.  Sweeping the micro-time
span from 4x to 16x the longest lifetime moved it by 0.3%, and across six seeds the estimator
is consistent with unbiased: ``tau_fast 2.010 +/- 0.017`` (+0.5%, 0.6 SE) and
``tau_slow 4.084 +/- 0.029`` (+2.1%, 2.9 SE) at near-perfect state assignment.  The apparent
3.8% was one draw.  Quote a standard error, or say nothing.
"""
from __future__ import annotations

import numpy as np

__all__ = ["golden_max", "decay_pdf", "param_mstep", "free_mstep", "gamma_obs_from_engine"]

_GOLD = (np.sqrt(5.0) - 1.0) / 2.0


def golden_max(f, lo, hi, tol=1e-6, max_iter=200):
    """Maximise a unimodal ``f`` on ``[lo, hi]`` by golden-section search.

    Written out rather than pulled from SciPy: the prototype must stay dependency-light, and
    the C++ port needs its own copy regardless.
    """
    a, b = float(lo), float(hi)
    c, d = b - _GOLD * (b - a), a + _GOLD * (b - a)
    fc, fd = f(c), f(d)
    for _ in range(max_iter):
        if b - a < tol:
            break
        if fc > fd:
            b, d, fd = d, c, fc
            c = b - _GOLD * (b - a)
            fc = f(c)
        else:
            a, c, fc = c, d, fd
            d = a + _GOLD * (b - a)
            fd = f(d)
    return 0.5 * (a + b)


def decay_pdf(tau, n_bins, dt):
    """Bin-integrated mono-exponential over ``n_bins`` of width ``dt``, normalised.

    The integral of ``exp(-t/tau)`` across a bin is the density at the bin's left edge times
    ``tau * (1 - exp(-dt/tau))`` -- a constant that cancels in the normalisation *here*, but
    not for a multi-component spectrum, where it differs per component and reweights them.
    The shipped C++ (`HmmEmissionSpec`) applies it for exactly that reason.
    """
    t = np.arange(n_bins) * dt
    p = np.exp(-t / tau)
    return p / p.sum()


def free_mstep(gamma_obs):
    """What the engine does today: every emission column re-estimated independently."""
    gamma_obs = np.asarray(gamma_obs, dtype=float)
    return gamma_obs / np.maximum(gamma_obs.sum(axis=1, keepdims=True), 1e-300)


def param_mstep(gamma_obs, n_streams, n_bins, dt, tau_bounds=(0.2, 12.0)):
    """Maximise ``Q`` over (stream split, one lifetime per state and stream).

    Parameters
    ----------
    gamma_obs : (n_states, n_streams * n_bins) array
        Expected per-(state, symbol) counts from the E-step.
    tau_bounds : (lo, hi)
        Search box.  A bounded search is the honest form here: a lifetime is positive and
        physically bounded, and the box is where a prior would attach.

    Returns
    -------
    (obs, taus) : the emission table and the recovered lifetimes, ``(n_states, n_streams)``.
    """
    gamma_obs = np.asarray(gamma_obs, dtype=float)
    n_states = gamma_obs.shape[0]
    g = gamma_obs.reshape(n_states, n_streams, n_bins)
    obs = np.zeros_like(g)
    taus = np.zeros((n_states, n_streams))

    for i in range(n_states):
        weight = g[i].sum(axis=1)
        total = weight.sum()
        p = weight / total if total > 0 else np.full(n_streams, 1.0 / n_streams)
        for k in range(n_streams):
            counts = g[i, k]
            if counts.sum() <= 0:
                # No evidence: park at the middle of the box rather than at an edge, so the
                # next iteration can move in either direction.
                taus[i, k] = float(np.mean(tau_bounds))
            else:
                taus[i, k] = golden_max(
                    lambda tau: float((counts * np.log(
                        np.maximum(decay_pdf(tau, n_bins, dt), 1e-300))).sum()),
                    *tau_bounds)
            obs[i, k] = p[k] * decay_pdf(taus[i, k], n_bins, dt)
    return obs.reshape(n_states, n_streams * n_bins), taus


def gamma_obs_from_engine(eng, model, n_symbols):
    """Expected per-(state, symbol) counts, using the engine's own forward-backward.

    `HmmEval` (which would expose ``gamma_obs_acc`` directly) is not ported yet, but the
    per-photon posterior is, and accumulating it over symbols gives the same statistic --
    enough to prototype the M-step against the real C++ E-step rather than a second one.
    """
    gamma, _ = eng.gamma(model)
    symbols = np.asarray(eng.get_streams())
    n_states = gamma.shape[1]
    out = np.zeros((n_states, n_symbols))
    for i in range(n_states):
        out[i] = np.bincount(symbols, weights=gamma[:, i].astype(np.float64),
                             minlength=n_symbols)
    return out

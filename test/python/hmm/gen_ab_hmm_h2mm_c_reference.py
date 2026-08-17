#!/usr/bin/env python
"""Record the H2MM_C reference for ``test_ab_hmm_reference.py``.

Runs under the H2MM_C competitor venv (``benchmarks/.venvs/h2mm_c/bin/python``,
built by ``benchmarks/build_envs.sh``) so that H2MM_C never enters the project
environment. Bursts are simulated here with a plain NumPy Markov chain (not
with tttrlib and not with H2MM_C's own simulator), and the *inputs are stored
beside the outputs*, so the fixture does not depend on any RNG being stable
across versions.

What is recorded per case: the model, the bursts, H2MM_C's total and per-burst
log-likelihood of that model on those bursts, its per-photon posteriors (gamma),
its Viterbi path and per-burst path log-likelihood, and the model H2MM_C
returns after exactly one Baum-Welch step from the same starting point.

    benchmarks/.venvs/h2mm_c/bin/python test/python/hmm/gen_ab_hmm_h2mm_c_reference.py
"""
import os
import sys

import numpy as np

import H2MM_C as h2

OUT = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "..", "data", "reference", "hmm_h2mm_c_reference.npz",
)


def normalize(prior, A, B):
    prior = np.asarray(prior, float)
    A = np.asarray(A, float)
    B = np.asarray(B, float)
    return prior / prior.sum(), A / A.sum(1, keepdims=True), B / B.sum(1, keepdims=True)


def simulate(prior, A, B, n_bursts, burst_len, mean_gap, rng):
    """Tick-level Markov chain, observed only at photon ticks."""
    n = len(prior)
    times, streams = [], []
    for _ in range(n_bursts):
        gaps = rng.geometric(1.0 / mean_gap, size=burst_len)
        gaps[0] = 0
        t = np.cumsum(gaps).astype(np.int64)
        s = rng.choice(n, p=prior)
        st = np.empty(burst_len, int)
        st[0] = s
        for k in range(1, burst_len):
            # propagate the chain over the gap
            Adt = np.linalg.matrix_power(A, int(gaps[k]))
            s = rng.choice(n, p=Adt[s])
            st[k] = s
        obs = np.array([rng.choice(B.shape[1], p=B[x]) for x in st], dtype=np.int64)
        times.append(t)
        streams.append(obs)
    return times, streams


def cases():
    rng = np.random.default_rng(20260817)
    out = []
    # (name, prior, A, B, n_bursts, burst_len, mean_gap)
    out.append(("two_state_2det",
                *normalize([0.6, 0.4], [[0.995, 0.005], [0.01, 0.99]], [[0.8, 0.2], [0.25, 0.75]]),
                30, 120, 25))
    out.append(("three_state_2det",
                *normalize([0.3, 0.3, 0.4],
                           [[0.99, 0.005, 0.005], [0.004, 0.992, 0.004], [0.006, 0.006, 0.988]],
                           [[0.9, 0.1], [0.5, 0.5], [0.15, 0.85]]),
                25, 150, 20))
    out.append(("three_state_3det",
                *normalize([0.5, 0.25, 0.25],
                           [[0.98, 0.01, 0.01], [0.02, 0.97, 0.01], [0.01, 0.02, 0.97]],
                           [[0.6, 0.3, 0.1], [0.2, 0.5, 0.3], [0.1, 0.2, 0.7]]),
                20, 100, 10))
    out.append(("fast_switching",
                *normalize([0.5, 0.5], [[0.9, 0.1], [0.15, 0.85]], [[0.85, 0.15], [0.2, 0.8]]),
                15, 80, 3))
    out.append(("two_photon_bursts",
                *normalize([0.7, 0.3], [[0.99, 0.01], [0.02, 0.98]], [[0.9, 0.1], [0.3, 0.7]]),
                12, 2, 5))
    recs = {}
    for name, prior, A, B, nb, bl, gap in out:
        times, streams = simulate(prior, A, B, nb, bl, gap, rng)
        recs[name] = (prior, A, B, times, streams)
    return recs


def main():
    recs = cases()
    store = {}
    for name, (prior, A, B, times, streams) in recs.items():
        model = h2.h2mm_model(prior, A, B)
        # total + per-burst loglik and gamma of the *given* model
        models, ll_burst, gamma = h2.H2MM_arr([model], streams, times, ll=True, gamma=True)
        ll_tot = float(np.asarray(models).ravel()[0].loglik)
        ll_burst = np.asarray(ll_burst[0]).ravel()
        gamma = gamma[0]  # list of (n_phot, n_state) arrays
        # viterbi
        path = h2.viterbi_path(model, streams, times)
        # returns (paths, scale/loglik, ll per burst?, icl) -- store what comes back
        paths, path_scale, path_ll, icl = path
        # one Baum-Welch step from this model
        step = h2.EM_H2MM_C(model, streams, times, max_iter=1, print_func=None)
        offsets = np.cumsum([0] + [len(t) for t in times])
        store[f"{name}/prior"] = prior
        store[f"{name}/trans"] = A
        store[f"{name}/obs"] = B
        store[f"{name}/offsets"] = offsets
        store[f"{name}/times"] = np.concatenate(times)
        store[f"{name}/streams"] = np.concatenate(streams)
        store[f"{name}/loglik"] = np.array(ll_tot)
        store[f"{name}/loglik_burst"] = ll_burst
        store[f"{name}/gamma"] = np.concatenate(gamma, axis=0)
        store[f"{name}/viterbi"] = np.concatenate([np.asarray(p) for p in paths])
        store[f"{name}/viterbi_ll_burst"] = np.asarray(path_ll, float).ravel()
        store[f"{name}/viterbi_icl"] = np.array(float(icl))
        store[f"{name}/step_prior"] = np.asarray(step.prior)
        store[f"{name}/step_trans"] = np.asarray(step.trans)
        store[f"{name}/step_obs"] = np.asarray(step.obs)
        store[f"{name}/step_loglik"] = np.array(float(step.loglik))
        print(name, "loglik", ll_tot, "step loglik", step.loglik, "niter", step.niter)
    store["h2mm_c_version"] = np.array(h2.__version__)
    store["cases"] = np.array(sorted(recs))
    np.savez_compressed(OUT, **store)
    print("wrote", OUT)


if __name__ == "__main__":
    sys.exit(main())

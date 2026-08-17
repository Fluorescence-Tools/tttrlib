#!/usr/bin/env python
"""Record the hmmlearn reference for ``TestVariationalBayesAgainstHmmlearn``.

Runs under the sciref competitor venv (``benchmarks/.venvs/sciref/bin/python``,
built by ``benchmarks/build_envs.sh``) so hmmlearn never enters the project
environment. On a stream with a photon at *every* tick (dt == 1 throughout) the
photon-stream VB-HMM of ``HMMVB.h`` is exactly a categorical VB-HMM with
Dirichlet factors, which is what ``hmmlearn.vhmm.VariationalCategoricalHMM``
implements (MacKay 1997 / Beal 2003 mean-field VB, Beal's lower bound). The
chains are simulated here with plain NumPy and *stored beside the outputs*.

Recorded per case: the observation sequences, the Dir(1) priors, the posterior
seed both sides start from, hmmlearn's converged posterior Dirichlet parameters
and its lower bound at that posterior.

    benchmarks/.venvs/sciref/bin/python test/python/hmm/gen_ab_hmm_vb_hmmlearn_reference.py
"""
import os

import numpy as np
import hmmlearn
from hmmlearn.vhmm import VariationalCategoricalHMM

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "..", "..", "data", "reference", "hmm_vb_hmmlearn_reference.npz")


def simulate(pi, A, B, n_bursts, rng):
    K, P = B.shape
    lengths, X = [], []
    for _ in range(n_bursts):
        L = int(rng.integers(120, 260))
        z = np.empty(L, int)
        z[0] = rng.choice(K, p=pi)
        for t in range(1, L):
            z[t] = rng.choice(K, p=A[z[t - 1]])
        X.append(np.array([rng.choice(P, p=B[k]) for k in z]))
        lengths.append(L)
    return np.concatenate(X), np.array(lengths)


CASES = {
    "two_state_2det": dict(
        pi=[0.6, 0.4], A=[[0.97, 0.03], [0.05, 0.95]], B=[[0.8, 0.2], [0.3, 0.7]],
        seed_pi=[0.5, 0.5], seed_A=[[0.9, 0.1], [0.2, 0.8]], seed_B=[[0.6, 0.4], [0.4, 0.6]], n_bursts=20, rng=11),
    "three_state_3det": dict(
        pi=[0.5, 0.3, 0.2], A=[[0.96, 0.03, 0.01], [0.02, 0.95, 0.03], [0.02, 0.04, 0.94]],
        B=[[0.7, 0.2, 0.1], [0.2, 0.6, 0.2], [0.1, 0.2, 0.7]],
        seed_pi=[1 / 3] * 3, seed_A=[[0.8, 0.1, 0.1], [0.1, 0.8, 0.1], [0.1, 0.1, 0.8]],
        seed_B=[[0.5, 0.3, 0.2], [0.3, 0.4, 0.3], [0.2, 0.3, 0.5]], n_bursts=30, rng=12),
}


def main():
    out = {"cases": np.array(list(CASES)), "hmmlearn_version": np.array(hmmlearn.__version__)}
    for name, c in CASES.items():
        rng = np.random.default_rng(c["rng"])
        pi, A, B = (np.asarray(c[k], float) for k in ("pi", "A", "B"))
        K, P = B.shape
        X, lengths = simulate(pi, A, B, c["n_bursts"], rng)
        seed_pi, seed_A, seed_B = (np.asarray(c[k], float) for k in ("seed_pi", "seed_A", "seed_B"))
        # posterior seed = prior + pseudo-counts of the seed model (same on both sides)
        ap0, at0, ao0 = 1 + 20 * seed_pi, 1 + 100 * seed_A, 1 + 100 * seed_B
        m = VariationalCategoricalHMM(n_components=K, n_features=P, n_iter=5000, tol=1e-12,
                                      init_params="", params="ste", implementation="log")
        m.startprob_prior_, m.transmat_prior_, m.emissionprob_prior_ = np.ones(K), np.ones((K, K)), np.ones((K, P))
        m.startprob_posterior_, m.transmat_posterior_, m.emissionprob_posterior_ = ap0.copy(), at0.copy(), ao0.copy()
        m.fit(X.reshape(-1, 1), lengths)
        assert m.monitor_.converged
        print(f"{name}: hmmlearn converged in {m.monitor_.iter} iterations, bound {m.monitor_.history[-1]:.6f}")
        for k, v in dict(X=X, lengths=lengths, pi=pi, A=A, B=B, seed_pi=seed_pi, seed_A=seed_A, seed_B=seed_B,
                         alpha_prior0=ap0, alpha_trans0=at0, alpha_obs0=ao0,
                         alpha_prior=m.startprob_posterior_, alpha_trans=m.transmat_posterior_,
                         alpha_obs=m.emissionprob_posterior_, lower_bound=np.array(m.monitor_.history[-1]),
                         n_iter=np.array(m.monitor_.iter)).items():
            out[f"{name}/{k}"] = v
    np.savez(OUT, **out)
    print("wrote", OUT)


if __name__ == "__main__":
    main()

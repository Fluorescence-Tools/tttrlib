"""Variational Bayes for the photon-stream HMM: posterior and ELBO
================================================================

``tttrlib.fit_vb`` is the mean-field variational Bayes fit of the photon-stream
HMM (``spectroscopy/hmm``): Dirichlet factors over the initial distribution,
the transition matrix and the emission table, iterated with the same
forward-backward engine as the maximum-likelihood fit. Unlike ``HMM.optimize``
it returns a **posterior** -- means with standard deviations -- and the
**evidence lower bound** (ELBO), which is the model-selection score priors ask
for where BIC counts parameters instead.

The bound is Beal's (MacKay 1997, Beal 2003): the forward pass under the
geometric-mean weights :math:`\\tilde\\theta = \\exp E_q[\\log\\theta]`, with the
unobserved ticks between photons marginalised as :math:`\\tilde A^{\\Delta t}`,
minus the Dirichlet KL terms. On streams with a photon at every tick this is a
plain categorical VB-HMM, and tttrlib's posterior and ELBO agree with
`hmmlearn <https://hmmlearn.readthedocs.io>`_'s ``VariationalCategoricalHMM``
to 1e-4 / 2e-10 (``test/python/hmm/test_ab_hmm_reference.py``), 13x faster.

Two numbers come back: ``elbo`` (the bound, compare models with it) and
``elbo_normalised`` -- the iteration's own convergence variable, which sits
exactly K(K-1)/2 nat above the bound because the engine's :math:`A^{\\Delta t}`
cache row-normalises the sub-stochastic :math:`\\tilde A`. Both are shown below.

The example simulates a two-state molecule (two detection channels, photons at
random ticks), fits K = 1..4 states and compares ELBO with BIC.
"""
import numpy as np
import matplotlib.pyplot as plt

import tttrlib

rng = np.random.default_rng(3)

# %%
# Simulate: a tick-level Markov chain observed only where a photon arrives
# ------------------------------------------------------------------------
# Two states with different colour ratios; a photon every ~6 ticks on average.
A_true = np.array([[0.995, 0.005],
                   [0.008, 0.992]])
B_true = np.array([[0.85, 0.15],          # state 0: mostly channel 0
                   [0.30, 0.70]])         # state 1: mostly channel 1
pi_true = np.array([0.5, 0.5])
K_true, P = B_true.shape


def simulate_burst(n_photons, mean_gap):
    gaps = rng.geometric(1.0 / mean_gap, size=n_photons)
    gaps[0] = 0
    t = np.cumsum(gaps).astype(np.int64)
    z = np.empty(n_photons, int)
    z[0] = rng.choice(K_true, p=pi_true)
    for k in range(1, n_photons):
        z[k] = rng.choice(K_true, p=np.linalg.matrix_power(A_true, int(gaps[k]))[z[k - 1]])
    s = np.array([rng.choice(P, p=B_true[x]) for x in z])
    return t, s, z


bursts = [simulate_burst(int(rng.integers(60, 160)), 6.0) for _ in range(60)]
times = [b[0].tolist() for b in bursts]
streams = [b[1].tolist() for b in bursts]
print(f"{len(bursts)} bursts, {sum(len(t) for t in times)} photons, "
      f"{sum(t[-1] for t in times)} ticks")

eng = tttrlib.HMM()
eng.set_bursts(times, streams, P)

# %%
# Fit K = 1..4 with VB and with maximum likelihood
# ------------------------------------------------
# VB starts from a point model (one E-step at it seeds the posterior); the
# ML fit (``optimize``) supplies BIC for comparison. Flat Dir(1) priors.


def seed_model(K):
    A0 = np.full((K, K), 0.02 / max(K - 1, 1)) + np.eye(K) * (0.98 - 0.02 / max(K - 1, 1))
    if K == 1:
        A0 = np.ones((1, 1))
    B0 = np.linspace(0.8, 0.2, K)[:, None] * np.array([[1.0, -1.0]]) + np.array([[0.0, 1.0]])
    return tttrlib.HmmModel([1.0 / K] * K, A0.ravel().tolist(), B0.ravel().tolist())


rows = []
for K in (1, 2, 3, 4):
    m0 = seed_model(K)
    vb = tttrlib.fit_vb(eng, m0, None, 2000, 1e-8)
    ml = eng.optimize(m0, 2000, 1e-8)
    rows.append((K, vb, ml))
    print(f"K={K}: elbo {vb.elbo:10.2f}   elbo_normalised - elbo {vb.elbo_normalised - vb.elbo:5.2f} "
          f"(K(K-1)/2 = {K * (K - 1) / 2:.0f})   BIC {ml.bic():10.2f}   VB iters {vb.n_iter}, converged {vb.converged}")

# %%
# The posterior for K = 2
# -----------------------
vb2 = next(vb for K, vb, _ in rows if K == 2)
mean = vb2.mean()
sd = np.asarray(vb2.std())
Kp = 2
A_hat = np.asarray(mean.trans).reshape(Kp, Kp)
B_hat = np.asarray(mean.obs).reshape(Kp, P)
sd_A = sd[Kp:Kp + Kp * Kp].reshape(Kp, Kp)
sd_B = sd[Kp + Kp * Kp:].reshape(Kp, P)
# label switching is arbitrary: order states by their channel-0 fraction
order = np.argsort(-B_hat[:, 0])
print("transition matrix (posterior mean +- sd) vs truth:")
for r, i in enumerate(order):
    print("  ", "  ".join(f"{A_hat[i, j]:.4f}+-{sd_A[i, j]:.4f}" for j in order), "  |  ",
          "  ".join(f"{A_true[r, c]:.4f}" for c in range(K_true)))
print("emission table (posterior mean +- sd) vs truth:")
for r, i in enumerate(order):
    print("  ", "  ".join(f"{B_hat[i, c]:.3f}+-{sd_B[i, c]:.3f}" for c in range(P)), "  |  ",
          "  ".join(f"{B_true[r, c]:.3f}" for c in range(P)))

# %%
# ELBO and BIC against the number of states
# -----------------------------------------
# Both peak (ELBO up, BIC down) at the simulated K = 2. The dashed curve is
# ``elbo_normalised``: the same ranking, K(K-1)/2 nat higher. (The K = 3, 4
# fits are over-parameterised and creep for hundreds of iterations without
# moving the bound; ``max_iter`` caps them.)
Ks = [K for K, _, _ in rows]
fig, (a, b) = plt.subplots(1, 2, figsize=(9, 3.4))
a.plot(Ks, [vb.elbo for _, vb, _ in rows], "o-", label="ELBO (Beal's bound)")
a.plot(Ks, [vb.elbo_normalised for _, vb, _ in rows], "s--", label="elbo_normalised (+K(K-1)/2)")
a.set_xlabel("states K"); a.set_ylabel("nat"); a.set_xticks(Ks); a.legend(); a.set_title("VB evidence bound")
b.plot(Ks, [ml.bic() for _, _, ml in rows], "o-", color="C3")
b.set_xlabel("states K"); b.set_ylabel("BIC"); b.set_xticks(Ks); b.set_title("maximum-likelihood BIC")
plt.tight_layout()
plt.show()

# %%
# Notes
# -----
# * ``elbo`` is conservative: ln K! (label switching) and the mean-field gap are
#   not recovered, so it trails the exact log evidence by a few nat that grow
#   with K. Compare models with it; do not read it as log p(y).
# * ``history`` holds ``elbo_normalised`` per iteration -- the convergence
#   variable -- and ``loglik`` / ``loglik_beal`` are the two data terms.
# * Restraints (``HmmRestraints``) supply non-flat Dirichlet priors; pinned
#   parameters are not supported in VB (a point mass is not a Dirichlet).

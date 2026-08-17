"""The HMM lattice on a two-state intensity trace
==============================================

`tttrlib`'s ``hmm_*`` functions are the log-domain hidden-Markov recursions
over a **caller-supplied** frame-probability matrix ``log_frameprob`` of shape
(T × K): forward pass, a fused backward / posteriors / ξ sweep, and Viterbi.
Emissions are the caller's business — a Gaussian mixture, a Poisson rate, a
lookup table — which is exactly what lets one lattice serve every model. (This
is *not* the photon-stream HMM in ``spectroscopy/hmm``; that one has
Δt-dependent transitions and a scaled recursion. This one is a uniform-bin
lattice, and it is bit-identical to hmmlearn's ``_hmmc``.)

The example: a molecule switching between a dim and a bright state observed
through Poisson counts per bin. The caller builds ``log_frameprob`` from the
Poisson pmf of each state, then asks the lattice for the log-likelihood, the
per-bin posteriors and the most probable path.
"""
import numpy as np
import matplotlib.pyplot as plt
from scipy.stats import poisson

import tttrlib

rng = np.random.default_rng(11)

# %%
# Simulate the hidden two-state process and its Poisson observations
# -------------------------------------------------------------------
T = 3000                                # bins
rates = np.array([3.0, 12.0])           # mean counts per bin, state 0 / state 1
A = np.array([[0.995, 0.005],
              [0.010, 0.990]])          # per-bin transition matrix
start = np.array([0.6, 0.4])
states = np.empty(T, dtype=np.int64)
states[0] = rng.choice(2, p=start)
for t in range(1, T):
    states[t] = rng.choice(2, p=A[states[t - 1]])
counts = rng.poisson(rates[states])

# %%
# The caller's emission model: log P(count_t | state k)
# ------------------------------------------------------
log_frame = np.ascontiguousarray(poisson.logpmf(counts[:, None], rates[None, :]))   # (T, K)
K = log_frame.shape[1]
log_start = np.log(start)
log_trans = np.log(A)

# %%
# Forward, fused backward/posteriors/xi, Viterbi
# ----------------------------------------------
fwd = np.empty_like(log_frame)
log_prob = tttrlib.hmm_forward_log(log_start, log_trans, log_frame, fwd)

posteriors = np.empty_like(log_frame)
xi_sum = np.zeros((K, K))               # ACCUMULATED (+=): a fit sums it over sequences
tttrlib.hmm_backward_posteriors_xi(log_trans, log_frame, fwd, log_prob, posteriors, xi_sum)

path = np.empty(T, dtype=np.int64)
viterbi_score = tttrlib.hmm_viterbi_log(log_start, log_trans, log_frame, path)

print(f"log P(observations) = {log_prob:.2f}   Viterbi log-score = {viterbi_score:.2f}")
print(f"Viterbi path agrees with the truth on {np.mean(path == states) * 100:.1f}% of bins")

# %%
# One M-step from the sufficient statistics the lattice returned: the expected
# transition counts xi_sum re-estimate the transition matrix.
A_hat = xi_sum / xi_sum.sum(axis=1, keepdims=True)
print("re-estimated transition matrix:\n", np.round(A_hat, 4))

# %%
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(11, 6), sharex=True)
tt = np.arange(T)
ax1.plot(tt, counts, lw=0.5, color="0.6", label="counts per bin")
ax1.plot(tt, rates[states], lw=1.0, color="C3", label="true rate")
ax1.plot(tt, rates[path], lw=1.0, color="C0", ls="--", label="Viterbi rate")
ax1.set_ylabel("counts / bin")
ax1.legend(loc="upper right", ncol=3)
ax2.plot(tt, posteriors[:, 1], lw=0.8, color="C0", label="P(bright | data)")
ax2.plot(tt, states, lw=0.8, color="C3", alpha=0.6, label="true state")
ax2.set_ylim(-0.05, 1.05)
ax2.set_xlabel("bin")
ax2.set_ylabel("posterior")
ax2.legend(loc="center right")
fig.tight_layout()
plt.show()

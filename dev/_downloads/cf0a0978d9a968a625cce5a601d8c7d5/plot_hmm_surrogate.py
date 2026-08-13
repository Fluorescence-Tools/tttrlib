"""
Training an HMM surrogate (amortised neural estimator)
=======================================================

Photon-by-photon hidden Markov modelling (H2MM) normally recovers a model by
iterating Baum-Welch EM on every dataset. A *surrogate* takes a different route:
train a small neural network **once** on data simulated from the HMM generative
model, then estimate the parameters of a real dataset in a **single forward
pass**.

This example trains a surrogate from scratch, saves it, and compares it with EM
on simulated data where the answer is known.

The estimate is approximate. It is fast and it is well behaved exactly where EM
struggles — when two states are so close that the likelihood surface is flat —
but on well-separated states EM remains more accurate. The comparison at the end
shows both regimes rather than a single headline number.
"""

import matplotlib.pyplot as plt
import numpy as np

import tttrlib

# %%
# Choose the regime
# -----------------
# A surrogate is specific to a ``(n_states, n_streams)`` pair *and* to the burst
# length / inter-photon spacing it was trained on, so these numbers should match
# the experiment you intend to analyse.

N_STATES = 2
N_STREAMS = 2      # donor and acceptor
N_BURSTS = 150     # bursts per simulated dataset
BURST_LEN = 80     # photons per burst
MEAN_DT = 4.0      # mean inter-photon gap, macro-time ticks
SEED = 12345

# %%
# Train
# -----
# Training simulates ``n_samples`` labelled datasets and regresses the model
# parameters on a 24-element permutation-invariant summary of each. Both the
# simulation and the network run in C++, so this takes seconds rather than
# minutes.

options = tttrlib.TrainOptions()
options.hidden_layer_sizes = tttrlib.VectorInt32([256, 256, 128])
options.max_iter = 800
options.early_stopping = True
options.seed = SEED

surrogate = tttrlib.HmmSurrogate.train(
    N_STATES, N_STREAMS,
    2500,          # n_samples: more is better, with diminishing returns
    N_BURSTS, BURST_LEN, MEAN_DT,
    options, SEED,
)

print(surrogate)
print("features:", surrogate.get_net().n_inputs())
print("targets: ", surrogate.get_net().n_outputs())

# %%
# The training curve shows the fit converging. Early stopping halts training
# once the held-out loss stops improving, so the curve is usually shorter than
# ``max_iter``.

loss = surrogate.get_net().loss_curve_
validation = surrogate.get_net().validation_curve_

fig, ax = plt.subplots(figsize=(6, 4))
ax.plot(loss, label="training")
if validation.size:
    ax.plot(validation, label="validation")
ax.set_xlabel("epoch")
ax.set_ylabel("mean squared error")
ax.set_yscale("log")
ax.set_title("Surrogate training")
ax.legend()
fig.tight_layout()

# %%
# Save it
# -------
# The model is written as JSON, not as a pickle: it is human-readable, safe to
# share, and loadable from any tttrlib binding (and from ChiSurf).

surrogate.to_json_file("hmm_surrogate_2state.json")
reloaded = tttrlib.HmmSurrogate.from_json_file("hmm_surrogate_2state.json")

# %%
# Compare with EM
# ---------------
# Simulate datasets with known FRET efficiencies and estimate them both ways.
# ``separation`` is the gap between the two true efficiencies — the variable
# that decides whether the problem is identifiable at all.


def simulate(e_lo, e_hi, switch_prob, rng):
    """Simulate a two-state kinetic dataset and load it into an HMM engine."""
    times, streams = [], []
    for _ in range(N_BURSTS):
        t = np.concatenate(
            [[0], np.cumsum(rng.poisson(MEAN_DT, BURST_LEN - 1) + 1)]
        ).astype(np.int64)
        s = np.empty(BURST_LEN, dtype=np.int32)
        state = rng.random() < 0.5
        for j in range(BURST_LEN):
            if j and rng.random() < switch_prob:
                state = not state
            s[j] = rng.random() < (e_hi if state else e_lo)
        times.append(t)
        streams.append(s)

    engine = tttrlib.HMM()
    engine.set_bursts(
        tttrlib.VectorVectorInt64([tttrlib.VectorInt64(t.tolist()) for t in times]),
        tttrlib.VectorVectorInt32([tttrlib.VectorInt32(s.tolist()) for s in streams]),
        N_STREAMS,
    )
    return engine


separations, err_surrogate, err_em = [], [], []
for trial in range(24):
    rng = np.random.default_rng(SEED + 1000 + trial)
    e_lo, e_hi = sorted(rng.uniform(0.15, 0.85, 2))
    engine = simulate(e_lo, e_hi, 0.02, rng)
    truth = np.array([e_lo, e_hi])

    estimate = reloaded.predict(engine)
    fit = engine.fit(N_STATES, 1, 500, 1e-7, SEED, True, False)

    separations.append(e_hi - e_lo)
    err_surrogate.append(np.abs(np.sort(estimate.obs_np[:, 1]) - truth).mean())
    err_em.append(np.abs(np.sort(fit.obs_np[:, 1]) - truth).mean())

# %%
# Plotted against state separation, the trade-off is clear: the surrogate's
# error is nearly flat, while EM is excellent on well-separated states and
# degrades sharply as they merge and the maximum-likelihood estimate drifts.

fig, ax = plt.subplots(figsize=(6, 4))
ax.scatter(separations, err_surrogate, label="surrogate (one forward pass)")
ax.scatter(separations, err_em, marker="x", label="EM (Baum-Welch)")
ax.set_xlabel("true separation of the two FRET states")
ax.set_ylabel("mean |E error|")
ax.set_title("Where each estimator wins")
ax.legend()
fig.tight_layout()

plt.show()

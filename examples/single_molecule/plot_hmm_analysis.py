"""
Photon-by-photon HMM (H2MM) of smFRET dynamics
==============================================

Photon-by-photon hidden Markov modelling (H2MM, Pirchi *et al.*, J. Phys. Chem.
B 2016) fits a hidden Markov model directly to the arrival times **and** detection
channels of individual photons inside single-molecule bursts. Unlike a burst-wise
FRET histogram — which averages each burst to a single efficiency and blurs any
sub-burst kinetics — H2MM resolves state transitions on the inter-photon
timescale, down to microseconds.

This example is the ``tttrlib`` counterpart of the analysis workflow in the
`burstH2MM <https://bursth2mm.readthedocs.io>`_ documentation, built entirely on
``tttrlib``'s own C++ H2MM engine (:class:`tttrlib.HMM`): simulate a known
three-state kinetic system, recover it by an EM state-count scan with BIC model
selection, decode the most-likely state path with Viterbi, and reproduce the
central burstH2MM figures — the model-selection curve, per-state FRET states, a
transition-density plot, per-state dwell-time distributions, and a single burst's
state trajectory.

No experimental file is needed: the data is generated from the generative model
so the recovered parameters can be checked against ground truth.
"""

import matplotlib.pyplot as plt
import numpy as np

import tttrlib

STATE_COLORS = ["#4e79a7", "#59a14f", "#e15759", "#b07aa1", "#f28e2b"]

# %%
# A known three-state system
# --------------------------
# The generative model is an :class:`tttrlib.HmmModel` — an initial-state vector
# ``prior``, a one-tick row-stochastic transition matrix ``trans``, and an
# emission matrix ``obs`` giving, per state, the probability that a photon lands
# in each stream. With two streams (donor and acceptor) the per-state apparent
# FRET efficiency is simply ``E = obs[:, 1] / (obs[:, 0] + obs[:, 1])`` — here
# three well-separated states at roughly 0.15, 0.47 and 0.81.

N_STREAMS = 2  # donor, acceptor
trans = np.array(
    [
        [0.990, 0.006, 0.004],
        [0.005, 0.990, 0.005],
        [0.004, 0.006, 0.990],
    ]
)
obs = np.array(
    [
        [0.85, 0.15],  # low FRET
        [0.53, 0.47],  # mid FRET
        [0.20, 0.80],  # high FRET
    ]
)
ground_truth = tttrlib.HmmModel(
    [1 / 3, 1 / 3, 1 / 3],
    [float(x) for x in trans.ravel()],
    [float(x) for x in obs.ravel()],
)
true_E = obs[:, 1] / obs[:, 0:2].sum(axis=1)
print("true FRET efficiencies:", np.round(true_E, 2))

# %%
# Simulate bursts
# ---------------
# A burst is a list of monotonically increasing photon macro-times; the mean
# inter-photon gap (here ~4 ticks) sets how finely the kinetics are sampled.
# :meth:`tttrlib.HMM.simulate_bursts` advances the hidden chain tick-by-tick
# along each time axis and emits a stream index per photon, so it exercises the
# exact same propagation the fit inverts.

N_BURSTS = 500
BURST_LEN = 130
MEAN_DT = 4
rng = np.random.default_rng(0)

burst_times = [
    np.concatenate([[0], np.cumsum(rng.poisson(MEAN_DT, BURST_LEN - 1) + 1)]).astype(np.int64)
    for _ in range(N_BURSTS)
]
times_vv = tttrlib.VectorVectorInt64([tttrlib.VectorInt64(t.tolist()) for t in burst_times])
sim_streams = tttrlib.HMM.simulate_bursts(ground_truth, times_vv, 1)

# %%
# Load the photons into an HMM engine
# ------------------------------------
# The engine stores the bursts in a compact CSR layout keyed by the *unique*
# inter-photon gaps, which is what makes the Baum-Welch maps fast.

engine = tttrlib.HMM()
engine.set_bursts(
    times_vv,
    tttrlib.VectorVectorInt32([tttrlib.VectorInt32(list(s)) for s in sim_streams]),
    N_STREAMS,
)
print(f"{engine.get_n_bursts()} bursts, {engine.get_n_photons()} photons, "
      f"{len(engine.get_unique_dt())} unique inter-photon gaps")

# %%
# State-count scan and BIC model selection
# ----------------------------------------
# H2MM does not know the number of states in advance. Fit models with one to five
# states (each with a few random restarts, keeping the best) and select the state
# count that minimises the Bayesian Information Criterion — the burstH2MM
# ``ICL_plot`` / ``BIC_plot`` step. BIC penalises the extra free parameters of a
# larger model, so it turns over at the true state count.

state_counts = [1, 2, 3, 4, 5]
fits = [engine.fit(k, 6, 500, 1e-7, 0, True, False) for k in state_counts]
bic = np.array([m.bic() for m in fits])
best_k = state_counts[int(np.argmin(bic))]
best = fits[best_k - 1]
print(f"BIC selects {best_k} states")

fig, ax = plt.subplots(figsize=(6, 4))
ax.plot(state_counts, bic, "o-", color="#4e79a7")
ax.axvline(best_k, color="#e15759", ls="--", label=f"selected: {best_k} states")
ax.set_xlabel("number of states")
ax.set_ylabel("BIC")
ax.set_title("H2MM model selection")
ax.set_xticks(state_counts)
ax.legend()
fig.tight_layout()

# %%
# Recovered FRET states
# ---------------------
# The selected model's emission matrix gives the per-state apparent efficiency.
# Sorting by ``E`` and comparing with the dashed ground-truth lines shows the
# three input states are recovered.

E = best.obs_np[:, 1] / best.obs_np[:, 0:2].sum(axis=1)
order = np.argsort(E)
E_sorted = E[order]
populations = best.prior_np[order]
print("recovered FRET efficiencies:", np.round(E_sorted, 2))

fig, ax = plt.subplots(figsize=(6, 4))
for rank, state in enumerate(order):
    ax.bar(E[state], populations[rank], width=0.03, color=STATE_COLORS[rank], zorder=3)
for e in true_E:
    ax.axvline(e, color="0.6", ls="--", zorder=1)
ax.set_xlim(0, 1)
ax.set_xlabel("apparent FRET efficiency E")
ax.set_ylabel("initial-state population")
ax.set_title(f"{best_k} recovered FRET states (dashed = ground truth)")
fig.tight_layout()

# %%
# Decode the state path (Viterbi)
# -------------------------------
# :meth:`tttrlib.HMM.viterbi_path` returns the most-likely hidden state for every
# photon. From that path we reconstruct *dwells* — maximal same-state runs within
# a burst — and the transitions between them. The helper below walks the engine's
# CSR arrays (burst offsets, per-photon stream, and the inter-photon gap slots)
# and measures, per dwell, its duration and its *observed* FRET efficiency from
# the photons it contains.

path, icl = engine.viterbi_path(best)

offsets = np.asarray(engine.get_offsets())
streams = np.asarray(engine.get_streams())
gap_slot = np.asarray(engine.get_gap_slot())
unique_dt = np.asarray(engine.get_unique_dt())

# Map the fitted state index to its FRET rank so colours are E-ordered.
rank_of = {int(state): rank for rank, state in enumerate(order)}


def burst_rel_times(b):
    """Relative macro-times of burst ``b`` (rebuilt from the gap-slot cache)."""
    s, e = int(offsets[b]), int(offsets[b + 1])
    t = np.zeros(e - s, dtype=np.int64)
    if e - s > 1:
        slots = gap_slot[s : e - 1]
        dt = np.where(slots >= 0, unique_dt[np.clip(slots, 0, len(unique_dt) - 1)], 0)
        t[1:] = np.cumsum(dt)
    return s, e, t


dwell_state, dwell_dur, dwell_E = [], [], []
trans_from, trans_to = [], []
for b in range(engine.get_n_bursts()):
    s, e, t = burst_rel_times(b)
    seg = path[s:e]
    run_start = 0
    for rel in range(1, e - s):
        if seg[rel] != seg[rel - 1]:
            g0, g1 = s + run_start, s + rel
            d, a = np.count_nonzero(streams[g0:g1] == 0), np.count_nonzero(streams[g0:g1] == 1)
            dwell_state.append(rank_of[int(seg[rel - 1])])
            dwell_dur.append(int(t[rel] - t[run_start]))
            dwell_E.append(a / (d + a) if (d + a) else np.nan)
            trans_from.append(rank_of[int(seg[rel - 1])])
            trans_to.append(rank_of[int(seg[rel])])
            run_start = rel
    # Trailing dwell of the final run.
    g0, g1 = s + run_start, e
    d, a = np.count_nonzero(streams[g0:g1] == 0), np.count_nonzero(streams[g0:g1] == 1)
    dwell_state.append(rank_of[int(seg[-1])])
    dwell_dur.append(int(t[e - s - 1] - t[run_start]))
    dwell_E.append(a / (d + a) if (d + a) else np.nan)

dwell_state = np.array(dwell_state)
dwell_dur = np.array(dwell_dur)
dwell_E = np.array(dwell_E)
print(f"{dwell_state.size} dwells, {len(trans_from)} transitions")

# %%
# Transition-density plot
# -----------------------
# A 2-D histogram of the FRET efficiency *before* versus *after* each transition
# (burstH2MM's transition-density plot). Off-diagonal density marks which state
# interconversions the chain actually took: here the three states exchange mostly
# with their FRET neighbours.

Ef = np.array([E_sorted[i] for i in trans_from])
Et = np.array([E_sorted[i] for i in trans_to])
fig, ax = plt.subplots(figsize=(5, 4.5))
h = ax.hist2d(Ef, Et, bins=41, range=[[0, 1], [0, 1]], cmap="inferno")
fig.colorbar(h[3], ax=ax, label="transitions")
ax.set_xlabel("E before")
ax.set_ylabel("E after")
ax.set_title("Transition-density plot")
fig.tight_layout()

# %%
# Per-state dwell-time distributions
# ----------------------------------
# The dwell durations per state are exponentially distributed; their mean is the
# inverse of the total escape rate out of the state. Longer-lived states (flatter
# transition rows) sit further to the right.

fig, ax = plt.subplots(figsize=(6, 4))
for rank in range(best_k):
    d = dwell_dur[dwell_state == rank]
    if d.size:
        ax.hist(d, bins=30, histtype="step", lw=2, color=STATE_COLORS[rank],
                label=f"state {rank} (E={E_sorted[rank]:.2f})")
ax.set_xlabel("dwell time (macro-time ticks)")
ax.set_ylabel("dwells")
ax.set_title("Per-state dwell-time distributions")
ax.legend()
fig.tight_layout()

# %%
# A single burst's state path
# ---------------------------
# Finally, the burstH2MM ``plot_burst_path`` view for one dynamic burst: the
# most-likely state (as its FRET efficiency) over time, with each photon drawn as
# a marker coloured by state and shaped by its stream (● donor, ▲ acceptor). This
# is the sub-burst dynamics that a burst-wise FRET histogram averages away.

n_trans_per_burst = np.array(
    [np.count_nonzero(np.diff(path[int(offsets[b]):int(offsets[b + 1])])) for b in range(engine.get_n_bursts())]
)
b = int(np.argmax(n_trans_per_burst))  # the most dynamic burst
s, e, t = burst_rel_times(b)
seg_rank = np.array([rank_of[int(x)] for x in path[s:e]])
seg_E = E_sorted[seg_rank]
seg_streams = streams[s:e]

fig, ax = plt.subplots(figsize=(8, 4))
ax.plot(t, seg_E, color="0.6", lw=1, zorder=1)
for rank in range(best_k):
    for stream, marker in ((0, "o"), (1, "^")):
        m = (seg_rank == rank) & (seg_streams == stream)
        if m.any():
            ax.scatter(t[m], seg_E[m], color=STATE_COLORS[rank], marker=marker, s=40, zorder=2)
ax.set_ylim(-0.05, 1.05)
ax.set_xlabel("time in burst (macro-time ticks)")
ax.set_ylabel("state FRET efficiency")
ax.set_title(f"Burst {b}: Viterbi state path ({int(n_trans_per_burst[b])} transitions)")
fig.tight_layout()

plt.show()

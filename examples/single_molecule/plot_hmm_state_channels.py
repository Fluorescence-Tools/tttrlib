"""
HMM state decoding: faithful distributions and how to store them
================================================================

Fitting an HMM tells you how many states there are. *Using* it means
deciding which state each photon belongs to — and which decoder is right depends
on the question.

The usual decoder, Viterbi, answers "what is the single most likely state
sequence". A lot of downstream analysis instead asks "how do the photons
distribute over the states", and the maximum-likelihood path answers that badly:
photons whose posterior is (0.7, 0.3) all land in state 0, so the 30 % is erased.
Well-separated states get inflated and ambiguous ones vanish.

This example walks through the alternative:

1. simulate a two-state system with **deliberately overlapping** emission
   profiles and an **uneven** state population, the regime where the bias is
   largest,
2. fit it, and pull out the per-photon posterior ``gamma``,
3. decode three ways — Viterbi, a marginal draw from ``gamma``, and FFBS path
   sampling — and compare each against the truth we simulated,
4. persist the decode two ways: state-encoded routing channels in a PTU, and a
   msgpack sidecar of photon states,
5. read both back and confirm they give the *same* per-state histogram.

No experimental file is needed.
"""

import os
import tempfile

import matplotlib.pyplot as plt
import numpy as np

import tttrlib

STATE_COLORS = ["#4e79a7", "#e15759"]

# %%
# A two-state system with overlapping states
# ------------------------------------------
# ``obs`` gives, per state, the probability that a photon lands in each stream.
# With two streams (donor, acceptor) the apparent FRET efficiency per state is
# ``E = obs[:, 1] / obs.sum(axis=1)``. Here the two states sit at E = 0.40 and
# E = 0.60 — close enough that individual photons are genuinely ambiguous — and
# the exit rates are asymmetric, so the chain spends about three quarters of its
# time in the low-FRET state. Overlapping *and* uneven is the regime where
# winner-takes-all hurts most: the majority state wins the argmax even in
# photons where it is only mildly favoured.

N_STREAMS = 2
K01, K10 = 3e-4, 9e-4          # per-tick exit rates -> 75 % / 25 % occupancy
prior = np.array([K10 / (K01 + K10), K01 / (K01 + K10)])
trans = np.array([[1 - K01, K01],
                  [K10, 1 - K10]])
obs = np.array([[0.60, 0.40],
                [0.40, 0.60]])

true_model = tttrlib.HmmModel(list(prior.ravel()), list(trans.ravel()), list(obs.ravel()))
print("true FRET per state:", obs[:, 1] / obs.sum(axis=1))
print("true occupancy    :", prior)

# %%
# Simulate bursts
# ---------------
# ``simulate_bursts`` advances the hidden chain tick by tick along each burst's
# macro-time axis and emits a stream index per photon. We also want the *true*
# state path, to score the decoders against. Running the simulator a second time
# with an identity emission matrix, the same chain and the same seed gives
# exactly that: the identity draw consumes one random number per photon just as
# the real one does, so the two runs stay in lockstep and the second returns the
# hidden states themselves.

rng = np.random.default_rng(3)
N_BURSTS, BURST_LEN = 250, 300
times = [
    np.cumsum(rng.integers(1, 60, size=BURST_LEN)).astype(np.int64).tolist()
    for _ in range(N_BURSTS)
]

identity = tttrlib.HmmModel(list(prior.ravel()), list(trans.ravel()),
                             list(np.eye(2).ravel()))
true_states = np.concatenate(
    [np.asarray(s) for s in tttrlib.HMM.simulate_bursts(identity, times, 11)])
streams = [list(s) for s in tttrlib.HMM.simulate_bursts(true_model, times, 11)]

print(f"{N_BURSTS} bursts, {sum(len(t) for t in times)} photons")
print("simulated occupancy:", np.bincount(true_states, minlength=2) / true_states.size)

# %%
# Fit, then look at the posterior
# -------------------------------
# ``gamma`` is the per-photon posterior state probability from the scaled
# forward-backward recursion — the same array the reference ``H2MM_C`` calls
# ``gamma``. Its rows sum to 1. Unlike a Viterbi path it is a *distribution*,
# and its column means are an unbiased estimate of the state occupancy.

engine = tttrlib.HMM()
engine.set_bursts(times, streams, N_STREAMS)
model = engine.optimize(tttrlib.HMM.factory_model(2, 2, 1e-3, 0), 400, 1e-9)

# EM has no reason to number the states the way we did, so order them by FRET
# before comparing anything against the truth.
fitted_E = model.obs_np[:, 1] / model.obs_np.sum(axis=1)
order = np.argsort(fitted_E)
print("fitted FRET per state:", fitted_E[order])

gamma, n_underflow = engine.gamma(model)
print(f"gamma {gamma.shape} {gamma.dtype}, {n_underflow} underflowed rows")
print("posterior occupancy:", gamma.mean(axis=0)[order])

# %%
# How confident is the model, photon by photon? If every photon were
# unambiguous the histogram would pile up at 0 and 1 and the choice of decoder
# would not matter. It does not.

fig, ax = plt.subplots(figsize=(6, 3.2))
ax.hist(gamma[:, order[0]], bins=50, color="#666666")
ax.set_xlabel(r"$\gamma$(low-FRET state) per photon")
ax.set_ylabel("photons")
ax.set_title("Most photons are genuinely ambiguous")
fig.tight_layout()

# %%
# Three decoders
# --------------
# ``viterbi_path`` takes the most likely sequence. ``jitter_path`` draws each
# photon's state independently from its own ``gamma`` row. ``ffbs_paths`` draws
# whole trajectories from ``P(path | data)`` by forward filtering and backward
# sampling.
#
# Both draws are seeded and reproducible, and — because the random numbers are
# keyed by ``(seed, draw, photon)`` rather than pulled from a shared stream —
# the result does not depend on how many threads ran the decode.

vpath, icl = engine.viterbi_path(model)
jpath, _ = engine.jitter_path(model, seed=0)
fpaths = engine.ffbs_paths(model, seed=0, n_samples=50)


def occupancy(path):
    """Fraction of photons per state, in FRET order."""
    flat = np.asarray(path).ravel()
    return (np.bincount(flat, minlength=2) / flat.size)[order]


truth = np.bincount(true_states, minlength=2) / true_states.size
occ = {
    "truth": truth,
    "posterior": gamma.mean(axis=0)[order],
    "viterbi": occupancy(vpath),
    "jitter": occupancy(jpath),
    "ffbs": occupancy(fpaths),
}
for name, v in occ.items():
    err = "" if name == "truth" else f"   (error {np.abs(v - truth).max():.4f})"
    print(f"{name:>10s}: {v[0]:.4f} {v[1]:.4f}{err}")

# %%
# Plotted against the truth, the argmax is visibly off in one direction while
# both draws sit on it.

fig, ax = plt.subplots(figsize=(6.5, 3.6))
labels = ["viterbi", "jitter", "ffbs"]
x = np.arange(len(labels))
for k, name in enumerate(["low FRET", "high FRET"]):
    ax.bar(x + 0.35 * k, [occ[m][k] for m in labels], width=0.32,
           color=STATE_COLORS[k], label=name)
    ax.axhline(truth[k], color=STATE_COLORS[k], ls="--", lw=1)
ax.set_xticks(x + 0.17)
ax.set_xticklabels(labels)
ax.set_ylabel("fraction of photons")
ax.set_title("Occupancy vs ground truth (dashed)")
ax.legend()
fig.tight_layout()

# %%
# What the marginal draw costs you
# --------------------------------
# The marginal draw is faithful *per photon* but the draws are independent, so
# it has none of ``gamma``'s temporal correlation: a solidly occupied state
# fragments into spurious one-photon dwells. FFBS draws the whole path jointly
# and keeps the dwell structure. This is why dwell times and transition counts
# must come from FFBS, never from a marginal draw.

offsets = np.asarray(engine.get_offsets())
rank_of_state = np.argsort(order)      # fitted state index -> FRET rank


def n_dwells(path):
    return sum(1 + int((np.diff(path[a:b]) != 0).sum())
               for a, b in zip(offsets[:-1], offsets[1:]))


print(f"dwells -- truth {n_dwells(true_states)}, viterbi {n_dwells(vpath)}, "
      f"jitter {n_dwells(jpath)}, ffbs {n_dwells(fpaths[0])}")

# %%
# One burst, decoded four ways. The marginal draw is visibly speckled.

b = int(np.argmax(np.diff(offsets)))
s, e = offsets[b], offsets[b + 1]
fig, axes = plt.subplots(4, 1, figsize=(7.5, 4.6), sharex=True)
for ax, (name, p) in zip(axes, [("truth", true_states),
                                ("viterbi", rank_of_state[vpath]),
                                ("jitter", rank_of_state[jpath]),
                                ("ffbs", rank_of_state[fpaths[0]])]):
    ax.step(np.arange(e - s), p[s:e], where="post", color="#333333", lw=0.9)
    ax.set_ylabel(name, rotation=0, ha="right", va="center")
    ax.set_yticks([0, 1])
axes[-1].set_xlabel("photon index within burst")
fig.suptitle("State trajectory of one burst")
fig.tight_layout()

# %%
# Persisting the decode
# ---------------------
# The decode has to outlive the process. Both paths below need the source photon
# index, which only ``set_bursts_from_tttr`` / ``set_bursts_from_filter`` record
# — so we rebuild the same data as an actual TTTR.

macro, chan, bounds = [], [], []
t0 = 0
for burst_times, burst_streams in zip(times, streams):
    start = len(macro)
    base = t0 - burst_times[0]
    for tt, ss in zip(burst_times, burst_streams):
        macro.append(base + tt)
        chan.append(ss)
    bounds.append((start, len(macro) - 1))
    t0 = macro[-1]
    # Background between bursts: real data has photons outside every burst, and
    # how a decode treats them is a question both persistence paths must answer.
    for _ in range(30):
        t0 += int(rng.integers(1, 3000))
        macro.append(t0)
        chan.append(int(rng.integers(0, 2)))
    t0 += 100000

data = tttrlib.TTTR()
data.append_events(np.asarray(macro, np.uint64),
                   np.zeros(len(macro), np.uint16),
                   np.asarray(chan, np.int8),
                   np.zeros(len(macro), np.int8), False, 0)

green = tttrlib.Channel("green"); green.add_component(0, 0, 65535)
red = tttrlib.Channel("red"); red.add_component(1, 0, 65535)

eng2 = tttrlib.HMM()
eng2.set_bursts_from_tttr(data, np.asarray(bounds, np.int64), [green, red], 3, 1)
model2 = eng2.optimize(tttrlib.HMM.factory_model(2, 2, 1e-3, 0), 400, 1e-9)
path2, _ = eng2.jitter_path(model2, seed=7)

# %%
# Path A — state-encoded routing channels.
#
# The whole id space is compacted. A file's channels are usually sparse — 1, 12
# and 30 for three detectors is ordinary — and those gaps are dead weight in a
# record field a few bits wide. So the used source ids compress to ``0..k-1``
# and the ``(stream, state)`` pairs are allocated immediately after, densely.
# The result is one PTU holding every photon, in which each state is an ordinary
# channel selection — no downstream tool has to know an HMM was involved.
#
# The budget is the *container's* record field: PTU stores 6 channel bits
# (0..63). Narrower formats truncate silently, which is why the target is fixed
# to PTU and why ``build_channel_map`` throws rather than overflowing.
# Compressing is what keeps a split inside a small field: a file whose detectors
# sat at 1, 12 and 30 would otherwise still need 5 bits to hold 7 channels.

cmap = eng2.build_channel_map(data, model2.n_states())
print("source channels compressed:", cmap.source_map)
print("channel map (rows = streams, cols = states):\n", cmap.channels_np)
print(f"highest id written: {cmap.highest_channel()} "
      f"({max(1, cmap.highest_channel().bit_length())} bits)")

split = eng2.split_routing_channels(data, path2, cmap)

# %%
# Path B — the msgpack sidecar.
#
# The source file is untouched; the assignment travels beside it. What is stored
# is the per-photon state *array* (an assignment is a partition, so one byte per
# photon holds what N bitmasks would), plus the model, decoder and seed. Masks
# are materialised on demand.

sidecar = eng2.state_sidecar(path2, model2, "jitter", 7, cmap)

tmp = tempfile.mkdtemp()
ptu_path = os.path.join(tmp, "decoded.ptu")
sidecar_path = os.path.join(tmp, "decoded_hmm_states.msgpack")
split.write(ptu_path, "PTU")
sidecar.write(sidecar_path)
print(f"PTU      {os.path.getsize(ptu_path):>9d} bytes ({data.size()} photons)")
print(f"sidecar  {os.path.getsize(sidecar_path):>9d} bytes")

# %%
# Read both back and check they agree
# -----------------------------------
# A per-state macro-time histogram obtained through a channel selection on the
# re-read PTU, and through a mask from the re-read sidecar, must be the same
# histogram. That equivalence is the contract between the two paths.

reread = tttrlib.TTTR(ptu_path, "PTU")
back = tttrlib.HmmStateSidecar.read(sidecar_path)
print(f"sidecar says: decoder={back.decoder!r} seed={back.seed} "
      f"n_states={back.n_states}")

reread_ch = np.asarray(reread.routing_channels)
mt = np.asarray(reread.macro_times, dtype=np.float64) * 1e-6

fig, axes = plt.subplots(1, 2, figsize=(8.5, 3.4), sharey=True)
bins = np.linspace(mt.min(), mt.max(), 60)
for st in range(model2.n_states()):
    want = [cmap.channels_np[s, st] for s in range(N_STREAMS)]
    idx_a = np.flatnonzero(np.isin(reread_ch, want))   # Path A: channel select
    idx_b = back.indices_for_state(st)                 # Path B: mask select
    assert np.array_equal(idx_a, idx_b), "the two paths must agree photon for photon"
    axes[0].hist(mt[idx_a], bins=bins, histtype="step",
                 color=STATE_COLORS[st], label=f"state {st}")
    axes[1].hist(mt[idx_b], bins=bins, histtype="step",
                 color=STATE_COLORS[st], label=f"state {st}")
axes[0].set_title("Path A: channel selection on the PTU")
axes[1].set_title("Path B: mask from the sidecar")
for ax in axes:
    ax.set_xlabel("macro time / a.u.")
    ax.legend()
axes[0].set_ylabel("photons")
fig.tight_layout()

print("both paths select identical photons for every state")

# %%
# Note one consequence of the renumbering: photons that no decoder assigned —
# background outside every burst, or matching no stream — move to the
# *compressed* form of the channel they were on. So after a split those ids hold
# only unassigned photons; summing what used to be the donor channel gives
# background, not the donor total. A tool that hard-codes channel numbers will be
# wrong about a split file — read ``cmap.source_map`` instead.

states = np.asarray(back.states_np)
print(f"{int((states == 255).sum())} photons unassigned, now on the compressed "
      f"source ids {sorted(set(cmap.source_map.values()))}")

plt.show()

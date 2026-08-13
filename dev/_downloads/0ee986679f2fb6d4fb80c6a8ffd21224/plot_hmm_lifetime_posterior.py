"""
Lifetime-resolved HMM: separating a dark state from real FRET
=============================================================

Intensity alone cannot tell a **dark acceptor** from real FRET: both lower the
acceptor fraction. Only energy transfer *also* shortens the donor lifetime. This
example builds two states that are deliberately identical in intensity — both
emit donor and acceptor in a 50:50 ratio — and separates them on the micro-time
axis alone.

Two pieces of :class:`tttrlib.HMM` do the work:

* **A product alphabet.** ``set_bursts_micro`` packs each photon as
  ``stream * n_micro_bins + micro_bin``, so a state is constrained by *when* its
  photons arrive as well as by *where*. The recursions are unchanged — they read
  ``obs[i*p + y]`` and never ask what a symbol means.
* **A parameterised emission.** :class:`tttrlib.HmmEmissionSpec` generates the
  emission table from a lifetime spectrum instead of leaving every column free.
  Passing it to ``optimize`` re-fits the *lifetimes*, which is what makes the fit
  findable: a free table over this alphabet carries hundreds of numbers and EM
  does not find its good optimum.

The data are simulated, so every recovered number can be checked against the
value that generated it.

* **A posterior.** ``sample`` takes the *same* spec, so the emission is drawn
  rather than re-fitted: the stream split stays a conjugate Dirichlet draw and
  each lifetime gets one univariate slice update. Passing it matters — left out,
  the emission is sampled as a free categorical and the chain wanders into the
  degenerate family, which shows up as unconverged **transitions** (R-hat 1.44
  against 1.03, and an interval five times too wide).
"""

import matplotlib.pyplot as plt
import numpy as np

import tttrlib

STATE_COLORS = ["#4e79a7", "#e15759"]

# --- ground truth ----------------------------------------------------------
N_BINS = 64                 # micro-time bins per stream
SPAN_NS = 16.0              # micro-time axis, ns
DT_NS = SPAN_NS / N_BINS
TAU_DARK = 4.0              # donor unquenched: the acceptor is dark
TAU_FRET = 2.0              # donor quenched by transfer (E = 0.5)
TAU_ACCEPTOR = 2.5
K_SWITCH = 1e-3             # per macro-time tick

# %%
# Build the generating model
# --------------------------
# Both states emit donor and acceptor with probability 0.5, so the intensity
# ratio carries **no** information about which state a photon came from. The
# only difference is the donor lifetime.

spec = tttrlib.HmmEmissionSpec.uniform(2, 2, N_BINS, DT_NS, TAU_DARK)
for state, tau in enumerate((TAU_DARK, TAU_FRET)):
    spec.set_stream_probability(state, 0, 0.5)     # donor
    spec.set_stream_probability(state, 1, 0.5)     # acceptor
    spec.set_spectrum(state, 0, tttrlib.HmmLifetimeSpectrum(tau))
    spec.set_spectrum(state, 1, tttrlib.HmmLifetimeSpectrum(TAU_ACCEPTOR))

true = tttrlib.HmmModel(
    [0.5, 0.5],
    [1 - K_SWITCH, K_SWITCH, K_SWITCH, 1 - K_SWITCH],
    spec.build(),
)
true.n_micro_bins = N_BINS

# %%
# Simulate bursts, keeping the state path
# ---------------------------------------
# ``simulate_bursts`` discards the trajectory, and here the trajectory is what
# lets us score the decoding, so the chain is walked explicitly — propagating
# across each inter-photon gap with :math:`A^{\Delta t}`, exactly as the engine
# does.

rng = np.random.default_rng(3)
A = true.trans_np
obs = true.obs_np
powers = {}

times, symbols, path_true = [], [], []
for _ in range(40):
    t = np.cumsum(rng.integers(1, 40, size=400)).astype(np.int64)
    state = rng.integers(0, 2)
    burst_sym, burst_state = [], []
    for k in range(len(t)):
        if k:
            dt = int(t[k] - t[k - 1])
            if dt not in powers:
                powers[dt] = np.linalg.matrix_power(A, dt)
            state = rng.choice(2, p=powers[dt][state])
        burst_state.append(state)
        burst_sym.append(int(rng.choice(obs.shape[1], p=obs[state])))
    times.append(t.tolist())
    symbols.append(burst_sym)
    path_true.append(burst_state)
path_true = np.concatenate(path_true)

# Split the product symbols back into (stream, micro-time bin), as a loader would
streams = [[y // N_BINS for y in b] for b in symbols]
bins = [[y % N_BINS for y in b] for b in symbols]

eng = tttrlib.HMM()
eng.set_bursts_micro(times, streams, bins, 2, N_BINS, DT_NS)
print(f"{eng.get_n_photons()} photons, alphabet = {eng.get_n_symbols()} symbols")

# %%
# Fit the lifetimes, from a deliberately wrong start
# --------------------------------------------------
# The spec is seeded at 8.0 / 0.8 ns against a truth of 4.0 / 2.0, so the fit has
# to travel. Passing it to ``optimize`` keeps the emission inside the
# lifetime-spectrum family: no free column can go to zero in the middle of a
# decay, which is the degenerate solution a free fit falls into.

fit_spec = tttrlib.HmmEmissionSpec.uniform(2, 2, N_BINS, DT_NS, 3.0)
for state, tau in enumerate((8.0, 0.8)):
    fit_spec.set_stream_probability(state, 0, 0.5)
    fit_spec.set_stream_probability(state, 1, 0.5)
    fit_spec.set_spectrum(state, 0, tttrlib.HmmLifetimeSpectrum(tau))
    fit_spec.set_spectrum(state, 1, tttrlib.HmmLifetimeSpectrum(TAU_ACCEPTOR))

init = tttrlib.HmmModel([0.5, 0.5], [0.99, 0.01, 0.01, 0.99], fit_spec.build())
init.n_micro_bins = N_BINS
fit = eng.optimize(init, 200, 1e-9, 1e-12, True, False, None, None, fit_spec)

tau_fit = sorted((fit_spec.spectrum[i * 2].lifetimes[0] for i in range(2)), reverse=True)
print(f"donor lifetimes: fitted {tau_fit[0]:.2f} / {tau_fit[1]:.2f} ns"
      f"   truth {TAU_DARK} / {TAU_FRET} ns")

decoded, _ = eng.viterbi_path(fit)
decoded = np.asarray(decoded)
accuracy = max((decoded == path_true).mean(), (1 - decoded == path_true).mean())
print(f"per-photon decoding accuracy: {accuracy:.3f}  (0.5 would be chance)")

# %%
# A posterior over the kinetics
# -----------------------------
# EM returns one transition matrix; ``sample`` returns a distribution over them.
# The spec goes in too, so the emission is drawn inside the lifetime family
# instead of being freed.

post = eng.sample(fit, 400, 300, 4, 11, None, 2, fit_spec)
diag = post.diagnostics()
print(f"posterior: Rhat_max = {diag['rhat_max']:.3f}, ESS_min = {diag['ess_min']:.0f}")
_, trans_mean, _ = post.mean_model()
lo_i, hi_i = post.interval(0.95)
print(f"k(0->1) = {trans_mean[0, 1]:.5f}  "
      f"[{lo_i[1][0, 1]:.5f}, {hi_i[1][0, 1]:.5f}]   truth {K_SWITCH}")

# %%
# The negative control
# --------------------
# The claim is that the *lifetime* carried the separation, so the control is to
# throw the micro-time away and fit the same photons on streams alone. Both
# states emit 50:50 by construction, so there is nothing left to separate them —
# and a decoder that still succeeded would mean the states differed in some other
# way and the comparison above proved nothing.

eng_stream = tttrlib.HMM()
eng_stream.set_bursts(times, streams, 2)
flat = tttrlib.HmmModel(list(true.prior_np), list(true.trans_np.ravel()),
                        [0.5, 0.5, 0.5, 0.5])
path_stream, _ = eng_stream.viterbi_path(flat)
path_stream = np.asarray(path_stream)
acc_stream = max((path_stream == path_true).mean(),
                 (1 - path_stream == path_true).mean())
print(f"stream-only decoding accuracy: {acc_stream:.3f}")

# %%
# Plot
# ----
# Left: the two donor decays, which is the whole signal — the stream ratio is
# identical by construction. Right: the same photons decoded with and without
# the micro-time axis.

fig, (ax_decay, ax_post) = plt.subplots(1, 2, figsize=(10.5, 4.0))

t_axis = np.arange(N_BINS) * DT_NS
tab = fit.obs_micro_np
for state in range(2):
    donor = tab[state, 0] / tab[state, 0].sum()
    photons = np.bincount(
        np.asarray([b for burst in bins for b in burst])[decoded == state],
        minlength=N_BINS,
    ).astype(float)
    photons /= max(photons.sum(), 1)
    ax_decay.plot(t_axis, photons, lw=0, marker="o", ms=3, alpha=0.45,
                  color=STATE_COLORS[state])
    ax_decay.plot(t_axis, donor, lw=2, color=STATE_COLORS[state],
                  label=f"state {state}  ({tau_fit[state]:.1f} ns)")
ax_decay.set_yscale("log")
ax_decay.set_xlabel("micro-time (ns)")
ax_decay.set_ylabel("P(bin | state, donor)")
ax_decay.set_title("Donor decay per state\nmarkers: decoded photons   line: fitted spectrum",
                   fontsize=10, loc="left")
ax_decay.legend(frameon=False, fontsize=9)
ax_decay.grid(alpha=0.25, lw=0.6)

bars = [acc_stream, accuracy]
names = ["streams only", "+ micro-time"]
ax_post.bar(names, bars, color=["#9aa4ad", STATE_COLORS[0]], width=0.55)
ax_post.axhline(0.5, color="0.35", lw=1.4, ls="--")
ax_post.text(1.45, 0.505, "chance", color="0.35", fontsize=9,
             ha="right", va="bottom")
for i, v in enumerate(bars):
    ax_post.text(i, v + 0.012, f"{v:.3f}", ha="center", fontsize=10)
ax_post.set_ylim(0.0, 1.0)
ax_post.set_ylabel("per-photon decoding accuracy")
ax_post.set_title("The lifetime axis is the whole signal\nsame photons, same states",
                  fontsize=10, loc="left")
ax_post.grid(alpha=0.25, lw=0.6, axis="y")

fig.tight_layout()
plt.show()

# %%
# What this shows
# ---------------
# The two states are **indistinguishable by intensity** — both emit 50:50 — so
# the stream-only alphabet decodes them at chance, as the right panel shows. Add
# the micro-time axis and the same photons decode well above it, and the
# generating lifetimes come back from a start that was wrong by a factor of two
# in both directions.
#
# That contrast is the point. It is what separates a *dark acceptor* from real
# FRET: both lower the acceptor fraction, and only transfer also shortens the
# donor lifetime. An intensity-only analysis cannot tell them apart in principle,
# not merely in practice.

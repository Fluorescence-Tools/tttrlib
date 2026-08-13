"""
Refining distances and kinetics together (3-state)
==================================================

The emission table can be *generated* from physics rather than fitted freely —
and then the physics itself refined against the photon data. This example runs a
three-state system where each state is a **distance distribution**, and recovers
both the distances and the transition rates from a deliberately wrong start.

The architecture is the point. :class:`tttrlib.HMM` never learns what a Förster
radius is: it supplies the expensive part — the forward–backward recursions over
photons — through :func:`HMM.evaluate`, which returns the sufficient statistics
of one E-step. Everything physical happens outside it, in a few lines of NumPy::

    while not converged:
        ev = eng.evaluate(model)        # C++: one forward-backward pass
        A  = row_normalize(ev.xi)       # closed-form M-step for the kinetics
        R  = refine(R, ev.gamma_obs)    # 1-D search per state -- the physics

That loop is why the emission is *parameterised* and not free. One scalar per
state — the mean donor–acceptor distance — sets both how many acceptor photons
appear **and** how fast the donor decays. Fitting the two consistently is what
separates real transfer from a dark acceptor, and it is why a free table over
this alphabet is the wrong model even before it is the wrong optimum.

A state is a *distribution* over distances, not a single distance: dye linkers
have a width, so a state's donor decay is a sum of exponentials — one per
quadrature node — rather than a single lifetime. That is why the fitted quantity
is a distance and not a lifetime; a lifetime could not represent it.
"""

import matplotlib.pyplot as plt
import numpy as np

import tttrlib

STATE_COLORS = ["#4e79a7", "#59a14f", "#e15759"]

# --- physics: lives here, never in C++ -------------------------------------
R0 = 52.0          # Förster radius, Å
TAU_D0 = 4.0       # unquenched donor lifetime, ns
SIGMA_R = 6.0      # linker width, Å
TAU_A = 2.5        # acceptor lifetime, ns
N_NODE = 15        # distance quadrature nodes

N_BINS, DT_NS = 64, 0.25
R_TRUE = np.array([42.0, 52.0, 64.0])


def spectrum_for(r_mean):
    """One distance distribution -> (donor amplitudes, lifetimes, acceptor fraction).

    Both outputs come from the *same* scalar, which is the E–tau coupling: a
    shorter distance transfers more (more acceptor photons) and quenches the
    donor further (faster decay). A model free to move them independently can
    fit a dark acceptor as though it were FRET.
    """
    r = np.linspace(max(r_mean - 3 * SIGMA_R, 10.0), r_mean + 3 * SIGMA_R, N_NODE)
    w = np.exp(-0.5 * ((r - r_mean) / SIGMA_R) ** 2)
    w /= w.sum()
    e = 1.0 / (1.0 + (r / R0) ** 6)          # transfer efficiency per node
    tau_da = TAU_D0 * (1.0 - e)              # quenched donor lifetime per node
    amp = w * (1.0 - e)                      # donor photons survive transfer
    keep = tau_da > 1e-3
    return (amp[keep] / max(amp[keep].sum(), 1e-30), tau_da[keep], float((w * e).sum()))


def build_spec(distances):
    """Distances -> an emission spec the engine can score."""
    spec = tttrlib.HmmEmissionSpec.uniform(len(distances), 2, N_BINS, DT_NS, TAU_D0)
    for k, r in enumerate(distances):
        amp, tau, e = spectrum_for(r)
        spec.set_stream_probability(k, 0, 1.0 - e)
        spec.set_stream_probability(k, 1, e)
        spec.set_spectrum(k, 0, tttrlib.HmmLifetimeSpectrum(
            tttrlib.VectorDouble(list(amp)), tttrlib.VectorDouble(list(tau))))
        spec.set_spectrum(k, 1, tttrlib.HmmLifetimeSpectrum(TAU_A))
    return spec


def q_of_distance(r, state, gamma_obs):
    """Q for one state as a function of its distance — both terms together."""
    table = np.asarray(build_spec([r]).build_np())[0]
    counts = gamma_obs[state].reshape(2, N_BINS)
    return float((counts * np.log(np.maximum(table, 1e-300))).sum())


def refine(distances, gamma_obs, lo=25.0, hi=90.0):
    """The physical M-step: one bounded 1-D search per state, by golden section."""
    g = (np.sqrt(5) - 1) / 2
    out = []
    for state in range(len(distances)):
        a, b = lo, hi
        c, d = b - g * (b - a), a + g * (b - a)
        fc, fd = q_of_distance(c, state, gamma_obs), q_of_distance(d, state, gamma_obs)
        for _ in range(40):
            if b - a < 1e-3:
                break
            if fc > fd:
                b, d, fd = d, c, fc
                c = b - g * (b - a)
                fc = q_of_distance(c, state, gamma_obs)
            else:
                a, c, fc = c, d, fd
                d = a + g * (b - a)
                fd = q_of_distance(d, state, gamma_obs)
        out.append(0.5 * (a + b))
    return np.array(out)


# %%
# Simulate a three-state system
# -----------------------------
# Rates are asymmetric, so the stationary populations differ — which is what
# makes the kinetics worth recovering rather than assuming.

K = np.array([[0.0, 8e-4, 2e-4],
              [6e-4, 0.0, 6e-4],
              [2e-4, 8e-4, 0.0]])
A_TRUE = K.copy()
np.fill_diagonal(A_TRUE, 1.0 - K.sum(1))

true = tttrlib.HmmModel([1 / 3] * 3, list(A_TRUE.ravel()), build_spec(R_TRUE).build())
true.n_micro_bins = N_BINS

rng = np.random.default_rng(5)
obs, powers = true.obs_np, {}
times, symbols = [], []
for _ in range(60):
    t = np.cumsum(rng.integers(1, 40, size=500)).astype(np.int64)
    state = rng.integers(0, 3)
    row = []
    for i in range(len(t)):
        if i:
            dt = int(t[i] - t[i - 1])
            if dt not in powers:
                powers[dt] = np.linalg.matrix_power(A_TRUE, dt)
            state = rng.choice(3, p=powers[dt][state])
        row.append(int(rng.choice(obs.shape[1], p=obs[state])))
    times.append(t.tolist())
    symbols.append(row)

eng = tttrlib.HMM()
eng.set_bursts_micro(times,
                     [[y // N_BINS for y in b] for b in symbols],
                     [[y % N_BINS for y in b] for b in symbols],
                     2, N_BINS, DT_NS)
print(f"{eng.get_n_photons()} photons, {eng.get_n_symbols()} symbols, 3 states")

# %%
# Refine
# ------
# Each iteration is one C++ E-step and two M-steps: the transition matrix in
# closed form, the distances by a bounded search. The log-likelihood increases
# monotonically, which is what says the physical M-step is a genuine EM step and
# not a heuristic bolted on beside one.

R = np.array([35.0, 55.0, 75.0])                       # deliberately wrong
A = np.full((3, 3), 1e-3)
np.fill_diagonal(A, 1 - 2e-3)

history = []
for it in range(12):
    model = tttrlib.HmmModel([1 / 3] * 3, list(A.ravel()), build_spec(R).build())
    model.n_micro_bins = N_BINS
    ev = eng.evaluate(model)
    xi = ev.xi_np
    A = xi / xi.sum(1, keepdims=True)
    R = refine(R, ev.gamma_obs_np(3))
    history.append((R.copy(), ev.loglik))

print(f"distances: {R.round(2)}   truth {R_TRUE}")
print(f"rates k01={A[0, 1]:.5f} k12={A[1, 2]:.5f}   truth {K[0, 1]:.5f} {K[1, 2]:.5f}")

# %%
# Plot
# ----
# Left: each state's distance distribution, recovered against the truth. Right:
# the refinement trajectory, which starts wrong on purpose.

fig, (ax_dist, ax_trace) = plt.subplots(1, 2, figsize=(10.5, 4.0))

grid = np.linspace(25, 85, 400)
for k in range(3):
    for r, style, alpha, label in ((R_TRUE[k], "--", 0.55, None),
                                   (R[k], "-", 1.0, f"state {k}: {R[k]:.1f} Å")):
        p = np.exp(-0.5 * ((grid - r) / SIGMA_R) ** 2)
        ax_dist.plot(grid, p / p.max(), style, lw=2, alpha=alpha,
                     color=STATE_COLORS[k], label=label)
ax_dist.axvline(R0, color="0.4", lw=1.2, ls=":")
ax_dist.text(R0 + 0.6, 1.02, "$R_0$", color="0.4", fontsize=9)
ax_dist.set_xlabel("donor–acceptor distance (Å)")
ax_dist.set_ylabel("p(R), scaled")
ax_dist.set_title("Recovered distance distributions\nsolid: fitted   dashed: truth",
                  fontsize=10, loc="left")
ax_dist.legend(frameon=False, fontsize=9)
ax_dist.grid(alpha=0.25, lw=0.6)

steps = np.arange(len(history))
for k in range(3):
    ax_trace.plot(steps, [h[0][k] for h in history], lw=2, marker="o", ms=4,
                  color=STATE_COLORS[k], label=f"state {k}")
    ax_trace.axhline(R_TRUE[k], color=STATE_COLORS[k], lw=1.2, ls="--", alpha=0.55)
ax_trace.set_xlabel("EM iteration")
ax_trace.set_ylabel("mean distance (Å)")
ax_trace.set_title("Refinement from a wrong start\ndashed: truth", fontsize=10, loc="left")
ax_trace.legend(frameon=False, fontsize=9)
ax_trace.grid(alpha=0.25, lw=0.6)

fig.tight_layout()
plt.show()

# %%
# What this shows
# ---------------
# Distances recovered to a few tenths of an Ångström from a start wrong by up to
# 11 Å, with the transition rates refined in the same loop — and the engine
# never saw a Förster radius. That separation is deliberate: the photon
# recursions are expensive and generic, the physics is cheap and specific, and
# keeping them apart means a different physical model needs no C++ at all.
#
# The same loop takes any parameterisation with a tractable ``Q``. Swap
# ``spectrum_for`` for a different distance distribution, a multi-exponential
# donor, or a crosstalk-aware stream split, and nothing below it changes.
#
# What it does *not* do is give an interval on those distances. Increasing the
# state count or the parameter count per state makes the optimum harder to find
# and the answer easier to over-read; :func:`HMM.sample` supplies a posterior
# over the kinetics, and the same external loop can be run over a posterior draw
# rather than a point estimate.

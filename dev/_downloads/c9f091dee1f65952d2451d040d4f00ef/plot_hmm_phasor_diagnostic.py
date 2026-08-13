"""
A phasor check on a converged fit
=================================

A fit that converges and returns the right :math:`E` can still be built on the
wrong decay model, and nothing in the usual output says so. This example shows a
model-free check that does: the **phasor** of each state's decay, computed from
the data and from the model and compared.

The phasor transform reduces a decay to two numbers — the sine and cosine
moments of the micro-time histogram at one frequency — without fitting anything.
That is what makes it useful here. The likelihood already knows the model is
imperfect, but it reports one number for the whole dataset; the phasor localises
the discrepancy to a **state** and shows it as a distance on a plane.

The test case is the one that matters in practice. Real donors are
multi-exponential even unquenched, so a per-state mono-exponential
parameterisation is misspecified — and it is misspecified in a way the FRET
efficiency does not reveal, because :math:`E` comes from the *stream split*
while the error lives in the *decay shape*.

.. note::

   The IRF phasor is ``(1, 0)`` for an ideal (delta) instrument response, not
   ``(0, 0)``. The correction divides by :math:`g_{irf}^2 + s_{irf}^2`, so
   zeros are a division by zero; that used to yield a silent ``nan`` and now
   raises ``ValueError``. It is also the default, so it can simply be omitted.
"""

import matplotlib.pyplot as plt
import numpy as np

import tttrlib

STATE_COLORS = ["#4e79a7", "#e15759"]

N_BINS, DT_NS = 64, 0.25
FREQ = 1.0 / (N_BINS * DT_NS)          # one full period across the micro-time window
TAU_A = 2.5
E_TRUE = [0.25, 0.70]

# Each state's donor decay is a two-component mixture -- an intrinsic property of
# the dye, not of the FRET.
TAU_TRUE = [[4.5, 1.2], [2.2, 0.5]]
AMP_TRUE = [[0.6, 0.4], [0.6, 0.4]]


def multi_spec(taus, amps, efficiencies):
    spec = tttrlib.HmmEmissionSpec.uniform(len(efficiencies), 2, N_BINS, DT_NS, 4.0)
    for k, e in enumerate(efficiencies):
        spec.set_stream_probability(k, 0, 1.0 - e)
        spec.set_stream_probability(k, 1, e)
        spec.set_spectrum(k, 0, tttrlib.HmmLifetimeSpectrum(
            tttrlib.VectorDouble(list(amps[k])), tttrlib.VectorDouble(list(taus[k]))))
        spec.set_spectrum(k, 1, tttrlib.HmmLifetimeSpectrum(TAU_A))
    return spec


def mono_spec(efficiencies=(0.3, 0.6), tau=3.0):
    """The misspecified model: one lifetime per state, which cannot represent a
    mixture no matter what value it takes."""
    spec = tttrlib.HmmEmissionSpec.uniform(len(efficiencies), 2, N_BINS, DT_NS, 4.0)
    for k, e in enumerate(efficiencies):
        spec.set_stream_probability(k, 0, 1.0 - e)
        spec.set_stream_probability(k, 1, e)
        spec.set_spectrum(k, 0, tttrlib.HmmLifetimeSpectrum(tau))
        spec.set_spectrum(k, 1, tttrlib.HmmLifetimeSpectrum(TAU_A))
    return spec


def phasor(hist):
    """(g, s) of a micro-time histogram. Ideal IRF -> phasor (1, 0)."""
    counts = tttrlib.VectorInt32([int(round(v)) for v in hist])
    g, s = tttrlib.DecayPhasor.compute_phasor_bincounts(counts, FREQ, 1, 1.0, 0.0)
    return float(g), float(s)


# %%
# Simulate, then fit twice
# ------------------------
# The same photons are fitted with a mono-exponential spec (wrong) and with the
# generating multi-exponential one (right). Only the decay parameterisation
# differs; the alphabet, the data and the kinetics are identical.

def simulate(seed, n_burst=60, n_ph=500):
    true = tttrlib.HmmModel([0.5, 0.5], [0.999, 0.001, 0.001, 0.999],
                            multi_spec(TAU_TRUE, AMP_TRUE, E_TRUE).build())
    true.n_micro_bins = N_BINS
    rng = np.random.default_rng(seed)
    obs, powers = true.obs_np, {}
    A = np.asarray(true.trans_np)
    times, syms = [], []
    for _ in range(n_burst):
        t = np.cumsum(rng.integers(1, 30, size=n_ph)).astype(np.int64)
        state = rng.integers(0, 2)
        row = []
        for i in range(len(t)):
            if i:
                dt = int(t[i] - t[i - 1])
                if dt not in powers:
                    powers[dt] = np.linalg.matrix_power(A, dt)
                state = rng.choice(2, p=powers[dt][state])
            row.append(int(rng.choice(obs.shape[1], p=obs[state])))
        times.append(t.tolist())
        syms.append(row)
    eng = tttrlib.HMM()
    eng.set_bursts_micro(times, [[y // N_BINS for y in b] for b in syms],
                         [[y % N_BINS for y in b] for b in syms], 2, N_BINS, DT_NS)
    return eng


def fit_and_phasors(eng, spec):
    """Fit, then take each state's data and model phasor from the SAME E-step.

    The data histogram is the posterior-weighted one from `HmmEval.gamma_obs`,
    so 'the data for state k' means what the fitted model believes belongs to
    state k -- not a hard Viterbi assignment, which would discard the
    uncertainty and make a bad model look self-consistent.
    """
    init = tttrlib.HmmModel([0.5, 0.5], [0.99, 0.01, 0.01, 0.99], spec.build())
    init.n_micro_bins = N_BINS
    fit = eng.optimize(init, 300, 1e-9, 1e-12, True, False, None, None, spec)
    ev = eng.evaluate(fit)
    gamma = ev.gamma_obs_np(2)
    table = fit.obs_micro_np
    data_p, model_p, dist = [], [], []
    for k in range(2):
        d_hist = gamma[k].reshape(2, N_BINS)[0]                 # donor stream
        m_hist = table[k, 0] / table[k, 0].sum() * d_hist.sum()
        gd, sd = phasor(d_hist)
        gm, sm = phasor(m_hist)
        data_p.append((gd, sd))
        model_p.append((gm, sm))
        dist.append(float(np.hypot(gd - gm, sd - sm)))
    return fit, np.array(data_p), np.array(model_p), np.array(dist), ev.loglik


eng = simulate(3)
fit_bad, data_bad, model_bad, dist_bad, ll_bad = fit_and_phasors(eng, mono_spec())
fit_good, data_good, model_good, dist_good, ll_good = fit_and_phasors(
    eng, multi_spec(TAU_TRUE, AMP_TRUE, (0.3, 0.6)))

E_bad = fit_bad.obs_micro_np.sum(axis=2)[:, 1]
E_good = fit_good.obs_micro_np.sum(axis=2)[:, 1]
print(f"truth          E = {np.round(E_TRUE, 3)}")
print(f"mono  (WRONG)  E = {np.round(E_bad, 3)}   phasor distance {np.round(dist_bad, 4)}")
print(f"multi (RIGHT)  E = {np.round(E_good, 3)}   phasor distance {np.round(dist_good, 4)}")
print(f"\nboth fits recover E to within "
      f"{max(np.abs(E_bad - E_TRUE).max(), np.abs(E_good - E_TRUE).max()):.3f} -- "
      f"E does not reveal the misspecification")
# The likelihood is not blind to it -- it is just one number for the whole
# dataset, and it cannot say which state is at fault or how.
print(f"log-likelihood   mono {ll_bad:.1f}   multi {ll_good:.1f}   "
      f"(difference {ll_good - ll_bad:+.1f})")

# %%
# Replicate, because one dataset is not a measurement
# ---------------------------------------------------
# The phasor distance carries sampling noise, so a single pair of numbers cannot
# say whether the gap is real.

SEEDS = (3, 4, 5, 6, 7)
bad_all, good_all = [], []
for seed in SEEDS:
    e = simulate(seed)
    bad_all.append(fit_and_phasors(e, mono_spec())[3])
    good_all.append(fit_and_phasors(e, multi_spec(TAU_TRUE, AMP_TRUE, (0.3, 0.6)))[3])
bad_all, good_all = np.array(bad_all), np.array(good_all)


def summary(a):
    return a.mean(), a.std(ddof=1) / np.sqrt(a.size)


m_bad, e_bad_se = summary(bad_all)
m_good, e_good_se = summary(good_all)
print(f"\nover {len(SEEDS)} datasets:")
print(f"  misspecified  {m_bad:.4f} +- {e_bad_se:.4f}")
print(f"  correct       {m_good:.4f} +- {e_good_se:.4f}")
print(f"  separation    {m_bad / m_good:.1f}x   "
      f"(worst correct {good_all.max():.4f} < best wrong {bad_all.min():.4f})")

# %%
# Plot
# ----
# Left: the phasor plane. Every mono-exponential decay lies on the universal
# semicircle; a mixture lies inside it. Each state contributes a **data** point
# and a **model** point, joined by a line whose length is the diagnostic. Right:
# that length, against the FRET efficiency error, over the replicate datasets.

fig, (ax_ph, ax_cmp) = plt.subplots(1, 2, figsize=(10.5, 4.2))

theta = np.linspace(0, np.pi, 200)
ax_ph.plot(0.5 + 0.5 * np.cos(theta), 0.5 * np.sin(theta), color="0.55", lw=1.4)
ax_ph.text(0.62, 0.44, "universal semicircle\n(mono-exponential)",
           fontsize=8, color="0.4")

for k in range(2):
    for (dp, mp), lbl, mk in (((data_bad[k], model_bad[k]), "mono (wrong)", "X"),
                              ((data_good[k], model_good[k]), "multi (right)", "o")):
        ax_ph.plot([dp[0], mp[0]], [dp[1], mp[1]], "-", lw=1.2,
                   color=STATE_COLORS[k], alpha=0.55)
        # Label once per marker style, on the first state only, so the legend
        # explains the encoding instead of repeating it per state.
        ax_ph.plot(*mp, mk, ms=9, mfc="none", mew=2, color=STATE_COLORS[k],
                   label=f"model, {lbl}" if k == 0 else None)
    ax_ph.plot(*data_bad[k], "s", ms=8, color=STATE_COLORS[k],
               label="data" if k == 0 else None)
    ax_ph.annotate(f"state {k}", data_bad[k], textcoords="offset points",
                   xytext=(9, -3), fontsize=8.5, color=STATE_COLORS[k])
ax_ph.set_xlabel("g")
ax_ph.set_ylabel("s")
ax_ph.set_title("Phasor plane\nfilled: data   open: model", fontsize=10, loc="left")
ax_ph.legend(frameon=False, fontsize=7.5, loc="upper left")
ax_ph.grid(alpha=0.25, lw=0.6)
ax_ph.set_aspect("equal")
ax_ph.set_xlim(0, 0.75)
ax_ph.set_ylim(0, 0.55)

x = np.arange(2)
ax_cmp.bar(x - 0.18, [m_bad, np.abs(np.array(E_bad) - E_TRUE).mean()],
           width=0.34, color="#9aa4ad", label="mono (wrong)")
ax_cmp.bar(x + 0.18, [m_good, np.abs(np.array(E_good) - E_TRUE).mean()],
           width=0.34, color=STATE_COLORS[0], label="multi (right)")
ax_cmp.errorbar([x[0] - 0.18, x[0] + 0.18], [m_bad, m_good],
                yerr=[e_bad_se, e_good_se], fmt="none", ecolor="0.2", capsize=3)
ax_cmp.set_xticks(x)
ax_cmp.set_xticklabels(["phasor distance", "|E error|"])
ax_cmp.set_ylabel("value")
ax_cmp.set_title("The phasor separates the models\nE does not",
                 fontsize=10, loc="left")
ax_cmp.legend(frameon=False, fontsize=9)
ax_cmp.grid(alpha=0.25, lw=0.6, axis="y")

fig.tight_layout()
plt.show()

# %%
# What this shows
# ---------------
# Both fits recover :math:`E` to within about 0.01 of the truth, so on the
# headline number they are indistinguishable. Their phasor distances differ by
# roughly **4.6x** — 0.044 against 0.010 — with no overlap across the replicate
# datasets. The check sees what the FRET efficiency cannot, because :math:`E` is
# set by the stream split while the error lives in the decay shape.
#
# **Read it as a relative diagnostic, not an absolute one.** The correctly
# specified model does not score zero, and it should not be expected to: the
# distance carries sampling noise, and binning and truncation of the micro-time
# window move the data and model points slightly differently. For the same
# reason, distance from the universal semicircle is *not* a clean test of
# multi-exponentiality here — a binned, truncated mono-exponential does not sit
# exactly on the circle either. Compare candidate parameterisations **on the
# same data**; the smaller distance is the better-specified model.
#
# The natural companion is the likelihood, and the two answer different
# questions. The likelihood ranks models globally with one number and is what
# should decide a comparison. The phasor says *where* a model is failing —
# which state, and in which direction on the plane — and it does so without
# fitting anything, so it cannot be talked into agreeing with the model being
# checked.
#
# .. note::
#
#    The data histogram here is the posterior-weighted one from
#    :func:`HMM.evaluate`, not a hard Viterbi assignment. That matters: hard
#    assignment discards the fit's own uncertainty and pulls the data phasor
#    toward the model that produced the assignment, which would make a badly
#    specified model look self-consistent.

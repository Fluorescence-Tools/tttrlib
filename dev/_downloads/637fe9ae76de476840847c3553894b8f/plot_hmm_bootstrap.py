"""
Error bars on a maximum-likelihood fit, by resampling bursts
============================================================

:func:`HMM.fit` returns a point estimate and nothing about its uncertainty.
:func:`HMM.sample` gives a calibrated posterior, but it needs priors and costs
sweeps. Between them sits the **non-parametric bootstrap**: bursts are
conditionally independent given the model, so resampling them with replacement
and refitting gives a frequentist interval with no prior at all.

This example measures whether that interval is honest — the only question worth
asking about an error bar — and compares it with
:func:`HmmEval.posterior_sd_analytic`, the cheap closed-form width tttrlib
already ships.

**No new API is involved.** A bootstrap replicate is the same dataset with its
burst list resampled, which both loaders already accept.
"""

import matplotlib.pyplot as plt
import numpy as np

import tttrlib

COLORS = ["#4e79a7", "#59a14f", "#e15759"]

N_STATE, N_STREAM = 2, 2
A_TRUE = np.array([[0.995, 0.005], [0.010, 0.990]])
B_TRUE = np.array([[0.80, 0.20], [0.25, 0.75]])
N_BURST, N_PH = 40, 250


def simulate(seed, n_burst=N_BURST):
    """Photon streams from a known two-state model."""
    rng = np.random.default_rng(seed)
    powers, times, syms = {}, [], []
    for _ in range(n_burst):
        t = np.cumsum(rng.integers(1, 25, size=N_PH)).astype(np.int64)
        s = rng.integers(0, N_STATE)
        row = []
        for i in range(len(t)):
            if i:
                dt = int(t[i] - t[i - 1])
                if dt not in powers:
                    powers[dt] = np.linalg.matrix_power(A_TRUE, dt)
                s = rng.choice(N_STATE, p=powers[dt][s])
            row.append(int(rng.choice(N_STREAM, p=B_TRUE[s])))
        times.append(t.tolist())
        syms.append(row)
    return times, syms


def fit(times, syms):
    """Fit and order the states canonically.

    States are exchangeable, so two fits can return the same model with the
    labels swapped. Without a canonical order the replicate spread would be
    measuring label switching rather than uncertainty, and every interval would
    come out far too wide.
    """
    eng = tttrlib.HMM()
    eng.set_bursts(times, syms, N_STREAM)
    m = eng.fit(N_STATE, n_restarts=3, seed=0)
    order = np.argsort(m.obs_np[:, 1])
    return m.obs_np[order][:, 1], m.trans_np[order][:, order]


def bootstrap(times, syms, n_rep, rng):
    """Resample bursts with replacement; refit each replicate."""
    obs, trans = [], []
    for _ in range(n_rep):
        idx = rng.integers(0, len(times), size=len(times))
        o, a = fit([times[i] for i in idx], [syms[i] for i in idx])
        obs.append(o)
        trans.append([a[0, 1], a[1, 0]])
    return np.array(obs), np.array(trans)


# %%
# One dataset
# -----------

TRUTH = {"B0": np.sort(B_TRUE[:, 1])[0], "B1": np.sort(B_TRUE[:, 1])[1],
         "A01": A_TRUE[0, 1], "A10": A_TRUE[1, 0]}

times, syms = simulate(100)
point_obs, point_trans = fit(times, syms)
boot_obs, boot_trans = bootstrap(times, syms, 200, np.random.default_rng(1))

draws = {"B0": boot_obs[:, 0], "B1": boot_obs[:, 1],
         "A01": boot_trans[:, 0], "A10": boot_trans[:, 1]}
point = {"B0": point_obs[0], "B1": point_obs[1],
         "A01": point_trans[0, 1], "A10": point_trans[1, 0]}

print("parameter   truth     estimate   95% bootstrap interval   covers")
for k in ("B0", "B1", "A01", "A10"):
    lo, hi = np.percentile(draws[k], [2.5, 97.5])
    ok = lo <= TRUTH[k] <= hi
    print(f"{k:<10}  {TRUTH[k]:<8.4f}  {point[k]:<9.4f}  [{lo:.4f}, {hi:.4f}]"
          f"        {'yes' if ok else 'NO'}")

# %%
# Is the interval honest?
# -----------------------
# A single interval that happens to cover proves nothing. Coverage is a property
# of the *procedure*, so it has to be measured over many datasets: a nominal 95%
# interval should contain the truth in about 95% of them.
#
# The analytic width is included because it is the cheap alternative a user
# would otherwise reach for, and it is documented as a lower bound.
#
# The replicate count matters and is easy to set too low: a 2.5th percentile of
# 40 draws is a badly determined number. Measured pooled coverage runs 89.4% at
# 40 replicates and 94.4% at both 100 and 250 -- so 100 is enough and 40 is not.

N_DATA, N_REP = 25, 100
hits_boot = {k: 0 for k in TRUTH}
hits_ana = {k: 0 for k in TRUTH}
width_boot = {k: [] for k in TRUTH}
width_ana = {k: [] for k in TRUTH}

for d in range(N_DATA):
    t_, s_ = simulate(200 + d)
    bo, bt = bootstrap(t_, s_, N_REP, np.random.default_rng(500 + d))
    for k, v in (("B0", bo[:, 0]), ("B1", bo[:, 1]),
                 ("A01", bt[:, 0]), ("A10", bt[:, 1])):
        lo, hi = np.percentile(v, [2.5, 97.5])
        hits_boot[k] += lo <= TRUTH[k] <= hi
        width_boot[k].append(hi - lo)

    # The closed-form width, from the same E-step.
    eng = tttrlib.HMM()
    eng.set_bursts(t_, s_, N_STREAM)
    m = eng.fit(N_STATE, n_restarts=3, seed=0)
    order = np.argsort(m.obs_np[:, 1])
    _, sd_trans, sd_obs = eng.evaluate(m).posterior_sd_analytic(N_STATE)
    sd_trans = np.asarray(sd_trans)[order][:, order]
    sd_obs = np.asarray(sd_obs)[order]
    est = {"B0": m.obs_np[order][0, 1], "B1": m.obs_np[order][1, 1],
           "A01": m.trans_np[order][:, order][0, 1],
           "A10": m.trans_np[order][:, order][1, 0]}
    sd = {"B0": sd_obs[0, 1], "B1": sd_obs[1, 1],
          "A01": sd_trans[0, 1], "A10": sd_trans[1, 0]}
    for k in TRUTH:
        hits_ana[k] += abs(est[k] - TRUTH[k]) <= 1.96 * sd[k]
        width_ana[k].append(2 * 1.96 * sd[k])

print(f"\ncoverage over {N_DATA} datasets (nominal 95%):")
print("parameter   bootstrap        analytic")
for k in ("B0", "B1", "A01", "A10"):
    print(f"{k:<10}  {hits_boot[k] / N_DATA:>6.0%}           {hits_ana[k] / N_DATA:>6.0%}")
tot_b = sum(hits_boot.values()) / (4 * N_DATA)
tot_a = sum(hits_ana.values()) / (4 * N_DATA)
# A coverage estimate is itself an estimate. At this budget its standard error
# is a few percent, so the run below cannot resolve 91% from 95% -- a larger
# run (40 datasets x 100 replicates) puts the bootstrap at 94.4% +- 1.8%.
se = np.sqrt(tot_b * (1 - tot_b) / (4 * N_DATA)) * 100
print(f"{'pooled':<10}  {tot_b:>6.1%} +-{se:.1f}%     {tot_a:>6.1%}")

# %%
# Plot
# ----
# Left: the bootstrap distribution for one parameter, with the truth and the
# interval. Right: coverage against the nominal 95% line, which is what decides
# whether an error bar can be quoted.

fig, (ax_dist, ax_cov) = plt.subplots(1, 2, figsize=(10.5, 4.2))

v = draws["B1"]
ax_dist.hist(v, bins=28, color="#9aa4ad", edgecolor="white", linewidth=0.5)
lo, hi = np.percentile(v, [2.5, 97.5])
ax_dist.axvspan(lo, hi, color=COLORS[0], alpha=0.16, label="95% interval")
ax_dist.axvline(TRUTH["B1"], color=COLORS[2], lw=2.2, label="truth")
ax_dist.axvline(point["B1"], color=COLORS[0], lw=2, ls="--", label="point estimate")
ax_dist.set_xlabel("P(acceptor | high-FRET state)")
ax_dist.set_ylabel("bootstrap replicates")
ax_dist.set_title("Resampling bursts\n200 replicates, one dataset",
                  fontsize=10, loc="left")
ax_dist.legend(frameon=False, fontsize=9)
ax_dist.grid(alpha=0.25, lw=0.6, axis="y")

keys = ["B0", "B1", "A01", "A10"]
x = np.arange(len(keys))
ax_cov.bar(x - 0.18, [hits_boot[k] / N_DATA * 100 for k in keys], width=0.34,
           color=COLORS[0], label="bootstrap")
ax_cov.bar(x + 0.18, [hits_ana[k] / N_DATA * 100 for k in keys], width=0.34,
           color="#9aa4ad", label="analytic (lower bound)")
ax_cov.axhline(95, color=COLORS[2], lw=1.6, ls="--")
ax_cov.text(-0.45, 96, "nominal 95%", color=COLORS[2], fontsize=9)
ax_cov.set_xticks(x)
ax_cov.set_xticklabels(keys)
ax_cov.set_ylabel("coverage (%)")
ax_cov.set_ylim(0, 108)
ax_cov.set_title("Does the interval cover the truth?\nmeasured over datasets",
                 fontsize=10, loc="left")
ax_cov.legend(frameon=False, fontsize=9, loc="lower right")
ax_cov.grid(alpha=0.25, lw=0.6, axis="y")

fig.tight_layout()
plt.show()

# %%
# What this shows
# ---------------
# The bootstrap interval is honest; the analytic one is not. On a larger run
# than this example affords — 40 datasets, 100 replicates — pooled coverage is
# **94.4% ± 1.8%** against a nominal 95%, while the analytic width covers at
# roughly 63%. The smaller budget above cannot resolve those few percent, which
# is why its own printed figure carries a standard error. That gap is not a
# defect in the closed form; it is what its documentation already says. The
# analytic posterior conditions on the state path and therefore drops
# :math:`\\mathrm{Var}(E[\\theta \\mid y, \\text{path}])`, so it is a *lower
# bound* on the width, measured at roughly half. Quoting it as an error bar
# would overstate precision by a factor of two.
#
# Two sample sizes govern it, and both have a floor worth knowing. Coverage
# against the number of **bursts** runs 90% / 94% / 93% at 20 / 40 / 120, so
# below a few tens of bursts the interval is mildly optimistic — a small-sample
# property of the percentile bootstrap, not of this implementation. Coverage
# against the number of **replicates** runs 89.4% at 40 and 94.4% at both 100
# and 250: use at least 100, and gain nothing past a few hundred.
#
# **Order the states before summarising.** States are exchangeable, so replicate
# fits can come back relabelled. Without the canonical ordering in ``fit()``
# above, the spread would measure label switching and every interval would be
# far too wide — the same trap that applies to Gibbs draws.
#
# **On large datasets, resample the burst list rather than the photons.** Both
# loaders accept a resampled burst list directly, so no photon data need be
# copied::
#
#     rows = bursts[rng.integers(0, len(bursts), size=len(bursts))]
#     eng = tttrlib.HMM()
#     eng.set_bursts_from_tttr(data, rows, channels, min_photons=40)
#
# Duplicate rows are kept as separate bursts, which is exactly what a resample
# with replacement means. On 313 bursts and ~32k photons this runs at about 7 ms
# per replicate, so a 200-replicate interval costs a couple of seconds.
#
# **Which interval to use.** :func:`HMM.sample` is the calibrated reference and
# the right choice when priors are wanted or when the posterior shape matters.
# The bootstrap is the answer for the plain maximum-likelihood path, where there
# is no prior to speak of. The analytic width is for a quick relative comparison
# between parameters, never for a published error bar.

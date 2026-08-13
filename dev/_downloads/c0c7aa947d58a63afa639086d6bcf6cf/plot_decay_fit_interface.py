r"""
==============================
Fitting a decay: one interface
==============================

Every decay fit in tttrlib is reached the same way — you name the model, hand it
the measurement, and read the answer back. This example walks through that once,
end to end, and then shows the two things the interface gives you that a
per-model API could not: swapping models without changing your code, and fitting
thousands of decays in one call.

The four ingredients are always the same:

``DecayFit2``
    The model, built by name (``"fit23"``, ``"fit24"``, …). It is immutable and
    caches what it derives from the IRF, so build it once and reuse it.

``DecayFitProblem``
    The measurement: the data, the instrument response, the background.

``DecayFitConstraints``
    Which parameters are fitted, held, or tied together.

The outcome
    The fitted parameters, and a vector of results whose columns the registry
    names for you.
"""

# %%
import numpy as np
import matplotlib.pyplot as plt

import tttrlib

# %%
# What can I fit?
# ---------------
# The registry lists the models, so you never have to remember them. It also
# describes each one — parameters, defaults, units, ranges — which is how the
# helpers below know what your numbers mean.
print("available fits:", tttrlib.fit_names())

# %%
# Setting up
# ----------
# A fit needs an instrument description: the channel width, the excitation
# period, the polarisation corrections. These travel as one flat vector, but you
# never build that vector by hand — ``setup_vector`` fills every slot from the
# registry's documented defaults and applies only what you name.
#
# One value deserves attention. The **excitation period bounds the lifetime
# search**: the decay is convolved over one period, so a lifetime longer than
# the period cannot be measured. Here the period is 32 ns, comfortably above the
# 2 ns lifetime we are about to simulate.
n_bins = 128          # micro-time channels per polarisation
dt = 0.032            # ns per channel
period = 32.0         # ns between excitation pulses
true_tau = 2.0        # ns, the lifetime we will try to recover

setup = tttrlib.setup_vector(
    "fit23",
    dt=dt,
    period=period,
    g_factor=1.0,
    soft_bifl_scatter_flag=False,
)
print("setup slots:", list(tttrlib.decay_fit_setup_names("fit23")))
print("setup values:", np.round(setup, 4))

# %%
# A narrow, clean instrument response, given twice: parallel then perpendicular.
# This "Jordi" layout — one channel after the other in a single array — is what
# the polarisation-resolved models expect.
channel = np.arange(n_bins)
irf_half = np.exp(-0.5 * ((channel - 10) / 1.2) ** 2)
irf_half /= irf_half.sum()
irf = np.concatenate([irf_half, irf_half])
background = np.zeros(2 * n_bins)

# %%
# Building the fit and the problem
# --------------------------------
# The model is built once from the setup and the IRF. The problem holds the
# measurement; ``n_channels=2`` says this is polarisation-resolved.
fit = tttrlib.DecayFit2("fit23", setup, irf.tolist())

problem = tttrlib.DecayFitProblem(2, n_bins, dt)
problem.irf = tttrlib.VectorDouble(irf.tolist())
problem.background = tttrlib.VectorDouble(background.tolist())

# %%
# Simulating a measurement
# ------------------------
# ``model_curve`` gives the decay the model predicts for a set of parameters,
# with no reference to any data — which is exactly what simulating needs. (Its
# sibling ``evaluate`` scores parameters *against* data and scales the curve to
# the observed counts, so it cannot be used before the data exist.)
#
# We take that curve, scale it to a realistic photon budget, and draw Poisson
# counts from it.
curve = np.asarray(fit.model_curve([true_tau, 0.0, 0.0, 1.0], problem))
expected = curve / curve.sum() * 30000
data = np.random.default_rng(1).poisson(expected).astype(float)
problem.data = tttrlib.VectorDouble(data.tolist())

# %%
# Fitting
# -------
# ``DecayFitConstraints`` says what the optimiser may move. The codes are:
# ``0`` free, ``-1`` held, and any positive number ties every slot carrying it
# to one shared value.
#
# Here only the lifetime is free. ``default_links`` would give you the same
# thing from the registry, because scatter and anisotropy are not identifiable
# from a short decay and are marked as held by default.
constraints = tttrlib.DecayFitConstraints(tttrlib.VectorInt32([0, -1, -1, -1]))
print("registry defaults:", list(tttrlib.default_links("fit23")))

start = [0.5, 0.0, 0.0, 1.0]          # a deliberately poor starting lifetime
outcome = fit.fit(start, constraints, problem)

print(f"true lifetime      {true_tau:.3f} ns")
print(f"recovered lifetime {outcome.parameters[0]:.3f} ns")

# %%
# Reading the answer
# ------------------
# The results are a flat vector, and the registry names its columns — so you ask
# for a value by name instead of counting positions.
named = tttrlib.results_as_dict("fit23", list(outcome.results))
for key, value in named.items():
    print(f"{key:>16}: {value}")

# %%
# ``twoIstar`` is the fit quality: it compares the model against a hypothetical
# perfectly-fitting one, so a good fit to counting data sits near 1. A large
# value means the model cannot describe the decay.
#
# .. note::
#    ``converged`` reports that the optimiser met its tolerance — not that the
#    answer is meaningful. A lifetime longer than the excitation period converges
#    onto that bound and still reports ``True``. If a fitted lifetime sits
#    exactly on the period, lengthen the period rather than believe the number.

# %%
# Let us look at the fit.
model = np.asarray(problem.model)
time = channel * dt

fig, (ax_p, ax_s) = plt.subplots(2, 1, sharex=True, figsize=(7, 6))
for ax, offset, label in ((ax_p, 0, "parallel"), (ax_s, n_bins, "perpendicular")):
    ax.semilogy(time, data[offset:offset + n_bins], ".", ms=3,
                color="0.6", label="data")
    ax.semilogy(time, model[offset:offset + n_bins], "-", lw=1.5,
                color="C3", label="fit")
    ax.set_ylabel(f"counts\n({label})")
    ax.set_ylim(0.5, None)
    ax.legend(frameon=False)
ax_s.set_xlabel("time (ns)")
ax_p.set_title(
    f"fit23: recovered $\\tau$ = {outcome.parameters[0]:.2f} ns "
    f"(true {true_tau:.2f} ns), 2I* = {outcome.objective:.2f}")
fig.tight_layout()
plt.show()

# %%
# Swapping the model
# ------------------
# Because every fit is reached the same way, changing the model is a change of
# *name*. Nothing else about the call moves — which is the whole point of the
# interface, and what previously required a different class, a different
# parameter layout and a different result convention per estimator.
#
# ``fit24`` is bi-exponential, so it has five parameters rather than four. The
# registry says which, so you can build the call without reading the source.
for name in ("fit23", "fit24"):
    print(f"{name}: parameters = {list(tttrlib.decay_fit_parameter_names(name))}")

# fit24 needs a background pattern with some weight in it: its likelihood
# includes a background term, and against an all-zero background that term is
# undefined, so the fit returns NaN without moving off the start values. A small
# flat background is the usual choice.
problem24 = tttrlib.DecayFitProblem(2, n_bins, dt)
problem24.irf = tttrlib.VectorDouble(irf.tolist())
problem24.background = tttrlib.VectorDouble(
    (np.ones(2 * n_bins) / (2 * n_bins)).tolist())
problem24.data = tttrlib.VectorDouble(data.tolist())

fit24 = tttrlib.DecayFit2(
    "fit24", tttrlib.setup_vector("fit24", dt=dt, period=period), irf.tolist())
out24 = fit24.fit(
    [1.0, 0.0, 3.0, 0.5, 0.0],
    tttrlib.DecayFitConstraints(tttrlib.VectorInt32([0, -1, 0, 0, -1])),
    problem24)
print(f"fit24 lifetimes: {out24.parameters[0]:.2f} ns and "
      f"{out24.parameters[2]:.2f} ns, 2I* = {out24.objective:.2f}")

# %%
# .. note::
#    A bi-exponential model fitted to single-exponential data is not
#    identifiable — the two lifetimes are free to trade against each other, and
#    one of them will wander. The point here is that the *call* is the same, not
#    that the answer is meaningful. Choose the model that matches the
#    photophysics, and treat a second lifetime that moves freely as a sign the
#    data do not support it.

# %%
# Fitting many decays
# -------------------
# Batch fitting is part of the interface rather than something each model has to
# provide, so it works for every fit. Rows are spread across worker threads with
# the GIL released; the model is shared because it is immutable, and only the
# per-row working state is copied.
#
# This is the path a burst analysis or a FLIM image takes: thousands of decays,
# one call.
n_rows = 200
rng = np.random.default_rng(7)
matrix = rng.poisson(np.tile(expected, (n_rows, 1))).astype(float)

batch = fit.fit_many(
    problem, matrix.ravel().tolist(), n_rows, 2 * n_bins, start, constraints)

taus = np.asarray(batch.parameters).reshape(n_rows, 4)[:, 0]
print(f"{n_rows} decays fitted: tau = {taus.mean():.3f} +/- {taus.std():.3f} ns")

# %%
# The spread across repeats is the statistical uncertainty of the estimator at
# this photon count — which is what you should quote, rather than the optimiser's
# own error estimate from a single fit.
fig, ax = plt.subplots(figsize=(7, 4))
ax.hist(taus, bins=30, color="C0", alpha=0.8)
ax.axvline(true_tau, color="C3", lw=2, label=f"true $\\tau$ = {true_tau} ns")
ax.axvline(taus.mean(), color="k", ls="--", lw=1.5,
           label=f"mean = {taus.mean():.3f} ns")
ax.set_xlabel("recovered lifetime (ns)")
ax.set_ylabel(f"decays (of {n_rows})")
ax.set_title("Estimator spread at 30 000 photons")
ax.legend(frameon=False)
fig.tight_layout()
plt.show()

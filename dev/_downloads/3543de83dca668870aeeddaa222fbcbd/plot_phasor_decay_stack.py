"""
==================================
Phasors of a stack of decays
==================================

The phasor of a decay is its first Fourier coefficient at the excitation
frequency, normalised by the photon count:

.. math::

    g = \\frac{\\sum_i n_i \\cos(2\\pi f\\, t_i)}{\\sum_i n_i}, \\qquad
    s = \\frac{\\sum_i n_i \\sin(2\\pi f\\, t_i)}{\\sum_i n_i}

Mono-exponential decays fall on the *universal semicircle*
:math:`(g - 1/2)^2 + s^2 = 1/4`, mixtures inside it, and the position along
the circle reads the lifetime without any fit (Digman et al. 2008). A FLIM
image is 10\\ :sup:`4`\\ --10\\ :sup:`6` decays, so the operation that matters is
*the phasor of a stack*, not of one decay.

``tttrlib.DecayPhasor.compute_phasor_bincounts_batch`` does exactly that in
one call -- the cos/sin table computed once, rows in parallel -- and returns an
``(n, 2)`` array. It is digit-for-digit the per-decay ``phasor_of_bincounts``
and validated against ``phasorpy.phasor_from_signal`` (identical, 3.4x faster
on 10\\ :sup:`5` decays; see PERF.md). This example simulates the stack, so every
lifetime is known.
"""

# %%
import numpy as np
import matplotlib.pyplot as plt

import tttrlib

rng = np.random.default_rng(7)

# %%
# A simulated FLIM stack
# ----------------------
# 20 000 "pixels" of 256 micro-time bins. Two populations of mono-exponential
# decays (1.2 ns and 3.8 ns) and a third population that mixes them 50:50, all
# through the same Gaussian IRF and with a small background. The excitation
# period is 12.8 ns, so the phasor frequency is ``1 / n_bins`` per bin.
n_bins, dt = 256, 0.05                       # ns
t = np.arange(n_bins) * dt
period = n_bins * dt
freq = 1.0 / n_bins                          # cycles per bin = 1 / period in ns^-1 * dt

irf = np.exp(-0.5 * ((t - 1.0) / 0.12) ** 2)
irf /= irf.sum()

def decay(tau, photons):
    m = np.convolve(irf, np.exp(-t / tau))[:n_bins]
    return m / m.sum() * photons

n_each = 6000
photons = 800
stack = np.concatenate([
    np.tile(decay(1.2, photons), (n_each, 1)),
    np.tile(decay(3.8, photons), (n_each, 1)),
    np.tile(0.5 * decay(1.2, photons) + 0.5 * decay(3.8, photons), (n_each, 1)),
])
labels = np.repeat([0, 1, 2], n_each)
counts = rng.poisson(stack + 0.3).astype(np.int32)      # background 0.3 / bin
print("stack:", counts.shape, "decays x bins")

# %%
# The phasors, one call for the whole stack
# -----------------------------------------
# The IRF is not yet removed (``g_irf = 1, s_irf = 0`` is the identity), so
# these are the *raw* phasors: rotated and shrunk by the IRF's own phasor.
raw = np.asarray(tttrlib.DecayPhasor.compute_phasor_bincounts_batch(
    counts, freq, 1, 1.0, 0.0))
print("raw phasors:", raw.shape)

# the same numbers as the per-decay method, digit for digit
one = np.array([tttrlib.DecayPhasor.phasor_of_bincounts(counts[i], freq, 1, 1.0, 0.0)
                for i in range(0, len(counts), 997)])
print("max |batch - per-decay| =", np.abs(raw[::997] - one).max())

# %%
# IRF calibration
# ---------------
# The phasor of the measured signal is the product of the sample's phasor and
# the IRF's; dividing by the IRF phasor (``DecayPhasor.g`` / ``s`` do that
# complex division) puts the mono-exponentials back on the semicircle. Here the
# IRF phasor comes from the simulated IRF; in an experiment it is the phasor of a
# scatterer or of a dye of known lifetime.
irf_counts = rng.poisson(irf * 2e5).astype(np.int32)[None, :]
g_irf, s_irf = np.asarray(tttrlib.DecayPhasor.compute_phasor_bincounts_batch(
    irf_counts, freq, 1, 1.0, 0.0))[0]
cal = np.asarray(tttrlib.DecayPhasor.compute_phasor_bincounts_batch(
    counts, freq, 1, g_irf, s_irf))
print(f"IRF phasor: g = {g_irf:.4f}, s = {s_irf:.4f}")

# %%
# The phasor plot
# ---------------
# Mono-exponentials sit on the circle at the angle their lifetime dictates
# (``omega tau = s / g``), the 50:50 mixture on the chord between them -- the
# geometry that lets a phasor plot separate species and read fractions without
# a fit. The expected positions are drawn from the true lifetimes.
omega = 2 * np.pi / period                                # rad / ns

def on_circle(tau):
    m = 1.0 / np.sqrt(1 + (omega * tau) ** 2)
    phi = np.arctan(omega * tau)
    return m * np.cos(phi), m * np.sin(phi)

theta = np.linspace(0, np.pi, 200)
fig, axes = plt.subplots(1, 2, figsize=(11, 4.6))
for ax, gs, title in ((axes[0], raw, "raw (with the IRF)"), (axes[1], cal, "IRF-calibrated")):
    ax.plot(0.5 + 0.5 * np.cos(theta), 0.5 * np.sin(theta), "k-", lw=1)
    for k, (name, colour) in enumerate((("1.2 ns", "C0"), ("3.8 ns", "C1"), ("50:50 mixture", "C2"))):
        sel = labels == k
        ax.scatter(gs[sel, 0], gs[sel, 1], s=2, alpha=0.3, color=colour, label=name)
    ax.set_xlabel("g")
    ax.set_ylabel("s")
    ax.set_title(title)
    ax.set_aspect("equal")
    ax.set_xlim(-0.05, 1.05)
    ax.set_ylim(-0.05, 0.6)
for tau, colour in ((1.2, "C0"), (3.8, "C1")):
    g0, s0 = on_circle(tau)
    axes[1].plot(g0, s0, "o", mfc="none", mec="k", ms=10)
axes[1].plot([on_circle(1.2)[0], on_circle(3.8)[0]], [on_circle(1.2)[1], on_circle(3.8)[1]], "k:", lw=1)
axes[0].legend(markerscale=6)
plt.tight_layout()
plt.show()

# %%
# Reading lifetimes off the plot
# ------------------------------
# For a mono-exponential population the phase lifetime ``tau_phi = s / (omega g)``
# recovers the input; for the mixture it gives an apparent value between the
# two, which is the sign that the pixel is not mono-exponential (its modulation
# lifetime would disagree).
for k, name in enumerate(("1.2 ns", "3.8 ns", "50:50 mixture")):
    sel = labels == k
    g, s = cal[sel].mean(axis=0)
    print(f"{name:14s}: tau_phi = {s / (omega * g):.2f} ns")

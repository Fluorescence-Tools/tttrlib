"""
========================================
Richardson-Lucy deconvolution of an image
========================================

A microscope records the sample convolved with its point spread function
(PSF), plus photon noise. Deconvolution tries to invert that. Two estimators
are in ``tttrlib``:

* ``richardson_lucy_2d`` -- the Poisson maximum-likelihood fixed point
  (Richardson 1972, Lucy 1974): the estimate is reblurred, the measurement is
  divided by the reblurred image, and the ratio is pushed back through the
  PSF. It stays non-negative and conserves flux by construction, which is what
  a photon count owes reality, and it belongs in a photon-counting library
  because *its* noise model is the one counting actually obeys. There is no
  regularisation parameter: **the iteration count is the regularisation**.
* ``wiener_deconvolve_2d`` -- the linear Gaussian-noise answer, one transform
  pair and a single ``balance`` knob; instant, but it can go negative and it
  amplifies whatever sits at the frequencies the PSF suppresses.

Both are validated against scikit-image (Richardson-Lucy identical to 1e-15,
1.8x faster; see the validation register and PERF.md). The list-mode variant
``richardson_lucy_events_2d`` deconvolves *photon coordinates* without ever
forming a pixel grid first -- the form every algorithm in this library must
have -- and is shown at the end.
"""

# %%
import numpy as np
import matplotlib.pyplot as plt
from scipy.signal import fftconvolve

import tttrlib

rng = np.random.default_rng(5)

# %%
# A known object, blurred and counted
# -----------------------------------
# A few point-like emitters and one filament, blurred by a Gaussian PSF of
# 2.2 px sigma (a 15 x 15 kernel), Poisson noise at a realistic photon budget,
# and a flat background of 3 counts per pixel.
n = 128
gy, gx = np.mgrid[0:n, 0:n]
truth = np.zeros((n, n))
for cy, cx, b in ((30, 30, 400), (30, 44, 400), (36, 37, 250), (85, 30, 600), (90, 95, 500)):
    truth += b * np.exp(-((gx - cx) ** 2 + (gy - cy) ** 2) / (2 * 0.7 ** 2))
for s in np.linspace(0, 1, 60):                       # a curved filament
    cy, cx = 60 + 40 * s, 40 + 70 * s + 15 * np.sin(6 * s)
    truth += 60 * np.exp(-((gx - cx) ** 2 + (gy - cy) ** 2) / (2 * 0.9 ** 2))

k = np.arange(15) - 7
psf = np.exp(-(k[:, None] ** 2 + k[None, :] ** 2) / (2 * 2.2 ** 2))
psf /= psf.sum()
blurred = fftconvolve(truth, psf, mode="same") + 3.0
image = rng.poisson(np.clip(blurred, 0, None)).astype(np.float64)
image = np.ascontiguousarray(image)
psf = np.ascontiguousarray(psf)

# %%
# Richardson-Lucy at increasing iteration counts
# ----------------------------------------------
# ``clip=False`` (these are counts, not an image scaled to [-1, 1]),
# ``filter_epsilon=0`` (no floor on the ratio), no acceleration -- the plain
# fixed point, so the iteration count means what people expect.
counts = (5, 20, 60, 300)
restored = {it: np.asarray(tttrlib.richardson_lucy_2d(image, psf, it, False, 0.0, False))
            for it in counts}

fig, axes = plt.subplots(1, 2 + len(counts), figsize=(16, 3.4))
axes[0].imshow(truth, cmap="magma"); axes[0].set_title("truth")
axes[1].imshow(image, cmap="magma"); axes[1].set_title("blurred + Poisson")
for ax, it in zip(axes[2:], counts):
    ax.imshow(restored[it], cmap="magma")
    ax.set_title(f"RL, {it} iterations")
for ax in axes:
    ax.set_axis_off()
plt.tight_layout()
plt.show()

# %%
# The iteration count is the regularisation
# -----------------------------------------
# Every iteration raises the Poisson likelihood of the data -- that is what the
# fixed point does -- but the *error against the truth* has a minimum: after
# it, the estimate keeps fitting the noise, breaking the filament into beads
# and sharpening every fluctuation into a plausible-looking spot. Flux is
# conserved throughout (RL redistributes photons; it never creates them).
its = np.unique(np.round(np.geomspace(1, 400, 25)).astype(int))
err = []
for it in its:
    r = np.asarray(tttrlib.richardson_lucy_2d(image, psf, int(it), False, 0.0, False))
    err.append(np.sqrt(np.mean((r - 3.0 - truth) ** 2)))
best = its[int(np.argmin(err))]
print(f"lowest RMS error against the truth at {best} iterations")
print("flux: image {:.0f}, restored (300 it) {:.0f}".format(image.sum(), restored[300].sum()))

fig, ax = plt.subplots(figsize=(6, 3.8))
ax.semilogx(its, err, "o-")
ax.axvline(best, color="k", ls=":")
ax.set_xlabel("Richardson-Lucy iterations")
ax.set_ylabel("RMS error vs. truth")
ax.set_title("more iterations is not better")
plt.show()

# %%
# Wiener, for comparison
# ----------------------
# ``balance`` weighs data fidelity against noise amplification: too small and
# the result rings and goes negative, too large and it is hardly sharper than
# the input. It is a linear filter -- one FFT pair -- and worth having as the
# quick look, but it neither knows about Poisson noise nor keeps the result
# non-negative.
fig, axes = plt.subplots(1, 3, figsize=(10, 3.4))
for ax, bal in zip(axes, (1e-3, 1e-1, 3.0)):
    w = np.asarray(tttrlib.wiener_deconvolve_2d(image, psf, bal))
    ax.imshow(w, cmap="magma")
    ax.set_title(f"Wiener, balance {bal:g}\nmin {w.min():.0f}")
    ax.set_axis_off()
plt.tight_layout()
plt.show()

# %%
# The photon form: deconvolving coordinates
# -----------------------------------------
# A photon has a position, and a pixel is something a reader imposed. The
# list-mode entry point takes the coordinates and a PSF sampled finer than the
# reconstruction grid (here 8 samples per pixel; interpolating a PSF sampled
# once per pixel would broaden the result by up to 0.25 px^2 depending on each
# photon's sub-pixel offset). Photons are drawn from the blurred truth: two
# emitters 3 px apart that a pixel image barely separates.
def fine_gaussian(n_px, sigma, oversampling):
    h = int(round(n_px * oversampling / 2.0))
    g = (np.arange(2 * h + 1) - h) / oversampling
    kern = np.exp(-0.5 * (g[:, None] ** 2 + g[None, :] ** 2) / sigma ** 2)
    return np.ascontiguousarray(kern / kern.sum())

emitters = np.array([[16.0, 14.5], [16.0, 17.5]])
n_ph = 6000
which = rng.integers(0, 2, n_ph)
coords = emitters[which] + rng.normal(0, 1.4, (n_ph, 2))      # PSF sigma 1.4 px
coords = np.ascontiguousarray(coords)
psf_fine = fine_gaussian(9, 1.4, 8)
grid = 32
restored_ev = np.asarray(tttrlib.richardson_lucy_events_2d(coords, psf_fine, grid, grid, 40, 8))
binned, _, _ = np.histogram2d(coords[:, 0], coords[:, 1], bins=grid, range=[[0, grid], [0, grid]])

fig, axes = plt.subplots(1, 2, figsize=(7, 3.4))
axes[0].imshow(binned, cmap="magma"); axes[0].set_title("photons, binned")
axes[1].imshow(restored_ev, cmap="magma"); axes[1].set_title("list-mode RL, 40 it")
for ax in axes:
    ax.set_axis_off()
plt.tight_layout()
plt.show()
print(f"list-mode RL: {n_ph} photons in, {restored_ev.sum():.1f} out (flux conserved)")

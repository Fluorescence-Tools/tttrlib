"""
=========================================================
ISM, SOFISM and eSRRF side by side on a resolution target
=========================================================

The super-resolution families in :class:`tttrlib.CLSMSuperRes` sharpen an image
for different reasons, and they behave differently when pushed:

* **ISM** exploits the *detector*. A point object is imaged by every element of
  a SPAD array through its own displaced PSF; moving each element's image back
  by half its offset -- adaptive pixel reassignment, APR -- collects all of them
  onto the object. It is a linear, photon-conserving operation, so the gain is
  bounded (a factor near :math:`\\sqrt{2}` for a closed pinhole) but nothing is
  invented.
* **eSRRF** exploits the *image*. The radial gradient convergence (RGC) field
  peaks where local intensity gradients converge, which is where an emitter is,
  and rendering that field on a finer grid sharpens structure a great deal. It
  is a nonlinear transform, so it can also *create* structure -- see the
  ``eSRRF`` trace below, which recovers modulation at separations where the line
  pairs have already merged.

* **SOFISM** exploits the *emitters*. Replacing the mean signal by a
  second-order cumulant of the temporal fluctuations gives an effective PSF
  equal to the product of two elements' PSFs, and the pair is reassigned as one
  virtual detector midway between them. Like ISM it is a physical mechanism
  rather than a rendering choice, but unlike ISM it needs the fluorophores to
  blink independently -- on a static sample it returns nothing at all.

They compose. eSRRF applied to the reassigned image starts from an input that is
genuinely sharper rather than merely contrastier, and SOFISM reaches the same
smallest resolved separation on its own, without a nonlinear rendering step to
defend. Note that eSRRF *alone* recovers modulation at separations where the
line pairs have already merged -- structure it invented -- which is why a pair
counts as resolved here only when every wider pair is resolved too.

The target is a set of line pairs at shrinking separations, imaged through a
simulated 5x5 SPAD array. What each method can resolve is read off the
*modulation depth* of each pair -- how deep the dip between the two lines is --
against the Rayleigh criterion of 26%.
"""

import sys
from pathlib import Path
import os
# `__file__` does not exist inside a notebook; fall back to the working directory
_HERE_FILE = Path(globals().get("__file__", os.path.join(os.getcwd(), "example.py")))

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.gridspec import GridSpec
from scipy.ndimage import gaussian_filter
from scipy.signal import fftconvolve

import tttrlib

# The array-detector PSF model is a simulation helper, not part of the library.
sim_dir = _HERE_FILE.resolve().parent.parent / "simulation"
if str(sim_dir) not in sys.path:
    sys.path.insert(0, str(sim_dir))

from simulate import generate_ism_psf

PIXEL_SIZE_NM = 15.0
NY, NX = 64, 256
SEPARATIONS_NM = [400, 330, 270, 220, 180, 145, 115]
LINE_WIDTH_NM = 18.0
MAGNIFICATION = 4
RAYLEIGH = 0.26

# %%
# The target
# ----------
# Pairs of vertical lines, each pair closer together than the last.
x_nm = np.arange(NX) * PIXEL_SIZE_NM
pairs, cursor = [], 60.0
for sep in SEPARATIONS_NM:
    pairs.append((cursor, cursor + sep, sep))
    cursor += sep + 220.0
pairs = [p for p in pairs if p[1] < NX * PIXEL_SIZE_NM - 60]

sigma_nm = LINE_WIDTH_NM / 2.3548
profile_gt = np.zeros(NX)
for left, right, _ in pairs:
    profile_gt += np.exp(-((x_nm - left) ** 2) / (2 * sigma_nm ** 2))
    profile_gt += np.exp(-((x_nm - right) ** 2) / (2 * sigma_nm ** 2))
ground_truth = np.tile(profile_gt, (NY, 1))
ground_truth[:6] = ground_truth[-6:] = 0.0

# %%
# The microscope
# --------------
psf = generate_ism_psf(
    na=1.4, wavelength_exc=488.0, wavelength_det=520.0,
    n_det=5, pitch_au=0.5, nx=48, ny=48, pixel_size_nm=PIXEL_SIZE_NM,
)
channel_psfs = psf["channel_psfs"]
n_det = channel_psfs.shape[0]
NY, NX = ground_truth.shape

# %%
# The acquisition
# ---------------
# The target is built from *independently blinking* emitters and recorded as a
# time series at every scan position. That costs nothing for the other methods,
# which simply use the time average, but it is what SOFISM needs: its contrast
# comes from the fluctuation cross-correlation between detector elements, and a
# static sample would give it nothing to work with. All five reconstructions
# below therefore see exactly the same photons.
N_TIME = 20
P_ON = 0.3
rng = np.random.default_rng(11)

emitters = ground_truth > 0.05 * ground_truth.max()
ys, xs = np.nonzero(emitters)
amplitude = ground_truth[ys, xs]
# scaled so the time-averaged image carries the same flux as a static one would
photon_scale = 20.0 / P_ON

detector_cube = np.zeros((N_TIME, n_det, NY, NX))
for t in range(N_TIME):
    on = rng.random(len(amplitude)) < P_ON
    frame = np.zeros((NY, NX))
    frame[ys[on], xs[on]] = amplitude[on]
    for k in range(n_det):
        detector_cube[t, k] = fftconvolve(frame, channel_psfs[k], mode="same")
detector_cube = rng.poisson(
    np.clip(detector_cube * photon_scale, 0, None)
).astype(np.float64)
print(f"{n_det} detector elements, {N_TIME} time bins, "
      f"{detector_cube.sum():.3g} photons")

# %%
# The reconstructions
# -------------------
time_averaged = detector_cube.mean(axis=0)
confocal = time_averaged.sum(axis=0)
apr_ism = tttrlib.CLSMSuperRes.apr_reconstruction(time_averaged, usf=10)[0]
sofism = tttrlib.CLSMSuperRes.sofism_reconstruction(detector_cube, lag=0, usf=10)


def esrrf(img):
    # A light smoothing first: the gradient is a two-point difference, which
    # otherwise follows shot noise straight into the RGC field.
    return tttrlib.CLSMSuperRes.rgc_map(
        gaussian_filter(img, sigma=0.8), magnification=MAGNIFICATION,
        fwhm=2.5, sensitivity=1, intensity_weighting=True,
    )


methods = {
    "Confocal (channel sum)": (confocal, 1),
    "APR-ISM": (apr_ism, 1),
    "SOFISM": (np.clip(sofism, 0, None), 1),
    "eSRRF": (esrrf(confocal), MAGNIFICATION),
    "eSRRF on APR-ISM": (esrrf(apr_ism), MAGNIFICATION),
}
print(f"APR-ISM photons: {confocal.sum():.6g} -> {apr_ism.sum():.6g}")


# %%
# Modulation depth per line pair
# ------------------------------
def line_profile(img, upsample):
    """Average down the lines, then normalise to [0, 1]."""
    band = img[img.shape[0] // 4: 3 * img.shape[0] // 4]
    p = band.mean(axis=0)
    return (p - p.min()) / (p.max() - p.min() + 1e-12)


def modulation_depths(profile, upsample):
    """(I1 + I2 - 2*I_dip) / (I1 + I2) for every pair, the Rayleigh measure."""
    grid = np.arange(len(profile)) * PIXEL_SIZE_NM / upsample
    out = []
    for left, right, sep in pairs:
        i1 = int(np.argmin(np.abs(grid - left)))
        i2 = int(np.argmin(np.abs(grid - right)))
        if i2 - i1 < 2:
            out.append(0.0)
            continue
        peak1 = profile[max(0, i1 - 2):i1 + 3].max()
        peak2 = profile[i2 - 2:i2 + 3].max()
        dip = profile[i1:i2 + 1].min()
        out.append(max(0.0, (peak1 + peak2 - 2 * dip) / (peak1 + peak2 + 1e-12)))
    return np.array(out)


profiles, depths = {}, {}
for name, (img, up) in methods.items():
    profiles[name] = line_profile(img, up)
    depths[name] = modulation_depths(profiles[name], up)

seps = np.array([p[2] for p in pairs])
print("\nmodulation depth per separation")
print("             " + "".join(f"{s:7d}" for s in seps))
for name in methods:
    print(f"{name:24s}" + "".join(f"{d:7.2f}" for d in depths[name]))

# %%
# A pair only counts as resolved if every *wider* pair is resolved too. Without
# that rule a method whose modulation is not monotonic -- eSRRF, here -- gets
# credit at 115 nm for structure it invented after losing the pair at 220 nm.
smallest = {}
for name, depth in depths.items():
    limit = np.nan
    for sep, value in zip(seps, depth):   # seps run wide to narrow
        if value < RAYLEIGH:
            break
        limit = sep
    smallest[name] = limit

print("\nsmallest separation resolved (all wider pairs resolved too)")
for name, value in smallest.items():
    print(f"{name:24s} {value:.0f} nm")

# %%
# The comparison
# --------------
X_MAX_UM = (pairs[-1][1] + 200.0) / 1000.0

# One colour per method, used in every panel
COLOURS = {
    "Confocal (channel sum)": "#4c72b0",
    "APR-ISM": "#dd8452",
    "SOFISM": "#8172b3",
    "eSRRF": "#55a868",
    "eSRRF on APR-ISM": "#c44e52",
}

fig = plt.figure(figsize=(15, 11))
gs = GridSpec(3, 2, figure=fig, height_ratios=[0.95, 1.2, 1.15],
              hspace=0.55, wspace=0.2)

# --- the images, one strip per method, on a shared physical axis
ax_img = fig.add_subplot(gs[0, :])
strips, labels = [], []
gt_strip = ground_truth[NY // 2 - 4:NY // 2 + 4].mean(axis=0)
strips.append(np.tile(gt_strip / gt_strip.max(), (7, 1)))
labels.append("Ground truth")
for name, (img, up) in methods.items():
    band = img[img.shape[0] // 4: 3 * img.shape[0] // 4].mean(axis=0)
    band = band / (band.max() + 1e-12)
    if up != 1:  # bring the magnified rendering back to the common axis
        band = np.interp(np.linspace(0, 1, NX), np.linspace(0, 1, len(band)), band)
    strips.append(np.tile(band, (7, 1)))
    labels.append(name)
ax_img.imshow(np.vstack(strips), cmap="magma", aspect="auto",
              extent=[0, NX * PIXEL_SIZE_NM / 1000, len(strips), 0])
ax_img.set_yticks(np.arange(len(labels)) + 0.5)
ax_img.set_yticklabels(labels, fontsize=9)
ax_img.set_xlabel("position (µm)")
ax_img.set_xlim(0, X_MAX_UM)
ax_img.set_title("Line pairs at shrinking separation, as each method renders them",
                 fontsize=11, pad=22)
for left, right, sep in pairs:
    ax_img.annotate(f"{sep}", xy=((left + right) / 2000, 0),
                    xytext=(0, 6), textcoords="offset points",
                    ha="center", fontsize=8, color="0.35", annotation_clip=False)
ax_img.annotate("separation (nm):", xy=(0, 0), xytext=(-8, 6),
                textcoords="offset points", ha="right", fontsize=8, color="0.35",
                annotation_clip=False)

# --- the profiles, stacked
ax_prof = fig.add_subplot(gs[1, :])
offset = 0.0
for name in reversed(list(methods)):
    up = methods[name][1]
    p = profiles[name]
    grid = np.arange(len(p)) * PIXEL_SIZE_NM / up / 1000
    ax_prof.plot(grid, p + offset, lw=1.3, color=COLOURS[name])
    ax_prof.axhline(offset, color="0.9", lw=0.6, zorder=0)
    ax_prof.text(X_MAX_UM * 0.995, offset + 0.95, name, ha="right", va="top",
                 fontsize=8.5, color=COLOURS[name])
    offset += 1.15
for left, right, _ in pairs:
    for pos in (left, right):
        ax_prof.axvline(pos / 1000, color="0.85", lw=0.7, ls=":", zorder=0)
ax_prof.set_xlabel("position (µm)")
ax_prof.set_ylabel("normalised intensity (offset)")
ax_prof.set_yticks([])
ax_prof.set_xlim(0, X_MAX_UM)
ax_prof.set_title("Profiles across the target; dotted lines mark the true line positions",
                  fontsize=11)

# --- modulation depth vs separation
ax_mod = fig.add_subplot(gs[2, 0])
markers = ["o", "s", "v", "^", "D"]
for (name, depth), marker in zip(depths.items(), markers):
    ax_mod.plot(seps, depth, marker=marker, lw=1.6, ms=5, label=name,
                color=COLOURS[name])
ax_mod.axhline(RAYLEIGH, color="0.4", ls="--", lw=1)
ax_mod.annotate("Rayleigh, 26%", xy=(0.72, RAYLEIGH + 0.05),
                xycoords=("axes fraction", "data"), fontsize=8, color="0.4")
ax_mod.set_xlabel("line-pair separation (nm)")
ax_mod.set_ylabel("modulation depth")
ax_mod.invert_xaxis()
ax_mod.set_ylim(0, 1.12)
ax_mod.legend(fontsize=8, loc="lower left", framealpha=0.95)
ax_mod.set_title("A pair is resolved while it stays above 26%.\n"
                 "Note that eSRRF alone rises again after losing the 220 nm pair.",
                 fontsize=9.5)

# --- the smallest separation each method still resolves
ax_res = fig.add_subplot(gs[2, 1])
names = list(smallest)
values = [smallest[n] for n in names]
baseline = smallest["Confocal (channel sum)"]
bars = ax_res.barh(range(len(names)), values,
                   color=[COLOURS[n] for n in names])
ax_res.axvline(baseline, color="0.4", ls="--", lw=1)
ax_res.set_yticks(range(len(names)))
ax_res.set_yticklabels(names, fontsize=9)
ax_res.invert_yaxis()
ax_res.set_xlabel("smallest separation still resolved (nm)")
ax_res.set_xlim(0, max(v for v in values if np.isfinite(v)) * 1.55)
for bar, value in zip(bars, values):
    ax_res.text(value + 8, bar.get_y() + bar.get_height() / 2,
                f"{value:.0f} nm   ({baseline / value:.2f}×)", va="center", fontsize=8.5)
ax_res.set_title("Against the confocal baseline (dashed)", fontsize=10)

fig.suptitle(
    "ISM, SOFISM and eSRRF on one simulated array-detector acquisition\n"
    "of a blinking resolution target",
    fontsize=13, y=0.965,
)
plt.show()

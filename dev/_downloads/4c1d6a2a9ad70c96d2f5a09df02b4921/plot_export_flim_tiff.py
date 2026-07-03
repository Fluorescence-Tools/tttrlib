"""
==========================================
Exporting FLIM images to TIFF (no ImageJ)
==========================================
``tttrlib`` can write the images it computes from a TTTR/FLIM measurement
straight to standard TIFF files, so you can archive them or open them in any
image viewer **without ImageJ or any other extra software**. Reading TIFFs back
into NumPy is just as direct.

The whole TIFF layer is backed by a *bundled* libtiff (statically linked), so it
adds no runtime dependency to ``tttrlib``.

Two calls do everything:

* ``tttrlib.imwrite(path, array)`` - write a 2-D image or a 3-D ``(frames,
  height, width)`` stack. The array's dtype selects the on-disk pixel type
  (``uint8/16/32``, ``int32``, ``float32/64``); ``compression`` may be
  ``"none"``, ``"lzw"`` (default), ``"packbits"`` or ``"deflate"``.
* ``tttrlib.imread(path)`` - read a TIFF back, auto-detecting the pixel type and
  returning a NumPy array (2-D for a single page, 3-D for a stack).
"""

#%%
import os
import tempfile
from pathlib import Path

import numpy as np
import pylab as plt

import tttrlib
from examples._example_data import get_data_path

#%%
# Build FLIM images from a TTTR measurement
# -----------------------------------------
# Read a confocal laser-scanning (CLSM) measurement and map the photon stream to
# pixels. From the filled image we derive two per-pixel images:
#
# * the **intensity** image - a ``(frames, lines, pixels)`` ``uint16`` photon
#   count stack, and
# * a **mean micro time** image - a floating-point map that (without an IRF)
#   already reflects the fluorescence lifetime contrast.
data = tttrlib.TTTR(str(get_data_path('imaging/pq/ht3/pq_ht3_clsm.ht3')), 'HT3')

clsm = tttrlib.CLSMImage(data, fill=True, channels=(0, 1))

# 3-D intensity stack (one 2-D image per scanned frame), native uint16
intensity_stack = clsm.intensity
# 2-D intensity image summed over all frames (uint32 so counts never overflow)
intensity_sum = intensity_stack.sum(axis=0).astype(np.uint32)

# 2-D mean-micro-time image (float32), frames stacked into one
mean_micro_time = clsm.get_mean_micro_time(
    tttr_data=data,
    minimum_number_of_photons=3,
    stack_frames=True
).astype(np.float32)[0]

print("intensity stack :", intensity_stack.shape, intensity_stack.dtype)
print("intensity sum   :", intensity_sum.shape, intensity_sum.dtype)
print("mean micro time :", mean_micro_time.shape, mean_micro_time.dtype)

#%%
# Write the images to TIFF
# ------------------------
# A 3-D array becomes a multi-page TIFF (one page per frame); a 2-D array becomes
# a single-page TIFF. The dtype is preserved on disk, so the float lifetime map
# stays floating point and the integer intensities stay integer.
out_dir = Path(tempfile.mkdtemp(prefix="tttrlib_flim_tiff_"))

tttrlib.imwrite(out_dir / "intensity_stack.tif", intensity_stack, compression="lzw")
tttrlib.imwrite(out_dir / "intensity_sum.tif", intensity_sum, compression="lzw")
tttrlib.imwrite(out_dir / "mean_micro_time.tif", mean_micro_time, compression="lzw")

for f in sorted(out_dir.glob("*.tif")):
    print(f"{f.name:24s} {os.path.getsize(f):>8d} bytes")

#%%
# Read the TIFFs back
# -------------------
# ``imread`` auto-detects the pixel type. A single-page file returns a 2-D array;
# the multi-page stack returns a 3-D array. The round-trip is exact.
stack_back = tttrlib.imread(out_dir / "intensity_stack.tif")
sum_back = tttrlib.imread(out_dir / "intensity_sum.tif")
tau_back = tttrlib.imread(out_dir / "mean_micro_time.tif")

print("stack round-trip exact :", np.array_equal(stack_back, intensity_stack))
print("sum   round-trip exact :", np.array_equal(sum_back, intensity_sum))
print("tau   round-trip exact :", np.array_equal(tau_back, mean_micro_time))
print("read-back dtypes       :", stack_back.dtype, sum_back.dtype, tau_back.dtype)

# tiff_info / tiff_dtype expose the on-disk geometry and pixel type
info = tttrlib.tiff_info(str(out_dir / "intensity_stack.tif"))
print("stack on disk          :",
      (info.n_frames, info.height, info.width),
      tttrlib.tiff_dtype(str(out_dir / "intensity_stack.tif")))

#%%
# Visualise what was exported
# ---------------------------
# These are exactly the arrays that were written to (and read back from) the TIFF
# files - the figure below is rendered from the data ``imread`` returned.
mask = sum_back < 3  # hide near-empty pixels in the lifetime map
masked_tau = np.ma.masked_where(mask, tau_back)

fig, ax = plt.subplots(1, 3, figsize=(12, 4))
ax[0].set_title("Intensity (sum of frames)\nintensity_sum.tif")
im0 = ax[0].imshow(sum_back, cmap="cividis")
fig.colorbar(im0, ax=ax[0], fraction=0.046)

ax[1].set_title(f"Intensity stack, frame 0\nintensity_stack.tif ({stack_back.shape[0]} pages)")
im1 = ax[1].imshow(stack_back[0], cmap="cividis")
fig.colorbar(im1, ax=ax[1], fraction=0.046)

ax[2].set_title("Mean micro time\nmean_micro_time.tif")
im2 = ax[2].imshow(masked_tau, cmap="Spectral")
fig.colorbar(im2, ax=ax[2], fraction=0.046)

for a in ax:
    a.set_xticks([])
    a.set_yticks([])
plt.tight_layout()
plt.show()

#%%
# That's it: the FLIM intensity and lifetime images are now standard TIFF files
# on disk (``%s``) that open in any viewer - no ImageJ required.
print("TIFF files written to:", out_dir)

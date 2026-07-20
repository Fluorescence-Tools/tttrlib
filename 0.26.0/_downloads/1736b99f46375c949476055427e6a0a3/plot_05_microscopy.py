"""
Basic Example 5: Zeiss LSM 980 CLSM walkthrough
-----------------------------------------------
This gallery example mirrors the CLSM image construction steps exercised in the
unit tests (see ``test/test_CLSM_02.py``) and demonstrates how to

1. Load the Zeiss LSM 980 confocal TTTR file used in the tests.
2. Build a ``tttrlib.CLSMImage`` object with matching parameters.
3. Visualise a frame projection and the corresponding mean micro-time map.

Notes
-----
- The demo data ships with the tttrlib repository. Set ``TTTRLIB_DATA`` as
  explained in ``test/test_settings.py`` so the example can locate the files.
- Figures are rendered with a non-interactive backend during documentation
  builds. The script never calls :func:`matplotlib.pyplot.show`.
"""

from __future__ import annotations

import numpy as np
import matplotlib.pyplot as plt
import tttrlib

from examples._example_data import get_data_path


# sphinx_gallery_thumbnail_number = 2

# %%
# Load the Zeiss LSM 980 demo data --------------------------------------------
# ``get_data_path`` resolves the same file locations used by the unit tests. When
# the data bundle is not installed an informative error is raised.
try:
    zeiss_path = get_data_path(
        "imaging/zeiss/lsm980_pq/Training_2021-03-04.sptw/Cell_GFP/Cell1_T_0_P_0_Idx_4.ptu"
    )
except FileNotFoundError as exc:
    raise SystemExit(
        "Demo CLSM data not found. Set TTTRLIB_DATA to the directory containing "
        "the reference data bundle (see test/test_settings.py)."
    ) from exc

print(f"Using Zeiss file: {zeiss_path}")
zeiss_tttr = tttrlib.TTTR(str(zeiss_path))
print(f"Total TTTR events: {len(zeiss_tttr):,}")

# The Zeiss dataset relies on the library defaults, so no extra kwargs are
# required beyond the detector selection used in the tests.
channels = [0]

clsm_image = tttrlib.CLSMImage(
    tttr_data=zeiss_tttr,
    channels=channels,
)

# ``fill`` populates the CLSM hierarchy (frames → lines → pixels) with photon
# indices for the requested detector channels.
clsm_image.fill(tttr_data=zeiss_tttr, channels=channels)

intensity = clsm_image.intensity
print(f"Intensity volume shape (frames, lines, pixels): {intensity.shape}")

# %%
# Frame projection (sum over time) --------------------------------------------
# Project the 3-D intensity stack along the frame axis to approximate a
# wide-field view of the acquisition. ``np.log1p`` improves the contrast when the
# dynamic range is high.
projection = intensity.sum(axis=0)
projection_log = np.log1p(projection)

fig_proj, ax_proj = plt.subplots(figsize=(4.8, 4.0))
im = ax_proj.imshow(projection_log, cmap="magma", origin="lower")
fig_proj.colorbar(im, ax=ax_proj, shrink=0.8, label="log(1 + counts)")
ax_proj.set_title("Zeiss LSM 980 CLSM — sum over frames (channel 0)")
ax_proj.set_xlabel("Pixel index")
ax_proj.set_ylabel("Line index")
fig_proj.tight_layout()

# %%
# Mean micro-time map for the first frame -------------------------------------
# The unit tests validate ``get_mean_micro_time`` by comparing it to reference
# arrays. Here we reuse the same helper to derive a pseudo-lifetime image. The
# result is provided in seconds; multiply by ``1e9`` to obtain nanoseconds. Only
# pixels with at least three photons contribute to the estimate.
mean_micro = clsm_image.get_mean_micro_time(
    tttr_data=zeiss_tttr,
    minimum_number_of_photons=3,
)

if mean_micro.size == 0:
    raise SystemExit("Mean micro-time image is empty; check that the data file contains CLSM frames.")

frame0_micro = mean_micro[0]
frame0_micro = np.clip(frame0_micro, 0.0, None)
frame0_micro_ns = np.where(frame0_micro > 0.0, frame0_micro * 1e9, np.nan)

fig_micro, ax_micro = plt.subplots(figsize=(4.8, 4.0))
im2 = ax_micro.imshow(frame0_micro_ns, cmap="viridis", origin="lower")
fig_micro.colorbar(im2, ax=ax_micro, shrink=0.8, label="Mean micro time (ns)")
ax_micro.set_title("Zeiss LSM 980 CLSM — frame 0 mean micro time")
ax_micro.set_xlabel("Pixel index")
ax_micro.set_ylabel("Line index")
fig_micro.tight_layout()

# %%
# Summary ---------------------------------------------------------------------
# - The Zeiss LSM 980 TTTR file is converted into a CLSM image using the same
#   parameters as the unit tests (default reader).
# - The intensity volume can be projected to obtain a quick overview of the
#   recorded structure.
# - ``get_mean_micro_time`` yields a pseudo-lifetime map which highlights regions
#   with different fluorescence decay characteristics.

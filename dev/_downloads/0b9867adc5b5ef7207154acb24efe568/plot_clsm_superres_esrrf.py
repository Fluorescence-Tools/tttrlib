"""
CLSM Super-Resolution (eSRRF) Photon Reassignment Example
==========================================================

This example demonstrates how to perform photon-level enhanced Super-Resolution
Radial Fluctuations (eSRRF) on Confocal Laser Scanning Microscopy (CLSM) datasets
using ``tttrlib.CLSMSuperRes``.

The workflow:
1. Read a CLSM TTTR dataset into a ``CLSMImage`` object.
2. Compute the Radial Gradient Convergence (RGC) spatial prior map using ``CLSMSuperRes.rgc_map``.
3. Reassign individual TTTR photon positions stochastically based on the RGC prior using ``CLSMSuperRes.reassign_photons``.
4. Write out the reassigned stream to a high-resolution container file (e.g., PTU format) using ``CLSMSuperRes.write``.
5. Load the reassigned container back as a ``CLSMImage`` and display side-by-side comparisons.
"""

import os
import sys
import time
from pathlib import Path
import numpy as np
import matplotlib.pyplot as plt
import tttrlib

# %%
# Load CLSM TTTR Dataset
# ----------------------
# Resolve path to sample data
data_root = os.environ.get('TTTRLIB_DATA', 'tttr-data')
file_path = Path(data_root) / 'imaging' / 'pq' / 'Microtime200_TH260' / 'beads.ptu'

if not file_path.exists():
    print(f"Data file not found at {file_path}. Please set TTTRLIB_DATA.")
    # Fallback to simulated phantom for demonstration
    sys.path[:0] = [str(_p / "examples" / "simulation") for _p in Path(__file__).resolve().parents
                    if (_p / "examples" / "simulation" / "simulate.py").is_file()][:1]
    from simulate import ring_phantom, render_frame
    emitters, params = ring_phantom(radius=8.0, nx=64, ny=64, n_points=60, photons_per_point=100.0)
    img_orig = render_frame(emitters, params, sigma=1.5, background=5.0, noise_seed=42)
    nx, ny = 64, 64
    has_real_data = False
else:
    print(f"Loading CLSM dataset: {file_path.name}")
    t_src = tttrlib.TTTR(str(file_path))
    clsm = tttrlib.CLSMImage(str(file_path), build_pixels=True, fill=True)
    img_orig = clsm.intensity.squeeze()
    if img_orig.ndim == 3:
        img_orig = img_orig.mean(axis=0)
    nx, ny = clsm.n_pixel, clsm.n_lines
    has_real_data = True

print(f"Input image resolution: {nx} x {ny}")

# %%
# Compute RGC Spatial Prior & Perform Photon Reassignment
# -------------------------------------------------------
mag = 4           # 4x spatial magnification
fwhm = 1.5        # PSF FWHM in pixels
sens = 1          # eSRRF sensitivity parameter
search_radius = 2.0  # Search radius in native pixels

print("Computing RGC spatial prior map...")
rgc_map = tttrlib.CLSMSuperRes.rgc_map(
    img_orig, magnification=mag, fwhm=fwhm, sensitivity=sens, intensity_weighting=True
)

if has_real_data:
    print("Reassigning photon stream...")
    t0 = time.perf_counter()
    t_reassigned = tttrlib.CLSMSuperRes.reassign_photons(
        clsm, t_src, mag, fwhm, sens, search_radius, "merged", 42, "esrrf"
    )
    t1 = time.perf_counter()
    frames, lines, x_exact, y_line, events = tttrlib.CLSMSuperRes.get_photon_positions(clsm, t_src)
    n_clsm_photons = len(events)
    print(f"Reassigned {t_reassigned.n_valid_events:,} photons in {t1 - t0:.2f} s")
    print(f"Photon Conservation ($N_{{in}} == N_{{out}}$): {n_clsm_photons == t_reassigned.n_valid_events} ({n_clsm_photons:,} photons)")

    # Export to PTU format and read back
    out_ptu = "/tmp/beads_reassigned_4x.ptu"
    tttrlib.CLSMSuperRes.write(t_reassigned, nx, ny, mag, out_ptu)
    clsm_sr = tttrlib.CLSMImage(out_ptu, reading_routine='default')
    img_sr = clsm_sr.intensity.squeeze()
    if img_sr.ndim == 3:
        img_sr = img_sr.mean(axis=0)
else:
    img_sr = rgc_map

# %%
# Visual Inspection: Side-by-Side Comparison
# ------------------------------------------
fig, axes = plt.subplots(1, 3, figsize=(15, 5))

# Original CLSM
im0 = axes[0].imshow(img_orig, cmap='inferno', origin='lower')
axes[0].set_title(f'Diffraction-Limited CLSM ({nx}x{ny})')
fig.colorbar(im0, ax=axes[0], fraction=0.046, pad=0.04)

# RGC Spatial Prior Map
im1 = axes[1].imshow(rgc_map, cmap='inferno', origin='lower')
axes[1].set_title(f'RGC Spatial Prior ({mag*nx}x{mag*ny})')
fig.colorbar(im1, ax=axes[1], fraction=0.046, pad=0.04)

# Reassigned eSRRF Super-Resolution
im2 = axes[2].imshow(img_sr, cmap='inferno', origin='lower')
axes[2].set_title(f'eSRRF Reassigned CLSM ({mag*nx}x{mag*ny})')
fig.colorbar(im2, ax=axes[2], fraction=0.046, pad=0.04)

plt.tight_layout()
output_png = "clsm_superres_esrrf.png"
fig.savefig(output_png, dpi=150)
print(f"Saved visual comparison figure to {output_png}")
plt.show()

"""CLSM raster simulation → CLSMImage reconstruction (PRD-005).

Places immobile fluorophores in the shape of a star, scans them with the photon
simulator's discrete per-pixel-dwell raster (SimScanner), builds a marker-annotated
TTTR from the simulated event stream, and reconstructs the image with tttrlib's
CLSMImage — the reconstruction should match the input shape.

Run:  python examples/simulation/clsm_star_scan.py
"""
import matplotlib.pyplot as plt
import numpy as np
import tttrlib

N, PX = 24, 0.5  # pixels per side, µm per pixel

# --- input shape: a 5-point star mask ---
yy, xx = np.mgrid[0:N, 0:N]
cx = cy = (N - 1) / 2
ang = np.arctan2(yy - cy, xx - cx)
rad = np.hypot(xx - cx, yy - cy)
mask = (rad <= 4 + 6 * np.abs(np.cos(2.5 * ang))).astype(int)

# --- sample: one immobile, bright fluorophore per 'on' pixel ---
sample = tttrlib.SimSample()
sp = tttrlib.SimSpecies(); sp.D = 0.0; sp.q = tttrlib.VectorDouble([2000.0])
sample.add_species(sp)
sample.set_rate_matrices(tttrlib.VectorDouble([0.0]), tttrlib.VectorDouble([0.0]))
sample.set_background(tttrlib.VectorDouble([0.0]))
for iy in range(N):
    for ix in range(N):
        if mask[iy, ix]:
            sample.add_fluorophore(ix * PX, iy * PX, 0.0, 0, False)

# --- sharp excitation PSF (grid); uniform detection (single channel) ---
excitation = tttrlib.SimGrid.gaussian3d(0.2, 1.0, 0.8, 1.0, 0.04, 1.0)
settings = tttrlib.SimSettings()
settings.dt = 0.01
settings.n_channels = 1
settings.n_ph_max = 10 ** 9  # run_scan runs the full raster regardless
engine = tttrlib.SimEngine(sample, excitation, tttrlib.VectorSimGrid([]), settings)

# --- raster scan: per-pixel dwell time; default frame/line/pixel markers ---
scanner = tttrlib.SimScanner.uniform(N, N, dwell=0.05, pixel_dx=PX, pixel_dy=PX,
                                     origin_x=0.0, origin_y=0.0,
                                     markers=tttrlib.SimMarkerConfig(), bidirectional=False)
engine.run_scan(scanner)

# --- build a marker-annotated TTTR and reconstruct with CLSMImage ---
macro = np.array(engine.macro_window(), dtype=np.uint64)
micro = np.zeros(len(macro), dtype=np.uint16)
routing = np.array(engine.channel(), dtype=np.int8)
event = np.array(engine.event_type(), dtype=np.int8)  # 0 photon, 1 marker
data = tttrlib.TTTR(macro, micro, routing, event)

clsm = tttrlib.CLSMImage(
    tttr_data=data, marker_frame_start=[4], marker_line_start=1, marker_line_stop=2,
    n_pixel_per_line=N, use_pixel_markers=True, marker_pixel=8, settings={"n_lines": N})
clsm.fill()
image = np.array(clsm.intensity)[0]

corr = np.corrcoef(image.ravel(), mask.ravel())[0, 1]
print(f"photons={int((event == 0).sum())}  reconstruction={image.shape}  "
      f"corr(input, reconstruction)={corr:.3f}")

fig, ax = plt.subplots(1, 2, figsize=(8, 4))
ax[0].imshow(mask, origin="lower", cmap="gray")
ax[0].set_title("input emitter map")
im = ax[1].imshow(image, origin="lower", cmap="inferno")
ax[1].set_title(f"CLSM reconstruction (r={corr:.3f})")
for a in ax:
    a.set_xticks([]); a.set_yticks([])
fig.colorbar(im, ax=ax[1], fraction=0.046)
fig.suptitle("Simulated CLSM raster scan")
fig.tight_layout()
plt.show()

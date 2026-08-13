"""
============================
Streaming FCS Correlation
============================

Online FCS correlation of a live photon stream using
:class:`tttrlib.StreamingCorrelator`.

Simulates a realistic 3D Brownian diffusion experiment through a confocal
Gaussian volume, then streams the photons one at a time into the correlator.
The resulting G(τ) shows a characteristic diffusion decay.
"""
import numpy as np
import matplotlib.pyplot as plt

import tttrlib

#%%
# Simulate 3D diffusion through a confocal volume
# ------------------------------------------------
# Particles undergo Brownian motion in a box.  A 3D Gaussian excitation
# profile models the confocal volume.  Photons are emitted at a rate
# proportional to the local intensity, detected with Poisson statistics.

rng = np.random.default_rng(42)

# Confocal volume parameters
w_xy = 0.5e-6    # lateral 1/e² waist (m)
w_z  = 2.0e-6    # axial 1/e² waist (m)
D    = 4e-12     # diffusion coefficient (m²/s) — ~40 kDa protein in water
tau_D = w_xy**2 / (4 * D)   # characteristic diffusion time (s)

# Simulation grid
dt = tau_D / 50              # time step: 50 samples per τ_D
n_steps = 300_000
box = 8e-6                   # cubic box edge (m) — large enough for free diffusion
n_particles = 4              # average occupancy N ≈ 2-5 for visible G(0)

# Peak count rate per particle at the centre (counts per dt)
# Tune so total count rate is ~50-200 kHz
count_rate_per_particle = 3.0

print(f"τ_D = {tau_D*1e3:.2f} ms, dt = {dt*1e6:.1f} µs, "
      f"N = {n_particles} particles")

# Initialize particles uniformly in the box
pos = rng.uniform(-box/2, box/2, size=(n_particles, 3))
brownian = np.sqrt(2 * D * dt)

photon_macro_times = []

for step in range(n_steps):
    # Brownian step
    pos += brownian * rng.standard_normal((n_particles, 3))
    # Periodic boundary (mimics infinite dilution, no wall artefacts)
    pos = np.mod(pos + box/2, box) - box/2

    # 3D Gaussian intensity per particle
    r2_xy = pos[:, 0]**2 + pos[:, 1]**2
    z2    = pos[:, 2]**2
    brightness = np.exp(-2*r2_xy/w_xy**2) * np.exp(-2*z2/w_z**2)

    # Poisson detection
    expected = count_rate_per_particle * np.sum(brightness)
    n_ph = rng.poisson(expected)
    if n_ph > 0:
        photon_macro_times.extend([step] * n_ph)

photon_macro_times = np.array(photon_macro_times, dtype=np.uint64)
print(f"Generated {len(photon_macro_times)} photons over {n_steps} steps "
      f"({len(photon_macro_times)/n_steps/dt/1e3:.1f} kHz)")

#%%
# Stream photons one at a time
# ----------------------------
corr = tttrlib.StreamingCorrelator(
    n_bins=16, n_casc=25, macro_time_resolution=dt
)

# Snapshot at intervals to show the curve building up
snapshots = []
checkpoints = {n_steps // 4, n_steps // 2, 3*n_steps // 4, n_steps}
for i, t in enumerate(photon_macro_times):
    corr.push_photon(int(t))
    if i + 1 in {len(photon_macro_times)//4, len(photon_macro_times)//2,
                 3*len(photon_macro_times)//4, len(photon_macro_times)}:
        snapshots.append((
            i + 1,
            corr.x_axis.copy(),
            corr.correlation_normalized.copy()
        ))

print(f"Streaming correlator: {corr.photon_count()} photons")

#%%
# Plot the streaming FCS curve
# ----------------------------
fig, axes = plt.subplots(1, 2, figsize=(12, 4.5))

# Left: evolving snapshots
ax = axes[0]
for n, x, g in snapshots:
    mask = x > 0
    ax.semilogx(x[mask] * 1e3, g[mask] - 1, label=f"N = {n:,}", alpha=0.8)  # x_axis already in seconds
ax.axvline(tau_D * 1e3, color='k', ls=':', alpha=0.4, label=f"τ_D = {tau_D*1e3:.1f} ms")
ax.set_xlabel("Lag time τ (ms)")
ax.set_ylabel("G(τ) - 1")
ax.set_title("Streaming FCS: building up")
ax.legend(fontsize=8)
ax.set_ylim(bottom=0)

# Right: final curve with theoretical overlay
ax = axes[1]
x = corr.x_axis
g = corr.correlation_normalized
mask = x > 0
tau = x[mask]  # x_axis already in seconds (macro_time_resolution applied)
ax.semilogx(tau * 1e3, g[mask] - 1, 'b-', alpha=0.8, label="Streaming")

# Theoretical 3D diffusion model:
# G(τ) - 1 = (1/N) * (1 + τ/τ_D)^-1 * (1 + τ/(τ_D * κ²))^-1/2
# κ = w_z / w_xy (structure parameter)
kappa = w_z / w_xy
N_eff = n_particles * (np.pi**1.5 * w_xy**2 * w_z) / (box**3 / 8)  # avg in volume
G_model = (1.0 / N_eff) * (1 + tau/tau_D)**(-1) * (1 + tau/(tau_D * kappa**2))**(-0.5)
ax.semilogx(tau * 1e3, G_model, 'r--', alpha=0.8, label=f"Theory (N={N_eff:.1f})")

ax.axvline(tau_D * 1e3, color='k', ls=':', alpha=0.4, label=f"τ_D = {tau_D*1e3:.1f} ms")
ax.set_xlabel("Lag time τ (ms)")
ax.set_ylabel("G(τ) - 1")
ax.set_title("Streaming FCS: final curve")
ax.legend(fontsize=8)
ax.set_ylim(bottom=0)

plt.tight_layout()
plt.show()

#%%
# Compare streaming vs batch correlator
# -------------------------------------
# Same data, batch Wahl correlator for validation.
batch = tttrlib.Correlator()
batch.method = "wahl"
batch.n_bins = 16
batch.n_casc = 25
w = np.ones(len(photon_macro_times), dtype=np.float64)
batch.set_macrotimes(photon_macro_times, photon_macro_times)
batch.set_weights(w, w)

xb = np.array(batch.x_axis)
gb = np.array(batch.correlation)

# Plot overlay
fig, ax = plt.subplots(figsize=(7, 4))
mask_b = xb > 0
ax.semilogx(xb[mask_b] * dt * 1e3, gb[mask_b] - 1, 'k-', alpha=0.6, label="Batch (Wahl)")
ax.semilogx(tau * 1e3, g[mask] - 1, 'b.', markersize=3, alpha=0.6, label="Streaming")
ax.axvline(tau_D * 1e3, color='r', ls=':', alpha=0.4, label=f"τ_D")
ax.set_xlabel("Lag time τ (ms)")
ax.set_ylabel("G(τ) - 1")
ax.set_title("Streaming vs batch correlation")
ax.legend()
ax.set_ylim(bottom=0)
plt.tight_layout()
plt.show()

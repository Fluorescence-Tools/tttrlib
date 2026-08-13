"""Fluorescence anisotropy simulation.

Polarised excitation preferentially excites molecules whose absorption dipole is
aligned with the laser (photoselection), creating an oriented excited-state
population. Each molecule emits from a dipole that is (1) tilted away from the
absorption dipole by the intrinsic ``r0`` cone and (2) rotated by rotational
diffusion during the excited-state lifetime, then detected in a parallel or a
perpendicular channel. The steady-state anisotropy therefore follows the Perrin
equation ``r = r0 / (1 + tau/theta)`` with ``theta = 1 / (6*D_rot)``.

This script simulates the anisotropy for several rotational diffusion coefficients
and compares it with the analytic Perrin curve.
"""
import matplotlib.pyplot as plt
import numpy as np
import tttrlib

# Fluorophore constants: limiting anisotropy r0 and fluorescence lifetime tau (ns).
r0, tau = 0.4, 4.0

# Micro-time (TCSPC) axis and a mono-exponential decay pattern with lifetime tau.
n_channels, resolution = 1024, 0.032               # channels, ns per channel
micro_time = np.arange(n_channels) * resolution
decay_pattern = np.exp(-micro_time / tau)

# We sweep the rotational diffusion coefficient D_rot (rad^2 / ns).
D_rot_values = np.array([0.0, 0.01, 0.02, 1.0 / (6.0 * tau), 0.1, 0.25, 1.0])
measured_r = np.zeros_like(D_rot_values)

for k, D_rot in enumerate(D_rot_values):
    # A sample of many immobile fluorophores with random dipole orientations. Two
    # detection channels: 0 = parallel, 1 = perpendicular.
    sample = tttrlib.SimSystem()
    species = tttrlib.SimSpecies()
    species.D = 0.0                                 # no translational diffusion
    species.q = tttrlib.VectorDouble([100.0, 100.0])
    species.r0 = r0
    species.D_rot = D_rot                           # rotational diffusion (rad^2/ns)
    species.decay = tttrlib.SimDecay.from_pattern(
        tttrlib.VectorDouble(decay_pattern.tolist()), resolution, 0.0)
    sample.add_species(species)
    sample.set_rate_matrices(tttrlib.VectorDouble([0.0]), tttrlib.VectorDouble([0.0]))
    sample.set_background(tttrlib.VectorDouble([0.0, 0.0]))
    for _ in range(1500):
        sample.add_fluorophore(0.0, 0.0, 0.0, 0, False)

    settings = tttrlib.SimIntegrator()
    settings.dt = 0.01
    settings.n_channels = 2
    settings.n_ph_max = 700000
    settings.n_microtime_channels = n_channels
    settings.microtime_resolution = resolution
    settings.laser_period = n_channels * resolution

    engine = tttrlib.SimEngine(
        sample, tttrlib.SimGrid.gaussian3d(0.5, 1.0, 1.0, 2.0, 0.05, 1.0),
        tttrlib.VectorSimGrid([]), settings)
    engine.run()

    # Steady-state anisotropy r = (I_par - I_perp) / (I_par + 2*I_perp).
    channel = np.asarray(engine.channel())[np.asarray(engine.event_type()) == 0]
    i_par, i_perp = (channel == 0).sum(), (channel == 1).sum()
    measured_r[k] = (i_par - i_perp) / (i_par + 2 * i_perp)

# Compare the simulated anisotropy with the analytic Perrin curve.
D_fine = np.linspace(0, D_rot_values.max(), 200)
perrin = r0 / (1.0 + tau * 6.0 * D_fine)

fig, ax = plt.subplots(figsize=(6, 4))
ax.plot(D_fine, perrin, "k-", label="Perrin  r0 / (1 + τ/θ)")
ax.plot(D_rot_values, measured_r, "o", color="tab:red", label="simulated")
ax.axhline(r0, ls=":", color="gray")
ax.text(D_rot_values.max() * 0.7, r0 + 0.005, "r0")
ax.set_xlabel("rotational diffusion coefficient  D_rot (rad²/ns)")
ax.set_ylabel("steady-state anisotropy  r")
ax.set_title(f"Fluorescence anisotropy (r0={r0}, τ={tau} ns)")
ax.legend()
fig.tight_layout()
plt.show()

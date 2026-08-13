"""FLIM micro-time from an arbitrary decay pattern.

Each species carries a micro-time *decay pattern* (an arbitrary probability density
over micro-time — not restricted to a multi-exponential), optionally convolved with
an arbitrary IRF pattern (not restricted to a Gaussian). The simulator draws a
micro-time per photon from that pattern; the recovered micro-time histogram matches
the input pattern.

Run:  python examples/simulation/flim_decay_pattern.py
"""
import matplotlib.pyplot as plt
import numpy as np
import tttrlib

n, dt = 4096, 0.008  # micro-time channels and ns/channel
t = np.arange(n) * dt

# An arbitrary decay pattern (here bi-exponential — but any array works: an
# experimental decay, a simulated model, etc.) ...
decay = np.exp(-t / 2.5) + 0.4 * np.exp(-t / 0.6)
# ... convolved with an arbitrary IRF pattern (here a non-Gaussian multi-spike IRF).
irf = np.zeros(n); irf[8] = 1.0; irf[12] = 0.6; irf[16] = 0.2
pattern = np.array(tttrlib.SimDecay.convolve(
    tttrlib.VectorDouble(decay.tolist()), tttrlib.VectorDouble(irf.tolist())))
species_decay = tttrlib.SimDecay.from_pattern(
    tttrlib.VectorDouble(pattern.tolist()), dt, 0.0)

sample = tttrlib.SimSystem()
sp = tttrlib.SimSpecies(); sp.D = 0.0; sp.q = tttrlib.VectorDouble([1000.0])
sp.decay = species_decay
sample.add_species(sp)
sample.set_rate_matrices(tttrlib.VectorDouble([0.0]), tttrlib.VectorDouble([0.0]))
sample.set_background(tttrlib.VectorDouble([0.0]))
sample.add_fluorophore(0.0, 0.0, 0.0, 0, False)

settings = tttrlib.SimIntegrator()
settings.dt = 0.01; settings.n_channels = 1; settings.n_ph_max = 300000
settings.n_microtime_channels = n
settings.microtime_resolution = dt
settings.laser_period = n * dt
engine = tttrlib.SimEngine(sample, tttrlib.SimGrid.gaussian3d(0.3, 1.0, 0.8, 1.0, 0.04, 1.0),
                           tttrlib.VectorSimGrid([]), settings)
engine.run()

event = np.array(engine.event_type())
micro = np.array(engine.micro_time())[event == 0]
hist = np.bincount(micro, minlength=n)[:n].astype(float)
corr = np.corrcoef(hist / hist.sum(), pattern / pattern.sum())[0, 1]
print(f"photons={len(micro)}  corr(recovered, input pattern)={corr:.4f}")

fig, ax = plt.subplots(figsize=(6, 4))
ax.semilogy(t, pattern / pattern.max(), "k-", lw=2, label="input decay pattern")
ax.semilogy(t, hist / hist.max(), color="tab:red", alpha=0.7, lw=1,
            label="simulated micro-times")
ax.set_xlabel("micro-time (ns)")
ax.set_ylabel("normalised counts")
ax.set_ylim(1e-3, 2)
ax.set_title(f"FLIM decay-pattern recovery (corr={corr:.4f})")
ax.legend()
fig.tight_layout()
plt.show()

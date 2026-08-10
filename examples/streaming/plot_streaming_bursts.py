"""
===============================
Streaming Burst Detection
===============================

Online burst search on a live photon stream using
:class:`tttrlib.StreamingBurstDetector`.

The streaming burst detector uses a sliding photon window (m consecutive
photons) to compute the local count rate.  When the rate exceeds a
threshold (m / T), a burst opens; when it drops below, the burst closes.
Photons are accepted one at a time — no need to buffer the full record.
"""
import numpy as np
import matplotlib.pyplot as plt

import tttrlib

#%%
# Generate a bursty photon stream
# --------------------------------
# Create a synthetic trace with 3 bursts embedded in a low background.
rng = np.random.default_rng(42)
background_rate = 0.01   # photons per macro-time unit
burst_rate = 0.5         # photons per macro-time unit during bursts

times = []
state = False  # in burst?
t = 0
for _ in range(50_000):
    rate = burst_rate if state else background_rate
    dt = int(rng.exponential(1.0 / rate)) + 1
    t += dt
    times.append(t)
    # switch states randomly
    if rng.random() < (0.0003 if state else 0.0008):
        state = not state

times = np.array(times, dtype=np.uint64)
print(f"Generated {len(times)} photons over {times[-1]} macro-time units")

#%%
# Run streaming burst detection
# ------------------------------
# m = 10 photons per window, T = 100 macro-time units (threshold rate = 0.1)
detector = tttrlib.StreamingBurstDetector(
    window_photons=10,
    window_time=100.0,
    macro_time_resolution=1.0
)
detector.push_np(times)
detector.flush()  # close any open burst at the end

burst_indices = detector.bursts
n_bursts = len(burst_indices) // 2
print(f"Detected {n_bursts} bursts out of {detector.photon_count()} photons")

#%%
# Visualize bursts
# -----------------
fig, ax = plt.subplots(figsize=(10, 3))

# Plot photon density
hist, edges = np.histogram(times, bins=200, density=True)
centers = 0.5 * (edges[:-1] + edges[1:])
ax.fill_between(centers, hist, alpha=0.5, label="photon density")

# Highlight bursts
for i in range(n_bursts):
    s = burst_indices[2 * i]
    e = burst_indices[2 * i + 1]
    t0 = times[s]
    t1 = times[e]
    ax.axvspan(t0, t1, alpha=0.2, color="red")

ax.set_xlabel("Macro time")
ax.set_ylabel("Photon density")
ax.set_title(f"Streaming burst detection — {n_bursts} bursts")
plt.tight_layout()
plt.show()

#%%
# Burst statistics
# -----------------
if n_bursts > 0:
    sizes = [burst_indices[2*i+1] - burst_indices[2*i] + 1 for i in range(n_bursts)]
    durations = [times[burst_indices[2*i+1]] - times[burst_indices[2*i]] for i in range(n_bursts)]
    print(f"Burst sizes:     min={min(sizes)}, max={max(sizes)}, mean={np.mean(sizes):.1f}")
    print(f"Burst durations: min={min(durations)}, max={max(durations)}, mean={np.mean(durations):.1f}")

"""Benchmark Gopich-Szabo: C++ (tttrlib) vs Python (numba)."""
import sys, os, time
import numpy as np

sys.path.insert(0, '/Users/tpeulen/dev/chisurf')

import tttrlib
from chisurf.core.fluorescence.burst.gopich_szabo import (
    PhotonBursts, log_likelihood as py_log_likelihood,
    emission_from_efficiencies, rate_matrix_from_rates,
)

# --- synthetic bursts: two-state exchanging system ------------------------
np.random.seed(42)
k12, k21 = 1000.0, 500.0  # Hz
e1, e2 = 0.2, 0.8
mt_rate = 20e3  # 20 kHz mean photon rate

n_bursts = 200
mean_burst_len = 100  # photons per burst

all_times_list = []
all_colors_list = []
for b in range(n_bursts):
    n_ph = np.random.poisson(mean_burst_len)
    n_ph = max(n_ph, 5)
    # Simulate a two-state trajectory
    times = np.cumsum(np.random.exponential(1.0 / mt_rate, n_ph))
    # Assign states via Gillespie-like
    states = np.zeros(n_ph, dtype=int)
    state = 0 if np.random.rand() < k21 / (k12 + k21) else 1
    t_curr = 0.0
    for i in range(n_ph):
        while times[i] > t_curr:
            rate = k12 if state == 0 else k21
            t_curr += np.random.exponential(1.0 / rate)
            state = 1 - state
        states[i] = state
    # Generate colors based on state
    effs = np.where(states == 0, e1, e2)
    colors = (np.random.rand(n_ph) < effs).astype(np.int32)
    all_times_list.append(times)
    all_colors_list.append(colors)

bursts = PhotonBursts.from_lists(all_times_list, all_colors_list, n_colors=2, min_photons=5)
print(f"Bursts: {len(bursts)}, Total photons: {bursts.n_photons}")

rate_matrix = rate_matrix_from_rates([k12, k21], 2)
emission = emission_from_efficiencies([e1, e2])

# --- Python likelihood ---
# Warm up numba
_ = py_log_likelihood(bursts, rate_matrix, emission)

N_RUNS = 3
times_py = []
for _ in range(N_RUNS):
    t0 = time.perf_counter()
    ll_py = py_log_likelihood(bursts, rate_matrix, emission)
    t1 = time.perf_counter()
    times_py.append(t1 - t0)
print(f"\nPython log_likelihood: {ll_py:.4f} (best: {1000*min(times_py):.1f} ms)")

# --- C++ likelihood ---
gs = tttrlib.GopichSzabo()
rm_flat = rate_matrix.flatten().tolist()  # row-major: [target, source]
em_flat = emission.flatten().tolist()
ok = gs.set_scheme(rm_flat, em_flat, 2, 2)
assert ok

times_cpp = []
for _ in range(N_RUNS):
    t0 = time.perf_counter()
    ll_cpp = gs.log_likelihood(bursts.times, bursts.colors, bursts.offsets)
    t1 = time.perf_counter()
    times_cpp.append(t1 - t0)
print(f"C++ log_likelihood:    {ll_cpp:.4f} (best: {1000*min(times_cpp):.1f} ms)")

print(f"\n=== BENCHMARK (best of {N_RUNS}) ===")
print(f"Python (numba): {1000*min(times_py):.1f} ms")
print(f"C++ (tttrlib):  {1000*min(times_cpp):.1f} ms")
print(f"Speedup:        {min(times_py)/min(times_cpp):.2f}x")
print(f"LL match:       py={ll_py:.6f}, cpp={ll_cpp:.6f}, diff={abs(ll_py-ll_cpp):.2e}")

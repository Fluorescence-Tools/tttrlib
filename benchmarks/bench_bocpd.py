"""Benchmark BOCPD: C++ (tttrlib) vs Python (numba) on the same real data."""
import sys, os, time
import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'build', 'ext'))
sys.path.insert(0, '/Users/tpeulen/dev/chisurf')

import tttrlib
from chisurf.core.fluorescence.burst.bocpd import (
    bocpd_joint_poisson_optimized,
    bin_photons,
    extract_bursts,
)

test_file = '/Users/tpeulen/dev/tttr-data/pq/ptu/pq_ptu_hh_t3.ptu'
data = tttrlib.TTTR(test_file, 'PTU')
n = data.size()
mt_res = data.header.macro_time_resolution
mt = np.asarray(data.macro_times)
routing = np.asarray(data.routing_channels)
duration = (mt[-1] - mt[0]) * mt_res
print(f"Photons: {n}, Duration: {duration:.1f} s, MT res: {mt_res:.2e} s")

# Split channels for Python BOCPD (detect actual channel values)
channels_used = sorted(set(routing.tolist()))
print(f"Routing channels: {channels_used}")
ch_d, ch_a = channels_used[0], channels_used[-1]
donor_mask = routing == ch_d
acceptor_mask = routing == ch_a
donor_ts = mt[donor_mask] * mt_res
acceptor_ts = mt[acceptor_mask] * mt_res
print(f"Donor: {donor_mask.sum()}, Acceptor: {acceptor_mask.sum()}")

# --- parameters shared by both ---
dt = 1e-4  # 0.1 ms bins
prior_count = 1.0
prior_duration = 1.0
changepoint_prob = 0.1
max_run = 256

# --- Python BOCPD on binned arrays ---
D, A, bins = bin_photons(donor_ts, acceptor_ts, dt=dt)
print(f"Bins: {len(D)}, D/bin: {D.mean():.1f}, A/bin: {A.mean():.1f}")

# warm up numba
bocpd_joint_poisson_optimized(D[:50], A[:50],
    prior_count=prior_count, prior_duration=prior_duration,
    changepoint_prob=changepoint_prob, max_run=max_run)

# Run Python
t0 = time.perf_counter()
cps_py, rl_map_py = bocpd_joint_poisson_optimized(D, A,
    prior_count=prior_count, prior_duration=prior_duration,
    changepoint_prob=changepoint_prob, max_run=max_run)
t1 = time.perf_counter()
bursts_py = extract_bursts(D, A, bins, cps_py, min_counts=20)
print(f"\nPython BOCPD: {len(cps_py)} changepoints, {len(bursts_py)} bursts in {1000*(t1-t0):.1f} ms")

# --- C++ BOCPD via TTTR ---
# per_channel=False to match Python (which sums D+A counts per bin)
N_RUNS = 3
times_cpp = []
for _ in range(N_RUNS):
    t0 = time.perf_counter()
    bursts_cpp = data.burst_search_bocpd(
        L=20, dt=dt, prior_count=prior_count,
        prior_duration=prior_duration,
        changepoint_prob=changepoint_prob,
        max_run=max_run, per_channel=False)
    t1 = time.perf_counter()
    times_cpp.append(t1 - t0)

arr_cpp = np.asarray(bursts_cpp)
n_cpp = len(arr_cpp) // 2
print(f"C++ BOCPD: {n_cpp} bursts (best of {N_RUNS}: {1000*min(times_cpp):.1f} ms)")

# --- speed comparison ---
# Python timing: run a few times and take best
times_py = []
for _ in range(N_RUNS):
    t0 = time.perf_counter()
    _ = bocpd_joint_poisson_optimized(D, A,
        prior_count=prior_count, prior_duration=prior_duration,
        changepoint_prob=changepoint_prob, max_run=max_run)
    t1 = time.perf_counter()
    times_py.append(t1 - t0)

print(f"\n=== BENCHMARK (best of {N_RUNS}) ===")
print(f"Python (numba): {1000*min(times_py):.1f} ms")
print(f"C++ (tttrlib):  {1000*min(times_cpp):.1f} ms")
print(f"Speedup:        {min(times_py)/min(times_cpp):.2f}x")

# --- correctness: compare changepoints ---
print(f"\n=== CORRECTNESS ===")
print(f"Python changepoints: {len(cps_py)}")
# C++ changepoints are not directly exposed, but bursts count gives an idea
print(f"Python bursts: {len(bursts_py)}")
print(f"C++ bursts: {n_cpp}")
if n_cpp > 0 and len(bursts_py) > 0:
    py_sizes = sorted([b['donor'] + b['acceptor'] for b in bursts_py])
    cpp_sizes = sorted((arr_cpp[1::2] - arr_cpp[::2] + 1).tolist())
    print(f"Python burst sizes: median={np.median(py_sizes):.0f}, range=[{py_sizes[0]}, {py_sizes[-1]}]")
    print(f"C++ burst sizes:    median={np.median(cpp_sizes):.0f}, range=[{cpp_sizes[0]}, {cpp_sizes[-1]}]")

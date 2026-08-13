"""Benchmark PCH/FIDA: C++ (tttrlib) vs Python."""
import sys, time
import numpy as np
sys.path.insert(0, '/Users/tpeulen/dev/chisurf')

import tttrlib
from chisurf.core.models.pch.pch import (
    pch_single_species as py_pch_single,
    pch_open_system as py_pch_open,
)
from chisurf.core.models.pch.fida import (
    fida_pch as py_fida_pch, dvdx_gaussian as py_dvdx,
)

k_max = 50
brightness = 1.0
avg_n = 0.5

N_RUNS = 3

# PCH single
times_py, times_cpp = [], []
for _ in range(N_RUNS):
    t0 = time.perf_counter()
    py_p = py_pch_single(np.arange(k_max + 1), brightness)
    times_py.append(time.perf_counter() - t0)
    t0 = time.perf_counter()
    cpp_p = tttrlib.pch_single_species(k_max, brightness)
    times_cpp.append(time.perf_counter() - t0)

print("=== PCH single species ===")
print(f"Python: {1000*min(times_py):.1f} ms")
print(f"C++:    {1000*min(times_cpp):.1f} ms")
print(f"Speedup: {min(times_py)/min(times_cpp):.1f}x")
print(f"Match: {np.allclose(py_p, cpp_p, atol=1e-6)}")

# PCH open system
times_py, times_cpp = [], []
for _ in range(N_RUNS):
    t0 = time.perf_counter()
    py_p2 = py_pch_open(np.arange(k_max + 1), brightness, avg_n)
    times_py.append(time.perf_counter() - t0)
    t0 = time.perf_counter()
    cpp_p2 = tttrlib.pch_open_system(k_max, brightness, avg_n)
    times_cpp.append(time.perf_counter() - t0)

print("\n=== PCH open system ===")
print(f"Python: {1000*min(times_py):.1f} ms")
print(f"C++:    {1000*min(times_cpp):.1f} ms")
print(f"Speedup: {min(times_py)/min(times_cpp):.1f}x")
print(f"Match: {np.allclose(py_p2, cpp_p2, atol=1e-6)}")

# FIDA
species = [(1.0, 0.5)]
times_py, times_cpp = [], []
for _ in range(N_RUNS):
    t0 = time.perf_counter()
    py_f = py_fida_pch(k_max, species)
    times_py.append(time.perf_counter() - t0)
    t0 = time.perf_counter()
    cpp_f = tttrlib.fida_pch(k_max, [1.0, 0.5], 1, 0.0)
    times_cpp.append(time.perf_counter() - t0)

print("\n=== FIDA ===")
print(f"Python: {1000*min(times_py):.1f} ms")
print(f"C++:    {1000*min(times_cpp):.1f} ms")
print(f"Speedup: {min(times_py)/min(times_cpp):.1f}x")
print(f"Match: {np.allclose(py_f, cpp_f, atol=1e-6)}")

#!/usr/bin/env python3
"""A/B benchmark: tttrlib correlators (wahl, felekyan, laurence) vs
pycorrelate (Laurence 2006 numba reference).

Builds synthetic photon streams of varying sizes and correlates them with
each method, reporting wall-clock time and verifying the correlations agree.
"""
import numpy as np
import time
import sys

import tttrlib

# ---- pycorrelate reference (Laurence 2006) -------------------------------
# Inlined here so the benchmark is self-contained.
try:
    import numba
    HAVE_NUMBA = True
except ImportError:
    HAVE_NUMBA = False

if HAVE_NUMBA:
    @numba.jit(nopython=True)
    def _pcorrelate(t, u, bins):
        nbins = len(bins) - 1
        counts = np.zeros(nbins, dtype=np.float64)
        imin = np.zeros(nbins, dtype=np.int64)
        imax = np.zeros(nbins, dtype=np.int64)
        for ti in t:
            for k in range(nbins):
                tau_min = bins[k]
                tau_max = bins[k + 1]
                if k == 0:
                    j = imin[k]
                    while j < len(u):
                        if u[j] - ti >= tau_min:
                            break
                        j += 1
                imin[k] = j
                if imax[k] > j:
                    j = imax[k]
                while j < len(u):
                    if u[j] - ti >= tau_max:
                        break
                    j += 1
                imax[k] = j
            counts += (imax - imin)
        return counts / np.diff(bins)


def bench_tttrlib(times, channels, method, n_bins=16, n_casc=25):
    """Run tttrlib correlation with a given method."""
    w = np.ones_like(times, dtype=np.float64)
    corr = tttrlib.Correlator()
    corr.method = method
    corr.n_bins = n_bins
    corr.n_casc = n_casc
    corr.set_macrotimes(times, times)
    corr.set_weights(w, w)
    x = np.array(corr.x_axis)
    y = np.array(corr.correlation)
    return x, y


def timeit(fn, repeat=5):
    times = []
    for _ in range(repeat):
        t0 = time.perf_counter()
        fn()
        t1 = time.perf_counter()
        times.append(t1 - t0)
    return min(times), np.median(times)


def main():
    rng = np.random.default_rng(42)
    sizes = [50_000, 200_000, 500_000, 1_000_000]

    print(f"{'N photons':>12} {'method':>16} {'best ms':>10} {'med ms':>10} {'M ph/s':>10}")
    print("-" * 65)

    for n in sizes:
        # Synthetic photon stream: random-telegraph at ~1MHz with Poisson gaps
        gaps = rng.exponential(10.0, n).astype(np.uint64) + 1
        times = np.cumsum(gaps)
        channels = np.zeros(n, dtype=np.int32)

        # tttrlib wahl (default)
        def run_wahl():
            bench_tttrlib(times, channels, "wahl")
        b, m = timeit(run_wahl)
        print(f"{n:>12} {'tttrlib wahl':>16} {b*1e3:>10.2f} {m*1e3:>10.2f} {n/b/1e6:>10.1f}")

        # tttrlib felekyan
        def run_felekyan():
            bench_tttrlib(times, channels, "felekyan")
        b, m = timeit(run_felekyan)
        print(f"{n:>12} {'tttrlib felekyan':>16} {b*1e3:>10.2f} {m*1e3:>10.2f} {n/b/1e6:>10.1f}")

        # tttrlib laurence
        def run_laurence():
            bench_tttrlib(times, channels, "laurence")
        b, m = timeit(run_laurence)
        print(f"{n:>12} {'tttrlib laurence':>16} {b*1e3:>10.2f} {m*1e3:>10.2f} {n/b/1e6:>10.1f}")

        if HAVE_NUMBA:
            # pycorrelate reference
            bins = np.concatenate([[0], np.unique(np.round(
                np.logspace(0, 7, 25*16+1)).astype(np.int64))])
            # warmup JIT
            _pcorrelate(times[:1000], times[:1000], bins)
            def run_pycorr():
                _pcorrelate(times, times, bins)
            b, m = timeit(run_pycorr)
            print(f"{n:>12} {'pycorrelate numba':>16} {b*1e3:>10.2f} {m*1e3:>10.2f} {n/b/1e6:>10.1f}")

        print()

    # ---- Correctness check: wahl vs felekyan (same algorithm, different code paths)
    print("=== Correctness: wahl vs felekyan (should agree on overlapping lags) ===")
    n = 200_000
    gaps = rng.exponential(10.0, n).astype(np.uint64) + 1
    times = np.cumsum(gaps)
    channels = np.zeros(n, dtype=np.int32)
    xw, yw = bench_tttrlib(times, channels, "wahl")
    xf, yf = bench_tttrlib(times, channels, "felekyan")
    # compare the overlap region
    n_overlap = min(len(yw), len(yf))
    max_diff = np.max(np.abs(yw[:n_overlap] - yf[:n_overlap]))
    print(f"  max |wahl - felekyan| over {n_overlap} bins: {max_diff:.6e}")
    rel = np.max(np.abs(yw[:n_overlap] - yf[:n_overlap]) / (np.abs(yw[:n_overlap]) + 1e-30))
    print(f"  max relative diff: {rel:.6e}")


if __name__ == "__main__":
    main()

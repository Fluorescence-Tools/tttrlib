"""HistogramNd against boost-histogram, on matched features.

The comparison that matters: both sides carry flow bins, both use every core.
boost is timed single-threaded too, because that is the number a casual
benchmark reports and it flatters tttrlib by a factor of three.

Each case runs in its own process -- see bench_hist.py for why.
"""
import os
import sys
import time

import numpy as np

import tttrlib

try:
    import boost_histogram as bh
except ImportError:
    bh = None

N_CPU = os.cpu_count() or 1
A = tttrlib.Axis.regular


def timeit(fn, repeat=11):
    best = float("inf")
    for _ in range(repeat):
        t0 = time.perf_counter()
        fn()
        best = min(best, time.perf_counter() - t0)
    return best


def _data(n, dims):
    rng = np.random.default_rng(0)
    return [rng.uniform(0, 100, n) for _ in range(dims)]


def bench(n, dims, n_bins):
    cols = _data(n, dims)
    h = tttrlib.HistogramNd(tttrlib.AxisVector([A(n_bins, 0.0, 100.0)] * dims))
    if dims == 1:
        run = lambda: h.fill_1d(cols[0])
    elif dims == 2:
        run = lambda: h.fill_2d(cols[0], cols[1])
    else:
        rows = np.column_stack(cols)
        run = lambda: h.fill_rows(rows)

    out = {"tttrlib": timeit(run)}
    if bh is not None:
        b = bh.Histogram(*[bh.axis.Regular(n_bins, 0, 100) for _ in range(dims)])
        out["boost 1t"] = timeit(lambda: b.fill(*cols))
        out[f"boost {N_CPU}t"] = timeit(lambda: b.fill(*cols, threads=N_CPU))
    return out


CASES = {
    "1d-128":  ("1D, 128 bins",       lambda n: bench(n, 1, 128)),
    "1d-1024": ("1D, 1024 bins",      lambda n: bench(n, 1, 1024)),
    "2d-128":  ("2D, 128 x 128",      lambda n: bench(n, 2, 128)),
    "2d-512":  ("2D, 512 x 512",      lambda n: bench(n, 2, 512)),
    "2d-1024": ("2D, 1024 x 1024",    lambda n: bench(n, 2, 1024)),
    "3d-32":   ("3D, 32 x 32 x 32",   lambda n: bench(n, 3, 32)),
}


def report(title, res, n):
    base = res["tttrlib"]
    print(f"\n{title}")
    for name, t in sorted(res.items(), key=lambda kv: kv[1]):
        rel = "  --" if name == "tttrlib" else f"{t / base:5.2f}x tttrlib"
        print(f"  {name:9s} {t * 1e3:8.2f} ms   {n / t / 1e6:7.1f} M pts/s   {rel}")


if __name__ == "__main__":
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 5_000_000
    case = sys.argv[2] if len(sys.argv) > 2 else None
    if case:
        title, fn = CASES[case]
        report(title, fn(n), n)
        sys.exit(0)
    import subprocess
    print(f"n = {n:,} points, {N_CPU} cores"
          + ("" if bh else "   (boost-histogram not installed)"))
    for key in CASES:
        subprocess.run([sys.executable, __file__, str(n), key], check=True)

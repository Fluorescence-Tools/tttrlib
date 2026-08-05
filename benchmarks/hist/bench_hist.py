"""tttrlib.hist against boost-histogram and numpy, on the shapes tttrlib sees.

Fill throughput only -- the axis objects are built once outside the timed
region for every library, so what is measured is the inner loop.

boost-histogram is timed BOTH single-threaded and with threads=n_cpu, because
the single-threaded number flatters tttrlib and is not what a caller who cares
about speed actually writes. NDXplorer, the reason this comparison exists, sets
`threads=` on every fill above 200k points.
"""
import sys
import time

import os

import numpy as np

import tttrlib

N_CPU = os.cpu_count() or 1

try:
    import boost_histogram as bh
except ImportError:
    bh = None


def timeit(fn, repeat=7):
    best = float("inf")
    for _ in range(repeat):
        t0 = time.perf_counter()
        fn()
        best = min(best, time.perf_counter() - t0)
    return best


def bench_1d(n, n_bins, log=False):
    rng = np.random.default_rng(0)
    x = rng.uniform(1.0, 1000.0, n)
    edges = (np.logspace(0, 3, n_bins) if log else np.linspace(1, 1000, n_bins))
    w = np.ones(n)
    out = {}

    h = np.zeros(n_bins)
    axis = "log10" if log else "lin"
    out["tttrlib"] = timeit(lambda: tttrlib.histogram1D_double(x, w, edges, h, axis, False))

    if bh is not None:
        ax = (bh.axis.Regular(n_bins - 1, 1, 1000, transform=bh.axis.transform.log)
              if log else bh.axis.Regular(n_bins - 1, 1, 1000))
        hb = bh.Histogram(ax)
        out["boost 1t"] = timeit(lambda: hb.fill(x))
        out[f"boost {N_CPU}t"] = timeit(lambda: hb.fill(x, threads=N_CPU))

    out["numpy"] = timeit(lambda: np.histogram(x, bins=n_bins - 1, range=(1, 1000)))
    return out


def bench_2d(n, nx, ny):
    rng = np.random.default_rng(0)
    x = rng.uniform(0, 100, n)
    y = rng.uniform(0, 100, n)
    ex = np.linspace(0, 100, nx)
    ey = np.linspace(0, 100, ny)
    w = np.ones(n)
    out = {}

    h = np.zeros(nx * ny)
    out["tttrlib"] = timeit(lambda: tttrlib.histogram2D_double(
        x, y, w, ex, ey, h, "lin", "lin", False))

    if bh is not None:
        hb = bh.Histogram(bh.axis.Regular(nx - 1, 0, 100), bh.axis.Regular(ny - 1, 0, 100))
        out["boost 1t"] = timeit(lambda: hb.fill(x, y))
        out[f"boost {N_CPU}t"] = timeit(lambda: hb.fill(x, y, threads=N_CPU))

    out["numpy"] = timeit(lambda: np.histogram2d(x, y, bins=(nx - 1, ny - 1),
                                                 range=((0, 100), (0, 100))))
    return out


def report(title, res, n):
    base = res["tttrlib"]
    print(f"\n{title}")
    for name, t in sorted(res.items(), key=lambda kv: kv[1]):
        rate = n / t / 1e6
        rel = f"{t / base:5.2f}x tttrlib" if name != "tttrlib" else "  --"
        print(f"  {name:9s} {t * 1e3:8.2f} ms   {rate:7.1f} M pts/s   {rel}")


CASES = {
    "1d-128":    ("1D, 128 bins, linear",    lambda n: bench_1d(n, 128)),
    "1d-128log": ("1D, 128 bins, log",       lambda n: bench_1d(n, 128, log=True)),
    "1d-4096":   ("1D, 4096 bins, linear",   lambda n: bench_1d(n, 4096)),
    "2d-128":    ("2D, 128 x 128, linear",   lambda n: bench_2d(n, 128, 128)),
    "2d-512":    ("2D, 512 x 512, linear",   lambda n: bench_2d(n, 512, 512)),
    # Where replication stops paying: one private copy of a 1024x1024 histogram
    # is 8 MB per thread, and the scratch costs more than the threading saves.
    "2d-1024":   ("2D, 1024 x 1024, linear", lambda n: bench_2d(n, 1024, 1024)),
}


if __name__ == "__main__":
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 10_000_000
    case = sys.argv[2] if len(sys.argv) > 2 else None

    if case:
        title, fn = CASES[case]
        report(title, fn(n), n)
        sys.exit(0)

    # Each case in its own process. Run back to back in one process and they
    # interfere badly -- a threaded boost fill leaves its pool warm and the next
    # case measures the contention, not the histogram. Measured drift was over
    # 5x on the 2D cases, which is larger than any difference being compared.
    import subprocess
    print(f"n = {n:,} points, {N_CPU} cores"
          + ("" if bh else "   (boost-histogram not installed)"))
    for key in CASES:
        subprocess.run([sys.executable, __file__, str(n), key], check=True)

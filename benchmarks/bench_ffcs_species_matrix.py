"""Species-matrix (fFCS) correlation: pair loop vs a single pass over the stream.

Design probe for the batched filtered-FCS entry point. ``bench_ffcs_species_matrix.cpp``
holds two arms -- a verbatim transcription of ``ccf_wahl`` run once per species
pair, and the shared-time-axis single pass -- and this driver checks both
against the real installed ``tttrlib.Correlator`` before timing them.

Build and run::

    c++ -std=c++17 -O3 -o /tmp/bench_ffcs benchmarks/bench_ffcs_species_matrix.cpp
    BENCH_FFCS_BIN=/tmp/bench_ffcs python benchmarks/bench_ffcs_species_matrix.py

Set ``TTTRLIB_NUM_THREADS=1 TTTRLIB_USE_OPENMP=0`` for the reproducible
single-threaded comparison: the two arms are then bit-identical on every
element, which is the parity claim. With threads on they agree to ~1e-12
relative, the difference being the order of the partial-sum reduction.
"""
import os, struct, subprocess, sys, tempfile, time
import numpy as np
import tttrlib

N_BINS, N_CASC = 8, 20
BIN = os.environ.get("BENCH_FFCS_BIN", "/tmp/bench_ffcs")
TMP = tempfile.mkdtemp(prefix="bench_ffcs_")


def run(n_ph, n_sets, seed=0):
    rng = np.random.default_rng(seed)
    times = np.cumsum(rng.integers(1, 200, size=n_ph)).astype(np.uint64)
    # realistic fFCS filter weights: signed, O(1), a few exact zeros
    W = rng.normal(0.0, 1.0, size=(n_sets, n_ph))
    W[rng.random(W.shape) < 0.02] = 0.0
    W = np.ascontiguousarray(W, dtype=np.float64)

    with open(f"{TMP}/in.bin", "wb") as f:
        f.write(struct.pack("<4q", n_ph, n_sets, N_BINS, N_CASC))
        f.write(times.tobytes())
        f.write(W.tobytes())

    out = subprocess.run([BIN, f"{TMP}/in.bin", f"{TMP}/A.bin", f"{TMP}/B.bin"],
                         capture_output=True, text=True)
    print(out.stdout.strip())
    if out.returncode:
        print(out.stderr); sys.exit(1)

    n_lags = N_BINS * N_CASC + 1
    A = np.fromfile(f"{TMP}/A.bin").reshape(n_sets, n_sets, n_lags)
    B = np.fromfile(f"{TMP}/B.bin").reshape(n_sets, n_sets, n_lags)

    # --- (1) is arm A a faithful transcription of the real Correlator? -------
    t0 = time.perf_counter()
    R = np.empty_like(A)
    for a in range(n_sets):
        for b in range(n_sets):
            c = tttrlib.Correlator()
            c.n_bins, c.n_casc = N_BINS, N_CASC
            c.set_macrotimes(times, times)
            c.set_weights(np.ascontiguousarray(W[a]), np.ascontiguousarray(W[b]))
            c.run()
            R[a, b] = np.asarray(c.get_corr_normalized(), dtype=float)
    t_real = time.perf_counter() - t0

    def cmp(name, X, Y):
        fin = np.isfinite(X) & np.isfinite(Y)
        assert fin.sum() > 0
        den = np.maximum(np.abs(Y[fin]), 1e-30)
        rel = np.abs(X[fin] - Y[fin]) / den
        exact = int((X[fin] == Y[fin]).sum())
        print(f"   {name}: max|rel|={rel.max():.3e}  median|rel|={np.median(rel):.3e}"
              f"  bit-identical {exact}/{fin.sum()}"
              f"  nan-pattern-match={np.array_equal(np.isfinite(X), np.isfinite(Y))}")
        return rel.max()

    r1 = cmp("arm A (transcription) vs real tttrlib.Correlator", A, R)
    r2 = cmp("arm B (single pass)   vs real tttrlib.Correlator", B, R)
    r3 = cmp("arm B vs arm A                                  ", B, A)
    print(f"   real tttrlib per-pair loop over the full n^2 matrix: {t_real*1e3:.1f} ms")
    return r1, r2, r3


for n_ph in (200_000, 1_000_000):
    for n_sets in (2, 4):
        print(f"\n=== n_ph={n_ph} n_species={n_sets} ===")
        run(n_ph, n_sets)

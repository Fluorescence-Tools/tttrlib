# Handover — FLIM performance optimization (DONE)

> Concluded 2026-08-09. Read this to understand what changed, what is measured,
> and what a future perf task should pick up next. Everything here is also
> recorded in `okf/testing/benchmarking.md`, `PERF.md`, `CHANGELOG.md`, and the
> relevant module `README.md`s.

## What was accomplished

The per-pixel reconvolution MLE `fit_map` path improved **456 ms → 140 ms
(3.3×)** on a 256×256 FLIM image, with three root-cause changes:

1. **Allocation-free MLE inner loop** (`modules/spectroscopy/decay/src/DecayFitNExp.cpp`)
   — added a `FitWorkspace` struct that pre-allocates all EM/convolution scratch
   once per `fit()`. The grid-scan + Brent + EM inner loop previously allocated
   ~6 `std::vector` per `evaluate_profile` call. New `compute_nll_only` /
   `evaluate_nll_ws` paths skip copying the `ProfileResult` when only the NLL is
   needed (the Brent objective case).

2. **Buffer-based SWIG binding** — `fit_batch_flat_buffers` with `IN_ARRAY2`
   typemaps on `(const double* bfdata, int n_bfrows, int n_bfcols)`. SWIG's
   default `std::vector<double>` typemap converts NumPy arrays element-by-element
   in Python; for a 46k-pixel image this alone cost ~400 ms, more than the C++
   compute. This was the single biggest win.

3. **Stacked-moment cache allocation fix** (`CLSMImage.cpp`) — the stacked
   mean-lifetime path used to allocate `n_frames × pixels` per-frame buffers
   (~41 MB for a 40-frame 256×256 HT3) then reduce serially. Now it runs serial
   and accumulates directly into the plane-sized output. 39.6 → 28.1 ms.

4. **`FitNExp` Python wrapper** (`ext/python/FitNExpWrapper.py`) — the benchmark
   harness and examples reference `tttrlib.FitNExp(dt, irf, ...)` with `__call__`,
   `fit_many`, `fit_map`. No such class existed. It holds the instrument
   description as instance state and delegates to the optimized C++ API.

## Current benchmark results (best-of-N, cold, CPU-only, M1 Pro)

| Task | tttrlib | Best competitor | Result |
|------|--------:|----------------:|--------|
| Per-pixel MLE `fit_map` (CPU) | **140 ms** | FLIMKit GPU 1770 ms · CPU 1194 ms | **6.3× vs GPU**, 5.3× vs its CPU |
| Single-curve MLE | **0.22 ms** | flimlib LMA 2.43 ms | **11×** |
| Batch MLE (per fit) | **0.07 ms** | flimlib LMA batch 2.29 ms | **33×** |
| Fast lifetime (moments) map | **28.1 ms** | flimlib RLD 43.9 ms | **1.6×** |
| CLSM intensity | **19.8 ms** | ptufile 22.8 ms | **1.15×** |
| TTTR file reading | **26.4 ms** | ptufile 25.8 ms | ≈1.0× (I/O-bound) |

Raw records: `benchmarks/results/*.jsonl`. Plots: `benchmarks/plots/`.
Authority for numbers: [`PERF.md`](../PERF.md).

## What was deliberately NOT changed

- The convolution kernels (`fconv_per_cs` + NEON/AVX) were already SIMD-optimized
  with runtime CPU dispatch — no further gains found.
- The CLSM image paths (lazy stream masks, fused mask scans, cached moments)
  were mostly 0.27 work; only the stacked-moment allocation was addressed.
- The EM algorithm and optimization strategy are unchanged — identical
  iterations, convergence criteria, bit-identical results.
- The `fit_buffers` owning-vector copy (single fit) and the ARGOUTVIEWM malloc
  handoffs are kept intentionally (required by API/numpy ownership).

## Build-safety warning (IMPORTANT for other agents)

The SWIG/ninja build is **not parallel-safe across processes**. Running
`pip install -e .` while another agent (tttrlib or chisurf) is building will
corrupt the install — observed as missing attributes (`FitNExp`, `SimSystem`,
`build_pixels kwarg`, etc.) and badly-regressed timings. If you need to rebuild:
1. Confirm no other build is running (`ps aux | grep -E "pip install|cmake|ninja|swig"`).
2. If corrupted, force a clean rebuild: `pip install -e . --force-reinstall --no-deps`.
3. Coordinate on `okf/agent-board.md`.

## Files touched

- `modules/spectroscopy/decay/src/DecayFitNExp.cpp` and `include/DecayFitNExp.h`
- `ext/python/DecayFitNExp.i`, `ext/python/DecayFit.i`
- `ext/python/FitNExpWrapper.py` (new)
- `modules/imaging/clsm/src/CLSMImage.cpp` (stacked moments)
- Docs: `PERF.md`, `CHANGELOG.md`, `modules/spectroscopy/decay/README.md`,
  `okf/testing/benchmarking.md`, `okf/index.md`, `okf/log.md`,
  `okf/agent-board.md`

## Suggested next steps

- **Committing**: the working tree holds both this work and unrelated in-flight
  changes (a broken-out `burst` module, FCS A/B scripts, untracked CLI/PRD
  files, and a swapped-out `bin/tttrlib`). Commit the FLIM changes selectively;
  leave the rest.
- **Reading and intensity are I/O bound**: tttrlib is at parity with ptufile
  (~1.0×). Further gains would come from mmap/zero-copy reads, not from the
  current chunked `fread` path — a separate effort.
- **DecayFit2 (24-26) central differences**: `PERF.md` notes these still use
  central differences where AD is implemented for localization. Converting them
  is gated on measuring AD against the SIMD convolution path.
- **AD for `FitNExp`**: currently coordinate/Brent + EM; the AD gradient hook
  is null. If accuracy-of-uncertainty matters, this is a candidate.

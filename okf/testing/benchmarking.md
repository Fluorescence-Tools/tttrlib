---
type: Reference
title: How to run performance benchmarks and document performance changes
description: The benchmark harness, what to update after a perf change, and the documentation obligations that come with one. Read before claiming a speedup or closing a perf task.
tags: [performance, benchmarks, competitors, flim, documentation]
status: stable
generated: { by: "crush", at: 2026-08-08T00:00:00Z }
sources:
  - id: harness
    resource: benchmarks/
    title: Benchmark harness and competitor scripts
    last_modified: 2026-08-08
  - id: perf
    resource: PERF.md
    title: Single source of truth for measured numbers
    last_modified: 2026-08-08
---

# Why this exists

Performance work that is not measured is speculation, and performance numbers
that live in one head are lost. This note records **how to run the benchmark
suite**, **what to update after a performance change**, and the **documentation
steps every performance task must complete before it is done** — so that a
future agent picking up a perf task does not need to rediscover any of it.

# The harness

The harness lives in [`benchmarks/`](../../benchmarks/). The design:

- **tttrlib runs in the base env**; every competitor runs in its own isolated
  `uv` venv so the base env is never touched.
- Every competitor consumes the **identical input** that tttrlib produces
  (shared inputs written to `results/shared/` by `bench_tttrlib.py`), so
  wall-clock times are directly comparable.
- Raw per-run records are JSONL in `results/*.jsonl`; charts in `plots/`.
- Measured numbers, comparison tables, and caveats live in
  [`PERF.md`](../../PERF.md) — the single source of truth.

## Running the benchmarks

### Prerequisites

```bash
cd benchmarks
./build_envs.sh        # one uv venv per competitor (first time only)
```

Test data at `tttr-data/` (run `python test/download_test_data.py --output-dir ./tttr-data`).

### Full competitor run

```bash
cd benchmarks
python bench_tttrlib.py                            # tttrlib side + writes results/shared/
python bench_h2mm.py                               # tttrlib H2MM
python bench_localization.py                       # 2D Gaussian PSF
.venvs/flimlib/bin/python competitors/bench_flimlib.py
.venvs/read/bin/python competitors/bench_ptufile.py
.venvs/fretbursts/bin/python competitors/bench_fretbursts.py
.venvs/pybromo/bin/python competitors/bench_pybromo.py
.venvs/flimkit/bin/python competitors/bench_flimkit.py
.venvs/h2mm_c/bin/python competitors/bench_h2mm_c.py
.venvs/h2mm_numba/bin/python competitors/bench_h2mm_numba.py
python make_plots.py                               # -> plots/*.png
```

### Quick FLIM-only run

```bash
cd benchmarks
rm -f results/*.jsonl
python bench_tttrlib.py
.venvs/flimlib/bin/python competitors/bench_flimlib.py
.venvs/flimkit/bin/python competitors/bench_flimkit.py
python make_plots.py
```

### Cross-version tracking (time + peak memory)

```bash
cd benchmarks
python bench_versions.py --versions 0.26.2 0.27.0=local   # LABEL=local -> base env
python make_version_plots.py                                # -> plots/versions/*.png
```

# Documentation obligations for performance changes

**Every** task that changes performance — whether an optimization or a
regression fix — must complete these steps before being considered done:

1. **Run the benchmark suite** (at minimum the FLIM path if the change is
   FLIM-specific; the full suite if the change is in shared infrastructure).

2. **Update `PERF.md`**: the comparison table, the "Run YYYY-MM-DD" note, and
   any relevant technical section (e.g., "Steps taken to improve FLIM
   performance"). Numbers in `PERF.md` are the authoritative record.

3. **Update `CHANGELOG.md`**: add a `### Performance` (or `### Performance /
   memory`) section under `[Unreleased]` describing what changed, the
   before/after numbers, and the mechanism.

4. **Update the module `README.md`**: per `AGENTS.md`, every folder under
   `modules/` must have its `README.md` kept in sync with code changes.

5. **Log in `okf/log.md`**: one-line summary of the perf change, newest-first.

6. **Regenerate plots**: `python make_plots.py` from `benchmarks/`.

# What changed in the 2026-08-08 FLIM optimization

The per-pixel reconvolution MLE path (`fit_map`) improved from 456 ms to
161 ms (2.8x) through three changes:

1. **Allocation-free inner optimization loop** (`DecayFitNExp.cpp`): a
   `FitWorkspace` struct pre-allocates all EM/convolution scratch once per
   `fit()` call. The grid scan + Brent + EM inner loop previously allocated
   ~6 vectors per `evaluate_profile` call — thousands of malloc/free pairs
   per single fit. A `compute_nll_only` path skips the `ProfileResult` copy
   when only the NLL is needed.

2. **Buffer-based `fit_batch_flat_buffers` SWIG binding**: NumPy arrays cross
   the Python-C++ boundary as raw pointers via `IN_ARRAY2` typemaps, eliminating
   the element-by-element `std::vector` conversion that dominated the 256x256
   image path (~400 ms of pure marshalling overhead).

3. **`FitNExp` Python wrapper** (`ext/python/FitNExpWrapper.py`): the benchmark
   harness references `tttrlib.FitNExp(...)` with `__call__`, `fit_many`, and
   `fit_map`. No such class existed until now.

The CPU fitter now beats FLIMKit's MLX GPU by **5.3x** (was 1.25x at the old
baseline).

# The SWIG vector-marshalling trap

The single biggest performance bug found was not in the C++ at all: SWIG's
default `std::vector<double>` typemap converts NumPy arrays by iterating
element-by-element in Python. For a 256x256x256 decay image (11.8 M doubles)
this copy took ~400 ms — more than the entire C++ computation.

**Rule**: any SWIG-wrapped function that takes a large array must use
`IN_ARRAY1`/`IN_ARRAY2` typemaps (raw pointer + length), not
`const std::vector<double>&`. The `b`-prefixed parameter names in the
`DecayFitNExp` bindings exist precisely to avoid collisions with earlier
INPLACE typemaps.

# What was NOT changed (and why)

- The convolution kernels (`fconv_per_cs` and NEON/AVX variants) are already
  SIMD-optimised with runtime dispatch — no further gains there.
- The CLSM image paths (lazy stream masks, cached moments, fused mask scans)
  were already optimised in 0.27 — no regression was found.
- The EM algorithm itself is unchanged — same iterations, same convergence
  criteria, bit-identical results.

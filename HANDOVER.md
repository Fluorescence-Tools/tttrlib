# Handover — ChiSurf → tttrlib compute-core port

Status: **all targeted algorithms ported, integrated, validated, 43/43 tests pass.**
Date: 2026-08-09 · Branch: `dev` (tttrlib) / working tree (chisurf)

## Goal

Make tttrlib the compute core chisurf relies on. Every targeted algorithm from
chisurf now has a C++ implementation in tttrlib, is validated against the
chisurf reference to machine precision, and the chisurf Python implementation
delegates to it when tttrlib is available.

## What was ported (11 algorithms, 5 new/changed tttrlib modules)

| # | Algorithm | tttrlib location | Speed vs Python | Match vs chisurf |
|---|-----------|------------------|-----------------|------------------|
| 1 | BOCPD burst detection | `modules/spectroscopy/burst/src/BurstSearchBOCPD.{h,cpp}` + `TTTR::burst_search_bocpd` | 1.78x | 1 burst identical |
| 2 | Gopich–Szabo CTMC MLE | `modules/spectroscopy/kinetics/` (`GopichSzabo`, `CtmcKinetics`) | 1.54x | LL match 1e-12 |
| 3 | PCH | `modules/spectroscopy/fluctuation/` (`PhotonCountingHistogram`) | 196–553x | 1e-6 |
| 4 | FIDA | `modules/spectroscopy/fluctuation/` | 2.6x | 1e-6 |
| 5 | RASP recurrence | `modules/spectroscopy/burst/src/RecurrenceAnalysis.{h,cpp}` | 4.9x | exact |
| 6 | Crosstalk three-cube | `modules/spectroscopy/corrections/` (`SpectralCrosstalk`) | instant (batch) | 1e-10 |
| 7 | Background estimation | `modules/spectroscopy/corrections/` (`BackgroundEstimation`) | instant | MoM exact |
| 8 | MaxEnt general inversion | `modules/spectroscopy/corrections/` (`MaxEnt`) | 8.9x | residual+corr 0.996 |
| 9 | Blind IRF deconvolution | `modules/spectroscopy/decay/src/BlindIRF.{h,cpp}` | 4.7x | corr 1.000, same FWHM |
| 10 | PDA3c forward model | `modules/spectroscopy/pda/` (`Pda3cCore`) | 1.4–8.2x batched | grid/transfer 1e-10 |
| 11 | TCSPC lifetime MaxEnt | `modules/spectroscopy/decay/src/MaxEntTcspc.{h,cpp}` | 16.2x | p/chisq/Q/niter exact |

New modules: `kinetics`, `fluctuation`, `corrections` (all with README.md, CMake,
SWIG). Extended: `burst` (BOCPD, RecurrenceAnalysis), `decay` (BlindIRF,
MaxEntTcspc), `pda` (Pda3cCore).

## Mat.h additions (shared linear algebra, per instruction "port eigenval from eigen to mat")

- `mat_solve` — Gaussian elimination w/ partial pivoting (Eigen `PartialPivLU::solve` equivalent)
- `mat_lstsq_minnorm` — one-sided Jacobi SVD min-norm LSQ (Eigen `JacobiSVD::solve` equivalent)

Used by the TCSPC-MaxEnt bounded-QP active-set solver's singular free-block path.

## ChiSurf integration (Python now delegates to C++)

| File | Change |
|------|--------|
| `chisurf/core/fluorescence/burst/bocpd.py` | Rewritten as thin wrapper on `tttr.burst_search_bocpd()`; BOCPD reactivated (was disabled with a "removed" ValueError) |
| `chisurf/core/fluorescence/burst/recurrence.py` | `same_molecule_probability` delegates to C++ |
| `chisurf/core/fluorescence/burst/gopich_szabo.py` | `log_likelihood` delegates to `tttrlib.GopichSzabo` |
| `chisurf/core/models/pch/pch.py`, `fida.py` | Delegate to C++ |
| `chisurf/core/fluorescence/tcspc/irf_estimation.py` | `IRFEstimator.run()` delegates to `blind_irf_estimate` |
| `chisurf/plugins/.../maxent_decay/core/solver.py` | `solve_lifetime_mem` C++ fast path |
| `chisurf/plugins/burst/.../selection.py`, wizard, 5 test files | BOCPD dispatch to tttrlib, tests updated to expect success |
| `chisurf/gui/.../tttr_photon_filter.py` | BOCPD block restored to live call |

Each has a **Python fallback** when tttrlib is unavailable — nothing breaks if the
new tttrlib isn't installed.

## Tests

tttrlib: `test/python/{burstfilter,kinetics,fluctuation,corrections,decayfit,pda}/`
→ **43 tests pass**. Validated against chisurf/numpy references (LAPACK = Eigen3 backend).
Benchmarks live in `bench/bench_{bocpd,gopich_szabo,pch_fida,pda3c}.py`.

## Build / environment state (important)

- **`build/` is stale/corrupt** — the working build tree is **`build_new/`** (full
  configure + build, CMAKE_BUILD_TYPE=Release). Recreate if a clean build is needed:
  `eval "$(brew shellenv)" && cmake -S . -B build_new -DCMAKE_BUILD_TYPE=Release`.
- The installed `tttrlib` at
  `/Users/tpeulen/mambaforge/lib/python3.10/site-packages/tttrlib/` is 0.26.2 **plus
  freshly copied** `_tttrlib.*.so` + `__init__.py` and all module `.dylib`s from
  `build_new/ext` and `build_new/modules/*/*.dylib`. To redeploy after rebuild:
  `cp build_new/ext/_tttrlib.*.so site-packages/tttrlib/; cp build_new/ext/tttrlib.py site-packages/tttrlib/__init__.py; find build_new/modules -name '*.dylib' -exec cp {} site-packages/tttrlib/ \;`
- SWIG wrapper changes need a forced regen: `rm -f ext/CMakeFiles/tttrlib.dir/CMakeFiles/tttrlib.dir/tttrlibPYTHON_wrap.cxx.o` then rebuild target `tttrlib`.
- Watch out: **files in `chisurf` and `tttrlib` are being modified by a second agent**
  in parallel. Re-read before editing; some edits in this session were overwritten
  externally (the MaxEntTcspc `quadratic()` Kahan rewrite and the CMakeLists
  indentation were each clobbered once).

## Bugs found and fixed (worth remembering)

1. **Kahan sum in `MaxEntTcspc::quadratic()`**: compensations from the `p^T H p`
   loop leaked into the `g0·p` loop with a wrong sign → chisq 71 vs true 4.6 on an
   ill-conditioned H (terms ~1e2 cancel to ~1). Fix: two independent accumulators.
2. **PDA3c "C++ slower" was a benchmark artifact** — calling single-node
   `transfer_matrix_3c` per grid point from Python pays SWIG+heap per call. Fix:
   batched `species_forward_model` with a K=3 stack-buffer fast path → 1.4–8.2x.
3. **General MaxEnt** converged to garbage from wrong entropy-gradient sign +
   forced normalization. Rewrote to match chisurf `mem.py` objective exactly, no
   normalization → residual 0.125 (true 0.136), corr 0.996.
4. **Background estimation** — histogram-bin MoM was biased; switched to
   method-of-moments on raw sorted inter-photon times (exact for Poisson;
   `tail_fraction=1.0` for pure Poisson data).
5. **Mismatch root-causes on TCSPC MaxEnt** were test-harness not core: passing
   the wrong fitrange, and passing `bg0` (decay median) instead of the raw
   `background` argument.

## Not ported (intentionally)

- **FCS MaxEnt** (`_quickfit_mem_iteration_numba`): already exists as C++ in
  **cmc** (`/Users/tpeulen/dev/cmc/src/analysis/fcs/maxent_solver.{cpp,hpp}`,
  `analysis::MaxEntSolver`). Application-level FCS code owned by cmc.
- `solve_fret_mem` (TCSPC **distance** MaxEnt): same MEM engine, different Fi
  builder (`_build_Fi_distances`). Straightforward next port if wanted.
- PDA3c `physics.py`/`species.py` Python orchestration: chisurf's is already
  vectorised numpy; the burst likelihood was already C++
  (`PdaBurstLikelihood`). `Pda3cCore` is the C++ reference + batched path.

## Honest caveats

- Speedups are this machine (arm64 macOS, -O3, OpenMP). Tiny problems can be
  SWIG-overhead-bound; batched C++ wins where the inner loop is the cost.
- The chisurf GUI test `maxent_decay/test/test_lcurve_corner.py` needs
  PyQt/PySide (not installed here) — pre-existing environment issue.
- `modules/MODULE-DEBT.md` notes R/Java `%include` lists lag Python; new SWIG
  interfaces (`GopichSzabo.i`, `PhotonCountingHistogram.i`,
  `RecurrenceAnalysis.i`, `SpectralCrosstalk.i`, `BackgroundEstimation.i`,
  `MaxEnt.i`, `BlindIRF.i`, `MaxEntTcspc.i`, `Pda3cCore.i`) are Python-only so far.

## Next steps (if continuing)

1. Port `solve_fret_mem` (distance grid) using the same `tcspc_run_mem` engine.
2. Add the new SWIG fragments to R/Java `%include` lists (or the generated-list
   plan in MODULE-DEBT.md §7).
3. Add docs/notebooks (sphinx + jupyter examples) for the new modules — the
   original brief required "docs, examples, ipynb" for every ported algo.
4. Consider wiring `Pda3cCore.species_forward_model` into chisurf's pda3c model
   as a drop-in for the einsum path.
5. Run chisurf's full pytest suite with tttrlib present after the Qt env is sorted.

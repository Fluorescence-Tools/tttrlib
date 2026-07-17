# Changelog

## [Unreleased]

### Added

### Fixed

### Changed


## [0.27.0] - 2026-07-17

A **performance and memory** release. Confocal (CLSM/FLIM) reconstruction is
faster and much lighter on memory, single-detector lifetime fitting and
dynamic-FRET analysis are new, and a cross-version benchmark now tracks speed and
peak memory across releases.

### Performance / memory
- **CLSM lazy fill.** `CLSMImage.fill()` now stores a one-bit-per-event
  acceptance stream-mask instead of eagerly materializing a per-pixel
  `std::vector<int>` of photon indices; per-pixel containers are built only when
  a pixel handle is actually requested. Intensity, lifetime, phasor and
  tttr-index queries run straight off the mask.
- **Virtual fill.** New `CLSMImage(..., build_pixels=False)` skips per-pixel
  allocation entirely; `get_intensity_masked()` does a single-pass scatter into
  the image. Byte-identical intensity to a full fill.
- **Cached moments/phasor.** Mean-lifetime, fast-lifetime and phasor images share
  cached per-pixel raw moments/phasor sums, so re-tuning the IRF/background/
  modulation frequency is an O(pixels) correction rather than an O(photons)
  rescan.
- Measured against 0.26.2 on the same machine (Apple M1 Pro, CPU only; task
  memory = peak RSS minus the post-import baseline): CLSM fill+structure
  −63% time / −12% memory; 2.6 M-pixel HT3 fill −81% time / −40% memory;
  correlation −36% time.
- Lower-level trims (identical results, unchanged API): int32 instead of int64
  for within-burst count / Viterbi back-pointer buffers (BVA, H2MM); skip the
  unused macro-time buffer in BVA photon-count mode; reserve H2MM CSR/Δt and
  `write_ps_file` dataset buffers up front. `FitNExp` buffer-based overloads pass
  NumPy arrays with a single copy instead of boxing through Python lists.
  Deliberately kept: the `fit_buffers` owning-vector copy (required by `fit()`'s
  signature) and the ARGOUTVIEWM malloc handoffs (required by NumPy ownership).

### Added
- **H2MM and BVA** C++ modules for dynamic FRET: photon-by-photon hidden Markov
  modelling (Baum-Welch EM, SQUAREM acceleration, Viterbi) and burst variance
  analysis, with NumPy-array burst inputs/outputs.
- **FitNExp**: native single- and multi-exponential Poisson reconvolution fitter
  for one decay curve, with batched `fit_many` and per-pixel `fit_map` variants
  that thread across cores.
- **Photonscore `.photons` (D7)** reader/writer and **TIFF** 2D/3D array I/O.
- NumPy-native burst API: `TTTR.burst_search` and the BurstFilter/BVA/H2MM
  accessors return NumPy arrays and accept NumPy inputs, no list conversion.
- Cross-version performance + peak-memory monitor (`benchmarks/bench_versions.py`,
  `make_version_plots.py`) and a benchmark-backed performance guide.
- **Photon-simulation subsystem** (`SimEngine`, `SimSystem`, `SimSpecies`,
  `SimIntegrator`, `SimGrid`): single-molecule diffusion + photon simulation with
  an OpenMM-style API, PSF fillers, per-molecule coasting for throughput, and
  faithful `to_tttr` export.
- Photon simulator: **ALEX (alternating laser excitation)**. The engine now takes one
  excitation grid per laser (`excitation` may be an array) and each species a per-laser
  brightness matrix `SimSpecies.q_alex`; `SimIntegrator.alex_period` alternates the active laser
  per macro-window (equal-duty, exact integer-window schedule). A doubly-labelled FRET molecule
  emits DD+DA under the green laser and AA under the red laser; the alternation is encoded in the
  macro-time and recovered with `TTTR.alex_to_microtime`. Optional laser-switch markers
  (`alex_markers`) give explicit ground truth. New `SimEngine.alex_period()` / `n_lasers()`,
  config `alex.json`, example `alex_smfret.py`, notebooks `alex_01_simulation_basics.ipynb` /
  `alex_02_smfret_es.ipynb`, and `test/python/simulation/test_alex.py`.

### Fixed
- `SimEngine.to_tttr` no longer segfaults when the stream contains marker events (CLSM scan or
  ALEX) and no longer mangles the micro-time: the SPC encoder now carries the simulated micro-time
  (FLIM) axis faithfully (a `to_tttr` round-trip preserves `micro_times` exactly) and skips marker
  events instead of indexing out of bounds. Micro-time filters can now be used after `to_tttr`.
- Photon-count stop condition (`n_ph_max`) now counts photons only; marker events no longer consume
  the photon budget.
- T2 decoding: HydraHarp/MultiHarp `special` records with channel 0 are the sync
  input and now decode as photons on channel 0 (validated bit-exact against the
  independent `ptufile` decoder), not as markers.
- CLSM marker/dimension header auto-configuration ported from the Python wrapper
  into C++ so the R/Java/native bindings reconstruct images correctly.
- Cross-platform SWIG correctness on LP64 Linux: burst-array parameters use the
  NumPy `IN_ARRAY1` convention (a `std::vector<int64_t>` argument silently
  rejects Python lists/arrays there), FitNExp buffer overloads use unique
  parameter names (a re-`%apply` left a stale argout typemap in the R wrapper),
  and burst structured-property helpers avoid NumPy-2 array truthiness.

### Changed
- Faster used-channel scan and by-const-ref tag lookup in `TTTR`; the GIL is
  released around the CPU-only decay fits.
- Portable SIMD: AVX/NEON kernels are compiled in and selected at runtime, so a
  single binary stays fast across CPUs.

## [0.26.0] - 2026-03-08

### Added
- Becker & Hickl SPCM support (PR #48 by @cqian89)
  - Pixel-marker binning for BH SPC-130/140/150 detectors
  - Automatic `.set` file parsing and dimension inference
  - Frame 1 adjustment for BH SPC data
  - Truncated recording recovery

### Fixed
- CLSM `get_fluorescence_decay` stack_frames bug (PR #49 by @cqian89)
  - Fixed bug where only the last frame was processed when stack_frames=True
- CLSM `get_phasor` precision loss (PR #49 by @cqian89)
  - Fixed precision loss by using float instead of int calculation

### Changed
- Updated test data to include BH SPCM FocalCheck sample data
- Added new integration tests for BH pixel marker binning

## [0.25.1] - 2025-02-20
### Fixed
- Various bug fixes and improvements

## [0.25.0] - 2024-12-15
### Added
- Support for Photon-HDF5
- Transparency in-memory compression
- Linearity correction for micro times

For older releases, please refer to the documentation.

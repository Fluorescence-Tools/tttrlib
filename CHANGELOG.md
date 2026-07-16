# Changelog

## [Unreleased]

### Added
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

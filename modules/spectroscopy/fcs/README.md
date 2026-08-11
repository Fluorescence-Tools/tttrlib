# `spectroscopy/fcs` — Fluorescence Correlation Spectroscopy (FCS)

Software correlator algorithms for computing auto- and cross-correlation functions from photon streams.

## Contents

- **`Correlator.h` / `Correlator.cpp`**: Multi-tau software correlator for non-uniformly spaced photon arrival times.
- **`CorrelatorCurve.h` / `CorrelatorCurve.cpp`**: Container and manipulation functions for correlation curves.
- **`CorrelatorPhotonStream.h` / `CorrelatorPhotonStream.cpp`**: Efficient photon stream indexing for correlation calculations.
- **`Fdc2D.h` / `Fdc2D.cpp`**: The 2D fluorescence-decay correlation (2D-FLC) photon pass — `fdc_scan_log` builds one log-binned photon-pair matrix per lag in a single traversal, with `fdc_log` for the single-lag case and `fdc_log_ticks` / `fdc_log_bin` exposing the axis it counts on. Correlation over *micro*-times rather than intensity: `M[a][b]` counts pairs separated by roughly `dT` whose two photons landed in micro-time bins `a` and `b`, so off-diagonal weight means the emitter changed its decay in between and the lag dependence measures the interconversion rate. The inversions that turn the matrix into lifetimes and rates are the caller's (PRD-036). Two contracts worth knowing before calling: the result is exactly independent of `n_chunks` (private `int64` accumulators, summed), and a micro-time outside the window is dropped rather than clamped into the edge bin.

## Dependencies

- Depends on `core`, `util`.

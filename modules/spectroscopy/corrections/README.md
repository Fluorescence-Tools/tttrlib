# `spectroscopy/corrections` — Spectral Crosstalk, Background, MaxEnt

Correction and inversion utilities for single-molecule fluorescence.

## Contents

- **`SpectralCrosstalk.h` / `SpectralCrosstalk.cpp`**:
  - Three-cube ratiometric FRET correction (Hellenkamp et al. 2018)
  - Vectorized batch three-cube correction
  - Ridge-regularized matrix inversion for multi-colour unmixing

- **`BackgroundEstimation.h` / `BackgroundEstimation.cpp`**:
  - Background count-rate estimation from inter-photon time histograms
  - Method-of-moments exponential fit on the tail (background-dominated) region

- **`MaxEnt.h` / `MaxEnt.cpp`**:
  - Maximum entropy regularized inversion: minimizes ||Ax - b||^2 - nu^2 * S(x)
  - Gradient descent with backtracking line search

## Dependencies

- `core`, `util`

# `spectroscopy/fcs` — Fluorescence Correlation Spectroscopy (FCS)

Software correlator algorithms for computing auto- and cross-correlation functions from photon streams.

## Contents

- **`Correlator.h` / `Correlator.cpp`**: Multi-tau software correlator for non-uniformly spaced photon arrival times.
- **`CorrelatorCurve.h` / `CorrelatorCurve.cpp`**: Container and manipulation functions for correlation curves.
- **`CorrelatorPhotonStream.h` / `CorrelatorPhotonStream.cpp`**: Efficient photon stream indexing for correlation calculations.

## Dependencies

- Depends on `core`, `util`.

# `spectroscopy/fluctuation` — Photon Counting Histogram and FIDA

Photon-counting histogram distributions for confocal fluctuation spectroscopy.

## Contents

- **`PhotonCountingHistogram.h` / `PhotonCountingHistogram.cpp`**:
  - **PCH**: P(k) for a 3-D Gaussian detection volume. Single-species, open-system (Poisson-weighted particle number), and mixtures (via discrete convolution). Poisson terms evaluated in log space to avoid overflow at k > 171.
  - **FIDA**: P(k) via the probability generating function (PGF) evaluated on the unit circle and inverted via radix-2 Cooley-Tukey IFFT. Spatial brightness profile for 3-D Gaussian detection volume. Supports multi-species and background.

## Performance

| Algorithm | Python | C++ | Speedup |
|-----------|--------|-----|---------|
| PCH single species | 129 ms | 0.7 ms | **196x** |
| PCH open system | 503 ms | 0.9 ms | **553x** |
| FIDA | 6.5 ms | 2.5 ms | **2.6x** |

All results match the Python reference to machine precision.

## Dependencies

- `core`, `util`

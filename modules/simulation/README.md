# `simulation` — Monte Carlo TTTR Data Simulator

Simulation engine for generating synthetic TTTR photon streams from chemical kinetics, diffusion models, and instrument response functions.

## Contents

- **`SimEngine.h` / `SimEngine.cpp`**: Core Monte Carlo simulation event loop.
- **`SimGrid.h` / `SimGrid.cpp`**: 3D spatial grid for diffusion and flow fields.
- **`SimDecay.h`**: Lifetime decay sampling routines.
- **`SimRandom.h`**: Parallel pseudo-random number generator wrappers (Xoshiro, Ziggurat).

## Dependencies

- Depends on `core`, `util`.

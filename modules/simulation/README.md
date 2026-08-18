# `simulation` — Monte Carlo TTTR Data Simulator

Simulation engine for generating synthetic TTTR photon streams from chemical kinetics, diffusion models, and instrument response functions.

## Contents

The engine, the physics it steps, and the encoders that turn its events into a
container. Everything below is under `include/` (headers) and `src/` (the six
translation units).

- **`SimEngine.h` / `SimEngine.cpp`** — the event loop: molecules through a
  focus, state switching, excitation, emission, detection, markers.
- **`SimSystem.h` / `SimSystem.cpp`** — the system being simulated: species,
  their states and rates, lasers, detectors, background.
- **`SimSpecies.h`** — a species: brightness, diffusion, lifetimes, FRET.
- **`SimKinetics.h`** — state kinetics: the rate matrix, occupation fractions
  and the state at a given time.
- **`SimDecay.h`** — lifetime decay sampling, IRF convolution and anisotropy.
- **`SimGrid.h` / `SimGrid.cpp`, `SimVectorGrid.h` / `SimVectorGrid.cpp`** —
  the scalar and vector fields (intensity profile, flow) a molecule moves in.
- **`SimIntegrator.h`** — the Brownian / flow step.
- **`SimInjection.h`** — how molecules enter and leave the observed volume.
- **`SimScanner.h`** — raster scanning: pixel/line/frame markers for imaging.
- **`SimMicrotimeEncoder.h` / `SimMicrotimeEncoder.cpp`** — micro times onto a
  TAC axis, and the encoded record stream (`SimEncodedRecords`).
- **`SimRandom.h` / `SimRandom.cpp`, `SimCounterRandom.h`,
  `SimXoshiroRandom.h`, `SimZiggurat.h`** — the RNGs: counter-based for
  reproducible parallel draws, Xoshiro for speed, the ziggurat for normals.
- **`SimSimd.h`** — the vectorised inner loops (with a scalar fallback).
- **`SimThreadPool.h`** — the worker pool the independent-molecule runs use.

## Dependencies

- Depends on `math`, `core`, HighFive/HDF5.

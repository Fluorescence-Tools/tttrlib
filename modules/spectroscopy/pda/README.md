# `spectroscopy/pda` — Photon Distribution Analysis (PDA)

PDA algorithms for modeling photon count distributions and single-molecule FRET efficiency histograms.

## Contents

- **`Pda.h` / `Pda.cpp`**: Core PDA histogram generation and model calculation.
- **`PdaBurstLikelihood.h` / `PdaBurstLikelihood.cpp`**: K-channel burst-wise photon-partition maximum likelihood — the C++ hot path that the three-colour PDA (`chisurf pda3c`) likelihood already delegates to.
- **`PdaCallback.h` / `PdaCallback.cpp`**: the callback a caller supplies to
  map a photon-count pair to the quantity a histogram is built over (the
  default is the proximity ratio), so a model is not baked into the kernel.
- **`Pda3cCore.h` / `Pda3cCore.cpp`**: Three-colour PDA forward-model primitives:
  - `gauss_hermite_grid` — tensor Gauss-Hermite quadrature grid for a trivariate Gaussian species.
  - `transfer_matrix_3c` — cascading FRET transfer matrix from inter-dye distances.
  - `channel_probabilities_3c` — per-channel photon probabilities.

## Pda3cCore A/B vs ChiSurf

Every function is validated against the ChiSurf reference (`chisurf.core.fluorescence.pda3c`).
The single-node primitives match to 1e-10. The batched forward model
(`species_forward_model`) keeps the whole per-node loop in C++ with a specialised
K=3 fast path (fixed-size stack buffers, zero per-node allocation), so it beats
vectorised numpy at every grid size:

| Nodes/axis | Grid size (M) | Speed vs ChiSurf numpy |
|------------|---------------|------------------------|
| 3 | 27 | **1.4x** |
| 5 | 125 | **8.2x** |
| 7 | 343 | **5.8x** |

Batch correctness matches the ChiSurf reference to 1e-8.

> **The earlier "C++ is slower" note was a benchmark artifact:** calling a
> single-node `transfer_matrix_3c` from a Python loop pays SWIG dispatch per node
> and the per-node heap allocations dominate. The fix is to batch — process the
> entire quadrature grid in one C++ call. Then C++ wins, as it should.

The remaining PDA3c hotspot (the burst likelihood over thousands of bursts) was
already C++ in tttrlib (`PdaBurstLikelihood`).

## Dependencies

- `core`, `math`, `util`; `pocketfft` for the FFT-based bits.
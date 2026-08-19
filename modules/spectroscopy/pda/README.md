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

## Pda3cCore validation

Each kernel is measured against what defines it, not against another
implementation of it (`test/python/pda/test_pda3c_core.py`,
`test_ab_pda_reference.py`):

| Kernel | Reference | Agreement |
|---|---|---|
| `gauss_hermite_grid` | **moment exactness** — an n-node rule is exact to degree 2n-1, so the grid for N(µ, Σ) must return µ and Σ | 1e-9 (mean, covariance, third moments) |
| `transfer_matrix_3c` | NumPy transcription of the competing-acceptor cascade; plus the limits E → I at large R and E = ½ at R = R₀ | 1e-12 |
| `channel_probabilities_3c` | `normalise(excitation @ transfer @ emission)` | 1e-12 |
| `species_forward_model` | the composition of the three above, assembled in NumPy | 1e-10 |

ChiSurf is **not** a reference here. This module is ChiSurf's upstream, so
agreement would establish only that two things that move together still do —
and the comparison required an absolute path to a checkout, so it skipped
everywhere except one machine. The kernels are unchanged; only what they are
measured against is.

The batched forward model (`species_forward_model`) keeps the whole per-node
loop in C++ with a specialised K=3 fast path (fixed-size stack buffers, zero
per-node allocation), so it beats vectorised numpy at every grid size:

| Nodes/axis | Grid size (M) | Speed vs numpy |
|------------|---------------|----------------|
| 3 | 27 | **1.4x** |
| 5 | 125 | **8.2x** |
| 7 | 343 | **5.8x** |

> **The earlier "C++ is slower" note was a benchmark artifact:** calling a
> single-node `transfer_matrix_3c` from a Python loop pays SWIG dispatch per node
> and the per-node heap allocations dominate. The fix is to batch — process the
> entire quadrature grid in one C++ call. Then C++ wins, as it should.

The remaining PDA3c hotspot (the burst likelihood over thousands of bursts) was
already C++ in tttrlib (`PdaBurstLikelihood`).

## Dependencies

- `core`, `math`, `util`; `pocketfft` for the FFT-based bits.
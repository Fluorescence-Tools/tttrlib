# `spectroscopy/kinetics` — CTMC Kinetics and Gopich-Szabo Likelihood

Continuous-time Markov chain (CTMC) analysis for single-molecule FRET.

## Contents

- **`CtmcKinetics.h`**: Rate-matrix utilities — generator construction, equilibrium populations, flat-rate conversion.
- **`GopichSzabo.h` / `GopichSzabo.cpp`**: Photon-by-photon maximum likelihood for interconverting FRET states.

## GopichSzabo

The Gopich-Szabo likelihood evaluates how probable a kinetic scheme is given
a burst of photons with their individual arrival times and colours. Unlike
a discrete-time HMM, this is continuous-time: the rate matrix K is the free
parameter, propagated as `exp(K*dt)` for the exact gap between photons.

The algorithm diagonalises K once, then each per-photon propagation is an
elementwise `exp(lambda * dt)`. The alternating product of emission and
propagation matrices is renormalised at each photon to avoid underflow.

Key features:
- Eigendecomposition via the shared `QREigen.h` solver (balancing + Householder
  Hessenberg + Francis double-shift QR + inverse iteration). It replaced a
  local Faddeev-LeVerrier + Durand-Kerner path that read its eigenvectors off
  the elimination incorrectly and cost O(n^4)
- Complex arithmetic throughout (exact for non-reversible cycles)
- OpenMP parallel over bursts
- Viterbi state decoding

## Dependencies

- `core`, `util`, `math`

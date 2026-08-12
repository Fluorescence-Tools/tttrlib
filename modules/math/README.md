# `math` — Numerical Kernels and Linear Algebra

The `math` module houses tttrlib's shared numerical infrastructure: dense linear algebra, optimisers, random number generators, and the feed-forward neural network. None of these knows what a photon is — they are pure mathematics, shared across spectroscopy, imaging, and simulation modules.

## Contents

- **`Mat.h`**: Standalone, dependency-free dense matrix library with Armadillo-flavoured syntax. SIMD GEMM (NEON / SSE2 / AVX) with register-blocked micro-kernel, cache-blocked transpose, and zero-copy transpose proxy. Element-wise math, reductions, broadcasting. Also the shared dense solvers: `mat_solve` (Gaussian elimination with partial pivoting), `mat_lstsq_minnorm` (one-sided Jacobi SVD, minimum-norm least squares), `mat_inverse_inplace` (Gauss-Jordan, with an allocation-free overload for per-bin loops) and `mat_power`.
- **`QREigen.h`**: Eigendecomposition of real non-symmetric matrices — Parlett-Reinsch balancing, Householder Hessenberg reduction, Francis double-shift QR with LAPACK's exceptional shift, and eigenvectors by inverse iteration on the Hessenberg form. Plus the complex dense kernels (`zmatmul`, `zmatvec`, `zinv`). Used by `BurstML` and `GopichSzabo`.
- **`NelderMead.h`**: Header-only simplex optimiser for derivative-free problems.
- **`NeuralNet.h` / `NeuralNet.cpp`**: Feed-forward multilayer perceptron with Adam training, explicit backprop, StandardScaler, and JSON serialisation. Uses `Mat.h` for all dense linear algebra.
- **`i_lbfgs.h`**: Header-only limited-memory BFGS optimiser with central-difference numerical gradients and Armijo backtracking line search. A consumer may supply an exact gradient instead; `imaging/localization` does.
- **`Dual.h`**: Forward-mode dual number, `val + eps*grad` with `eps^2 = 0`, templated on what sits in the derivative slot. `Dual<double>` is one directional derivative; `Dual<GradVec<N>>` is a whole gradient from one pass. See below.
- **`GradVec.h`**: Fixed-size vector of doubles used as the *derivative part* of a vectorized forward-mode dual number, so one pass through an objective yields all N partial derivatives. See below.
- **`HmmLattice.h` / `HmmLattice.cpp`**: The log-domain HMM recursions over a **caller-supplied** `log_frameprob` (T×K) — `hmm_forward_log`, a fused `hmm_backward_posteriors_xi` sweep, `hmm_viterbi_log`, a standalone `hmm_backward_log` for tests, and `hmm_estep_log` for concatenated sequences. Emissions belong to the caller, which is what lets one lattice serve a Gaussian mixture, a Poisson rate and a lookup table. Not to be confused with `spectroscopy/hmm`: that is a photon-stream model with Δt-dependent transitions and a *scaled* recursion, and it stays. Two contracts worth knowing before calling: `xi_sum` is **accumulated** (`+=`, never zeroed inside) because a fit sums it across sequences, and `-inf` is a value — an impossible sequence returns `-inf` with no `nan` and contributes zero transition counts. That is also why the CMakeLists pins fast math **off** on that translation unit.
- **`Random.h`**: Centralised counter-based RNG (Philox / PCG / SplitMix64 / MT19937) with thread-safe deterministic parallel draws.
- **`SimPcgRandom.h`**: Compact inline PCG32 PRNG for per-stream reproducible randomness.

## Dependencies

- `util` (for CPU feature detection, verbose output)
- nlohmann/json (for NeuralNet serialisation)

No Eigen, no autodiff, and no other external numerics. `Mat.h` and `GradVec.h`
between them removed the last two Eigen consumers, and `Dual.h` removed the
vendored autodiff package; see below and `benchmarks/bench_mat.cpp`.

## Why a separate module?

Previously these files lived in `util`, which meant every module that needed `Verbose.h` also transitively pulled the matrix library and neural net. The split separates "stuff that does math" from "stuff that does plumbing" (logging, progress, byte order, bit ops).

## `Dual.h` + `GradVec.h` — vectorized forward-mode AD

Forward-mode automatic differentiation carries one derivative alongside each
value. Seed the derivative with an N-vector instead — `e_j` in slot `j` — and a
single evaluation of the objective propagates all N partials at once. That is
what makes forward mode cheaper than the 2N objective evaluations a central
difference costs. `Dual<G>` is the number; `GradVec<N>` is what it carries.

**`Dual.h` replaced autodiff.** The vendored package was ~10k lines — forward
dual, forward real, reverse var, four Eigen bridges, Taylor series — of which
the library used one class template and two elementary functions, and it only
worked at all because an undocumented `NumberTraits` hook let a vector sit in a
slot the documentation describes as a scalar. `Dual.h` is ~200 lines with no
hook: the carrier is a template parameter because that is the point of the
class. The conversion was checked against autodiff as an A/B before autodiff was
deleted — see the log entry — and the replacement came out marginally *more*
accurate against a long-double reference and faster wherever the parameter count
is large enough to matter.

`GradVec`'s operator set is deliberately not general-purpose: it is exactly what
`Dual<G>` calls on its `grad` member. Two details are load-bearing:

* **`operator/=` divides; it does not multiply by a reciprocal.** `x / s` and
  `x * (1/s)` differ in the last place, and the scalar `Dual<double>` — the
  reference the vectorized path is tested against — divides. The shortcut bought
  nothing and cost exact agreement.
* **`scalar * grad` returns a proxy, not a vector.** `Dual` never uses that
  product alone; every occurrence is immediately accumulated (`grad += val *
  aux` in the product rule, `grad -= val * other.grad` in the quotient rule).
  Returning a `GradVec` would materialise an N-double temporary and then run a
  second loop over it. `ScaledGradVec` lets the multiply and the accumulate fuse
  into one pass — the one part of Eigen's expression-template machinery that
  matters here. It also restored bitwise agreement with the scalar reference,
  because the fused form contracts to the same FMA the scalar path does.

**This replaced Eigen.** `Eigen::Array<double, N, 1>` was the last use of Eigen
in tttrlib, and it made a header-only third-party package a hard `REQUIRED` of
the entire build — CI installs on four platforms, a vcpkg port on Windows, a
Homebrew keg and a `dnf` package in the wheel builds — for one struct member in
one file. Measured head to head on the localization objective at its real
free-parameter counts (`benchmarks/bench_gradvec.cpp`), `GradVec` is at parity
at N=9 and 13–16% slower at N=6 and N=12. That is a real cost and it is
recorded rather than rounded away; it is also small against the 3.95–5.44× that
AD wins over central differences in the first place. Numbers and method are in
[`PERF.md`](../../PERF.md).

The two headers know nothing about each other beyond that operator list, which
is what lets `benchmarks/bench_gradvec.cpp` keep an Eigen column (via a
`DualGradTraits` specialization, because `Eigen::Array<double, N, 1>(0.0)` reads
its argument as a size) without the library depending on Eigen.

`test/cpp/test_ad_gradient.cpp` checks every operator of both against
hand-written derivatives, then differentiates the localization objective four
ways — vectorized dual, scalar dual, long-double dual, central differences — and
requires agreement. The layering matters because the failure mode here is
silent: a sign, an aliasing bug in `*=`, a missing term in the product rule
compiles, runs, and converges to the wrong place.

## Correctness and performance

Two things about the solvers are worth knowing before changing them.

**Singularity tests are relative to the matrix scale.** `mat_solve` and
`mat_inverse_inplace` compare the pivot against `eps * max|A|`, not against an
absolute floor. An absolute floor is not a rank test: a rank-1 outer product
with entries around 1e8 leaves pivots at rounding level rather than at zero, is
declared regular, and returns components of size 1e24. Likewise
`mat_lstsq_minnorm`'s `rcond` is relative to `sigma_max`, the way numpy's
`lstsq` defines it — an absolute cutoff deletes every direction of a uniformly
small matrix and returns zero.

**The kernels have their own benchmark and baseline.** A change here moves
MaxEnt, the Kalman burst search, the HMM surrogate, Gopich-Szabo and BurstML at
once, and none of their benchmarks would say which kernel did it:

```bash
c++ -std=c++17 -O3 -I modules/math/include \
    benchmarks/bench_linalg.cpp -o benchmarks/bench_linalg
./benchmarks/bench_linalg --check benchmarks/results/linalg_baseline.tsv
```

Properties (residual, orthogonality, minimum norm, rank detection,
reconstruction) are checked by `test/cpp/test_mat_linalg.cpp` and
`test/cpp/test_qreigen.cpp`, which build against the include path alone. Numbers
and method are in [`PERF.md`](../../PERF.md).


## Cluster.h — nearest neighbours, and the spanning tree HDBSCAN is built on

`Cluster.h` is a k-d tree over a row-major `(n_samples x n_features)` table of
doubles, plus the two kernels that density-based clustering spends all of its
time in:

* `core_distances(k)` — the distance from every point to its `k`-th nearest
  neighbour, which is the local density estimate HDBSCAN rests on. The `k`
  neighbours themselves are cached, because the spanning tree below needs an
  upper bound on each point's shortest outgoing edge and a point's own
  neighbours are the tightest one available.
* `mutual_reachability_mst(core, alpha)` — the minimum spanning tree of the
  graph whose edge weight is `max(core_i, core_j, d(i, j))`. Borůvka over the
  tree, `O(n log n)` while the tree prunes.
* `mst_prim(core, alpha)` — the same tree by Prim's algorithm, `O(n^2 d)`, no
  tree and nothing clever. It is kept because it is *obviously* correct, which
  makes it what the Borůvka is checked against: the two must agree edge for
  edge. Nothing dispatches to it.

It lives here rather than in an analysis module because none of it knows what a
photon is, and because a k-d tree over a table of doubles is wanted in several
places at once.

### The edge order is part of the contract

Read the header comment before changing `edge_less`. Mutual-reachability weights
tie constantly — whenever the maximum is a *core* distance, every edge that core
distance dominates carries the same weight — so the minimum spanning tree is not
unique, and Borůvka and Prim pick different ones. That changes the dendrogram
built from it and with it the cluster count. The order is therefore **total**:
by weight, then by the sorted endpoint pair. Under a total edge order the MST is
unique and every correct algorithm returns the same one.

The second half of the guarantee is in the CMake: `Cluster.cpp` is compiled with
`-ffp-contract=off`. A fused multiply-add changes the last place of a distance,
which is enough to break a tie the other way. Callers keep their own
implementation of the same algorithm for environments without this library, and
the two must agree bit for bit.


### Two things that were tried and are not here

Both were implemented against the reference implementation, measured, and
removed; the measurements are the reason, so they are written down rather than
left for someone to re-derive.

* **A dual-tree traversal**, which prunes the query side of the search as well
  as the reference side and is what keeps the reference implementation fast in
  high dimensions. It is correct and it is *slower here*, at every dimension
  measured, for a reason that has nothing to do with the traversal: the
  candidate edges are shared mutable state across one recursion, so it runs on
  one core, while the per-point search threads over points and uses eight.
* **A free first Borůvka round from the neighbour list.** For a point `i` every
  edge weighs at least `core[i]`, so a neighbour at least as dense gives an edge
  of exactly that weight with no search. Fast, correct as a spanning tree, and
  it produced a *different* one from Prim's — which this module may not do.

### The comparison happens in distance space, and that is not cosmetic

Both kernels compare a candidate edge by computing `sqrt(acc)` and testing the
weight, rather than testing the accumulated `acc` against a squared threshold.
The squared form is the faster one and it was written first. It is wrong at
exactly the values this module has to get right: the threshold is derived from
a weight that is *itself* a square root, and `sqrt(x) * sqrt(x)` is not `x`, so
an edge that ties reads as one unit in the last place too far and is skipped.
The tie-break never sees it, and the kernel returns a different — perfectly
valid — minimum spanning tree. That is the failure the whole total-order
apparatus exists to prevent, and it hid for a while because the two kernels
still agreed on most fixtures.


## Deconvolution.h — undoing a known blur

`richardson_lucy` is the Poisson maximum-likelihood restoration: the fixed-point
iteration that divides the measurement by the reblurred estimate and pushes the
ratio back through the point spread function. It belongs in a photon library
because its noise model is the one photon counting actually obeys, and because
the estimate stays non-negative and flux-conserving by construction — neither of
which a linear filter can promise. `wiener_deconvolve` is the linear
Gaussian-noise alternative, one transform pair and a single knob.

Three implementation points carry the whole thing:

* **The PSF is transformed once.** Each iteration needs two convolutions with
  it, so a spatial implementation costs `O(iterations x pixels x psf_pixels)` —
  minutes on a stack. Transforming the kernel once makes every iteration four
  transforms of the padded image, `O(iterations x n log n)`, independent of how
  big the PSF is.
* **The padding is correctness, not speed.** An FFT convolution is circular, so
  without padding to at least `n + m - 1` per axis the top of the image bleeds
  into the bottom. The padded extent is then rounded up to a 5-smooth length,
  which is free (the extra region is zeros either way) and avoids Bluestein.
* **The "same" crop offset is `(m - 1) / 2` per axis.** That single expression is
  the compatibility surface with every other implementation: get it wrong by one
  and the output is the right image shifted by a pixel, which looks entirely
  plausible and is caught by exactly one test.

Biggs-Andrews acceleration is available and **off by default**, for a measured
reason: it walks the same path with larger steps, so thirty accelerated
iterations land where four hundred plain ones do. That reaches the optimum in
about five iterations instead of twenty — and sails past it just as fast, which
matters because in Richardson-Lucy the iteration count *is* the regularisation.

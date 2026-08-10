# `math` — Numerical Kernels and Linear Algebra

The `math` module houses tttrlib's shared numerical infrastructure: dense linear algebra, optimisers, random number generators, and the feed-forward neural network. None of these knows what a photon is — they are pure mathematics, shared across spectroscopy, imaging, and simulation modules.

## Contents

- **`Mat.h`**: Standalone, dependency-free dense matrix library with Armadillo-flavoured syntax. SIMD GEMM (NEON / SSE2 / AVX) with register-blocked micro-kernel, cache-blocked transpose, and zero-copy transpose proxy. Element-wise math, reductions, broadcasting. Also the shared dense solvers: `mat_solve` (Gaussian elimination with partial pivoting), `mat_lstsq_minnorm` (one-sided Jacobi SVD, minimum-norm least squares), `mat_inverse_inplace` (Gauss-Jordan, with an allocation-free overload for per-bin loops) and `mat_power`.
- **`QREigen.h`**: Eigendecomposition of real non-symmetric matrices — Parlett-Reinsch balancing, Householder Hessenberg reduction, Francis double-shift QR with LAPACK's exceptional shift, and eigenvectors by inverse iteration on the Hessenberg form. Plus the complex dense kernels (`zmatmul`, `zmatvec`, `zinv`). Used by `BurstML` and `GopichSzabo`.
- **`NelderMead.h`**: Header-only simplex optimiser for derivative-free problems.
- **`NeuralNet.h` / `NeuralNet.cpp`**: Feed-forward multilayer perceptron with Adam training, explicit backprop, StandardScaler, and JSON serialisation. Uses `Mat.h` for all dense linear algebra.
- **`i_lbfgs.h`**: Header-only limited-memory BFGS optimiser with central-difference numerical gradients and Armijo backtracking line search.
- **`Random.h`**: Centralised counter-based RNG (Philox / PCG / SplitMix64 / MT19937) with thread-safe deterministic parallel draws.
- **`SimPcgRandom.h`**: Compact inline PCG32 PRNG for per-stream reproducible randomness.

## Dependencies

- `util` (for CPU feature detection, verbose output)
- nlohmann/json (for NeuralNet serialisation)

## Why a separate module?

Previously these files lived in `util`, which meant every module that needed `Verbose.h` also transitively pulled the matrix library and neural net. The split separates "stuff that does math" from "stuff that does plumbing" (logging, progress, byte order, bit ops).

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

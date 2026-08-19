# `math` — Numerical Kernels and Linear Algebra

The `math` module houses tttrlib's shared numerical infrastructure: dense linear algebra, optimisers, random number generators, and the feed-forward neural network. None of these knows what a photon is — they are pure mathematics, shared across spectroscopy, imaging, and simulation modules.

## Contents

- **`Mat.h`**: Standalone, dependency-free dense matrix library with Armadillo-flavoured syntax. SIMD GEMM (NEON / SSE2 / AVX) with register-blocked micro-kernel, cache-blocked transpose, and zero-copy transpose proxy. Element-wise math, reductions, broadcasting. Also the shared dense solvers: `mat_solve` (Gaussian elimination with partial pivoting), `mat_lstsq_minnorm` (one-sided Jacobi SVD, minimum-norm least squares), `mat_inverse_inplace` (Gauss-Jordan, with an allocation-free overload for per-bin loops) and `mat_power`.
- **`QREigen.h`**: Eigendecomposition of real non-symmetric matrices — Parlett-Reinsch balancing, Householder Hessenberg reduction, Francis double-shift QR with LAPACK's exceptional shift, and eigenvectors by inverse iteration on the Hessenberg form. Plus the complex dense kernels (`zmatmul`, `zmatvec`, `zinv`). Used by `BurstML` and `GopichSzabo`.
- **`NelderMead.h`**: Header-only simplex optimiser for derivative-free problems.
- **`NeuralNet.h` / `NeuralNet.cpp`**: Feed-forward multilayer perceptron with Adam training, explicit backprop, StandardScaler, JSON serialisation, and the derivative entry points a caller needs to use the network as one term of a larger differentiable model: `backward` (adjoint of the outputs → adjoint of the weights and inputs, for any loss), `predict_derivatives` / `backward_derivatives` (the same for a loss on `dy/dx` and `d²y/dx²`, e.g. a PDE residual), `jacobian` / `hessian`, `get_parameters` / `set_parameters` (the flat vector an outside optimiser such as L-BFGS works on). Uses `Mat.h` for the batch GEMMs. All the arithmetic is in `MlpCore.h`.
- **`MlpCore.h`**: Header-only, std-only kernels of the dense network — activations with derivatives to third order (sklearn's four plus `softplus`, `silu`, `sin`), the batch forward and reverse passes, and both augmented with a directional Taylor expansion of the input to second order (so `J v` and `vᵀ H v` come out of the forward pass and a loss on them can be backpropagated to the weights), a scalar-templated single-sample forward for `Dual`, and the flat parameter layout. The GEMM is a template policy: `NeuralNet.cpp` plugs in `Mat.h`, and imp.bff carries a verbatim copy of this header that runs on the portable loops. See below.
- **`i_lbfgs.h`**: Header-only limited-memory BFGS optimiser with central-difference numerical gradients and Armijo backtracking line search. A consumer may supply an exact gradient instead; `imaging/localization` does.
- **`Dual.h`**: Forward-mode dual number, `val + eps*grad` with `eps^2 = 0`, templated on what sits in the derivative slot. `Dual<double>` is one directional derivative; `Dual<GradVec<N>>` is a whole gradient from one pass. See below.
- **`GradVec.h`**: Fixed-size vector of doubles used as the *derivative part* of a vectorized forward-mode dual number, so one pass through an objective yields all N partial derivatives. See below.
- **`HmmLattice.h` / `HmmLattice.cpp`**: The log-domain HMM recursions over a **caller-supplied** `log_frameprob` (T×K) — `hmm_forward_log`, a fused `hmm_backward_posteriors_xi` sweep, `hmm_viterbi_log`, a standalone `hmm_backward_log` for tests, and `hmm_estep_log` for concatenated sequences. Emissions belong to the caller, which is what lets one lattice serve a Gaussian mixture, a Poisson rate and a lookup table. Not to be confused with `spectroscopy/hmm`: that is a photon-stream model with Δt-dependent transitions and a *scaled* recursion, and it stays. Two contracts worth knowing before calling: `xi_sum` is **accumulated** (`+=`, never zeroed inside) because a fit sums it across sequences, and `-inf` is a value — an impossible sequence returns `-inf` with no `nan` and contributes zero transition counts. That is also why the CMakeLists pins fast math **off** on that translation unit.
- **`Random.h`**: Centralised counter-based RNG (Philox / PCG / SplitMix64 / MT19937) with thread-safe deterministic parallel draws.
- **`MaxEntQp.h` / `MaxEntQp.cpp`**: Shared engine behind every maximum-entropy inversion in the library — `quadpr_bound` (bound-constrained QP, active-set sweep) and `run_mem` (the Skilling-Bryan outer iteration built on it), plus `build_normal_equations` for turning an arbitrary design matrix into the quadratic form both expect. Relocated (not rewritten) from `spectroscopy/decay/MaxEntTcspc.cpp`, which now delegates to it, and now also used by `spectroscopy/corrections/MaxEnt.cpp` in place of a second implementation whose entropy term had the wrong sign. See PRD-038. Also `run_mem_target_chisq` — opt-in "historic MaxEnt": a joint (p, nu) Gull-Skilling controller that updates nu *inside* the MEM loop (secant in log-nu/log-chisq space, warm-started) so the fit's chi-square lands at a caller-chosen target. An earlier outer-bisection design failed on steep cases (1M-photon FRET: 500 cold MEM solves, no convergence) — the joint controller converges in ~157 warm QP steps; both numbers measured and pinned by a regression test. See PRD-039, including why the reference implementation deliberately doesn't have this mode.
- **`Cluster.h` / `Cluster.cpp`**: Single-linkage bundling on a mutual-reachability
  MST (the reader and the union-find in one pass, sorted edge list in, node
  counts out), plus the HDBSCAN condensed tree and per-point label read-off.
  The MST end of the pipeline is called by `spectroscopy/burst`, the HDBSCAN
  end was ported verbatim from ChiSurf's pure-Python implementation and must
  agree with it bit for bit — which is why the translation unit compiles with
  `-ffp-contract=off` (a fused multiply-add changes a tied edge, then the
  dendrogram; see the CMakeLists comment).
- **`KMeans.h` / `KMeans.cpp`**: k-means. k-means++ seeding over **caller-supplied
  uniforms** (the consumer owns the RNG and the reproducibility contract) plus
  the Lloyd sweeps and a final assignment pass that re-measures the inertia of
  the *returned* centres. The whole fit is one call. Exactness is carried in
  source: the file opens with `#pragma STDC FP_CONTRACT OFF`, because a
  compiled-in multiply-add would round `acc += diff*diff` once instead of
  twice and drift the inertia — the number restarts are ranked on — by one
  ulp while centres and labels stay identical. (The contract lives in the
  pragma deliberately: a per-file compile flag silently skipped MSVC and any
  other build that did not apply it.) See PRD-037.
- **`Nnls.h` / `Nnls.cpp`**: Non-negative least squares by the classical Lawson-Hanson (1974) algorithm — KKT-correct, unlike `quadpr_bound`'s active-set sweep (see that header's docstring for why the two are not interchangeable). Verified against `scipy.optimize.nnls`.
- **`Kalman.h` / `Kalman.cpp`**: The Kalman filter recursion over a whole count-rate trace in one call — `kalman_filter(y, x0, P0, Q, dt, r_scale)` → `(x_filt, P_filt, D_mahal)`. A bit-exact port of ChiSurf's `_kalman_filter_loop` (`core/fluorescence/burst/kalman.py`), which since ChiSurf dropped numba runs as plain Python per trace. The closed-form 2×2 inverse (`_inv2x2`) is ported as-is, so the two-channel single-molecule case agrees with the reference digit for digit; dimensions above two fall back to the library's own Gauss-Jordan inverse and are *not* bit-parity with ChiSurf's LAPACK path. Parity rides on two things carried in source: `#pragma STDC FP_CONTRACT OFF`, and reproducing BLAS's fused-second-product inner sum (`std::fma`) that numpy's `@` emits for 2×2 — a plain `a0*b0 + a1*b1` disagrees with numpy ~44% of the time, one ulp, and the Mahalanobis threshold ChiSurf's fcs plugin bursts on moves. See PRD-037 B3.
- **`Watershed.h` / `Watershed.cpp`**: Two region-segmentation kernels — a priority-queue watershed flood (`watershed(image, markers, mask, connectivity)`, label image out) and iso-contour extraction by marching squares (`marching_squares(image, level, vertex_connect_high)`, `(n, 4)` endpoint pairs out). Both match **scikit-image exactly**, digit for digit (current upstream, ≥ 0.25.1 — see the marker-seed note in the header), not ChiSurf: ChiSurf's `core/roi` is documented as skimage-exact `regionprops` and its tests compare against skimage, so a merely-correct port fails them. The two places ChiSurf's own `segmentation.py` diverges were measured and settled in skimage's favour — the flood seeds its queue with markers at their own image value (skimage 0.25.0 briefly used `-inf`, reverted upstream in 0.25.1 — the port now follows ChiSurf and current skimage), and the marching-squares case bits are `ul=1, ur=2, ll=4, lr=8` in raster emission order (ChiSurf swaps the lower row and inverts the ambiguous squares). Raster order is part of the marching-squares contract because skimage chains the segments into polygons in that order. `#pragma STDC FP_CONTRACT OFF` carries the exactness: a fused `_fraction` interpolation rounds once instead of twice and moves a contour endpoint by a ulp. The mask argument is required (an all-true uint8 image is skimage's `mask=None`); padding, footprint and output are allocated inside the call. See PRD-037 B4.
- **`Deconvolution.h` / `Deconvolution.cpp`**: Richardson-Lucy (2-D, 3-D and
  event-based with PSF oversampling) and Wiener deconvolution over the vendored
  FFT, plus `scan_blur_kernel_1d` (the dwell/jitter kernel of a scan) and the
  event <-> count-image conversions. Richardson-Lucy is scikit-image-exact; the
  Wiener estimator differs from scikit-image's by design (documented in the
  test).
- **`Jitter.h` / `Jitter.cpp`**: sub-pixel dithering of integer coordinates
  (`jitter_coordinates_2d`, in place) — what turns a pixel grid back into
  positions for list-mode deconvolution and super-resolution.
- **`Sampling.h`**: drawing from a distribution given as data —
  `sample_from_cdf` and `weighted_choice`, both on caller-supplied uniforms so
  the consumer owns the reproducibility contract.
- **`SimPcgRandom.h`**: Compact inline PCG32 PRNG for per-stream reproducible randomness.

## Examples

- `examples/miscellaneous/plot_watershed_marching_squares.py` (+ `.ipynb`): `watershed` and `marching_squares` on a simulated field of touching cells -- markers, mask, labels as ROIs, iso-contour outlines, connectivity 1 vs 2.
- `examples/miscellaneous/plot_neural_net_differentiable.py` (+ `.ipynb`): `NeuralNet` as a differentiable building block -- XOR and a sine by `train`, `backward` / `jacobian` / `hessian` checked against finite differences, and a Sobolev fit (values *and* derivative in the loss) through `backward_derivatives` + SciPy L-BFGS on `parameters`.
- `examples/miscellaneous/plot_pinn_heat_equation.py` (+ `.ipynb`): physics-informed fit of `u_t = α u_xx` -- `u_xx` from an order-2 pass along `(1,0)`, `u_t` from an order-1 pass along `(0,1)`, residual + initial/boundary loss, gradient assembled from three `backward` calls; scored against `sin(πx) e^{-απ²t}` (5e-3 in ~5 s).
- `examples/miscellaneous/plot_pinn_burgers.py` (+ `.ipynb`): the canonical PINN benchmark, viscous Burgers `u_t + u u_x = ν u_xx` (Raissi et al. 2019), scored against the Cole-Hopf solution by Gauss-Hermite quadrature; sized to `ν = 0.05`, 2-20-20-20-1, 2000 points, ~1 min, 1e-3 relative error.
- `examples/miscellaneous/plot_richardson_lucy_deconvolution.py` (+ `.ipynb`): `richardson_lucy_2d` on a simulated blurred, Poisson-noised image -- the iteration count as the regularisation (error-vs-truth minimum), `wiener_deconvolve_2d` for comparison, and the list-mode `richardson_lucy_events_2d` on photon coordinates.
- `examples/single_molecule/plot_burst_feature_clustering.py` (+ `.ipynb`): `kmeans` (caller-owned uniforms) and the HDBSCAN pipeline `core_distances` -> `mutual_reachability_mst` -> `hdbscan_condensed_tree` -> excess-of-mass selection (in the caller) -> `hdbscan_label_points` on a simulated burst table with two FRET populations and noise.
- `examples/single_molecule/plot_kalman_burst_detection.py` (+ `.ipynb`): `kalman_filter` on a simulated two-channel count trace -- filtered background rate, Mahalanobis distance as burst score, and `TTTR.burst_search_kalman` on the same photons.
- `examples/single_molecule/plot_hmm_lattice_two_state.py` (+ `.ipynb`): `hmm_forward_log`, `hmm_backward_posteriors_xi`, `hmm_viterbi_log` on a simulated two-state Poisson trace -- the caller builds `log_frameprob`, the lattice returns log-likelihood, posteriors, xi sums (one M-step shown) and the Viterbi path.

## Dependencies

- `util` (for CPU feature detection, verbose output)
- nlohmann/json (for NeuralNet serialisation)

No Eigen, no autodiff, and no other external numerics. `Mat.h` and `GradVec.h`
between them removed the last two Eigen consumers, and `Dual.h` removed the
vendored autodiff package; see below and `benchmarks/bench_mat.cpp`.
`MlpCore.h` has no dependency at all, not even on the rest of this module: that
is the condition for imp.bff to vendor it (see below).

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

## `MlpCore.h` — the network as a differentiable building block

`NeuralNet` used to be a regressor: train on `(X, Y)`, predict. That is enough
for a surrogate that replaces an EM fit, and not enough for a network that
*parametrises an unknown field inside a physical model* — a potential of mean
force, a position-dependent rate, an orienting potential — where the loss is
computed by a solver downstream of the network and the network's derivatives
with respect to its **input** are part of that loss. `MlpCore.h` is what that
takes, and nothing more:

- **`backward(dL/dy)`**: the reverse pass with the upstream adjoint supplied by
  the caller instead of a target. Returns `dL/dparams` (flat) and `dL/dx`. The
  training loop is a caller of this same function.
- **Taylor-augmented passes**: give each sample a direction `v` and the forward
  pass carries `a1 = da/dv` and `a2 = d²a/dv²` next to every activation, so
  `J v` and `vᵀ H v` come out of the output layer at ~3× the cost of a plain
  forward. Because the companions are ordinary elementwise and linear
  operations, the adjoint of the augmented pass is another backward pass with
  the same GEMMs plus `f''` and `f'''` — which is what a loss on `y`, `∇y`
  and `Δy` (a PDE residual) needs to be differentiated with respect to the
  weights. A Laplacian is `n_in` unit-direction passes. **No tape.** A tape
  for this problem would be four orders of magnitude too large once the
  network sits inside a lattice solver; the Taylor adjoint is O(batch).
- **Smooth activations**: `softplus`, `silu`, `sin` alongside sklearn's four,
  each with `f'`, `f''`, `f'''`. ReLU has `f'' ≡ 0`, so a diffusion residual
  cannot train through it — that is the reason these exist.
- **`predict_scalar<T>`**: the single-sample forward templated on the scalar,
  so `Dual<GradVec<N>>` returns the full input Jacobian in one pass. This is
  the *independent* reference the reverse pass is tested against (forward mode
  and reverse mode share no code beyond `f`), through the dot-product
  identity `<w, J v> = <Jᵀ w, v>`; and it is what a Dual-templated objective
  elsewhere in the library calls when a network is one of its terms.
- **Flat parameters**: `flatten` / `unflatten`, layer by layer, weight then
  bias — the vector `i_lbfgs.h` or scipy's L-BFGS works on.
- **A whole model, and its file format**: `MlpModel` = layers + input/output
  `StandardScaler`s; `model_predict` / `model_backward` apply the scalers and
  their chain rule so a consumer stays in physical units; `model_from_json` /
  `model_to_json` are templated on the JSON type (any nlohmann-compatible
  object) so the header stays std-only while both repositories deserialise
  the `tttrlib.neural_net` document with the nlohmann copy they vendor.
  `NeuralNet` is now a shell over `MlpModel` (`get_model()`).

The GEMM is a template policy. `NeuralNet.cpp` plugs in `Mat.h`'s SIMD
kernels; the header itself ships portable loops. That split is what makes it
shareable: **imp.bff carries a verbatim copy** of `MlpCore.h` under its
`include/internal/` (the way pcg and nlohmann/json are vendored there) and a
test that fails when the copies diverge, so a network trained here can be
evaluated and differentiated inside a coordinate-space solver without linking
tttrlib. Keep the header free of anything that would break the copy: no
`Mat.h`, no json include, no registry, no OpenMP beyond the `simd` hint.
imp.bff's `test/test_vendored_mlpcore.py` compiles a program against its
vendored nlohmann copy + `MlpCore.h` (namespace `IMP::bff::internal` via
`TTTRLIB_MLPCORE_NAMESPACE`), loads a model trained here and checks
predictions to 1e-12 and the gradients against finite differences — that is
the contract.

Numerics: the refactor of `NeuralNet::train` onto these kernels reproduces the
previous implementation's predictions to 1e-15 on the same seed (same GEMM
kernels, same operation order). Performance was measured the way `AGENTS.md`
asks — thread CPU time (`CLOCK_THREAD_CPUTIME_ID`), the pre-refactor
`NeuralNet.cpp` extracted from git and compiled into the same benchmark,
interleaved runs — because the first wall-clock numbers said "faster" while
the machine was idle and "slower" while it was loaded, and neither was true.
The first version *was* 10–45 % slower on training and 22 % on `predict_batch`
for a ReLU 2-32-32-2 net: a per-element `switch` on the activation inside the
hot loops (no vectorisation) and a fresh workspace per call. With the switch
hoisted (`act_apply`, `act_derivs_n`) and a thread-local workspace in the
model entry points, old vs new over four interleaved runs: train 2-32-32-2 ×
200 epochs relu 109 → 107 ms, tanh 244 → 238 ms, logistic 617 → 596 ms; the
256-256-128 default × 30 epochs 2 614 → 2 690 ms (±10 % run to run under
load); `predict_batch(1500×2)` × 200: relu 116 → 109 ms, tanh 435 → 429 ms,
logistic 340 → 345 ms. Parity within noise, and the derivative kernels
(order-2 forward + backward, 5000 × 2-20×8-1) got 3–9 % faster than their
first version in the same pass. Every gradient path is checked in
`test/cpp/test_mlp_core.cpp`; the end-to-end demonstration is
`test/python/test_neural_net.py::test_pinn_poisson_1d` — a 1-16-16-1 tanh net
fitted to `u'' = -π² sin(πx)`, `u(0) = u(1) = 0`, by L-BFGS on the residual
loss, gradient from `backward_derivatives`: max error 9e-6 in 0.5 s.

## Validation status

Every header in this module is A/B-tested against an independent reference
implementation (sklearn, scipy, scikit-image, numpy, hmmlearn, filterpy,
Random123 / pcg32 known answers; ChiSurf's Python where the kernel is a port of
it) and carries a `// Validation: A/B-TESTED <date> -- ...` block after its
include guard naming the reference, the metric and the test. The register with
every row, and what the A/B found, is
[`okf/testing/math-kernel-validation.md`](../../okf/testing/math-kernel-validation.md);
the suites are `test/python/misc/test_math_ab_{clustering,imaging,probabilistic,numerics}.py`
(the last one compiles `test/cpp/ab_numerics_harness.cpp` for the unbound C++).
A new kernel is not done until it has a row, a block and a test.

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

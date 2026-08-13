# PRD-010 — Reusable neural network + surrogate models, AD gradients, optimiser benchmarks

> **PRD #:** 010 · **Status:** ✅ Done · **Created:** 2026-07-20 · **Updated:** 2026-08-13 · **Owner:** tpeulen
> **Related:** PRD-005 (photon simulator, reuses `SimPcgRandom`), ChiSurf OKF `prd-60`
> (amortised neural estimator for H2MM)

## Summary

Add a small, general **`NeuralNet`** class to tttrlib — a feed-forward MLP that both trains and
infers, with scikit-learn-compatible semantics and a language-neutral JSON model format. Build the
**H2MM surrogate estimator** on top of it, and separately evaluate **forward-mode automatic
differentiation** as a replacement for the central-difference gradients used by the L-BFGS fitting
path.

The net is deliberately domain-agnostic: a surrogate model supplies only its own feature extractor
and output decoder. That is the reusable part — H2MM is simply the first consumer.

## Problem / motivation

**The H2MM surrogate is Python-only.** ChiSurf has an optional amortised neural estimator that
summarises a photon dataset into ~24 permutation-invariant features and maps them to
`(prior, trans, obs)` in a single forward pass, instead of iterating Baum-Welch EM. It trains with
scikit-learn and is stored as a **pickle**. Meanwhile the EM engine it competes against already has
a fast C++ implementation here (`include/H2MM.h`), so ChiSurf dispatches `em` to C++ but falls back
to numba + scikit-learn for the surrogate. Consequences: the surrogate cannot be used from tttrlib
at all (C++/R/Java callers, or a Python environment without scikit-learn), and the artefact is
unsafe to share and unreadable outside Python.

**Fitting pays for numerical gradients.** `include/i_lbfgs.h` is an L-BFGS minimiser with
central-difference gradients (`fgrad1/2/4`), costing 2N objective evaluations per gradient.
Consumers: `DecayFit23.cpp` (4 params), `DecayFit24.cpp` (5), `DecayFit26.cpp` (1),
`ImageLocalization.cpp` (18).

## Measured evidence

Measured on an M1 Pro (clang 17, `-O2`) against a synthetic objective replicating this library's
shape (4-exponential decay, recursive IRF convolution, Poisson 2I*, 1024 channels, 8 parameters).

**Automatic differentiation is right for fitting and catastrophic for neural networks.** The
`autodiff` library's reverse mode has no tape, no topological sort and no memoization —
`AddExpr::propagate` recurses into both children unconditionally, so a shared DAG is traversed as a
tree. A dense layer is the pathological case. For a 24→256→256→128→10 net (106k parameters), one
sample:

| approach | time |
|---|---|
| hand-written backprop (Eigen GEMM), batch=32 | 0.68 ms/minibatch (0.021 ms/sample) |
| `autodiff::var` | 7251 ms (tape build 8.8 ms, **propagate 7242 ms**) |

≈341,000× slower per sample. Over 2500 samples × 300 epochs: ~16 s versus ~170 years. Eigen-backed
`Matrix<var,...>` does not help — `reverse/var/eigen.hpp` supplies only `NumTraits`, so Eigen falls
back to its generic scalar path.

For fitting (8 parameters, 1024 channels):

| approach | cost | vs objective |
|---|---|---|
| plain `double` objective | 0.0147 ms | 1.0× |
| central differences (2N=16 evaluations) | 0.2329 ms | 15.8× |
| forward `dual`, N seeded passes | 0.1518 ms | 10.3× |
| **vectorized forward** (`Dual<double,Array<double,N,1>>`) | **0.0320 ms** | **2.2×** |
| reverse `var`, 1 pass | >10 min | ~230,000× |

Vectorized forward scales sublinearly in N (1.6× speedup at N=1, 7.6× at N=8, 16.8× at N=16) against
central differences' strict 2N, and matches scalar `dual` to 1.29e-14.

## Decisions

- **Neural network training uses explicit backpropagation, not AD.** Backprop for a dense layer is a
  transposed GEMM; there is nothing to differentiate that is not already known in closed form.
- **Reverse-mode AD is rejected for both jobs.** For fitting it is slower than the finite differences
  it would replace.
- ~~**Eigen is an acceptable dependency**: header-only with no link step. This is the distinction that
  ruled out mlpack (BLAS/LAPACK via Armadillo) while admitting Eigen.~~ — **reversed; see Phase 5.**
  "Header-only" understated the cost: a `REQUIRED` package is a package on every CI platform, in the
  wheel builder image and in the vcpkg manifest whether or not it links anything.
- **Public headers stay Eigen-free.** `NeuralNet.h` exposes `std::vector<double>` and raw pointers,
  matching this library's existing SWIG conventions; Eigen is confined to the `.cpp`. (This is what
  made the Phase 5 removal a two-file change rather than an API break — the constraint outlived the
  dependency it was written for.)
- **JSON model files** via the already-vendored `nlohmann_json`.

## Scope

### Phase 1 — `NeuralNet` (done)

`include/NeuralNet.h`, `src/NeuralNet.cpp`, `ext/python/NeuralNet.i`,
`test/python/test_neural_net.py`.

- `StandardScaler` with scikit-learn semantics (population std, zero-variance → scale 1).
- `Activation`: `Identity`, `ReLU`, `Tanh`, `Sigmoid`, parsed from scikit-learn names.
- `DenseLayer` with row-major `n_out × n_in` weights (scikit-learn's `coefs_` are transposed).
- `NeuralNet::train` — Adam with bias correction, minibatching, Glorot-uniform init, L2 on weights,
  early stopping with best-weight restore. Seeded by `SimPcgRandom` (PRD-005), no new RNG.
- `NeuralNet::predict` / `predict_batch`, JSON round-trip, and **validation at load** so a malformed
  model fails immediately rather than mid-forward.

### Phase 2 — `H2mmSurrogate` (done)

`include/H2MMSurrogate.h`, `src/H2MMSurrogate.cpp`, `ext/python/H2MMSurrogate.i`,
`test/python/h2mm/test_surrogate.py`. Thin adapter: feature extraction → `NeuralNet` → decode to
`H2mmModel`, plus training-set simulation and training. Added const CSR accessors to `H2MM`
(`get_streams`/`get_gap_slot`/`get_offsets`) rather than befriending the surrogate.

Feature parity was the whole risk, and needed three separate fixes:

1. **Histogram binning.** `numpy.histogram` computes the index as `(v-lo)/(hi-lo)*nbins`, *not*
   `(v-lo)/width` — the two round differently for values on a bin edge — then corrects it against
   the edges `linspace` actually produced. Density normalises as `(n/db)/n.sum()` over in-range
   samples only.
2. **FMA contraction.** Fusing `wsum += streams[k]*scale` in the sliding-window sum changes it by
   ~1e-16, enough to move a value across a bin edge and shift a whole count into the neighbouring
   bin. Measured 33 of 200 photons differing with contraction on, 0 with it off. The file now
   compiles with strict multiply-then-add (`#pragma clang fp contract(off)` / GCC equivalent); the
   loop is memory-bound so the FMA bought nothing.
3. **Sliding versus recomputed window.** The windowed mean must use the running two-pointer sum, not
   a fresh `slice.mean()` per photon. This one was a bug in the *test's* reference, not the port —
   worth noting because the "cleaner" formulation is the wrong one.

Validated against the real ChiSurf/numba implementation to 1e-12, not only against a
reimplementation.

### Phase 3 — AD gradients (measure only; convert nothing)

Vendor `autodiff`, add a vectorized-forward gradient provider to `i_lbfgs.h` as an opt-in policy,
and produce per-path numbers against **both scalar and AVX builds**. Each consumer is converted, or
not, on its own measured result in a later phase.

Prerequisites recorded for that decision:

1. The enabling `NumberTraits<Eigen::Array<double,N,1>>` specialization is **undocumented and
   unsupported upstream**. Pin the vendored version and test vectorized gradients against scalar
   `dual`, so a future bump fails red rather than producing silently wrong derivatives.
2. `fconv_avx` / `fconv_per_avx` use intrinsics and **cannot be templated**, so the AD path loses
   them while central differences keep them. The measured 7× is scalar-versus-scalar.
3. `sanitise_parameters` clamps `tau` and `gamma`. Under AD a clamped parameter propagates an exactly
   zero derivative and L-BFGS can stall at the bound, where central differences give a nonzero
   one-sided estimate. Reparameterize (`tau = exp(u)`, `gamma = sigmoid(v)`) rather than clamp.
4. Baseline first: the current step `h = eps*|x|` is not optimal for central differences
   (≈ ε^(1/3)|x|). Retuning is free and is the honest baseline AD must beat.

Note the accuracy argument is weak and should not be oversold: measured relative error of the current
scheme is 2e-9 to 2e-6, ample for L-BFGS curvature pairs. The real wins are cost per gradient and
robustness of the `EpsG` termination test near the optimum — which is why `ImageLocalization.cpp`
carries a hand-tuned `seteps(1e-12)`.

### Phase 4 — benchmarks (done)

`benchmarks/bench_nn.py` and `benchmarks/bench_ad_gradients.cpp`. Measured on an M1 Pro, arm64 conda
env.

**Neural net and surrogate** (400 training datasets, 150 bursts × 80 photons, 2 states):

| task | result |
|---|---|
| `generate_training_set` | ~0.9 ms per simulated dataset |
| `train_mlp` tttrlib | 277–333 ms |
| `train_mlp` scikit-learn | 585–1003 ms (noisy; ~2–3× slower) |
| held-out MAE | tttrlib 0.1019 vs scikit-learn 0.1006 |
| `extract_features` | 0.31 ms / 12 000 photons |
| `surrogate_predict` | 0.34 ms |
| `em_fit` (warm, 1 restart) | 3.3 ms |

Two results worth keeping in view:

- **The network is not the bottleneck.** Feature extraction is 0.31 ms of the 0.34 ms forward pass,
  so the MLP is ~10% of it. Library choice was never going to matter for speed here.
- **A full surrogate trains in seconds.** At 5000 samples: 4.6 s to simulate, 3.5 s to fit.

Training-set size versus accuracy (30 evaluation trials each, held-out MAE on 400 datasets):

| n_train | held-out MAE | sur (sep<0.2) | EM (sep<0.2) | sur (sep≥0.2) | EM (sep≥0.2) |
|---|---|---|---|---|---|
| 500 | 0.1118 | 0.0255 | 0.0669 | 0.0235 | 0.0067 |
| 1000 | 0.0876 | 0.0182 | 0.0669 | 0.0136 | 0.0067 |
| 2500 | 0.0774 | 0.0143 | 0.0669 | 0.0137 | 0.0067 |
| 5000 | 0.0729 | 0.0168 | 0.0669 | 0.0096 | 0.0067 |

**Do not quote an averaged surrogate-versus-EM accuracy number.** The average is dominated by
whichever regime the trial mix favours. Stratified by state separation the picture is unambiguous
and stable: the surrogate is ~4× better than EM where the states are barely separated and EM is
unidentifiable, and EM stays better on well-separated states at every training size tested. The
surrogate's error is nearly flat in separation because it regresses toward a prior over models; EM's
error is 0.0067 when identifiable and 0.0669 when not. EM maximising likelihood is not the same as
being close to the truth: verified a case where more restarts found a *higher* likelihood
(−8052.873 → −8052.838) that was *less* accurate (0.027 → 0.080). That is correct behaviour of
`fit()`, not a bug.

**AD gradients** (`bench_ad_gradients.cpp`, NCH=1024, objective replicating `fconv`'s recursion +
Poisson 2I*, cost quoted as a multiple of one objective evaluation):

| N | objective | CD `h=eps·|x|` | CD `h=eps^⅓·|x|` | AD | AD vs tuned CD |
|---|---|---|---|---|---|
| 1 (DecayFit26) | 0.0074 ms | 3.07× (err 1.8e-09) | 3.07× (err 3.5e-09) | 1.95× exact | **1.57×** |
| 4 (DecayFit23) | 0.0134 ms | 8.01× (err 1.5e-07) | 7.23× (err 5.1e-08) | 1.56× exact | **4.63×** |
| 5 (DecayFit24) | 0.0121 ms | 9.19× (err 1.5e-07) | 9.17× (err 5.1e-08) | 1.69× exact | **5.44×** |
| 8 | 0.0150 ms | 15.36× (err 6.8e-07) | 14.72× (err 8.4e-08) | 2.05× exact | **7.18×** |
| 18 (ImageLocalization) | 0.0252 ms | 35.89× (err 4.8e-06) | 35.88× (err 7.8e-07) | 3.65× exact | **9.83×** |

Conclusions for the later conversion decision:

- **Retuning the finite-difference step is a free ~6× accuracy win** (`eps·|x|` → `eps^⅓·|x|`, e.g.
  4.8e-06 → 7.8e-07 at N=18) at essentially no cost in time. Worth doing on its own merits,
  independent of AD.
- ~~**`ImageLocalization` (N=18) is the strongest AD candidate**: 9.8× on gradient cost~~ —
  **corrected below.** That row used a decay objective at N=18; `ImageLocalization` is neither.
- **`DecayFit26` (N=1) is not worth converting** at 1.57×.
- The **AVX caveat stands**: these are scalar-versus-scalar numbers. `fconv_avx` cannot be templated,
  so a converted `DecayFit` path loses it while the finite-difference path keeps it. The real ratio
  for AVX-enabled models is narrower and must be measured before converting those paths.

autodiff was **not** vendored into `thirdparty/`, since this phase converts nothing; the benchmark
builds against a checkout.

### Phase 3b — the SIMD comparison, and two corrections (`bench_ad_vectorized.cpp`)

**Does AD still win when central differences keep the vectorized kernel? Yes, with a narrower
margin.** Measured with the real dispatcher (NEON on this AArch64 host, 2 doubles/register):

| N | objective speedup from SIMD | AD vs CD-scalar | AD vs CD-**SIMD** |
|---|---|---|---|
| 4 (2 exp) | 1.25× | 4.64× | **3.70×** |
| 8 (4 exp) | 1.41× | 7.21× | **5.16×** |
| 16 (8 exp) | 1.59× | 9.17× | **5.80×** |

AVX packs 4 doubles per register to NEON's 2, so on x86 the vectorized objective should gain more
and the AD margin narrow further. That is an **extrapolation, not a measurement** — this host cannot
execute AVX at all.

**Correction 1 — `ImageLocalization` was mischaracterised.** It is a 2D Gaussian PSF fit
(`target2DGaussian` → `model{,Two,Three}2DGaussian` + Poisson `W2DG`) and never calls `fconv`, so no
SIMD kernel is at stake. It also never optimises 18 free parameters: entries 12..17 are flags and
outputs and are fixed, and `i_lbfgs` minimises in the reduced free-parameter space, so N is 6/9/12
for one/two/three Gaussians. On its real objective, AD is **3.95×–5.44×** faster than tuned central
differences — not 9.83×.

**Correction 2 — retuning the FD step is not a universal win.** On the 2D Gaussian objective the
central-difference error is already 1.5e-08–2.2e-07 and retuning changes little. The ~6× accuracy
improvement holds for the decay objective, not everywhere.

### Blocker for converting `ImageLocalization`: `varinbounds` is not a clamp

`localization::varinbounds` (`src/ImageLocalization.cpp:45`) reads:

```cpp
if (var < min || var > max) var = (max - min) / 2;   // teleport to the midpoint
```

It does not clamp to the bound — it **jumps the parameter to the middle of the range**, and
`target2DGaussian` writes the result back into the caller's array, so the objective is not a pure
function of its input. `varlowerbound` similarly sets `var = min + 1`.

This already corrupts the *existing* central-difference gradient whenever a step straddles a bound.
Demonstrated at `x0 = 15.0` with `xlen = 15` and `h = 1e-6·|x|`: `f(x−h)` is evaluated at 15.0 and
`f(x+h)` at **7.5**, so the difference quotient divides a step of 7.5 by 3e-05. The gradient
component is meaningless there.

So converting to AD is **not** a mechanical templating job, and doing it first would be the wrong
order:

- Under AD the reset branch propagates an exactly-zero derivative, so L-BFGS can stall at the bound
  where central differences currently produce (wrong, but nonzero) motion. Behaviour would change.
- The in-place write-back must be removed before the objective can be differentiated at all.
- The flags/outputs interleaved into `vars` (12..17) must be separated from the parameters.

**Recommended order:** fix the bound handling first — replace the teleport with a genuine clamp, or
better, reparameterize (`x0 = xlen·sigmoid(u)`) so the constraint is smooth and L-BFGS is better
conditioned — then convert. Fixing the bounds is a behaviour change to existing fits and should be
validated on real localization data on its own, separately from the AD work.

### Phase 5 — the conversion, and Eigen leaves the project (done)

`ImageLocalization` was converted, which is the outcome Phase 3b's blocker section recommended and in
the order it recommended: bounds first, then AD. The teleporting `varinbounds` is gone from the fit
path; positions go through a logistic and positive quantities through an exponential, so the search
space is interior everywhere and the objective is a pure function of its input. Measured before →
after on the same noise-free image: **2.6× / 4.3× / 9.3×** for one / two / three Gaussians (whole
fit, not just the gradient), agreeing with central differences to 2.5e-08 relative.

Then the dependency itself went. `Eigen::Array<double, N, 1>` was the derivative carrier and, once
`NeuralNet` moved to `Mat.h`, the only thing in tttrlib that needed Eigen. It is now `GradVec<N>`
(`modules/math/include/GradVec.h`) and `FIND_PACKAGE(Eigen3 REQUIRED)`, `tttrlib::eigen`, the apt /
brew / dnf packages on four CI platforms and the Windows vcpkg port are all gone.

Three results worth keeping:

1. **The measured cost of dropping Eigen is 0–16%.** Parity at N=9, 13–16% slower at N=6 and N=12
   (`benchmarks/bench_gradvec.cpp`). Against the 3.95–5.44× AD wins over tuned central differences,
   that changes no decision — but it is a real number and is recorded as one, not rounded to
   "equivalent".
2. **The gap closed by removing temporaries, not by alignment or padding.** `alignas(32)` measured
   *slower* (it inflates every `Dual`, and 169 of them are live in the inner loop); padding N to a
   multiple of the SIMD width did nothing at N=12, which is already a multiple of 4. Returning a
   proxy from `scalar * grad` — so the multiply fuses with the accumulate that always follows it in
   the product and quotient rules — is the one that worked. It also made the vectorized gradient
   *bitwise* identical to autodiff's scalar `dual`, because the fused form contracts to the same FMA.
3. **The guard test claimed in Phase 3's prerequisites had never been written.** The comment in
   `ImageLocalization.cpp` asserted it existed. `test/cpp/test_ad_gradient.cpp` now does what the
   comment promised. This is the prerequisite that mattered most: the enabling `NumberTraits`
   specialization is undocumented upstream, so a bump would compile and be wrong.

A methodological note that cost real time: the first benchmark used `steady_clock` and reported
speedups from 0.22× to 4.77× for the same binary on consecutive runs. The machine was at load average
43 and wall clock keeps counting while the thread is descheduled. `CLOCK_THREAD_CPUTIME_ID`,
interleaved carriers and min-of-nine made it repeatable to a few percent. A kernel comparison that
cannot separate a 15% difference from the scheduler is not measuring the kernel.

## Definition of done

- [x] `NeuralNet` trains, infers, round-trips JSON, and rejects malformed models.
- [x] Agrees with scikit-learn's forward pass to 1e-10 on imported weights.
- [x] 24 tests in `test/python/test_neural_net.py`; no regression in the existing suite.
- [x] `H2mmSurrogate` reproduces the ChiSurf feature vector to 1e-12 (cross-checked against the real
      numba implementation, not a reimplementation).
- [x] 25 tests in `test/python/h2mm/test_surrogate.py`; full suite 701 passed, 0 failed.
- [x] AD gradients benchmarked per parameter count on the real objective shape (scalar; the AVX
      comparison remains open and gates converting the `DecayFit` paths).
- [x] ChiSurf routes its surrogate engines through tttrlib when a JSON surrogate is present, with
      8 cross-engine agreement tests proving a scikit-learn-trained surrogate gives identical
      estimates through either path.
- [x] Worked example: `examples/single_molecule/plot_h2mm_surrogate.py`.
- [x] `ImageLocalization` converted to an exact AD gradient, with the bound handling fixed first.
- [x] The vectorized derivative carrier is tttrlib's own (`GradVec<N>`), measured against Eigen, and
      Eigen is gone from the project — CMake, CI, wheels and vcpkg.
- [x] `test/cpp/test_ad_gradient.cpp` guards the undocumented `NumberTraits` contract, which the
      Phase 3 prerequisites asked for and which had never actually been written.
- [x] `DecayFit23`'s general (tau/gamma) fit branch converted to an exact AD gradient (Phase 6),
      measured 1.27x–1.68x end to end (single-call and 8000-row batch), gradient and value verified
      against central differences to the finite-difference error floor, and the full existing
      `test/python/decayfit/` suite (109 tests) reproduces its pinned reference values within tolerance.
- [x] `DecayFit24`'s `tau1`/`tau2` moved to `soft_floor` (Phase 7, shipped). An exact gradient was
      built and verified to the finite-difference floor, then **declined**: measured net win at 8000
      rows was not clear (wall clock 1.08x-1.15x faster, total CPU roughly flat to 6% slower) against
      `DecayFit23`'s clean 1.3x-1.7x, so it was removed from `DecayFit24.cpp` rather than shipped for
      an inconclusive number. `A2`, `gamma` and `offset` stay deliberately hard-clamped either way.
- [x] The central-difference step every remaining `bfgs` consumer uses is retuned (Phase 8):
      `fd_eps = eps^(1/3)`, a member separate from `sqrt_eps`'s `EpsG`/`EpsX` roles. Measured
      62x-440x more accurate than the `sqrt_eps` step it replaced, across the full existing
      C++/Python/conformance suite with zero cases needing a re-pin.
- [x] `EpsG` and `EpsX` no longer share a variable either (Phase 9): `epsg`/`epsx`, each with its own
      `set_epsg`/`set_epsx`. `EpsG` auto-tightens to `eps` when an exact gradient is registered,
      finally acting on what `set_gradient`'s docstring has said since Phase 6. Zero regressions
      across the full suite, `DecayFit23` and `ImageLocalization` checked specifically.
- [x] `FitNExp` revisited and shipped a real improvement without an AD gradient replacing its optimizer
      (Phase 10): a joint `bfgs`+AD refinement of the coordinate search's own answer, exact by the
      envelope theorem, additive (never runs instead of Brent+EM, only after it), and impossible to
      make worse by construction (`bfgs`'s line search only accepts strictly decreasing steps).
      Measured at scale (batched, realistic photon counts): 0.5% overhead, 100% of tested rows
      improved, 0% regressed. `N=1` (the library's most benchmarked path) gated out after a real ~35%
      slowdown was measured and fixed before shipping. The fixed-lifetime `fit_map` path is
      structurally unreachable by this change, confirmed by reading the guard.

### Correction 3 — the finite-difference baseline in Phase 4 is not the step the optimiser uses

Phase 4 concluded that retuning the step from `eps·|x|` to `eps^(1/3)·|x|` is "a free ~6× accuracy
win". **`bfgs` does not use `eps·|x|`.** Its central difference is `h = sqrt_eps·|x|`
(`i_lbfgs.h:328`), and with the default `seteps(2.2e-16)` that is 1.49e-08·|x|. The `eps·|x|` form
belongs to the standalone `fgrad1/2/4` helpers at the top of the header, which the optimiser never
calls.

So the retune is still worth doing — `sqrt(eps)` is the optimal step for a *forward* difference, not
a central one, where the optimum is `eps^(1/3)` — but the available win is
1.5e-08 → 6.1e-06 in step size, not the 2.2e-16 → 6.1e-06 the table implies, and the accuracy
improvement will be correspondingly smaller than "6×". The measured error column in
`bench_ad_gradients.cpp` is for a step no production fit has ever run.

None of this touches the **cost** argument, which is what the conversion decision rests on: central
differences pay 2N objective evaluations at any step size, so the 3.70×/5.16×/5.80× AD advantage
measured against the real NEON dispatcher in Phase 3b stands unchanged.

### Phase 8 — the retune landed, and the pessimism above was itself wrong (done)

Landed as `fd_eps` (`modules/math/include/i_lbfgs.h`), a member separate from `sqrt_eps` so the FD
step no longer entangles with the `EpsG`/`EpsX` convergence thresholds `sqrt_eps` also serves --
`seteps(e)` now sets both `sqrt_eps = sqrt(e)` (unchanged, convergence tests) and
`fd_eps = cbrt(e)` (the central-difference step), and only the step computation
(`h = fd_eps * |x|`) changed.

**Correction 3's own prediction -- "the accuracy improvement will be correspondingly smaller than
6×" -- was wrong, measured against the step actually replaced rather than assumed.**
`bench_ad_gradients.cpp` was updated to compare `sqrt(eps)` (the real old step, ~1.49e-08) against
`eps^(1/3)` (the real new step, ~6.06e-06) instead of the never-used `eps` baseline the original Phase
4 table used:

| N | central-diff error, old step (`sqrt_eps`) | error, new step (`fd_eps`) | improvement |
|---|--:|--:|--:|
| 1 (`DecayFit26`) | 2.2e-07 | 3.5e-09 | ~63× |
| 4 (`DecayFit23`'s `p2s_twoIstar` branch) | 1.6e-05 | 5.1e-08 | ~314× |
| 5 (`DecayFit24`) | 1.6e-05 | 5.1e-08 | ~314× |
| 8 | 3.7e-05 | 8.4e-08 | ~440× |
| 18 | 2.0e-04 | 7.8e-07 | ~256× |

The reasoning in Correction 3 was sound (`sqrt(eps)` is a smaller, more-precise-looking step than
`eps^(1/3)`, so the *ratio of step sizes* moved less than the original table implied) but the
conclusion did not follow: a central difference's error is `O(h^2) + O(eps/h)`, and `sqrt_eps` is
*too small* for that trade-off despite being closer to zero -- the roundoff term `eps/h` dominates
at the smaller step, which is exactly the failure mode `eps^(1/3)` exists to avoid. Being closer to
zero is not the same as being the right size.

**Verified against the full existing suite, not assumed safe.** `sqrt_eps`'s two other roles
(`EpsG`, `EpsX`) are untouched by construction (`fd_eps` is a new member, not a repurposing), and
every `bfgs` consumer still on central differences was checked directly, not just `DecayFit24`:
`DecayFit25`, `DecayFit26`, `DecayFit23`'s `p2s_twoIstar` branch, `DecayFitModel.cpp`'s `fit_linked`
joint-fit path, and `DecayFitPlugin.cpp`'s plugin-fit path. Full C++ (`ctest`, 4/4) and Python suite
(2698 passed, 49 skipped, 74 subtests, two unrelated pre-existing failures -- a stale system `tttr`
binary missing `libomp.dylib`, and a load-sensitive performance-comparison test that passes cleanly
in isolation) ran clean: **zero conformance cases needed re-pinning.** The accuracy gain above is real
and substantial, but apparently not large enough at any pinned fixture's scale to cross a tolerance
that survived the fit23 AD conversion's much smaller (~1e-4 relative) shift -- consistent with this
being a gradient-*precision* improvement inside an already-converged optimizer's tolerance, not a
change to what any fit converges to.

### Phase 9 — EpsG and EpsX stop sharing a variable too, and EpsG acts on its own promise (done)

Phase 8 fixed one accidental sharing (the FD step); `sqrt_eps` was still doing double duty for two
*more* unrelated things -- `EpsG` (gradient-norm convergence, `gnorm > sqrt_eps`) and `EpsX`
(step-size convergence, `step <= sqrt_eps`) -- the same smell that let the FD-step bug hide for years.
Split into their own members, `epsg` and `epsx` (`modules/math/include/i_lbfgs.h`), each with its own
public setter (`set_epsg`/`set_epsx`), matching the class's existing per-concern API (`set_bounds`,
`set_gradient`). `seteps()` still derives sensible defaults for both -- identical to the old shared
value, so this half of the change is architecture only, zero behaviour change.

**The second half acts on a promise `set_gradient`'s own docstring already made and never kept.** It
has said, since Phase 6, that an exact gradient "makes the EpsG termination test trustworthy at tight
tolerances" -- but `EpsG` stayed at `sqrt(eps)` (~1.49e-08) whether or not a gradient was exact. A
central difference's own noise floor is `sqrt(eps)`-scale, which is why the threshold lived there;
an exact (AD) gradient's floor is accumulated rounding in the objective itself, `eps`-scale
(~2.22e-16), a further ~6.7e7x tighter. `set_gradient(g)` now tightens `epsg` to `eps` automatically
when `g` is non-null, and restores `sqrt(eps)` when `g` is `nullptr` (reverting to central
differences) -- both directions skipped if a caller has called `set_epsg` explicitly, so nothing
silently overrides an intentional choice.

**Verified, not assumed helpful.** This directly affects both fits that register an analytic
gradient -- `DecayFit23`'s general branch and `ImageLocalization` -- so both were checked specifically:
`test/python/decayfit/` (109 passed), `test/python/misc/test_image_localization.py`, and
`test/python/test_conformance.py::test_conformance[decayfit.fit23_published_answer]` (the `1e-6`
tolerance case Phase 6 already re-pinned once) all still pass, at the **same** re-pinned values --
this change did not move that fit's answer further, meaning `EpsG` was not its binding termination
criterion. That is a legitimate outcome, not a null result: which criterion binds is data-dependent,
and the fix is correct regardless of whether any one fixture happens to visibly move. Full suite
(2698 passed, 74 subtests, the same two unrelated pre-existing failures as Phase 8) confirmed no
regression anywhere else either.

### Phase 10 — `FitNExp` revisited: Phase 5e was right that it isn't an AD *candidate*, but that wasn't the whole question (done)

Phase 5e closed `FitNExp` as "not an AD candidate ... no gradient optimiser" -- true of the shipped
design (coordinate-wise Brent + EM, `DecayFitNExp.cpp`), but the actual question asked of this PRD
later was narrower and different: not "does `FitNExp` use `bfgs`", but "would a joint gradient step
*on top of* the existing search recover anything the search's one-lifetime-at-a-time updates
structurally cannot see." `okf/handover/flim-performance-opt.md:93-94` had flagged the AD half of
this earlier and it was never followed up.

**Why the design wasn't naively replaced.** `DecayFitNExp.cpp`'s Brent+EM is deliberate VARPRO:
amplitudes enter the model linearly given fixed lifetimes, so EM's per-step update is closed-form, and
the coordinate search is explicitly multistart-aware ("a profiled mixture likelihood need not be
unimodal in one lifetime", `DecayFitNExp.cpp:614-616`, unchanged). A generic joint optimizer that
reimplemented neither would plausibly be both slower and less robust on multimodal data -- so this
was investigated with the design's own reasoning taken seriously, not assumed to be a stopgap.

**The resolution is the envelope theorem: a joint AD step that does not touch either property.**
Amplitudes stay profiled by the *same* EM, in plain `double`, at every trial lifetime vector; the AD
gradient (`Dual<GradVec<N>>`, reusing `fconv_per_cs_ad`) is taken with those amplitudes held constant.
This is exact, not an approximation -- at the EM optimum `d(NLL)/d(weight) = 0`, so
`d/d(tau)[profiled NLL]` equals the partial derivative of `NLL(tau, weights)` holding weights fixed,
by the envelope theorem. Multistart is not reimplemented: the refinement only ever polishes the
coordinate search's *own* answer, never replaces the search that found it -- a prototype
(`benchmarks/bench_fitnexp_bfgs_ad.cpp`, kept for the record) measured that a cold joint start with no
multistart can land in a worse local optimum than Brent's grid scan finds, on the same data a
refinement-from-that-answer improves.

**Measured before shipping anything**, on the prototype: value/gradient correct (reuses `fconv_per_cs_ad`
directly, not a reimplementation); a joint refinement pass after the shipped `DecayFitNExp::fit`
converged never made the answer worse and improved it in every one of 6 tested cases (2-exp
well-separated, 2-exp correlated, 3-exp, across multiple random seeds) -- including fixing a 3-exp fit
where the coordinate search had pinned a lifetime at the `tau_max` bound. At the scale that matters
(a batched fit, realistic per-curve photon count, `DecayFitNExp::fit_batch_flat` -- not the toy
single-case numbers above): Brent+EM cost 210 ms/row, the refinement added 1.02 ms/row, **0.5%
overhead**, and improved **100/100** rows with **zero** regressions.

**Shipped as an additive stage in `DecayFitNExp::fit`** (`modules/spectroscopy/decay/src/DecayFitNExp.cpp`),
run once after the coordinate-descent loop converges, keeping only the refined `lifetimes` -- the
existing unconditional re-evaluation immediately after already recomputes weights/NLL/probability from
scratch, so nothing about the surrounding code had to change. `bfgs`'s Armijo line search only ever
accepts a strictly decreasing step, so the refinement cannot make the result worse by construction, not
merely by what got measured. `N` (the exponential count) is a runtime value but `Dual<GradVec<N>>`
needs it at compile time, same constraint every other AD consumer in this PRD has; dispatched via a
`switch` covering `N = 1..6` (real fits are 1-4 per `DecayFitNExp.h`'s own docs), silently skipping the
refinement beyond that -- the coordinate result stands unrefined, exactly as it always has.

**One real regression found and fixed before shipping, by the same A/B discipline as every other
change in this PRD.** `N=1` -- the mono-exponential case, the library's single most benchmarked path
(`PERF.md`'s "Single-curve lifetime fit" / `bench_tttrlib.py`'s `bench_fit_curve`) -- has no
cross-lifetime correlation for a joint step to recover, so the refinement there is pure overhead.
Measured directly against that exact benchmark: **~35% slower per call** (0.25 ms -> 0.34 ms single,
0.06 ms -> 0.08 ms batched) for an unchanged answer. Gated the refinement to `N >= 2`; re-measured N=1
back to the unmodified baseline (0.25 ms / 0.06 ms, matching published `PERF.md` numbers) with zero
change to N=2/3's measured improvement.

**The fixed-lifetime, per-pixel `fit_map` path (`PERF.md`'s headline "Per-pixel reconvolution MLE" 140 ms
number) is structurally unaffected, not just measured to be.** `bench_tttrlib.py`'s `fit_map` call uses
`fixed=[1]` (`ext/python/FitNExpWrapper.py:91` -- "1 holds the lifetime fixed"), so no lifetime is ever
free there; the refinement is gated on `any_free`, so that code path cannot be reached regardless of
`N`. Confirmed by reading the guard, not assumed from the benchmark alone.

### Phase 5b — the decay conversion is mechanically feasible (probed, not landed)

Transcribed the production chain (`fconv_per_cs_scalar` → `model23` → `normM` → `Wcm`), templated on
the scalar type with nothing else changed, and instantiated it under `Dual<double, GradVec<4>>`:

- it compiles;
- the value matches the plain-`double` objective to 2.8e-14 absolute on a value of −240.9 (one ulp,
  FMA contraction);
- the gradient matches central differences to 8.3e-08 relative — the finite-difference error floor.

**The NEON kernel is not at stake, which was the fear.** `fconv_per_cs()` already dispatches to
`fconv_per_cs_scalar()`, and that scalar fallback is what templates; the intrinsic path stays exactly
as it is and continues to serve the plain-`double` objective. The concern recorded in Phase 3 — that
converting a `DecayFit` path costs it the SIMD kernel — applies to the *AD column only*, which is
what Phase 3b already measured.

### Phase 5c — bounds *are* priors, and the likelihood had to be fixed first (done)

Minimising `-log L + p(x)` is MAP estimation with `p = -log prior`, so a bound and a prior are the
same object seen from two sides. Both codebases already say so: `DecayFitContext.h:65` — "A bound
*is* a uniform prior in this interface" — and ChiSurf's `prior` setter folds a `UniformPrior` back
onto the port's bounds. It follows that `i_lbfgs`'s soft bound, `k(x-hi)^2` outside the box, is
already a proper prior (flat-topped, Gaussian-shouldered); nobody had named it one. So the four
mechanisms in the decay fits — clamps in `sanitise_parameters`, the `fit_settings.penalty` hack, the
optimiser's `set_bounds`, and `DecayFitContext`'s bounds — are one mechanism wearing four hats.

**ChiSurf's transforms are not the thing to copy.** `leastsqbound.py` uses the MINUIT scheme: `sin`
two-sided, `sqrt(v^2+1)` one-sided. Measured, both reintroduce exactly the pathology the
reparameterisation exists to avoid — the `sin` derivative is 3e-17 at the bound (`v = pi/2`) and the
transform is periodic and non-monotonic; the `sqrt` form is *even* in `v` and its derivative is
exactly 0 at `v = 0`, which maps to `x = lower`. The logistic/exponential pair used in Phase 5 is
monotonic, bijective, and attains its bounds only asymptotically, so the derivative is never zero
anywhere reachable. What *is* worth taking from ChiSurf is `priors.py`: priors as extra residuals,
imposing no bound at all (`HalfNormalPrior` is documented as "a soft positivity prior").

**But a penalty can only replace a clamp if the objective is defined off-support, and it was not.**
`Wcm` and `wcm_p2s` skipped any model bin at or below `1e-12`. That is not a guard — it is a
discontinuous 828.9-unit *reward* for driving a bin under the floor, with a perfectly flat objective
below it. Fixed (see CHANGELOG): `log` continued by its tangent at the floor, C1 across it, finite
and monotone below. **Not inert, and the first write-up of this said it was.** Bitwise identical
above the floor holds; the 143,360-bin sweep that appeared to prove the floor unreachable used a flat
*non-zero* background and so never tested the reference data, which has none. Measured effect:
`fit23` 23.802337 -> 23.791124 (tau 0.74219 -> 0.721353), `fit25` 4.738831 -> 3.887975; `fit24` and
`fit26` unchanged because their background is 0.2. `fit25` is the clean attribution -- its only other
change removed an always-zero addend -- so the whole 0.85 is the likelihood correction, most of it
from `wcm_p2s` discarding the *pair* when either channel underflowed. Re-pinned across all four
conformance runners and both Python reference tests. The lesson: an inertness sweep proves nothing
outside the inputs it sweeps.

A correction to this PRD's own earlier text: an intermediate probe here reported that `gamma < 0`
makes the objective NaN. That was a transcription error in the probe, which guarded on `C > 0` where
`Wcm` guards on `M > 1e-12`. `Wcm` never returns NaN; it silently drops the bin, which is the worse
failure because it is invisible. `twoIstar` *does* guard on `C` and *can* return NaN — but it is
computed after the fit for reporting and is never minimised, so it is left alone.

### Phase 5d — `DecayFit23`'s bounds unified onto `set_bounds` (done)

Doing this turned up two things measurement contradicted, both worth recording because the wrong
version is the intuitive one.

**"Clamps make L-BFGS stall" was not true of this fit, and the real defect was different.** Measured
on the real objective: below `kMinTau` the gradient was `-1` exactly, not zero — the hand-rolled
`fit_settings.penalty` was supplying it. But that term is
`(x[0] < kMinTau) ? -x[0] : 0`, which is **negative over the whole band `0 < tau < kMinTau`**, so
crossing below the bound *improved* the objective by up to 1e-3, and it was discontinuous at the
crossing (`d/dtau = 4999.5` there, against `-0.0004` just above). The flat-region stall was real for
*gamma* (`d/dgamma` exactly 0 at 1.0, 1.2, 2.0), rescued by a `set_bounds` call that was only made
inside the branch that frees gamma.

**The clamps cannot be deleted.** Without the `tau` floor `exp(-dt/tau)` overflows for `tau` in
roughly `(-dt/709, 0)`: measured at `tau = -1e-6`, every model bin is `inf` and the objective is
`NaN`. No penalty rescues a NaN — the line search has to be able to score the point it proposes. So
the two roles the clamp was playing are now separated: the guards stay, and every bound that shapes
the fit goes through `set_bounds`, set once and unconditionally.

**They can, however, be made smooth, and are.** `tau` and `rho` go through `soft_floor`
(`DecayFit.h`): exactly the identity at and above the floor — bit-for-bit, so no ordinary fit
moves — and `m0*exp((v-m0)/m0)` below it, C1 at the join, strictly positive, nonzero derivative. The
failure direction flips from overflow to underflow. This removes the corner in the parameter map,
which is what an AD pass would otherwise inherit as a structural zero.

**But it does not make the objective non-flat below `kMinTau`, and no choice of floor could.** That
flatness is physical: at `dt = 0.032` the factor `exp(-dt/tau)` is already 1.3e-14 at `tau = 1e-3`
and underflows below, so the model is saturated. The proof the clamp was never the cause is that
`d/dtau` is already exactly 0 at `tau = 1.1e-3`, *above* the floor. `tau_eff` keeps moving; the model
stops caring. Recorded because the tempting next move — tuning `kMinTau` — cannot work: no parameter
map manufactures information the likelihood does not contain. The restoring force there is
`set_bounds`, and only `set_bounds`.

That separation is also the AD prerequisite. `i_lbfgs` adds the bound penalty **and its gradient** to
whatever a registered analytic callback returns (`i_lbfgs.h:313-320`); a term added to the objective
by hand is invisible to that callback, so the old `tau` penalty would have made an AD gradient wrong
by exactly `-1` below the bound.

Verified against the pre-change code end to end: four ordinary starts give identical `tau`, `gamma`
and 2I* to every printed digit; starts below the bound and at negative `tau` still reach the same
minimum. `DecayFit26`'s equivalent penalty is correctly signed and left alone; `DecayFit25`'s is dead.

### Phase 5e — the scaling limit, and a correction about `FitNExp` (done)

**`FitNExp` was never an AD candidate.** It appears in this PRD's problem statement as an `i_lbfgs`
consumer. It is not one: `DecayFitNExp.cpp` never constructs a `bfgs`. Lifetimes are optimised
coordinate-wise by Brent and amplitudes are profiled out by EM, so there is no N-dimensional gradient
to convert. Struck from the open list.

**The AD advantage peaks and then decays, which nothing measured so far had shown.** Every decision
in this PRD was taken at N <= 18. Both methods are O(N) -- central differences pay 2N objective
evaluations, a vectorized forward pass makes every scalar carry an N-vector -- so the ratio is a race
between two O(N) costs settled by constants and memory traffic. Measured on a 1024-channel
multi-exponential decay (`benchmarks/bench_ad_scaling.cpp`):

| n_exp | N | CD (x obj) | AD (x obj) | AD gain | MB per model intermediate |
|---|---|--:|--:|--:|--:|
| 2 | 4 | 7.9 | 1.6 | 4.8x | 0.04 |
| 8 | 16 | 32.4 | 2.4 | **13.3x** | 0.13 |
| 32 | 64 | 142 | 16.4 | 8.7x | 0.51 |
| 128 | 256 | 586 | 161 | 3.7x | 2.01 |
| 200 | 400 | 895 | 257 | **3.3x** | 3.13 |

Central differences stay near-linear (895x against a theoretical 2N = 800x); AD goes *superlinear*,
257x where pure O(N) predicts ~160x. The last column is the reason: a `Dual<double, GradVec<400>>` is
3.2 kB, so one 1024-channel intermediate is 3.13 MB, far outside cache, while the finite-difference
path re-walks a plain 8 kB array. In absolute terms one gradient at N = 400 costs 149 ms by AD
against 518 ms by CD.

So AD still wins at 200 exponentials, by 3.3x rather than the 13x its peak suggests. **At that size
both are the wrong tool**: for a sum of exponentials the analytic gradient is closed form --
d/d(amplitude) *is* the convolved exponential already computed -- so a hand-written gradient costs
about one objective evaluation. The dip at N = 32 reproduces across runs and was not chased; it
changes no decision.

### Phase 6 — `DecayFit23` converted (done)

`DecayFit23`'s general (tau/gamma) BFGS branch now gets an exact forward-mode gradient
(`decay23_gradient`, `DecayFit23.cpp`) instead of `i_lbfgs`'s central-difference default. This is
item 1 and item 2 of the "Still open" list below, done for the one fit Phase 5c/5d/5e had already
prepared -- the soft-floored bounds and the `set_bounds`-only mechanism they built are exactly the AD
prerequisite this phase needed.

What landed, in the shape the earlier phases called for:

- **`fconv_per_cs_ad<T>`** (`DecayConvolution.h`) -- a second, `template<T>` scalar body for the
  periodic-convolution-with-stop kernel, alongside (not replacing) the runtime-dispatched NEON/scalar
  `fconv_per_cs`. `lamp` (the IRF) stays `double`; `fit`/`x` carry whatever `T` the caller seeds. The
  fused two-channel NEON kernel (`fconv_per_cs_2ch`) is not templated -- vv and vh are convolved
  separately under AD, which is what that kernel's own docstring says is mathematically identical to.
- **`Wcm_ad<T>`/`log_m_ext_ad<T>`** (`DecayStatistics.h`) and **`soft_floor_ad<T>`/`clamp_value_ad<T>`**
  (`DecayFit.h`) -- templated siblings of the existing `double` functions, same branches (comparisons
  on a `Dual` look at the value only, so `T = double` takes the identical path). `Wcm_p2s`'s series
  expansion (chi2 fallback, overflow retry) is **not** templated -- real work for a branch few callers
  exercise -- so `decay23_gradient` is only registered when `fit_settings.p2s_twoIstar` is off; central
  differences remain the fallback there.
- **`decay23_cost<T>`** (`DecayFit23.cpp`, anonymous namespace) -- `modelf` + `normM` + `Wcm`,
  transcribed onto the templated pieces above, plus templated `Fp`/`Fs`/`r`/`rho`/harmonic-mean
  helpers so gamma's effect on the *derived* rho (`fit_signals.rho(tau, r0)`, the common case when
  rho is not caller-fixed) differentiates correctly too -- that dependency chain was not exercised by
  Phase 5b's probe, which only carried tau through `fconv_per_cs`.

Verified two ways, not one:

- `test/cpp/test_ad_gradient.cpp` gained a `decay23` section reusing the real `fconv_per_cs_ad`/`Wcm_ad`
  (not a second reimplementation of those, only of the anisotropy glue) against central differences, at
  both an interior point and both the derived-rho and fixed-rho branches: value agrees to 2.3e-13,
  gradient to 5e-9 -- 1e-7 relative, the finite-difference error floor seen everywhere else in this PRD.
- The existing regression suite -- `test/python/decayfit/` (109 passed, 1 unrelated skip), including
  `test_fit2x_compat.py::test_fit23`, pinned against central differences before this change --
  reproduces its reference values (`twoIstar` 23.791124 → 23.791082, `tau` 0.721353 → 0.721273, both
  well inside the test's `places=3` tolerance) after switching to the analytic gradient. The small move
  is expected, not slop: `i_lbfgs.h`'s own `set_gradient` doc says the exact gradient "also makes the
  `EpsG` termination test trustworthy at tight tolerances", so the two paths can legitimately stop at
  very slightly different points near the same minimum.

**One conformance case needed re-pinning, and the tolerance that caught it is exactly why it exists.**
`test/conformance/cases/decayfit.fit23_published_answer` exercises this same branch (tau/gamma free)
at `1e-6` tolerance -- tight enough that the `EpsG` effect above moved it: objective
`23.79112398420848 → 23.791082287859183` (1.75e-6 relative), `tau`
`0.721353235715964 → 0.7212727630506686` (1.12e-4 relative). Re-pinned to the new values with the
reason recorded in the case's own `doc` field, since this is the cross-language answer key (Python, R,
Java and JS all resolve to the same C++ call and so all move together) -- not a second, independent
regression, but worth naming as a real, measured side effect of tightening the gradient rather than
glossing over it as noise.

Measured A/B (`benchmarks/bench_decayfit23_ad.py`, `benchmarks/bench_decayfit23_batch_ad.py`; both
scripts are the whole protocol -- build once with `set_gradient` active and once with it commented out,
`pip install -e .` between the two, run each), an M1 Pro:

| path | before (central diff) | after (AD) | speedup |
|---|--:|--:|--:|
| one `Fit23(...)` call through Python/SWIG, best of 3000 | 0.187 ms | 0.147 ms | **1.27x** |
| `fit_many`, 8000 rows, wall clock (parallel_for, all cores) | 2595 ms | 1540 ms | **1.68x** |
| `fit_many`, 8000 rows, total CPU across worker threads | 3721 ms | 2603 ms | **1.43x** |

The single-call number is diluted by SWIG marshalling that has nothing to do with the gradient (see
`okf/testing/benchmarking.md`'s vector-marshalling trap); `fit_many` crosses into C++ once and lets
every row pay only the fit cost, which is why its speedup is larger and closer to what
`bench_ad_gradients.cpp`'s N=4 row (4.63x, gradient-only, scalar) would predict once BFGS's own
line-search/history overhead is added back in. **This does scale to the parallel batch/pixel path**:
`fit_many` (`DecayFitModel.cpp`) parallelises over rows with a hand-rolled thread pool
(`tttrlib::parallel_for`, not OpenMP) once a batch reaches 1024 rows, and `DecayFit23.cpp`'s
`thread_local fit_signals`/`fit_corrections`/`fit_settings` -- which `decay23_gradient` reads the same
way `targetf` always did -- give each worker its own copy, so the analytic gradient is exercised
per-thread with no shared mutable state. Fitted values (median tau, objective mean) were identical
before and after at 8000 rows, confirming the speedup is not bought with accuracy.

`fit23` is documented (`doc/fit-guide.rst`) as both the single-molecule burst fit and a low-photon
per-pixel FLIM tool, but the repository's own FLIM competitor benchmark (`benchmarks/bench_tttrlib.py`)
exercises `DecayFitNExp`/`FitNExp` for per-pixel imaging, not `fit23` -- so this change does not move
any number already published in `PERF.md`'s FLIM table; it is a new, separate entry (see PERF.md's "AD
gradients" section).

### Phase 7 — `DecayFit24` tried, tested, and declined (no clear win)

`DecayFit24`'s bound handling needed one small change first, not the redesign Phase 6's write-up
predicted. `correct_input`'s `tau1`/`tau2` clamps are the exact same arithmetic guard `DecayFit23`'s
`tau` had (`exp(-dt/tau)` overflows below the floor) -- swapped for `soft_floor`, bit-identical above
it, same as Phase 5c. **This part shipped and stays**, independent of the AD decision below: it is a
real improvement to the central-difference gradient too. `A2`, `gamma` and `offset` stay hard-clamped,
deliberately, unchanged -- pure modelling constraints, same reasoning as `DecayFit23`'s `gamma`. One
thing found and left alone rather than fixed: `correct_input`'s gamma clamp *tests*
`x[1] > 0.999 - xm[3]` (coupling it to `A2`) but *assigns* the flat constant `0.999`, not
`0.999 - xm[3]` -- looks like a latent bug (a `gamma + A2` combination above 1.499 seems reachable),
noted here rather than silently carried or silently corrected.

An exact gradient was then built the same way as `DecayFit23`'s -- `decay24_cost<T>` (N=5:
tau1/gamma/tau2/A2/offset) reusing `fconv_per_cs_ad`/`Wcm_ad` unchanged, only its own
`modelf`/`normM_p2s` glue as new templated code (`normM_p2s` normalises each Jordi half to its *own*
integrated signal, `Sp` and `Ss` separately, unlike `DecayFit23`'s joint `Sexp` scaling) -- and
**tested** the same two ways as Phase 6: `test/cpp/test_ad_gradient.cpp`'s `decay24` section (value to
machine precision, gradient to 2.8e-8 relative against central differences) and the existing
`test/python/decayfit/`/conformance suites, which reproduced their pinned values with **no re-pin
needed** (that fixture reports the `-1` non-convergence sentinel, `info == 5`, identically either way).

**The measured A/B was not a clean win, so it was not shipped.** `benchmarks/bench_decayfit24_ad.py`
(the legacy 64-bin `test_fit2x_compat` fixture) showed no difference at all (0.770 ms vs 0.773 ms) --
that fixture never converges for this model (`A2` diverges to ~3.06, `tau1`/`tau2` hit their floor,
`info == 5` every time), so both paths pay the same fixed iteration budget regardless of per-call cost.
A second benchmark simulated a well-conditioned two-lifetime decay instead (period well above both
lifetimes) so the fit actually converges, at 8000 rows:

| metric | central differences | AD (tried), two runs | ratio |
|---|--:|--:|--:|
| wall clock (`parallel_for`, all cores) | 14861 ms | 12955 ms / 13783 ms | 1.08x-1.15x faster |
| total CPU across worker threads | 26463 ms | 28286 ms / 28147 ms | 0.94x (6% slower) |

CPU time repeats to <1% across the two AD runs (28286 vs 28147 ms) -- the more reliable of the two
metrics on a shared machine, per `bench_gradvec.cpp`'s own note on `steady_clock` noise -- and it says
AD was not cheaper here, marginally more expensive. Wall clock moved more between the two AD runs
(12955 vs 13783, ~6%) than the CD-vs-AD gap itself, so the wall-clock number would have been overreading
noise as a win. **Declined on that measurement** -- the same call this PRD already made for `DecayFit26`
at N=1 -- and the AD path was removed from `DecayFit24.cpp` (a note in the source, above `modelf`, says
what was tried and points here; `test/cpp/test_ad_gradient.cpp`'s `decay24` section stays, as the record
that the removed approach was correct, not merely attempted). The likely reason for the gap is
architectural, not a mistake in the templating: `bench_ad_gradients.cpp`'s own isolated measurement puts
`DecayFit24`'s gradient at 5.44x cheaper (Phase 4), so the saving is real at the gradient level -- it is
`i_lbfgs`'s own per-iteration overhead (line search, L-BFGS history update, bound-penalty pass) plus
`Dual<GradVec<5>>`'s larger memory footprint against a comparatively cheap 128-bin two-exponential model
that appears to absorb it. Not chased further. `benchmarks/bench_decayfit24_ad.py` and
`bench_decayfit24_batch_ad.py` were removed with the feature they measured -- the numbers are recorded
here, `PERF.md`, `CHANGELOG.md` and `okf/log.md` instead of left as a script with nothing to compare
against, matching `DecayFit26`'s decline (no benchmark script survives that one either).

## Nothing left open

**Not gated on an x86 measurement.** Phase 3b measured against the real NEON dispatcher, not a scalar
stand-in: AD is 3.70x/5.16x/5.80x ahead at N=4/8/16 *with* central differences keeping their SIMD
kernel. AVX packs 4 doubles to NEON's 2 and would narrow that, but not by the ~4x it would take to
flip the decision. Phase 5b showed the conversion is mechanically feasible, and Phase 5d moved the
bounds onto the one mechanism `i_lbfgs` applies to the analytic path as well. Phase 6 landed an exact
gradient for `DecayFit23`. Phase 7 built and tested the same for `DecayFit24`, then declined to ship
it (smaller, noisier, and net-negative-on-CPU-time measurement). Phase 8 retuned the central-difference
step every remaining consumer uses, landed with zero conformance re-pins needed. Phase 9 finished the
architecture: `EpsG`/`EpsX` no longer share a variable with each other either, and `EpsG` now actually
tightens for an exact gradient, as `set_gradient`'s docstring had promised since Phase 6. Phase 10
revisited `FitNExp` -- correctly not an AD-*replaces*-the-optimizer candidate, but a real, measured win
as an additive joint-refinement stage that never runs instead of the shipped Brent+EM search. Every
item this PRD ever opened is now resolved, one way or another:

1. **Retune the central-difference step -- done (Phase 8).** `fd_eps = eps^(1/3)` landed, measured
   62x-440x more accurate than the `sqrt_eps` step it replaced, zero regressions.
2. **Decline `DecayFit26` -- done.** N=1, measured at 1.57x; the templating costs more than the
   gradient saves. Recorded as a decision, not an omission.
3. **The AD candidate list is closed.** `DecayFit23` (Phase 6) is the only fit on an exact gradient.
   `DecayFit24` was built, tested and declined (Phase 7); `FitNExp` got a joint-refinement stage
   instead of an AD gradient replacing its optimizer (Phase 10); `ImageLocalization` is done;
   `DecayFit26` is declined. Every consumer still on central differences (`DecayFit24/25/26`,
   `DecayFit23`'s `p2s_twoIstar` branch, `fit_linked`, plugin fits) benefits from Phase 8's retuned
   step regardless.
4. **`EpsG`/`EpsX` architecture -- done (Phase 9).** Each convergence threshold is its own member with
   its own setter now, and `EpsG` auto-tightens to `eps` when an exact gradient is registered. Zero
   regressions across the full suite; `DecayFit23` and `ImageLocalization` (the two AD consumers this
   directly affects) checked specifically.
5. **`FitNExp` -- done (Phase 10).** Joint `bfgs`+AD refinement of the coordinate search's own
   converged answer, `N >= 2` only (measured zero benefit and real overhead at N=1, the library's
   flagship benchmarked case, so it is gated out there). 0.5% overhead, 100% of tested rows improved,
   0% regressed, at realistic batched photon counts.

Two things deliberately left as behaviour changes for their own change:

- **`gamma`'s hard clamp.** Unlike `tau` it has no arithmetic failure outside its range, so the clamp
  is purely a modelling constraint. Keeping it is a deliberate decision, not an oversight.
- **Penalty stiffness as a prior width.** `k = 1e6` is a guard-rail (`sigma ~ 7e-4` read as a prior),
  and `DecayFit23` minimises `W/Nchannels`, so a penalty added there is `N x` too strong in
  log-posterior units. Harmless while these are numerical bounds; wrong the moment anyone reports the
  result as a credible interval.

## Superseded: autodiff is no longer a dependency (2026-08-12)

Everything below about vendoring `autodiff`, pinning its version, and testing against its scalar
`dual` describes a state that no longer exists. `modules/math/include/Dual.h` (~160 lines) replaced
the vendored package, so Phase 3's prerequisite 1 -- "the enabling `NumberTraits` specialization is
undocumented and unsupported upstream, pin the version and test against scalar `dual` so a bump
fails red" -- is retired rather than satisfied: there is no upstream to bump. The other three
prerequisites (the non-templatable SIMD kernels, reparameterisation instead of clamping, retuning
the central-difference step) are unaffected and still stand.

The rest of the AD case is unchanged and re-measured with the replacement, which is faster wherever
N is large enough to matter: `DecayFit23` (N=4) and `DecayFit24` (N=5) are still the live
candidates, `DecayFit26` (N=1) is still declined, `ImageLocalization` is still the only converted
consumer. One number in Phase 5's scaling study moved: the dip at N=32 recorded there as
"reproducible, not noise" was autodiff materialising temporaries, and is gone. See the 24th entry in
`okf/log.md` and the A/B in `CHANGELOG.md`.

Not part of this PRD but found by it: `modules/imaging/localization/CMakeLists.txt` declares
`TEST_DIR test/python/clsm`, which contains no localization tests -- the test is
`test/python/misc/test_image_localization.py`.

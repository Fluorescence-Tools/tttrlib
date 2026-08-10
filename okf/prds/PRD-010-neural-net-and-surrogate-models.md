# PRD-010 — Reusable neural network + surrogate models, AD gradients, optimiser benchmarks

> **PRD #:** 010 · **Status:** In progress · **Created:** 2026-07-20 · **Updated:** 2026-08-10 · **Owner:** tpeulen
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
and monotone below. Verified inert two ways — bitwise identical above the floor, and a 143,360-bin
sweep of the clamped `DecayFit23` box that never goes below 1.86e-07.

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

## Still open

**Not gated on an x86 measurement.** Phase 3b measured against the real NEON dispatcher, not a scalar
stand-in: AD is 3.70x/5.16x/5.80x ahead at N=4/8/16 *with* central differences keeping their SIMD
kernel. AVX packs 4 doubles to NEON's 2 and would narrow that, but not by the ~4x it would take to
flip the decision. Phase 5b showed the conversion is mechanically feasible, and Phase 5d moved the
bounds onto the one mechanism `i_lbfgs` applies to the analytic path as well. What remains:

1. **Templated kernels in the library.** `fconv_per_cs`, `fconv_per` and friends need `template<T>`
   scalar bodies -- Phase 5b transcribed one into a probe; the production header needs the real
   thing -- with the intrinsic dispatch untouched for `double`.
2. **A gradient callback per fit, and `set_gradient` wired.** It has exactly one caller in the whole
   library (`ImageLocalization.cpp`).
3. **Retune the central-difference step.** Still worth doing, but see Correction 3: it is
   `sqrt_eps*|x|` today, not `eps*|x|`, so the win is smaller than Phase 4 claimed.
4. **Decline `DecayFit26`.** N=1, measured at 1.57x; the templating costs more than the gradient
   saves. Recorded as a decision, not an omission.
5. **Only `DecayFit23` (N=4) and `DecayFit24` (N=5) are live candidates.** `FitNExp` is not one
   (Phase 5e -- no gradient optimiser), `ImageLocalization` is done, `DecayFit26` is declined. Note
   from Phase 5e that N=4 sits at the low end of the AD advantage curve (4.8x), not its peak.

Two things deliberately left as behaviour changes for their own change:

- **`gamma`'s hard clamp.** Unlike `tau` it has no arithmetic failure outside its range, so the clamp
  is purely a modelling constraint. Keeping it is a deliberate decision, not an oversight.
- **Penalty stiffness as a prior width.** `k = 1e6` is a guard-rail (`sigma ~ 7e-4` read as a prior),
  and `DecayFit23` minimises `W/Nchannels`, so a penalty added there is `N x` too strong in
  log-posterior units. Harmless while these are numerical bounds; wrong the moment anyone reports the
  result as a credible interval.

Not part of this PRD but found by it: `modules/imaging/localization/CMakeLists.txt` declares
`TEST_DIR test/python/clsm`, which contains no localization tests -- the test is
`test/python/misc/test_image_localization.py`.

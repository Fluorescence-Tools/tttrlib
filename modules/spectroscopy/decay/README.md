# `spectroscopy/decay` — Fluorescence Decay Analysis and Fitting

Iterative reconvolution fitting algorithms and maximum likelihood estimation for fluorescence lifetime decay curves.

## Contents

**The fit models and the machinery around them.**

- **`DecayFitModel.h` / `DecayFitModel.cpp`** — the model interface and the factory
  (`make_decay_fit`), plus `DecayFitModelRegistration.h`, which is why a model
  survives being linked out of a static archive.
- **`DecayFitModelFit2x.cpp`** — the Fit2x family (`fit23`, `fit24`, `fit25`,
  `fit26`) and their registry entries.
- **`DecayFitModelNExp.cpp`** — the n-exponential model and its registry entry.
- **`DecayFitPlugin.cpp`** — a model a plugin contributed, wrapped so the
  library's own optimiser fits it.
- **`DecayFit.h` / `DecayFit.cpp`, and `DecayFit23.h` / `DecayFit23.cpp`,
  `DecayFit24.h` / `DecayFit24.cpp`, `DecayFit25.h` / `DecayFit25.cpp`,
  `DecayFit26.h` / `DecayFit26.cpp`** — the kernels:
  the objective, the profiled amplitudes and the analytic pieces of each model.
- **`DecayFitProblem.h` / `DecayFitProblem.cpp`** — a fit as data: parameters, links, bounds,
  results; `DecayFitSetup.cpp` derives the flat parameter layout from the
  registry entry (the flattening rule).
- **`DecayFitContext.h`** — what a kernel is given: the data, the IRF, the
  fitted range and the objective.
- **`DecayFitPrior.h` / `DecayFitPrior.cpp`** — the prior kinds (uniform, normal, truncated
  normal, half-normal, log-normal, exponential, gamma, beta, product, a Python
  callable, and any a plugin adds) and their registry entries.
- **`DecayFitDescriptors.h` / `DecayFitDescriptors.cpp`** — everything this module registers:
  `fit`, `fit_setup`, `objective`, `prior` and its pipeline operations.
- **`DecayStatistics.h` / `DecayStatistics.cpp`** — the objectives: Poisson MLE (2I*), P+2S,
  Neyman and Gehrels least squares, and the Pearson/Neyman chi-squares.
- **`DecayConvolution.h` / `DecayConvolution.cpp`** — the reconvolution kernels (`fconv` and
  its periodic / per-channel / SIMD variants), pile-up, lamp shift, rescaling.
- **`DecayFitDFA.h` / `DecayFitDFA.cpp`** — the anisotropy (VV/VH) forms of those kernels.
- **`DecayFitNExp.h` / `DecayFitNExp.cpp`** — the standalone n-exponential fitter.
- **`DecayPatternFit.h` / `DecayPatternFit.cpp`** — the linear unmixing of a decay into
  measured patterns (Poisson MLE or NNLS).
- **`BlindIRF.h` / `BlindIRF.cpp`** — the IRF recovered from a decay alone.
- **`MaxEntTcspc.h` / `MaxEntTcspc.cpp`** — maximum-entropy lifetime and distance
  distributions (the engine itself is `MaxEntQp` in [`math`](../../math)).

## Examples

- `examples/fluorescence_decay/plot_blind_irf_estimation.py` (+ `.ipynb`): BIRFI blind IRF estimation on a simulated multi-channel decay, then the recovered IRF in a `FitNExp` reconvolution fit.

## Dependencies

- Depends on `core`, `util`.

## The model floor in the Poisson likelihoods

`Wcm` and `wcm_p2s` (`DecayStatistics`) are the objectives `DecayFit23/24/25/26`
minimise. Both treat a model bin at or below `kModelFloor = 1e-12` specially,
and the way they do it changed — the reason is worth knowing before touching it.

They used to **skip** such a bin, under a comment reading "this is only for
stability reasons". It was not stability. The term a near-zero bin contributes
to the minimised objective is `-C·log(m)`, which is large and *positive*: at
`C = 30` and `m = 1e-12`, about `+829`. Skipping it is a discontinuous
improvement of exactly that size, awarded for pushing the bin one step further
down — and below the floor the objective is perfectly flat, so nothing pulls it
back. Measured: the objective falls **828.9** across the threshold and is
identical for every negative model value.

`log` is now continued below the floor by its tangent there,
`log m₀ + (m − m₀)/m₀`, which agrees in value *and* slope. The objective is C1
across the floor, finite for every finite model value including negative ones,
and strictly worse the further below it goes. `wcm_p2s` gets the same treatment
at C0 — its series' true slope at the floor is not `1/m₀`, and matching it would
mean differentiating the sum for a region no converged fit should visit.

**This moves two reference fits — and the first version of this note said it
moved none.** Above the floor the arithmetic is bit-for-bit what it was, and
that part is solid. The claim that went further rested on a sweep of 143,360
model bins across the clamped `DecayFit23` box that never produced a bin below
`1.86e-07` — but **that sweep used a flat non-zero background**, which is not
what the reference data is. It proved the floor unreachable for the inputs it
happened to choose.

Where the model does reach the floor:

| fit | before | after | why |
|---|--:|--:|---|
| `fit23` 2I* | 23.802337 | 23.791124 | zero background, 58 photons |
| `fit23` tau | 0.74219 | 0.721353 | |
| `fit25` 2I* | 4.738831 | 3.887975 | p2s path — `wcm_p2s` dropped the **pair** if *either* channel underflowed |
| `fit24`, `fit26` | unchanged | unchanged | background 0.2 |

`fit25` is the clean attribution: the only other change on that path removed an
always-zero addend, so all 0.85 of the movement is the likelihood correction.
The old values answered a likelihood that discarded occupied bins; they are not
the more correct ones. Generalisable lesson: **an inertness sweep proves nothing
outside the inputs it sweeps**, and a background of zero is exactly the corner a
"representative" parameter box omits.

Two things not to undo:

* **The multiply stays in `Wcm`'s loop body**, not inside `log_m_ext`. Inside,
  the compiler stops contracting it into the accumulate and every ordinary
  evaluation shifts by an ulp — in functions pinned by cross-language reference
  tests. `test/cpp/test_decay_likelihood.cpp` checks this bitwise.
* **`twoIstar`/`twoIstar_p2s` are deliberately left alone.** They guard on the
  *counts* rather than the model, so unlike `Wcm` they genuinely can return NaN
  for a non-positive model — but they are computed after the fit for reporting
  and are never minimised, and changing them would move numbers the reference
  tests pin for no benefit to any optimiser.

## Bounds in `DecayFit23`: one mechanism, and why the clamps stay

A bound and a prior are the same object: minimising `−log L + p(x)` is MAP
estimation with `p = −log prior`. `DecayFitContext.h` says so directly — *"A
bound is a uniform prior in this interface"* — and it follows that
`i_lbfgs::set_bounds`, a smooth exterior penalty `k(x−hi)²`, is already a proper
prior (flat-topped, Gaussian-shouldered). It is the one mechanism this fit uses.

It did not used to be. There were three:

* **`set_bounds` for gamma** — but only inside the branch that frees gamma, so
  the pre-fit ran under different rules than the main fit. Now set once,
  unconditionally, with every other bound.
* **A hand-rolled term for tau**, `penalty = (x[0] < kMinTau) ? -x[0] : 0`. This
  was **wrong**: it is negative over the whole band `0 < tau < kMinTau`, so
  crossing below the bound *improved* the objective (measured: 9e-4 better
  stepping from 1.1e-3 to 9e-4), and it was discontinuous at the bound. Removed.
* **Clamps in `sanitise_parameters`** — which stay, see below.

### The floors are numerical guards, not constraints

They cannot be deleted, and the reason is measurable rather than a matter of
taste: without the `tau` floor, `exp(-dt/tau)` overflows for `tau` in roughly
`(-dt/709, 0)`. At `tau = -1e-6` every model bin comes back `inf` and the
objective is `NaN` — and no penalty rescues a `NaN`, because the line search has
to be able to *score* the point it proposes.

`tau` and `rho` therefore go through `soft_floor` (`DecayFit.h`) — **exactly**
the identity at and above the floor, so no ordinary fit moves by an ulp, and
`m₀·exp((v−m₀)/m₀)` below it: C1 at the join, strictly positive for every finite
input, nonzero derivative throughout. The failure direction flips from overflow
to underflow, which the likelihood's own floor continuation absorbs.

**The smooth floor does not make the objective non-flat below `kMinTau`, and it
cannot.** That flatness is physical: at `dt = 0.032`, `exp(-dt/tau)` is already
`1.3e-14` at `tau = 1e-3` and underflows below, so the model is saturated. The
proof that the clamp was never the cause is that `d/dtau` is already exactly 0
at `tau = 1.1e-3` — *above* the floor. `tau_eff` keeps moving under the
transform; the model stops caring. Do not try to tune `kMinTau` to fix this; no
parameter map can manufacture information the likelihood does not contain. The
restoring force there comes from `set_bounds`.

A clamp is separately the wrong thing to enforce a bound *with*, because a
clamped objective is **flat** outside the box: `d/dgamma` is exactly 0 at
gamma = 1.0, 1.2 and 2.0. Nothing points home. `gamma` keeps its hard clamp —
unlike `tau` it has no arithmetic failure outside its range (the model is finite
at gamma = −0.2 and 1.5), so its clamp is purely a modelling constraint and
removing it is a behaviour change worth making on its own.

So the two roles are separated. The floors keep the model evaluable; the soft
bounds shape the fit. That split is also what made the AD conversion possible:
`i_lbfgs` adds the bound penalty *and its gradient* to whatever a registered
analytic-gradient callback returns (`i_lbfgs.h:313-320`), whereas a term added
to the objective by hand — as the old `tau` penalty was — is invisible to that
callback and would have made the analytic gradient wrong by exactly `-1` in the
`tau` component below the bound.

### The general (tau/gamma) branch now uses that gradient

`decay23_gradient` (`DecayFit23.cpp`, anonymous namespace) is a one-pass
forward-mode gradient — `tttrlib::Dual<GradVec<4>>`, the same machinery the
2D-Gaussian localization fit uses — registered via `bfgs_o.set_gradient(...)`
in place of `i_lbfgs`'s central-difference default. It differentiates the same
chain `targetf`/`modelf` runs (`sanitise_parameters`'s soft floor and hard
clamp, the derived-or-fixed `rho`, both `fconv_per_cs` calls, the background
mix, `normM`, `Wcm`) via templated siblings of those functions
(`fconv_per_cs_ad`/`Wcm_ad`/`log_m_ext_ad`/`soft_floor_ad`/`clamp_value_ad`),
not a reimplementation next to them. `Wcm_p2s`'s series expansion is not
templated, so `fit_settings.p2s_twoIstar` fits keep using central differences.

Measured (`benchmarks/bench_decayfit23_ad.py`, `benchmarks/bench_decayfit23_batch_ad.py`):
one `Fit23()` call through Python is **1.27×** faster; `fit_many` on 8000 rows
(`tttrlib::parallel_for`, all cores) is **1.68×** faster wall clock, with fitted
values unchanged. See PRD-010's Phase 6 and `PERF.md`'s "Exact gradient in
DecayFit23's general (tau/gamma) branch" for the full numbers.

### `DecayFit24`: an exact gradient was tried, tested, and declined

`tau1`/`tau2` moved to `soft_floor` (the same arithmetic-overflow guard as
`DecayFit23`'s `tau`) — this part shipped and stays. An exact gradient was
then built the same way as `DecayFit23`'s, for the two-lifetime model (N=5:
tau1/gamma/tau2/A2/offset), reusing `fconv_per_cs_ad`/`Wcm_ad` unchanged (only
`normM_p2s`'s per-half scaling needed new templated code); `A2`, `gamma` and
`offset` stay hard-clamped, deliberately, for the same reason `DecayFit23`'s
`gamma` does. One thing found and preserved rather than fixed while building
it: `correct_input`'s gamma clamp *tests* `x[1] > 0.999 - xm[3]` (coupled to
`A2`) but *assigns* the flat constant `0.999`, not `0.999 - xm[3]` —
reproducing `correct_input` exactly was the job, not correcting a latent bug
in it.

Measured at 8000 rows: wall clock **1.08×–1.15×** faster, but total CPU
across worker threads — the more repeatable metric — **roughly flat to 6%
slower**. Unlike `DecayFit23` this was not a clean win, so the gradient was
**not shipped** — removed from `DecayFit24.cpp` (a note above `modelf` says
what was tried and why), same call already made for `DecayFit26`.
`test/cpp/test_ad_gradient.cpp`'s `decay24` section stays, as the record that
the removed approach was correct, not merely attempted. See PRD-010's Phase 7
and `PERF.md` for the full numbers and the likely cause.

### Also unified

`DecayFit25` and `DecayFit26` carried the same idea and are now on `set_bounds`
too — see the section below.

### The same tidy in `DecayFit25` and `DecayFit26`

Both carried a thread-local `penalty` added to the objective in `targetf`.

`DecayFit25`'s was **dead** — set to zero and never to anything else. Removed.

`DecayFit26`'s was **correct**, unlike fit23's: `-x[0]` below zero and `x[0]-1`
above one, both positive outside the box. So this was a tidy, not a bug fix.
It still had fit23's other two problems — it duplicated a mechanism `i_lbfgs`
already provides, and being added to the objective outside the model it is
invisible to an analytic gradient, which sees only what the registered callback
returns. Replaced by `set_bounds(0, 0.0, 1.0)`; the clamp in `correct_input`
stays as the arithmetic guard.

Verified the same way as fit23: every in-range starting point gives an identical
fraction and 2I*; a start at `f = -0.3` differs in the sixth decimal with the
same 2I*, i.e. the same minimum reached by a marginally different path.

## `DecayFitNExp`: a joint AD refinement pass, additive to the shipped search

`DecayFitNExp.cpp` deliberately never used `bfgs`: amplitudes are profiled by
EM (closed-form given fixed lifetimes, since they enter the model linearly)
and lifetimes are searched one at a time by a Brent search that is explicitly
multistart-aware ("a profiled mixture likelihood need not be unimodal in one
lifetime", `DecayFitNExp.cpp`). Both properties are real and are unchanged —
this adds a joint gradient step *after* the coordinate search converges,
never instead of it.

`refine_lifetimes_ad` (`DecayFitNExp.cpp`, anonymous namespace) profiles
amplitudes with the *same* EM (`evaluate_profile_ws`, plain `double`, re-run
at every trial lifetime vector) and takes the AD gradient
(`tttrlib::Dual<GradVec<N>>`, reusing `fconv_per_cs_ad`) holding those
amplitudes constant. This is exact by the envelope theorem: at the EM
optimum `d(NLL)/d(weight) = 0`, so `d/d(tau)[profiled NLL]` equals the
partial derivative of `NLL(tau, weights)` holding weights fixed — the term
through weights' own dependence on tau vanishes identically, so there is no
need to differentiate through the EM iteration itself. `N` is a runtime
value but `Dual<GradVec<N>>` needs it at compile time, so
`refine_lifetimes_ad_dispatch` switches on it for `N = 1..6` and silently
skips the refinement beyond that (real fits are 1-4 exponentials).

Multistart robustness is deliberately not reimplemented: a prototype
(`benchmarks/bench_fitnexp_bfgs_ad.cpp`, kept for the record) found that a
*cold* joint start with no grid scan can land in a worse local optimum than
Brent's multistart finds, on data where refining Brent's own answer
afterward improves it. So the refinement only ever polishes the coordinate
search's own converged answer. `bfgs`'s Armijo line search only accepts
strictly decreasing steps, so it cannot make that answer worse by
construction, not merely by what was measured.

Measured at scale (batched, realistic per-curve photon counts,
`DecayFitNExp::fit_batch_flat`): **0.5% overhead**, **100%** of tested rows
improved, **0%** regressed. One real regression was found and fixed before
shipping: `N=1` (mono-exponential, the library's single most benchmarked
path — `PERF.md`'s "Single-curve lifetime fit") has no cross-lifetime
correlation for a joint step to recover, so the refinement there was pure
overhead — measured at +35% per call for an unchanged answer, so it is
gated to `N >= 2`. The fixed-lifetime `fit_map` path (`fixed=[1]`,
`ext/python/FitNExpWrapper.py`) never has a free lifetime, so the
refinement's `any_free` guard makes it structurally unreachable there
regardless of `N`. See PRD-010's Phase 10 and `PERF.md` for the full
numbers.

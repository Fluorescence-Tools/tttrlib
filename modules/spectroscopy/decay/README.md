# `spectroscopy/decay` — Fluorescence Decay Analysis and Fitting

Iterative reconvolution fitting algorithms and maximum likelihood estimation for fluorescence lifetime decay curves.

## Contents

- **`DecayFit.h` / `DecayFit.cpp`**: Base decay fitting engine and cost function evaluation.
- **`DecayConvolution.h` / `DecayConvolution.cpp`**: Fast numerical convolution routines with instrument response functions (IRF). SIMD-accelerated (AVX+FMA on x86_64, NEON on AArch64) with runtime CPU dispatch.
- **`DecayFit23.cpp` - `DecayFit26.cpp`**: Non-linear optimization algorithms (Levenberg-Marquardt, Nelder-Mead, L-BFGS) for multi-exponential model fits.
- **`DecayFitNExp.h` / `DecayFitNExp.cpp`**: General bounded multi-exponential reconvolution by Poisson MLE. Uses EM variable-projection for amplitudes and coordinate-wise Brent minimization for lifetimes. Allocation-free inner optimization loop via `FitWorkspace` scratch buffers. Supports single-curve, batch, and per-pixel image fitting.
- **`DecayStatistics.h` / `DecayStatistics.cpp`**: Goodness-of-fit statistics (chi-squared, weighted residuals, autocorrelation of residuals).
- **`BlindIRF.h` / `BlindIRF.cpp`**: Blind instrument response function estimation from fluorescence decays via Savitzky-Golay derivative, truncated exponential fitting, and Richardson-Lucy deconvolution with median-filter regularization. ~4.7x faster than the Python reference.
- **`MaxEntTcspc.h` / `MaxEntTcspc.cpp`**: Maximum-entropy TCSPC lifetime analysis (`me_vin4_E.m` analogue, ported from `chisurf maxent_decay.core.solver`). Recovers a lifetime distribution P(tau) via the quadratic MEM objective with bounded-QP active-set steps. Matches the ChiSurf reference to 1e-12 (p, chisq, Q, niter) and is ~11x faster.

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
bounds shape the fit. That split is also what makes the AD conversion possible:
`i_lbfgs` adds the bound penalty *and its gradient* to whatever a registered
analytic-gradient callback returns (`i_lbfgs.h:313-320`), whereas a term added
to the objective by hand — as the old `tau` penalty was — is invisible to that
callback and would have made the analytic gradient wrong by exactly `-1` in the
`tau` component below the bound.

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

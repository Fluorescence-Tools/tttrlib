# PRD-011 — Photon-by-photon HMM: one class, three inference paths

> **PRD #:** 011 · **Status:** Done · **Created:** 2026-08-02 · **Rewritten:** 2026-08-04 · **Owner:** tpeulen
> **Related:** PRD-005 (photon simulator — supplies the adversarial ground truth used throughout), `DecayFitPrior` system
> **Supersedes:** `PRD-011-physics-aware-h2mm.md`. That draft's framing — "make H2MM physics-aware" — was the source of most of its difficulties, and several of its premises were contradicted by measurement. See *What the previous draft got wrong*.

## Summary

tttrlib ships one `HMM` class over photon streams with **three inference paths**, selected by
which method you call rather than by a flag:

```cpp
HmmModel     fit      (...);                          // MLE — classic H2MM, unchanged
HmmModel     optimize (..., HmmRestraints*, HmmEmissionSpec*);  // MAP / parameterised M-step
HmmPosterior sample   (..., HmmEmissionSpec*);        // blocked Gibbs — calibrated
HmmEval      evaluate (const HmmModel&) const;        // sufficient statistics + score
```

**H2MM survives as an algorithm name, not a type.** It is a specific published method —
photon-by-photon maximum-likelihood EM with a categorical emission over detection streams — and
`fit()` still is exactly that. The physics-aware work is a *sibling* path, not a mutation of it.

**Physics lives outside C++ and enters as priors.** The engine never learns what a Förster
radius is. It is parameterised by what scoring needs — a lifetime spectrum and a stream split
per state — and the map from a structure to those quantities is supplied from outside, either
as a prior or through `evaluate()`, which hands back one E-step's sufficient statistics so an
external optimiser can own the M-step.

**tttrlib owns its inference and is fully self-validating.** This reverses the previous draft's
scope boundary in both directions: tttrlib owns MAP, Gibbs and `evaluate`, and its tests never
call IMP.bff. IMP.bff **supplies priors** and may optionally consume `HmmEval` for a joint
metamodel. Dependency direction (IMP.bff → tttrlib) is unchanged.

## The design hinge: the core is alphabet-agnostic

`forward_burst`, the backward pass, Viterbi and FFBS touch emission through exactly one
expression — `obs[i * p + y]`, with `p` a runtime int and `y` an int32 symbol. The core neither
knows nor cares what a symbol means:

```
H2MM / MLE      y = stream                             p = n_streams
physics-aware   y = stream * n_micro_bins + micro_bin  p = n_streams * n_micro_bins
                obs' = P(stream|k) * f_{k,stream}(micro_bin)
```

**The shared core is the same code for both** — no emission policy, no virtual dispatch, no
branch in the per-photon loop. The paths differ only *outside* the recursions: `fit` re-estimates
a free categorical table; the physics-aware path **generates** it from a few parameters per
state. That is where the E–τ coupling lives, and it is what separates a blinking (dark) acceptor
from real transfer: blinking moves the intensity ratio only, FRET moves ratio *and* lifetime
together.

## Measured evidence

Every number below is from this repository's tests and benchmarks. Where a figure was replicated
over seeds, the spread is given; single-seed figures were wrong three times during this work and
are not quoted.

### The parameterisation is what makes the fit findable

Negative control: two states with an **identical** intensity ratio (`P(acceptor) = 0.5`), one at
genuine FRET (`E = 0.5`, `τ_DA = 2.0`), one with a dark acceptor (`τ_D = 4.0`).

| model | emission dof / state | per-photon accuracy |
|---|---|---|
| stream-only, `p = 2` | 1 | 0.527 — chance; the oracle also scores 0.523 |
| lifetime, **free** categorical, `p = 64` | 63 | 0.616 |
| lifetime, **parameterised**, `p = 64` | 2 | **0.782** (oracle 0.792) |

The middle row is the finding: the lifetime axis alone is not enough. A free categorical over the
product alphabet carries 126 parameters and EM does not find the good optimum even from multiple
restarts. **`fit()`/`optimize()` are unsafe on a product alphabet without a spec**, and say so.

### Modelling terms that pay for themselves

| condition | naive `dE` | matched `dE` | gain |
|---|---|---|---|
| crosstalk α = 0.08 | 0.0328 | 0.0036 | 9× |
| linker width σ = 6 Å | 0.0181 | 0.0018 | 10× |
| multi-exponential donor | 0.0261 | 0.0034 | 8× |
| σ = 6 + multi-exp donor | 0.0358 | 0.0014 | **26×** |

Baseline noise floor `dE = 0.0035`.

### Micro-time resolution is a correctness requirement, not a refinement

Fitting a known 45.0 / 62.0 Å pair: 64 bins → 46.2 / 63.0; 256 → 45.2 / 62.3; **1024 → 45.0 /
62.2**; 4096 (native) → 45.0 / 62.2. At ~40k photons the *statistical* width on `R` is ±0.2 Å,
**smaller than the discretisation bias**, so a credible interval computed at 256 bins looks
precise while sitting a full interval-width from the truth. Bin width scales against the
*fastest* decay component. The distance quadrature was cut 61 → 15 nodes (relative error 4e-5)
to pay for it.

### Inference is calibrated — and one path is not

Simulation-based calibration, 300 replicates, draws thinned by a *measured* autocorrelation time:

| | p(uniform) | outer/flat | verdict |
|---|---|---|---|
| **Gibbs** (`B0`/`B1`/`A01`/`A10`) | 0.81 / 0.93 / 0.89 / 0.80 | 1.12 / 1.13 / 1.05 / 0.82 | **calibrated** |
| **VB** (same four) | <1e-4 all | 2.27 / 2.60 / 3.42 / 3.37 | too narrow |

VB carries 2.3–3.4× too much rank mass in the tails *and* its transition rows show a slope on the
U — it draws transition rates systematically **too low**, i.e. it reports slower dynamics than
reality. **VB is therefore not shipped.** Shipping it beside a calibrated sampler would mean
shipping a knowingly biased estimator whose only advantages are speed and the ELBO.

The headline deliverable — **a calibrated posterior over a physical quantity (distance) from
photon data** — is delivered and SBC-verified (p = 0.92, tail mass 1.06, flat).

### Uncertainty: three options, one of which must not be quoted

Nominal 95% interval, 40 datasets × 100 replicates:

| interval | pooled coverage | use for |
|---|---|---|
| `sample()` (Gibbs) | calibrated by SBC | anything kinetic; posterior shape |
| burst bootstrap | **94.4 % ± 1.8 %** | the plain MLE path (no prior) |
| `posterior_sd_analytic` | **~63 %** | quick relative comparison **only** |

The analytic width conditions on the state path and so drops `Var(E[θ|y,path])` — a lower bound
at roughly half width, exactly as its docstring says. It is 364× cheaper (0.04 s vs 16.3 s on
7500 photons) with means agreeing, and it **must not be quoted as an error bar**.

Bootstrap floors, both measured: by burst count 90/94/93 % at 20/40/120 bursts; by replicate
count 89.4 % at 40 versus 94.4 % at 100 and at 250 — **use ≥100 replicates**.

### Performance

~2 ms per forward–backward pass over 200k photons (400 bursts × 500, 3 states). A Gibbs sweep
costs **1.17×** an EM iteration (1.038 ms vs 0.886 ms) — an earlier claim that Gibbs would be
*cheaper* was wrong by the whole cost of the tick-level bridge, which is the piece FFBS needs
because `A` is the one-tick matrix while FFBS samples at photons. Emission-table cost is
negligible beside the `n²`/`n⁴` caches, which do not scale with `p` at all.

The bootstrap needs no engine change: a replicate is the same dataset with its **burst list**
resampled, and the loaders accept duplicate rows — ~7 ms/replicate on 313 bursts / 32k photons,
copying no photon data.

## What the previous draft got wrong

Kept deliberately, because each error cost real work and the reasoning generalises.

1. **"tttrlib adds no inference engine beyond MAP; the Bayesian compute lives in IMP.bff."**
   Wrong in both directions. tttrlib owns MAP, Gibbs and `evaluate`, and is fully testable alone;
   IMP.bff supplies *priors*.
2. **"Bit-identical when constraints are null" / "bit-comparable".** Unachievable in floating
   point, and vacuous besides — `fit` is the same code, untouched. Replaced with a 1e-10 relative
   agreement criterion.
3. **"Mode blending" for non-conjugate priors on simplex entries.** Not MAP. Rejected outright;
   `validate()` raises and points at the physical parameterisation where such priors belong and
   work.
4. **The ELBO as "the principled model-selection criterion".** hFRET's own authors caution that
   the evidence lower bound "is generally not a rigorous metric for de novo model selection".
   Combined with the SBC result, it is a heuristic scan here — and VB is not shipped anyway.
5. **Nonparametric state counting as out of scope.** It works, and beats the BIC scan: a sparse
   `Dirichlet(α/M)` at `α/M ≈ 1e-3`, M = 8, gets **4/4** against BIC's 3/4, including k = 4 where
   BIC fails. It fails on the MAP path for a structural reason (the `(α−1)` pseudo-count is
   bounded by 1 against thousands of counts) but succeeds under Gibbs, which can genuinely empty
   a component.
6. **`DecayState` carrying `brightness`, `decay_times`, `decay_amplitudes`, `decay_type`.**
   Replaced by `SimDecay`, which already *is* the shared decay representation; it gained
   `pdf(bin)` so one object both samples and scores. `brightness` was a dead field — see below,
   it stays dead.
7. **"Framework adopted by research community" as a success criterion.** Unmeasurable. Replaced
   by the CI gates below.
8. **Multi-molecule coincidence is never mentioned** — and it is the largest confound measured
   anywhere in this work.

## Findings that close features rather than open them

The most valuable outcome of the measurement programme was deciding **not** to build three
things. Each was in the plan; each is closed on evidence.

### Multi-molecule coincidence — real, large, and undetectable per burst

A burst holding two molecules is a superposition of two chains, and a single-chain model can only
explain interleaved photons as rapid switching. On two **static** species, **5 % coincidence**
already invents switching and makes the BIC scan report a third state in **8 of 8** datasets —
stronger than crosstalk or background, which bias `E` by 0.03–0.05 but create neither states nor
kinetics.

**No detector ships, because none works.** Against `SimEngine` ground truth
(`emitting_molecule()` names each photon's emitter, so diffusion decides overlap), replicated
over seeds: duration 0.53 ± 0.04, photon count 0.53 ± 0.04, peak rate 0.47 ± 0.04 at 5.3 %
coincidence — and 0.54 / 0.55 / 0.54 at 17.7 %, i.e. **no better where it matters more**, because
at high occupancy the "clean" bursts are contaminated too. The plausible rate-step-with-E-change
signature is also at chance.

Selection is a bad trade (5.3 % → 4.8 % for half the data, ~25 clean bursts lost per coincident
one removed). Occupancy is better but saturates (0.125/0.25/0.5/1.0 → 2.5/5.3/9.3/17.7 %).
**There is no setting at which coincidence goes away — it is an acquisition-design problem.**

### Rate-blindness is a robustness property, not a gap

The likelihood conditions on photon *arrivals*, so `P(symbol|state)` is invariant to the overall
rate. A **425,000×** intensity swing across a burst yields 1 state in 4 of 4 datasets. A
state-dependent rate term would attribute the PSF transit to state changes, exactly as
coincidence does. It is sound only for immobilized molecules — which is the setting Pressé's
generator formulation assumes.

**The invariance covers position, not states.** If states differ in brightness the kinetics are
biased: `k01/k10` reaches **2.69** against a truth of 1.0 at a 3:1 ratio. Emissions stay correct.
States differing *only* in brightness are invisible.

### Per-state brightness — not shippable, and the planned guard would not have helped

Only the ratio between two states is identifiable. Replicated, true ratio 3.00: with the **true
tick-level path** the estimator is unbiased at every switching rate (3.00–3.09), so there is no
information ceiling. What is lost is *attribution* — a gap is charged to the state at its start
even when the chain switched inside it (flat field: 2.96 slow → 1.99 fast).

Worse, **the PSF envelope does not cancel in the ratio**. At slow switching, where the
attribution error is absent, the bias grows with focus depth: flat 2.96, 55× → 2.66, 10⁶× → 2.19.
The plan was to ship behind a **switching-rate** guard; that would have caught only the first
error and left a number that looks trustworthy and is 10–27 % low. The tick-level bridge does not
rescue it either — it conditions on endpoint states but not on the gap containing *no photons*,
and using that evidence is precisely the rate-aware likelihood ruled out above. `brightness`
therefore stays off `DecayState`.

## Scope — delivered

| piece | status |
|---|---|
| Rename H2MM → HMM (types, files, on-disk tags, tests, docs); no alias layer, 0.27.0 untagged | done |
| `HmmRestraints` (scored) / `HmmConstraints` (imposed); penalised objective; `HmmModel::logpost` | done |
| Product alphabet (`set_bursts_micro`, `n_micro_bins`, `n_symbols()`) | done |
| `HmmEmissionSpec` — emission table from per-state, per-stream lifetime spectra via `SimDecay` | done |
| Parameterised M-step (re-fits decay parameters, not free columns) | done |
| Analytic EMG micro-time density for a Gaussian IRF | done |
| `HmmEval` — loglik, `xi`, `gamma_obs`, `prior_acc`, `score` (Fisher's identity) | done |
| `HMM::sample` — blocked Gibbs (FFBS + tick-level bridge + Dirichlet), split-R̂/ESS | done |
| `HmmEval::posterior_sd_analytic` — closed-form width, documented as a lower bound | done |
| Sparse `Dirichlet(α/M)` state counting under Gibbs | done |
| Burst bootstrap | done — **no API needed**, resample the burst list |
| Phasor diagnostic | done — **no API needed**, `gamma_obs` + `DecayPhasor` |
| Variational Bayes | **not shipped** — measured over-confident and biased low on rates |
| Multi-molecule detector | **not shipped** — no statistic beats chance |
| Per-state brightness | **not shipped** — envelope does not cancel |
| Rate-aware likelihood (diffusing data) | **not shipped** — would manufacture dynamics |

Unrelated but blocking, fixed on the way: `SimCounterRandom::reset` sought by *burning* draws,
making the Philox path quadratic — which is why no full test-suite run had ever completed.

## Success criteria

Replacing the previous draft's unmeasurable list. All are CI tests in `test/python/hmm/`.

**Correctness**
- [x] Free parameterisation at `n_micro_bins == 1` agrees with `fit` to 1e-10 relative under a flat prior
- [x] Gibbs posterior means match a brute-force enumerated posterior on a small model
- [x] `score` verified by finite differences (trans score is simplex-only — `matmul_norm` renormalises)
- [x] `SimDecay` round-trips sample ↔ score
- [x] Blinking-vs-FRET negative control separates under the parameterisation and fails under stream-only

**Calibration and coverage**
- [x] SBC flat for Gibbs on the categorical emission (4 parameters) and on **distance**
- [x] Bootstrap coverage 94.4 % ± 1.8 % against nominal 95 %
- [x] `posterior_sd_analytic` documented and tested as a lower bound, never as an error bar

**Robustness — what the model must *not* do**
- [x] A 425,000× intensity envelope adds no state
- [x] Static species + 5 % coincidence is characterised, not silently absorbed
- [x] No burst statistic exceeds AUC 0.70 for coincidence detection (pooled over seeds)
- [x] Replicates are verifiably independent (`SimEngine` is deterministic without seed variation)

**Performance**
- [x] `fit` unchanged — shared core is the same code
- [x] Gibbs sweep within ~1.2× an EM iteration

## Known limitations, accepted

- **Label switching at fit time is not solved.** Index-keyed priors attach to a state index, but
  states are exchangeable and `factory_model` seeds randomly across restarts. Mitigation is the
  fixing mask (pins identity hard) and seeding from prior modes. At *summary* time it **is**
  handled: draws are relabelled canonically before `mean`/`quantile`/`rhat`/`ess` — R-hat read
  15.2 on raw draws against 1.75 relabelled.
- **Second-order observation model.** Pressé's formulation makes a photon *mark a radiative
  transition*, so the observation depends on the previous *and* current superstate. tttrlib (like
  H2MM) conditions on the current state alone — the standard reduction once photophysics is
  marginalised into an emission distribution. Fine as it stands; not fine if the photophysical
  state is ever modelled explicitly.
- **Absolute brightness is not identifiable.** tttrlib sees only detections and conditions on `N`
  throughout. Estimating an absolute rate needs the empty pulses too (Fazel *et al.* 2023).
- **Non-conjugate priors on simplex parameters** are unsupported by design.
- **No registry entry**, so chisurf cannot build a GUI from the registry. Deferred.

## Process notes worth keeping

Four apparent failures during this work were errors in the *validation harness*, not the code,
and two published numbers were wrong for the same underlying reason. The rules that came out of
it:

- **Never score a detector on data whose confound you built in.** "Photon count separates
  coincident bursts at AUC 0.87" came from bursts made coincident by *concatenating* photon
  lists, so they held more photons by construction.
- **`SimEngine` is deterministic.** Repeated calls return byte-identical data unless
  `seed_diffusion`/`seed_emission` are varied — so "pooling 8 runs" pooled 8 copies, and the
  error bar computed from them was fiction. A test now asserts replicates differ.
- **A harness reporting "fail" is no more trustworthy than one reporting "pass".** The SBC
  harness was the one done right: checked against a conjugate Gaussian with known exact, inflated
  and deflated posteriors *before* its verdicts were believed.
- **Measure against the simulated path, never against a fit.** A negative control built on EM's
  search measures the search — EM escapes into a degenerate optimum with an exact zero whose
  likelihood is genuinely higher.
- **Thinning must be set per data size from a measured ESS**, never carried over from another run.

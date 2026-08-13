# PRD-038 — A general N-pattern fit, and one MaxEnt engine instead of two

**Status:** 🟢 Done (2026-08-13).
**Depends on:** `DecayFitProblem::patterns` (existing scaffolding, previously
unused by any built-in model), `MaxEntTcspc.cpp`'s Skilling-Bryan engine
(existing, verified), `Mat.h`'s `mat_lstsq_minnorm` (existing).

## The claim

tttrlib should expose a **general N-arbitrary-pattern fit**: given N fixed
reference decay shapes and data, find the non-negative amplitude of each,
with a choice of three regularisations — none (plain NNLS), Tikhonov (L2),
and maximum-entropy (Skilling-Bryan, toward a prior).

Investigating that surfaced a second problem worth fixing on the way: the
library had **two independent MaxEnt implementations that disagreed**, and
the one outside the decay module had a **sign-inverted entropy term**.

## Why the existing pattern fit does not cover it

`DecayFit26` ("pattern fit" by its own doc examples) mixes exactly **two**
fixed patterns (IRF and background) with **one** free mixing fraction,
constrained to `[0, 1]`. That is a narrower problem — a fixed pair, a
constrained scalar — not the general case: an arbitrary count of patterns,
independent non-negative amplitudes, no sum-to-one constraint. `DecayFit26`'s
own AD-conversion question was already closed elsewhere (PRD-010) and is
untouched by this PRD.

`DecayFitProblem::patterns` (`std::vector<std::vector<double>>`) already
carries an arbitrary list of fixed patterns — built for the C-ABI plugin
interface — but before this PRD, no built-in registered model read it.

## The MaxEnt duplication found on the way

Two implementations of "fit a maximum-entropy-regularised inversion" existed,
solving different objectives with different algorithms:

- `MaxEntTcspc.cpp` (`modules/spectroscopy/decay`): the real Skilling & Bryan
  (1984) algorithm, ported from ChiSurf's `maxent_decay.core.solver`. An
  active-set bound-constrained QP inside an outer Newton-like MEM iteration.
  Verified: `test_maxent_tcspc.py::TestTcspcMem*` recovers a known
  lifetime/distance from simulated Poisson data through it.
- `MaxEnt.cpp` (`modules/spectroscopy/corrections`): a separate
  projected-gradient implementation of what its own header called "Shannon
  entropy" — `S(x) = sum[x*log(prior*x) - x]`.

That second formula's sign is backwards. Measured directly: at the uniform
prior (`x = [1,1,1]`), the correct Skilling-Bryan entropy is `S = 0` (its
maximum, by construction); `MaxEnt.cpp`'s formula gave `S = -3.0` there and
`S = +12.9` for a spiky, far-from-prior solution — larger, not smaller, the
further the solution moves from the prior. Since the regulariser is
`objective = chi^2 - nu^2 * S(x)` and is *minimised*, a formula that grows
away from the prior makes the regulariser reward moving away from it — the
opposite of what a maximum-entropy prior is for.

## What changed

**One shared engine**, `modules/math/include/MaxEntQp.h` /
`src/MaxEntQp.cpp`: `quadpr_bound` (the bounded QP) and `run_mem` (the
Skilling-Bryan outer loop) — the exact algorithm from `MaxEntTcspc.cpp`,
relocated, not rewritten. Plus one new function, `build_normal_equations`,
that turns an arbitrary design matrix + measurement into the
`H, g0, const_term` quadratic form both callers need.

- `MaxEntTcspc.cpp`'s `tcspc_quadpr_bound` / `tcspc_run_mem` are now thin
  wrappers delegating to the shared engine — same public signature, same
  algorithm, so every existing caller (the Python bindings, both `solve_*`
  functions) is unaffected. Verified: all 15 `test_maxent_tcspc.py` cases
  pass unchanged, byte-for-byte through the relocated code.
- `MaxEnt.cpp`'s `maxent_invert` is now a wrapper around
  `build_normal_equations` + `run_mem` — the correct-sign engine, not the
  buggy projected-gradient one. Same public signature
  (`maxent_invert(A, b, nu, n_rows, n_cols, max_iter, tol)`). Both existing
  tests (`test_corrections.py::TestMaxEnt`) still pass — their tolerances
  (`delta=0.5`, "all positive") were always loose enough to hold under either
  engine, so the sign fix is a real behaviour change that the old tests
  happened not to catch.

**A new NNLS solver**, `modules/math/include/Nnls.h` / `src/Nnls.cpp`: the
classical Lawson & Hanson (1974) algorithm — the same one
`scipy.optimize.nnls` wraps. This is deliberately **not**
`quadpr_bound`'s active-set sweep at `nu=0`: that solver's own docstring says
it is not KKT-correct (never releases a clamped variable, no dual-feasibility
check), adequate for one MEM Newton step but not a general bounded solver.
NNLS needed the real thing, so it got one — verified against
`scipy.optimize.nnls` directly (see below).

**The new pattern fit**, `modules/spectroscopy/decay/include/DecayPatternFit.h`
/ `src/DecayPatternFit.cpp`: `decay_pattern_fit(data, patterns, mode,
reg_strength, prior, max_iter, tol)` — one design matrix (column `k` =
pattern `k`), three ways to resolve it:

- `PatternFitMode::kNone` — plain `nnls`.
- `PatternFitMode::kTikhonov` — `build_normal_equations` + `quadpr_bound` on
  `H + 2*lambda*I`, non-negative L2-regularised least squares.
- `PatternFitMode::kMaxEnt` — `build_normal_equations` + `run_mem`, toward a
  uniform (default) or caller-supplied prior.

SWIG-bound in all four languages (`ext/python/DecayPatternFit.i`, included
from all of `ext/{python,r,java,js}/tttrlib.i`, following `MaxEnt.i`'s
plain-`std::vector` pattern — no NumPy typemaps, this is not a hot loop).

## Verified

`test/python/decayfit/test_decay_pattern_fit.py`, 9 cases:

- `kNone` matches `scipy.optimize.nnls` to `1e-6` on a clean mixture, `1e-4`
  under noise with 6 patterns, and recovers a noiseless mixture (including a
  pattern with true amplitude exactly zero) to `1e-6` with `chisq < 1e-12`.
- Amplitudes are never negative, including on a near-collinear pattern set.
- `kTikhonov` at `lambda=0` matches `kNone` exactly; amplitude norm shrinks
  monotonically as `lambda` increases (`0 -> 1 -> 10 -> 100`).
- `kMaxEnt` at `nu=0` matches `kNone`; large `nu` collapses every amplitude
  toward the same multiple of the prior (uniform and non-uniform priors both
  checked).

`test/python/corrections/test_corrections.py::TestMaxEnt` (2 cases) and
`test/python/decayfit/test_maxent_tcspc.py` (15 cases) both still pass,
confirming the consolidation changed no observable behaviour for the two
pre-existing callers beyond the intended sign fix.

Full suite (2699 tests) run clean after the consolidation, before the new
pattern-fit files were added (see the C++/Python regression section below for
the run covering both together).

## Definition of Done

- [x] `MaxEntQp.h`/`.cpp`: `quadpr_bound`, `run_mem`, `build_normal_equations`
      — the shared engine, ported byte-identical from `MaxEntTcspc.cpp`.
- [x] `MaxEntTcspc.cpp`'s two functions become delegating wrappers; public
      signature unchanged; all 15 existing tests pass unchanged.
- [x] `MaxEnt.cpp`'s `maxent_invert` rewritten onto the shared engine, fixing
      the sign-inverted entropy term; public signature unchanged; both
      existing tests still pass.
- [x] `Nnls.h`/`.cpp`: Lawson-Hanson NNLS, verified against
      `scipy.optimize.nnls`.
- [x] `DecayPatternFit.h`/`.cpp`: the three-mode general pattern fit.
- [x] SWIG bindings in all four languages.
- [x] `test/python/decayfit/test_decay_pattern_fit.py`, 9 cases.
- [x] Full C++ build + full Python suite green.
- [x] This PRD; `CHANGELOG.md`; `okf/log.md`; module `README.md`s.

## Out of scope

- `DecayFit26`'s own two-pattern mixture is untouched — it solves a
  different, narrower problem (one constrained fraction, not N independent
  non-negative amplitudes) and its own AD question was already closed.
- No registry entry / C-ABI plugin wiring for `decay_pattern_fit` — it is a
  free function today, not a registered fit model. Wiring it through
  `DecayFitProblem`/`FitRegistry` so it is reachable the same way
  `fit23`..`fit26` are is a natural follow-up, not required by what was
  asked.
- No benchmark against a competitor: this is a new capability, not a
  performance change to an existing path, so PRD-010's A/B discipline
  ("if there is no clear win do not use") does not apply — there is nothing
  to A/B against.

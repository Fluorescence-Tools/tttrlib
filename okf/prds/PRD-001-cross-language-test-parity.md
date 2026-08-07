# PRD-001 — Cross-language test parity (Python ⇄ R ⇄ Java)

> **PRD #:** 001 · **Status:** Done · **Created:** 2026-07-03 · **Owner:** tpeulen

## Summary

Mirror the *core* behaviours verified by the Python test suite in the R and Java
bindings, asserting identical numbers on identical input files, so the shared C++
core is exercised through every language's marshalling layer.

## Problem / motivation

The three bindings are generated from one SWIG interface over one C++ core, but
test coverage is lopsided: **Python ~74 test files, R 1, Java 4.** The C++ core is
well tested via Python; the per-language marshalling (`numpy.i` / `rarrays.i` /
`jarrays.i`) is barely tested. Binding-specific bugs slip through — e.g. R's 2^53
macro-time precision loss, Java array copy-back (INPLACE release mode), S4 vs
proxy dispatch. These only surface when the same operation is run from R/Java.

## Goals

- Run the same *core operations* from R and Java as Python does, on the same
  files, asserting the same canonical values.
- Cover every array typemap (int / short / long-long / double; IN / INPLACE /
  ARGOUT) at least once per language.
- Wire the new tests into CI (`build_test_r_lnx`, `build_test_java_lnx`).

## Non-goals

- Full 1:1 port of all 74 Python files.
- Python-only surface: `%pythoncode` sugar, numpy dtype edge cases, plotting,
  pandas interop.
- R directors (`PdaCallback` subclassing) — unsupported in R.

## Current coverage

| Area | Python | R | Java |
|---|:--:|:--:|:--:|
| tttr (core) | 20 | 1 | 1 |
| clsm | 18 | 0 | 2 |
| correlator | 1 | 0 | 0 |
| decayfit | 13 | 0 | 0 |
| burstfilter | 15 | 0 | 0 |
| pda | 5 | 0 | 0 |
| misc | 2 | 0 | 0 |

## Proposed approach

**Canonical reference values.** The pattern already exists and is the template:
`test/python/tttr/test_cross_language_reference.py` pins source-of-truth constants
read from a fixed file; `test/r/test_tttr.R` and `test/java/TTTRSmokeTest.java`
assert the same constants:

```
REF_SIZE=183657  REF_N_MICRO_CHANNELS=4096  REF_MACRO_FIRST=56916
REF_SUM_MICRO=242477881  REF_SUM_MACRO=443406877425185
```

For each area: (1) pin canonical values in a Python reference test, then (2)
assert identical values from R and Java.

**Priority order:** tttr core → correlator → clsm → decayfit → burstfilter/pda.

**R** (`test/r/`, `Rscript` + `stopifnot`): S4 generics `Class_method(obj, …)`;
cast large sums with `as.numeric()`; assert exact integers only below 2^53.

**Java** (`test/java/`, plain asserts + `System.exit(1)`): `obj.method()`; native
`long`/`int[]` — no precision caveat.

## Milestones

- **M1 — done.** Python reference test pins constants for tttr size/micro-channels,
  macro/micro/routing sums, and the CorrelatorCurve(3,5) size.
- **M2 — done (verified locally in an R 4.4 conda env).** `test/r/test_tttr.R`
  mirrors tttr + correlator; `test/r/test_clsm.R` mirrors the CLSM intensity sum
  (40×256×256 = 3364714). Both wired into the `build_test_r_lnx` CI job. R passes
  routing channels as a native `c(0L)` vector (rarrays → `std::vector<int>`;
  `VectorInt32()` construction is not usable in R).
- **M3 — mostly done.** Java `TTTRSmokeTest` mirrors tttr (macro/micro/routing via
  the PRD-002 `%ARRAY_INTO` accessors) + CorrelatorCurve + burst search;
  `CLSMTest`/`LifetimeTest` cover CLSM. decayfit remains.
- **M4 — in progress.** **burst_search** (len 586, sum 59237329) and the
  **micro-time histogram** (len 4096, peak channel 814, value 676) now asserted
  across Python + R + Java, all verified locally. Remaining: decayfit (fit23–26)
  and PDA — these need helper-generated IRF/decay arrays and per-language struct
  construction, so they are **deferred** (not cleanly cross-language).

**Coverage now — all verified locally** (Python installed, Java JNI build, R in an
r-base 4.4 conda env):

| Operation | Python | R | Java |
|---|:--:|:--:|:--:|
| size / n_micro_channels | ✓ | ✓ | ✓ |
| macro/micro/routing array sums | ✓ | ✓ | ✓ |
| macro/micro/routing at index 0 | ✓ | ✓ | ✓ |
| micro-time resolution | ✓ | ✓ | ✓ |
| `get_tttr_by_channel` sizes (ch 0, 8) | ✓ | ✓ | ✓ |
| `get_used_routing_channels` | ✓ | ✓ | ✓ |
| correlator curve size | ✓ | ✓ | ✓ |
| burst search (len + sum) | ✓ | ✓ | ✓ |
| micro-time histogram (len/peak) | ✓ | ✓ | ✓ |
| CLSM dims + intensity sum/max | ✓ | ✓ | ✓ |
| CLSM mean-micro-time (nonzero count) | ✓ | ✓ | ✓ |
| CLSM phasor (valid count) | ✓ | ✓ | ✓ |
| **DecayFit23.modelf** (model function) | ✓ | ✓ | ✓ |
| **Full fit loop — fit23 / fit24 / fit25 / fit26** | ✓ | ✓ | ✓ |
| **PDA** (S1S2 probability matrix) | ✓ | ✓ | ✓ |
| **PDA** (1-D histogram) | ✓ | ✓ | ✓ |

CLSM phasor now works in **all three**. The native `get_phasor` has a pointer-typed
default argument (`TTTR* tttr_irf = nullptr`) that R's SWIG overload dispatch could
not resolve; the additive `CLSMImage::get_phasor_v(...)` accessor (scalar args only,
returns the flattened `[frame,line,pixel,2]` image) closes that gap. R takes `g` at
the odd 1-based positions and counts `g > -1` → 412275, identical to Python/Java.

### decayfit + PDA — now fully cross-language

- **`DecayFit23.modelf`** was already cross-language (plain numeric arrays).
- **Full fit loop (`fit23`)**: the classic `fit()` uses an in-place `double*`/
  `short*` interface that only Python's numpy marshals. Added a cross-language
  accessor **`DecayFit23::fit_v(vector<double> x, vector<int> fixed, DecayFitData*)`**
  returning `[twoIstar, fitted x…]` (by-value inputs → a clean vector return in
  every language). The classic `fit()` is retained unchanged. `DecayFitData` is
  built from the same numeric arrays in all three (numpy / `c()` / `VectorDouble`).
  Verified: twoIstar 23.802337, fitted τ 0.74219, r_s 0.25974.
- **PDA**: builds a Poisson `pF` + two-species model and asserts both the S1S2
  matrix (max 0.01800533) and the 1-D histogram (sum 0.92940452). Java reads the
  2-D/1-D outputs through new `Pda::get_S1S2_matrix_into(double[])` and
  `Pda::get_1dhistogram_y_into(double[])` accessors (helpers.i); R uses the 5-arg
  `Pda(...)` constructor and the all-defaults `get_1dhistogram` (the enum-typed and
  explicit-arg overloads don't match R dispatch), taking `y` as list element 3.
- **fit24 / fit25 / fit26** carry the same `fit_v` accessor (x lengths 8/9/2) and
  are **each verified cross-language** with deterministic inputs (a nonzero
  background of 0.2 keeps the MLE finite): fit24 2I* 2.41049, fit25 2I* 4.738831
  (best τ 0.5), fit26 2I* 2.218772 — identical in Python, R and Java. So **all four
  MLE fit models** are covered, not just fit23.

Additive binding accessors introduced (`DecayFit{23,24,25,26}::fit_v`,
`CLSMImage::get_phasor_v`, `Pda::get_S1S2_matrix_into`,
`Pda::get_1dhistogram_y_into`); **no existing interface changed**. PRD-001 is
complete — every core operation the Python suite exercises (tttr, correlator, CLSM
intensity/mean-micro-time/**phasor**, burst, histogram, decayfit modelf + all four
fit loops, PDA S1S2 + 1-D histogram) is now asserted with **identical values in
Python, R and Java, with no remaining language gaps**.

All share the constants in `test_cross_language_reference.py`. Notable
language-specific idioms captured in the tests: R takes channel lists as native
`c(0L)` vectors (`VectorInt32()` construction is unusable in R); R multi-output
getters return `list(NULL, hist, time)` (histogram is `[[2]]`); Java array getters
go through the PRD-002 `%ARRAY_INTO` accessors; burst_search returns a Python
tuple / Java `VectorInt64` / native R vector.

## Risks

- R 2^53 precision forces tolerance-based asserts for large 64-bit sums.
- Reference data must be available in CI (already downloaded for R/Java jobs).

## References

- `test/python/tttr/test_cross_language_reference.py`
- `test/r/test_tttr.R`, `test/java/TTTRSmokeTest.java`, `test/java/CLSMTest.java`
- [docs/r-package.md](../docs/r-package.md) · [docs/imagej-plugin.md](../docs/imagej-plugin.md)

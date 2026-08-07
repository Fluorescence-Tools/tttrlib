# PRD-015 — A conformance suite: one case list, four languages

> **PRD #:** 015 · **Status:** ✅ Implemented · **Created:** 2026-08-05 · **Updated:** 2026-08-06 · **Owner:** tpeulen
> **Related:** PRD-001 (cross-language test parity — this is its successor), PRD-002 (Java ND marshalling), PRD-016 (JavaScript bindings — supplies the fourth runner)
> **Supersedes the approach of:** PRD-001, which is `Done` and whose hand-copied-constant pattern does not scale past three languages.

## Summary

Replace hand-copied reference constants with a **declarative conformance suite**:
one committed, reviewed list of cases (input file, operation, expected values),
and a thin runner per language that executes it. Adding a case covers every
binding at once; a binding that cannot yet run a case declares it, so the gap is
data rather than silence.

## Problem / motivation

PRD-001 established the right *idea* — the same operation on the same file must
produce the same number in every binding — and the wrong *mechanism*: the
expected values live inline in each language's test file, written by hand.

Where that has left us:

| | Python | R | Java | JavaScript |
|---|--:|--:|--:|--:|
| test files | 126 | 4 | 5 | — |
| assertions | 1285 `test_` functions | 48 `stopifnot` | 5 `@Test` methods | — |
| location | `test/python/` | `test/r/*.R` | `ext/java/pkg/src/test/…` | — |
| CI job | pip/conda matrix | `build_test_r_lnx` | `build_test_java_lnx` (`mvn test`) | — |

Three problems follow, and the third is the one that matters.

**Copying does not scale.** Five reference constants live in
`test/python/tttr/test_cross_language_reference.py` and are re-typed in
`test/r/test_tttr.R` and `TTTRSmokeTest.java`. Adding a sixth is three edits in
three languages by hand; the fourth binding (PRD-016) makes it four. Nobody will
do it, so nobody has: R and Java between them assert on the order of 50 values
against Python's 1285 test functions.

**Nothing detects drift.** If someone changes a Python constant and not the R
one, both suites still pass — they are independent assertions that happen to
share a number. The only thing tying them together is that a human typed the same
digits twice. There is no mechanism that fails when they disagree.

**The gaps are invisible.** "R does not test the correlator" is currently a fact
you learn by reading `test/r/`. It should be a row in a generated table. The
per-language marshalling layers (`numpy.i` / `rarrays.i` / `jarrays.i`, and next
a `jsarrays.i`) are exactly where the binding-specific bugs live — R's 2^53
macro-time truncation, Java's INPLACE copy-back, the stale-argout `%apply` trap —
and they are the least-tested code in the project.

## Goals

1. **One source of truth** for cross-language expectations: a committed data file,
   not code, read identically by every runner.
2. A runner per language (Python, R, Java, JavaScript) small enough to port in an
   afternoon — target ≤ 300 lines each, no per-case code.
3. **Drift is a failure.** A changed expectation fails every language at once,
   which is what makes the number *shared* rather than *coincidentally equal*.
4. A **generated coverage matrix** (case × language × ran/skipped/unsupported)
   published in the docs, so gaps are a table rather than folklore.
5. Every array typemap category — `IN_ARRAY{1,2,3}`, `INPLACE_ARRAY*`,
   `ARGOUTVIEW[M]_ARRAY*` — exercised at least once per language, per PRD-001's
   original goal, which is still not met.

## Non-goals

- Porting all 1285 Python test functions. Python stays the deep suite; the
  conformance suite is the *shared* subset, targeted at ~120 cases.
- Testing language-idiomatic sugar (`%pythoncode`, S4 dispatch details, JS
  Promise ergonomics). Those stay in each language's own tests.
- Features a binding genuinely cannot express — directors are unavailable in R
  and in SWIG's Node-API generator, so `PdaCallback` is declared
  `unsupported: [r, js]` rather than worked around.

## Proposed approach

### The case list

`test/conformance/cases/*.json`, one file per area (`tttr`, `correlator`,
`clsm`, `decayfit`, `burst`, `pda`, `histogram`, `registry`). A case is a small
program in data:

```json
{
  "id": "tttr.spc130.basic",
  "data": "bh/bh_spc132.spc",
  "steps": [
    {"op": "TTTR", "args": ["$data", "SPC-130"], "as": "t"},
    {"op": "size", "on": "t", "as": "n"},
    {"op": "sum", "of": {"op": "macro_times", "on": "t"}, "as": "sum_macro"}
  ],
  "expect": {"n": 183657, "sum_macro": 443406877425185},
  "tolerance": {"sum_macro": 0},
  "unsupported": []
}
```

The vocabulary is deliberately tiny — construct, call a method, reduce an array
to a scalar (`sum`, `mean`, `min`, `max`, `first`, `nth`, `shape`, `dtype`),
compare. Anything needing more than that is a case for the Python suite, not for
here. Keeping the interpreter small is what keeps four runners maintainable; the
moment the case format grows control flow, this design has failed.

### Expected values: generated once, reviewed, then frozen

`tools/conformance_update.py` runs the cases through the **Python** binding and
writes the `expect` blocks. It is run explicitly, never in CI, and its output is
reviewed as a diff — an expectation that changes without an intended behaviour
change is the bug this suite exists to catch.

Python is the *generator* but not the *oracle*: the Python runner asserts against
the committed file exactly like the others, so a Python-side regression fails
Python too.

### Runners

| Language | Location | Harness | Notes |
|---|---|---|---|
| Python | `test/python/test_conformance.py` | pytest, one param per case | `json` + `numpy`, no new deps |
| R | `test/r/conformance.R` | `stopifnot`, exit 1 | needs a JSON reader; `jsonlite` is the only new dep, and R CI already installs packages |
| Java | `ext/java/pkg/src/test/…/ConformanceTest.java` | JUnit 5 `@ParameterizedTest` | Jackson via Maven |
| JavaScript | `test/js/conformance.test.mjs` | `node:test` | `JSON.parse`, no dep |

Each runner is a dispatch table from `op` name to a call. The per-language work
is the dispatch table, and it is where the marshalling actually gets exercised:
`macro_times` on a `TTTR` is a `numpy` array in Python, a numeric vector in R, a
`long[]` in Java, a `BigInt64Array` or `Float64Array` in JS.

### Numeric equality across languages, stated once

The parity rules that PRD-001 learned the hard way become properties of the case
format instead of comments in three files:

- `tolerance: 0` means exact integer equality. A runner in a language whose
  numbers are IEEE doubles (R; JavaScript without BigInt) must reject any
  expected integer with magnitude ≥ 2^53 rather than compare it as a double —
  the current macro-time sum (4.4 × 10^14) is safely below, and the runner must
  fail loudly if a future case is not.
- Floating-point cases carry a relative tolerance; the default is `1e-12` and is
  stated in the file, not chosen per runner.

### Coverage matrix

`tools/conformance_matrix.py` reads the case files plus each runner's
skip/unsupported report and emits `doc/conformance.rst`: rows = cases grouped by
area, columns = the four languages, cells = ✓ / skip(reason) / unsupported. The
docs job publishes it. This table is the artefact that answers "how good is
parity really" without anyone reading four test suites.

### Target coverage

Priority follows PRD-001's, extended: **tttr core → registry → correlator → clsm
→ decayfit → burst/burstfilter → pda → histogram.** The registry is new and
cheap: `registry()` is pure data, identical in every language, and comparing the
whole JSON blob across bindings is one case that covers a lot of surface.

## What landed (2026-08-06)

All six milestones are done. Concretely:

* `test/conformance/` holds the case format (`schema.json`), the vocabulary
  contract (`OPS.md`), the rules (`README.md`) and **67 cases** across thirteen
  areas — `tttr`, `datastore`, `registry`, `correlator`, `bitmask`, `histogram`,
  `clsm`, `decayfit`, `pda`, `burst`, `tiff`, `phasor`, `selection`.
* **Every case runs in every binding.** The matrix has no `skip` and no `n/a`
  in any column: 67 / 67 / 67 / 67. Two gaps that were declared got closed at
  the *binding* rather than papered over — see below.
* Four runners, all green: `test/python/test_conformance.py`,
  `test/r/conformance.R`, `ConformanceTest.java`, `test/js/conformance.test.mjs`.
* `tools/conformance_update.py` generates expectations; `tools/conformance_matrix.py`
  renders `doc/conformance.rst`. CI runs all four and publishes the matrix.
* The five PRD-001 constants now exist in **exactly one file**. The four files
  that duplicated them — `test_cross_language_reference.py`, `test_tttr.R`,
  `TTTRSmokeTest.java`, `cross_language_reference.test.mjs` — are deleted, their
  every assertion superseded by a case.

### Array-typemap coverage

The goal was at least one ✓ per language per category. Nine of eleven are there:

| category | covered by | all four? |
|---|---|---|
| `IN_ARRAY1` | channel selections, column fills, IRFs | ✓ |
| `IN_ARRAY2` | `hist.update` | ✓ |
| `IN_ARRAY3` | `tiff.write_f64` | ✓ |
| `INPLACE_ARRAY1` | `bitmask.to_bytes` | ✓ |
| `ARGOUTVIEW_ARRAY1` | `Column::get_*_view` | ✓ |
| `ARGOUTVIEWM_ARRAY1` | `hist.counts`, micro-time histogram | ✓ |
| `ARGOUTVIEW[M]_ARRAY2` | `pda.s1s2`, `burst.properties` | ✓ |
| `ARGOUTVIEWM_ARRAY3` | `clsm.intensity`, `tiff.read_f64` | ✓ |
| `ARGOUTVIEWM_ARRAY4` | `clsm.fluorescence_decay` | ✓ |
| `INPLACE_ARRAY3` | `clsm.decay_of_pixels` | ✓ |
| `INPLACE_ARRAY2` | *(no C++ user exists)* | n/a |

Getting there needed real binding work, not just cases: `jarrays.i` gained
`IN_ARRAY2` (`double[][]`) and `IN_ARRAY3` (`double[][][]`) typemaps, which it
had never had, and `helpers.i` gained `find_bursts_into`, `get_histogram_into`
and a free `tiff_read_f64_into`.

`INPLACE_ARRAY3` has exactly one live user in the library —
`CLSMImage::get_decay_of_pixels`'s mask — and `clsm.decay_of_pixels` now runs it
in all four. That needed `jarrays.i` to grow in-place 2-D and 3-D typemaps
(`double[][]` / `byte[][][]`, flattened in and written back out, because a Java
nested array has no contiguous block to hand C++).

**`INPLACE_ARRAY2` cannot be covered, because nothing uses it.** The `%apply` in
`misc_types.i` maps `(double* inplace_output, int n_output1, int n_output2)` and
no function in the library takes that signature — it is a dead mapping, and
`grep` is the whole proof. So the goal's "at least one ✓ per language per
category" is met for every category that has an entry point. Removing the dead
`%apply` is a separate tidy-up; leaving it costs nothing but a line in this
table.

`ARGOUTVIEWM_ARRAY4` was briefly declared unsupported in R and is not any more.
SWIG's R overload dispatcher tests the output-pointer parameter that
`rarrays.i` removes from the R signature, so its `argc` branches are off by one
and only the all-defaults call resolves. The fix is the one `get_phasor_v`
already used for the same reason: `CLSMImage::get_fluorescence_decay_v` returns
a plain `std::vector<int>`, which has no output pointers to trip over. The
defect still blocks the parameterised form of `tttr.microtime_histogram`, where
the all-defaults call happens to be the one the cases want.

### Two gaps closed at the binding, not in the case file

A declared gap is honest, but it is still a case that does not run somewhere.
Both are gone:

* **`clsm.fluorescence_decay` in R** — fixed with the `_v` accessor above.
* **The generic `dtype` op is removed.** It asked each binding for an array's
  element type, which R cannot answer *by construction* — it has one numeric
  type. A case using it could never run in all four. What that case was really
  guarding is a truncated 64-bit macro time or a sign-extended unsigned micro
  time, and `tttr.spc130.value_ranges` now guards exactly that with minima and
  maxima, which every language can produce. `ds.column_dtype` stays: it reads a
  C++ `ColumnType`, not a language type.

**Case count.** 67, against the PRD's "~120" target. That number was a guess at
what breadth would cost. All eight areas this PRD named are covered — tttr,
registry, correlator, clsm, decayfit, burst, pda, histogram — plus three it did
not: `datastore` (PRD-019), `bitmask` and `tiff`, each added because it was the
only way to reach a typemap category. The honest limit now is that a further
case in an area already covered buys much less than the first one did.

### The runners outgrew their line budget

Goal 2 asked for runners of ≤ 300 lines each. They are 751 (Python), 733 (Java),
528 (R) and 531 (JavaScript). The half of that goal which mattered still holds —
**no per-case code**: a runner is a dispatch table and a comparison, and adding
a case costs nothing in any of them. What the line count tracks is the
*vocabulary*, now 129 ops over thirteen areas, and roughly a third of each file is
the comment explaining why a given op is spelled differently in that language.

The number to watch is therefore ops, not lines. The cap that keeps this
maintainable is the one in `OPS.md` — construct / call / reduce / compare and
nothing else — and it has held: no op takes a branch or a loop over cases.

### One deviation, and what the suite turned up

**The matrix is an artefact, not a checked-in file to diff.** M6 says CI should
fail if a case file changes without the matrix being regenerated. It cannot:
which cases a runner executes depends on which data files that CI job
downloaded, so a byte-exact committed matrix would be wrong the moment the data
set changed. What CI enforces instead is deterministic and needs no reports —
the schema, unique ids, every op a case names existing in the vocabulary, every
op being documented in `OPS.md`, and every expectation being bound by a step.
The matrix is built from the four artefacts and published to the job summary.

**Bugs the suite found**, which is the argument for it:

* `jsarrays.i` rejected a **zero-length TypedArray** of the correct type — an
  empty `ArrayBuffer` has a null data pointer, and the caller read null as "wrong
  element type". Writing a zero-row table from JavaScript was impossible. Fixed.
* SWIG's R backend emits **two different names for the same scoped-enum
  constant** at namespace scope, so six enums (`ColumnType`, `Hdf5WriteMode`,
  `AxisKind`, `HistStorage`, `SuperResMethod`, `TiffDType`) were unusable from R
  — including two already in the shipped R module. Worked around by passing them
  as integers.
* Group proxies **dangled after GC in R and in Java**. See PRD-019 criterion 21.
* SWIG's R **overload dispatcher matches against the C++ parameter list**, so it
  tests the output-pointer arguments `rarrays.i` removes from the R signature.
  Only the all-defaults call resolves. It blocks the parameterised form of
  `TTTR::get_microtime_histogram` and of `CLSMImage::get_fluorescence_decay`;
  `test_clsm.R` had already worked around it without naming the cause.
* `DecayPhasor::compute_phasor_bincounts` takes a `std::vector<int>&`, which
  **R cannot pass at all**: SWIG's R dispatcher wants a typed S4 proxy and
  `VectorInt32()` hands back a bare externalptr it will not match. Added
  `phasor_of_bincounts`, the `IN_ARRAY1` overload, which is this project's
  house style for an array input and works natively in all four.
* Two runner bugs of the same shape, caught by cases rather than by review:
  Python's `len` on a 3-D image answered 40 (its first axis) where the others
  answered 2621440, and R's `unlist` on an ARGOUTVIEW matrix produced the
  **transpose** — PDA's S1S2 peak moved from cell 189 to cell 99. Both were
  invisible to sum/max, which is why the cases pin an index.

## Milestones

- **M1 — format & Python runner.** Case schema (documented, with a JSON Schema
  so a malformed case fails fast), `conformance_update.py`, ~20 `tttr` cases,
  Python runner green. The five PRD-001 constants become the first cases.
- **M2 — R runner** over the same 20 cases; `test/r/test_tttr.R`'s duplicated
  constants deleted in the same commit (the point is removal, not addition).
- **M3 — Java runner**, same, replacing `TTTRSmokeTest`'s inline constants.
- **M4 — breadth.** Grow to ~120 cases across all eight areas, including one
  case per array-typemap category. Coverage matrix generated and published.
- **M5 — JavaScript runner.** Lands with PRD-016; the JS binding's acceptance
  criterion is that it passes the same case list.
- **M6 — enforce.** CI fails if a case file changes without the matrix being
  regenerated; new-area checklist in `test/README.md` says "add a conformance
  case".

## Risks

- **The case interpreter grows.** The failure mode is a bespoke scripting
  language with four implementations. Mitigation: a hard cap — if an area needs
  an op that is not construct/call/reduce/compare, it does not belong in the
  suite. Review any PR that adds an `op`.
- **Regeneration used as a fix.** `conformance_update.py` makes failures easy to
  make disappear. Mitigation: it is not runnable in CI, its diff is required in
  the PR, and `test/README.md` says plainly that regenerating to make a test pass
  is the one thing this suite exists to prevent.
- **`jsonlite` in the R CI.** One more package on a job pinned to R 4.4.3 for
  SWIG-runtime reasons. Acceptable; the alternative (hand-rolled JSON parsing in
  R) is worse.
- **Data availability.** Cases name files in `tttr-data`; the R/Java jobs today
  `curl` exactly two files. The runner must skip-with-reason on a missing file
  and report it in the matrix, so a partial data checkout degrades to a visible
  gap rather than a red build.

## Acceptance

- ✅ The five PRD-001 constants exist in exactly one file in the repository —
  `test/conformance/cases/tttr.json`. The four files that duplicated them are
  deleted.
- ✅ Changing one expected value in `test/conformance/` fails Python, R, Java and
  JS in CI. Verified by flipping `tttr.spc130.size` and
  `datastore.group_handle_survives_50_adds` and watching the runners go red.
- ✅ `doc/conformance.rst` renders a matrix with no empty column for any built
  binding — **met**, 87 cases and four columns, 87/87 in each, with no skip, no
  `n/a` and no declared gap — and every array-typemap category has at least one
  ✓ per language — **met for every category that has a C++ entry point**.
  `INPLACE_ARRAY3` was the last one uncovered and is now exercised by
  `clsm.decay_of_pixels` in all four bindings.

  `INPLACE_ARRAY2` remains without a ✓, and **cannot get one by writing a
  case**: its `%apply` in `misc_types.i` binds
  `(double* inplace_output, int n_output1, int n_output2)`, and the only
  `inplace_output` function in the library is `DecayConvolution`'s **1-D**
  one. The `%apply` is dead — the honest close is to delete it, not to invent a
  caller so the matrix looks full.

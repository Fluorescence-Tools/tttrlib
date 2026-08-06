# The cross-language conformance suite

One committed list of cases, four thin runners. The same operation on the same
file must produce the same number in Python, R, Java and JavaScript — and the
number lives here, once, rather than being re-typed into four test files.

This is PRD-015. It replaces the mechanism of PRD-001, whose hand-copied
constants did not survive contact with a third binding, let alone a fourth.

## The one rule

**A failing case means a binding is wrong. It does not mean the number needs
updating.**

`tools/conformance_update.py` exists to add cases and to record intended
behaviour changes. Running it to make a red test go green destroys the only
thing this suite provides — it is not four suites that happen to agree, it is
one number that four runners read. There is no mechanism that can stop you; the
diff in the pull request is the mechanism, so the expectations are formatted to
be read.

## Layout

| | |
|---|---|
| `cases/*.json` | the cases, one file per area. **The source of truth.** |
| `schema.json` | JSON Schema for a case file; a malformed case fails fast |
| `OPS.md` | the op vocabulary — the contract the four dispatch tables implement |
| `py/interpreter.py` | the Python dispatch table, and the reference the other three are ported from |
| `py/cases.py` | loading, validating, comparing |

The runners:

| Language | File | Harness |
|---|---|---|
| Python | `test/python/test_conformance.py` | pytest, one param per case |
| R | `test/r/conformance.R` | plain script, exit 1 on failure |
| Java | `ext/java/pkg/src/test/…/ConformanceTest.java` | JUnit 5 `@ParameterizedTest` |
| JavaScript | `test/js/conformance.test.mjs` | `node:test` |

## Running them

```sh
# Python
TTTRLIB_DATA=/path/to/tttr-data python -m pytest test/python/test_conformance.py

# R  (needs jsonlite)
TTTRLIB_R_SO=build-r/ext/tttrlib.so TTTRLIB_R_WRAPPER=build-r/ext/tttrlib.R \
TTTRLIB_DATA=/path/to/tttr-data Rscript test/r/conformance.R

# Java
TTTRLIB_DATA=/path/to/tttr-data mvn -f ext/java/pkg test -Dtest=ConformanceTest

# JavaScript
TTTRLIB_DATA=/path/to/tttr-data node --test test/js/conformance.test.mjs
```

Set `TTTRLIB_CONFORMANCE_REPORT=<path>` on any of them to get the JSON report
`tools/conformance_matrix.py` merges into `doc/conformance.rst`.

## Adding a case

1. Write it in the right `cases/<area>.json` with an empty `"expect": {}`.
   Use only ops from `OPS.md`; if you need a new one, read the cap in that file
   first.
2. `python tools/conformance_update.py --id <your.case.id>` fills the
   expectations in.
3. **Read the diff.** Every number in it is a claim about behaviour.
4. Run the other three runners. A case that only Python can run is a case that
   is not doing its job, so either make it work everywhere or declare the gap:

```json
"unsupported": {"r": "R has one numeric type, so the element width is not observable"}
```

A declared gap shows up in the published matrix as `n/a` with its reason. An
undeclared one shows up as a failure, which is the right way round.

## Why integers stop at 2^53

`tolerance: 0` means exact equality. R has one numeric type and JavaScript's
plain numbers are doubles, so an exact integer expectation at or above 2^53
would silently become an approximate one in half the runners.

Every runner refuses such an expectation rather than comparing it — the
generator too, so the case cannot be written in the first place. When a real
quantity is that large (the HT3 file's macro-time sum is 1.7 × 10^16), pin
something exact instead: the first and last value, or the sum of a leading
slice. `tttr.ht3_clsm.basic` is the worked example.

## What belongs here, and what does not

The conformance suite is the **shared subset**, targeted at the operations every
binding exposes. It is not a port of the Python suite: most of those ~1300 tests
cover C++ behaviour, which is language-independent and would be tested twice
while testing the binding not at all.

What belongs here is what differs *between bindings* — the marshalling layers
(`numpy.i`, `rarrays.i`, `jarrays.i`, `jsarrays.i`), where the binding-specific
bugs actually live: R's macro-time truncation, Java's INPLACE copy-back, the
stale-argout `%apply` trap, a sliced TypedArray read from the wrong offset.

Language-idiomatic sugar stays in each language's own tests.

---
type: Reference
title: Test workload tiers and the fast lane
description: How the Python suite is tiered by measured cost, why the tiers live on the tests rather than in a durations file, and the cold-cache trap that made the first measurement wrong.
tags: [testing, pytest, performance, conftest, ci]
status: stable
generated: { by: "claude-code/claude-opus-5", at: 2026-08-06T16:20:00Z }
sources:
  - id: full-run
    resource: test/python
    title: Full-suite run with --durations=0, cold then warm, 2026-08-06 (1833 tests)
    author: claude-code/claude-opus-5
    last_modified: 2026-08-06
  - id: conftest
    resource: test/conftest.py
    title: Lane selection, module derivation and the workload audit
    last_modified: 2026-08-06
---

# Why this exists

The suite had one cost marker, `slow`, on 17 tests, and a README claiming the
whole thing took eleven minutes and that `-m "not slow"` cut it to three. All
three numbers were wrong, and the marker was on the wrong tests. This note
records what the cost actually looks like, because the shape is not what anyone
guesses, and the design that followed from it.

# What the suite actually costs

Measured warm, 8-core macOS laptop, 2026-08-06:

| | |
|---|---|
| full suite | **17.8 min** (32.6 min cold) |
| `--lane standard` | 2.8 min |
| `--lane fast` | 1.7 min |

Two facts that matter more than the totals:

**Cost is concentrated to an extreme degree.** 29 tests out of 1833 are **85% of
the runtime**. Five of them, in `clsm/test_ism_psf_model.py`, are a fifth of the
suite on their own. Dropping those 29 leaves 98% of the tests running in a tenth
of the time — which is why a fast lane is worth having at all, and why it costs
almost no coverage.

**The expensive group is not the one people name.** Warm, by group: `hmm` 46%,
`clsm` 25%, `simulation` 24%, everything else together under 6%. The old `slow`
marker was applied entirely to `simulation/` and `hmm/` and covered only 25% of
the runtime; the single biggest file, `clsm/test_ism_psf_model.py`, carried no
marker at all. "The simulations are the slow ones" is half right and led to the
wrong 17 tests being marked.

# The trap: cold cache inflates I/O-bound tests four-fold

**Do not tier from the first run after a checkout.** The initial full run was
32.6 min; the same suite warm was 17.8. The difference is not uniform — it lands
almost entirely on tests that open large `.ptu`/`.ht3`/image files, because each
one re-reads the file and the first read comes off disk. `test_clsm.py` measured
64s cold and 8s warm; individual `clsm` tests moved by 10-20x.

Tiering off the cold numbers put 39 heavy / 67 slow / 7 smoke markers in, most
of the `clsm` ones measuring disk rather than work. Re-measured warm it is
29 / 30 / 3. The audit (below) caught this on its first run, which is the only
reason it did not ship.

Warm is the right basis: the inner loop a fast lane serves is always warm, and
cold timings depend on what else touched the disk.

Beware also that a concurrent rebuild skews everything — several tests moved 2-4x
between runs while the extension was being relinked in another terminal.

# The design

Tiers are declared on the test, measured as setup + call + teardown:

| marker | cost | `fast` | `standard` |
|---|---|---|---|
| *(none)* | < 1s | yes | yes |
| `slow` | 1-5s | no | yes |
| `heavy` | > 5s | no | no |
| `smoke` | any | **yes** | **yes** |

## Why markers and not a durations file

The obvious alternative is a generated `durations.json` that conftest reads and
marks from. Rejected: it breaks on every rename, it is a second place the truth
lives, and it hides from the diff the fact that someone just made a test thirty
times more expensive. A marker moves with the function and shows up in review.

The cost of markers — that they drift — is paid for by the audit instead.

## The audit

Every run compares what each test actually cost against the tier it declares and
reports the mismatches:

```
========================= workload markers out of date =========================
    9.4s  mark `heavy`  test/python/hmm/test_gibbs.py::TestGibbs::test_converges
    0.2s  drop `slow`   test/python/tttr/test_TTTR.py::Tests::test_reading
```

It works from *any* run, because it only judges tests that actually executed —
the fast lane catches a test that has crept over a second just as well as a full
run catches one that has grown past five. There is nothing to regenerate.
`--strict-workload` turns the report into a non-zero exit.

It reports at **2x** the boundary, deliberately. Timings move with the machine,
and an audit that cries wolf on a loaded laptop is one everyone learns to ignore.

Not enabled in CI: a shared runner's timing variance would make it flaky. It is a
tool for a person, run when the report looks wrong.

## Why `smoke` exists

Cost and coverage are not aligned. A few files have **no** test under a second —
they open a large image per test — so a purely cost-based fast lane drops them
entirely, `test_clsm.py` among them. One test in each carries `smoke`, which
overrides the cost tier and keeps the file represented.

The lane also names any file it would leave with no tests at all. A lane that
silently stops covering a subsystem is the failure mode worth shouting about,
because it looks exactly like a subsystem with no bugs.

## Parametrized tests

A function-level marker covers every param. That is usually right, but where only
one param is expensive use `pytest.param(..., marks=pytest.mark.slow)` —
`simulation/test_engine.py::test_rng_thread_count_independent` is the example
(Mt19937 is 2.4s, the other three RNGs are 0.1s). The audit flags the
over-marking if you forget.

# The module map is now derived, not maintained

`--modules <name>` used to expand through a hand-written `MODULE_TEST_GROUPS`
dict with a comment asking for it to be kept in step with `modules/`. It was not.
It had drifted **both ways**: entries for five modules that no longer exist
(`hist`, `opt`, `superres`, `localization`, `nn`), and missing all twelve `io_*`
modules, so `--modules io_pq` — a real module with real tests — was rejected as
unknown.

It is now parsed out of the `NAME` / `DEPENDS` / `TEST_DIR` each module already
declares in `modules/**/CMakeLists.txt`, with the transitive closure taken over
*dependents* so `--modules core` still runs everything downstream. Verified live:
the `io_pto` module added mid-session became selectable with no edit.

Three `TEST_DIR` declarations were wrong and are fixed: `sim` pointed at
`test/python/sim` and `io_image` at `test/python/tiff`, neither of which exists,
and `pda` claimed the whole `test/python` tree. Note the `modules/io/*` modules
sit a directory deeper than the rest — a glob of `modules/*/CMakeLists.txt`
misses all of them, which is how `io_image` stayed broken.

**Unclaimed groups.** `bva`, `twocde` and `correlator` are named by no module's
`TEST_DIR`, so a strictly derived mapping would make them unreachable from
`--modules` forever. They run in every `--modules` invocation instead, and are
named in the output. Someone who knows should assign them an owner; it was not
guessed.

# Where it is wired

* `test/conftest.py` — lanes, module derivation, the audit. Lives at `test/`
  rather than `test/python/` because pytest reads `pytest_addoption` only from
  *initial* conftest files.
* `pyproject.toml` — marker registration.
* CI: the `Quick Test` PR gate ran the full 33-minute suite despite its name; it
  is now `--lane standard`. `main` still runs everything.

# Open

The fast lane **skips** the 29 `heavy` tests rather than running a cheaper
variant of them. Giving those tests a size knob — fewer photons, fewer sweeps —
would execute the code path in the fast lane and catch API breaks and crashes
that skipping cannot, at the cost of editing the test bodies. Not done.

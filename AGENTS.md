# AGENTS.md

Notes for anyone — human or agent — writing code in this repo.

## The wider stack's knowledge base is in ChiSurf

This repo keeps its own OKF bundle at `okf/` for photon-level concerns. Anything
spanning the fluorescence-modelling stack — `imp.bff`, `imp-tricks`, the IMP
build, and the scope boundaries between the four repositories — lives in
[`../chisurf/okf/`](../chisurf/okf/index.md), which is the largest bundle and the
one to write cross-project findings into. Start at
[`../chisurf/okf/references/imp-ecosystem.md`](../chisurf/okf/references/imp-ecosystem.md).
The two bundles share one agent message board (`okf/agent-board.md`).

## Inline comments are fragments; prose goes in the docstring

Two registers, don't mix them.

**Inline, in the body** — telegraphic. Lowercase, no full sentences, one or two
lines, sitting on the line it explains:

```cpp
// load-bearing: ~log2(dt) chained products drift off 1
// side effect: score simplex-only
if (s > 0.0) { ... }
```

**Docstring, above the declaration** — full prose, the argument, the measured
numbers, the thing that was tried and rejected. This is what ends up in the API
docs, so it can be long if it earns it.

Never write a paragraph inside a function body, and never leave a docstring so
terse it just restates the signature.

## Comments carry the *why*, not the *what*

The code says what it does; restating it is noise. Write down what is no longer
visible: the measurement that settled an argument, the alternative that was
tried and was worse, the trap that cost a day. If a line looks pointless and
isn't, say so — someone will otherwise "simplify" it back into a bug.

Keep them short. Any number you quote must have been measured, and replicated
over seeds — several single-seed figures here turned out to be wrong.

## Swearing is fine in code that users never read

Comments, commit messages, test names, developer-facing logs: swear if it makes
the point land. "Don't fuck with this ordering, it's load-bearing" beats three
sentences of hedging and flags the danger better.

Keep it out of anything a user sees:

- exception messages and warnings
- Doxygen/docstrings that end up in the published API docs
- `doc/`, `README.md`, `CHANGELOG.md`, gallery examples
- public identifiers — class, method, parameter, file names

Aim it at code, never at people. A vendor's undocumented record layout is fair
game; a contributor is not.

## The rest

Build commands, test invocation and the hard constraints (no public API breaks,
std-only C++) live in `BUILDING.md` and `modules/README.md`.

## Agent message board: coordinate before you act

Before starting any non-trivial work, read `okf/agent-board.md`. Post a claim
with your scope and the files you will touch. Update it when done, blocked,
or handing off. This is how agents across tttrlib and chisurf avoid editing
the same files and conflicting.

## Porting numerical code: check, implement, benchmark

When porting matrix/linear-algebra or numerical code into tttrlib from
another project (ChiSurf, mmfdb, a paper, anywhere):

1. **Check first.** Search tttrlib for an existing implementation of the
   operation you need (matrix multiply, solve, decomposition, convolution,
   etc.). Do not reach for an external library — including Eigen or
   Armadillo — until you have confirmed the math does not already live here.
2. **Implement if missing.** If tttrlib does not have it, implement it in
   std-only C++ (the project constraint — no third-party deps for the core).
3. **Benchmark vs Eigen and Armadillo.** Before merging, benchmark the
   tttrlib implementation against Eigen and Armadillo on representative
   input sizes. Record the numbers (replicated over seeds) in the PR
   description or the module README. If the hand-rolled version is
   significantly slower, that is a finding worth discussing — but the
   no-third-party-deps rule stands unless an exception is granted.

This is non-optional. A port that skips the benchmark step is incomplete.

**tttrlib has no external numerical dependency, and that is now literally
true.** Eigen was the last one and is gone; `Mat.h`, `QREigen.h`, `NelderMead.h`,
`i_lbfgs.h` and `GradVec.h` in `modules/math` are the library's own. The only
files that include Eigen are `benchmarks/bench_mat.cpp` and
`benchmarks/bench_gradvec.cpp`, which exist to measure against it — that is what
step 3 looks like when it is done. Two things it taught, both cheap to reuse:
record the measured *cost* of a replacement even when you keep it (`GradVec` is
13–16% behind Eigen at two of three sizes, and the README says so), and time
kernels with `CLOCK_THREAD_CPUTIME_ID` rather than wall clock — on a loaded
machine wall clock reported the same binary as anywhere from 0.22× to 4.77×.

## Module documentation: Every folder in `modules` must have a `README.md`

Every folder under `modules/` (including top-level module folders and submodules like `spectroscopy/fcs`, `spectroscopy/burst`, `spectroscopy/decay`, `spectroscopy/hmm`, `spectroscopy/pda`, `imaging/clsm`, `imaging/superres`, `imaging/localization`, `io/*`, `util`, `core`, `simulation`, `plugin`, `registry`, `cli`) must contain a `README.md`.

Whenever modifying, touching, or creating files in any folder under `modules/`, you MUST inspect and update the `README.md` file in that module folder to keep documentation aligned with code changes.


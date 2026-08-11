# PRD-035 — A generic log-domain HMM lattice, so the binned-trace HMM stops being a second implementation

**Status:** 🟢 Done both sides (2026-08-11). tttrlib: `opus-5/ac9f6757`, board
ticket `T-20260811-07` — the four functions plus `hmm_estep_log`, the NumPy
bindings, the tests and the benchmark. ChiSurf: the "Remove numba dependencies"
session, `0840f70f0` — delegation, five kernels deleted, allow-list 16 → 15.
**The tttrlib half is still uncommitted** (shared-index reasons); see the
known-dependency note at the end. See **Measured, after the port** below.
**Priority:** requested as **high** by the user (2026-08-11) — it is the last
structural blocker on ChiSurf's numba retirement for `core/math/hmm.py`.
**Depends on:** nothing new. Uses the NumPy-typemap pattern established in
`ext/python/MaxEntTcspc.i` (see `okf/bindings/marshalling-cost.md`).
**Consumer waiting on this:** ChiSurf `core/math/hmm.py`, item 3 of
`okf/subsystems/numba-retirement.md` (chisurf) — currently one of 16 files
still importing numba, and the only one whose route had to be corrected.

## The claim

tttrlib should expose a **generic log-domain HMM lattice** — forward, a fused
backward/posteriors/xi sweep, and Viterbi — over a **caller-supplied
log frame-probability matrix**. Emissions stay the caller's business; the
library owns the recursions.

That is the piece ChiSurf's `GaussianHMM` is missing and the reason it still
compiles five kernels with numba.

## Why the existing HMM does not cover it

This was checked before proposing, and the answer is not "bind what is already
there". tttrlib's `HMM` is a **photon-stream** model:

- per-**burst**, photon-indexed, with **Δt-dependent** transition matrices
  (`A = I` for coincident photons after down-scaling);
- **discrete symbol** emissions read from a table, `obs[k * p + y]`;
- its recursion is **scaled, not log-domain** — `forward_burst` in
  `modules/spectroscopy/hmm/src/HMM.cpp` normalises each row and returns the
  scale factors, precisely because "alpha underflows fast, and the sums get
  reused";
- and it is `static`, so nothing outside that translation unit can call it.

ChiSurf's is a **binned-trace** model: uniform time steps, one multivariate
normal per state, log domain throughout. `HmmEval.gamma_obs_np` / `xi_np` are
E-step outputs *of that other model*, not a lattice anyone can borrow. Two
different algorithms with two different numerics; the overlap is the name.

Nor is NumPy an option on the ChiSurf side: the recursions are serial in `t`
and vectorise only over states, so a NumPy rewrite pays Python dispatch once
per sample — for `T = 100,000` that is the whole budget.

## Measured baseline the port must not regress

ChiSurf's numba kernels, arm64, `float64`, random `log_frameprob`
(one call, not one EM iteration):

| T | K | forward | backward+posteriors+xi | Viterbi |
|---:|---:|---:|---:|---:|
| 10,000 | 3 | 1.07 ms | 1.35 ms | 0.14 ms |
| 10,000 | 6 | 2.59 ms | 3.66 ms | 0.24 ms |
| 100,000 | 3 | 10.81 ms | 12.66 ms | 2.78 ms |
| 100,000 | 6 | 26.77 ms | 36.76 ms | 3.70 ms |

`core/math/hmm.py` is the **hmmlearn replacement** and is recorded at 1.1–18×
faster per E-step than what it replaced, so a regression here is a visible loss
to users, not an internal detail. **A C++ implementation that merely matches
these numbers is a success** — the goal is removing the second implementation,
not winning a benchmark.

## Measured, after the port

Same machine, arm64, `float64`, one call, best of 20 — against the numba table
above. `tttrlib` includes the wrapper, so this is what a caller pays:

| T | K | forward | backward+posteriors+xi | Viterbi |
|---:|---:|---:|---:|---:|
| 10,000 | 3 | 0.89 ms *(1.07)* | 1.07 ms *(1.35)* | 0.12 ms *(0.14)* |
| 10,000 | 6 | 2.16 ms *(2.59)* | 2.34 ms *(3.66)* | 0.22 ms *(0.24)* |
| 100,000 | 3 | 8.50 ms *(10.81)* | 10.29 ms *(12.66)* | 1.21 ms *(2.78)* |
| 100,000 | 6 | 21.58 ms *(26.77)* | 23.39 ms *(36.76)* | 2.43 ms *(3.70)* |

Matching was the bar; it is 1.1–1.6× faster instead, and the fused backward
sweep gains most at K=6. The point remains removing the second implementation.

**One thing found on the way, and it is not a transcription detail.** The numba
original scaled its transition counts by `exp(maximum + fwd[t,i] - log_prob)`,
which for an impossible sequence is `exp(-inf + -inf - -inf)` = `exp(nan)`.
Because `xi_sum` is the accumulator **shared by every sequence** in an E-step,
one unexplainable frame turned the whole transition matrix into `nan` for that
EM iteration and every one after it — while the posteriors survived, their
uniform fallback absorbing the `nan` total, so the only symptom was a fit that
stopped improving. Correct behaviour: an impossible sequence contributes
**zero** counts. Fixed in ChiSurf first (`f6e960190`) so the parity fixture
records the fixed output; the C++ has the same guard and two tests on it.

## The parity fixture is anchored outside this pair of repositories

A fixture recorded from ChiSurf's numba kernels proves only that the port
matches *us*, which is worth less than it looks when both sides were written by
the same hands. So it was cross-checked against **hmmlearn** — the library
`core/math/hmm.py` replaced — over all ten cases (chisurf
`build_tools/dev_utils/hmm_lattice_fixture.py`, `45246e5ab`):

- forward and backward lattices **bit-identical**, `0.0` difference;
- log-likelihoods agree on every case, both `-inf` ones included;
- posteriors ≤ `3.1e-14` and `xi_sum` ≤ `6.8e-13` — accumulation-order noise
  from fusing three passes into one sweep, not a difference of method;
- Viterbi identical wherever a path exists.

**Where they diverge, and why it is not a defect:** the Viterbi *path* on an
impossible sequence. Every candidate scores `-inf`, so the arg-max is decided
entirely by the tie-break — ours is the lowest state index, hmmlearn's is
something else, and both report `-inf`. The tttrlib tests assert the
log-probability there and deliberately not the path: pinning it would be
pinning a convention as though it were a result.

## The numerics that must be preserved

These are the expensive part of this PRD and the reason it is not a
transcription. All were paid for once already:

1. **`-inf` is a value, not an error.** A structurally constrained model has
   whole columns of `-inf`, and an all-`-inf` frame must yield `-inf`, never
   `nan`. ChiSurf's kernels carry a hand-picked fast-math set —
   `nsz, arcp, contract, afn, reassoc`, deliberately **without `nnan`/`ninf`** —
   because those two license the compiler to fold away exactly the guards the
   recursion depends on, and the resulting `nan` then spreads through the
   M-step into every later iteration while the `-inf` log-likelihood that would
   have reported the problem is gone. In C++ this means **no `-ffast-math` on
   this translation unit**, and a test with an all-`-inf` frame.
2. **A frame no state can explain gets a uniform posterior.** Normalising its
   `-inf` entries otherwise produces `nan`.
3. **Zero sums stay zero.** Row normalisation must not turn `0/0` into `nan`
   — a dead state has to remain dead rather than become undefined.
4. **A sparse Dirichlet prior can underflow to exactly zero**, so `log(0)` must
   map to `-inf` and survive the lattice.

## Proposed surface

Free functions in `modules/math`, no model object, everything `float64`
row-major:

```cpp
// log_startprob[K], log_transmat[K*K], log_frameprob[T*K]
double hmm_forward_log(..., double* fwd /*T*K, out*/);

void   hmm_backward_posteriors_xi(..., double log_prob,
                                  double* posteriors /*T*K, out*/,
                                  double* xi_sum /*K*K, accumulated*/);

double hmm_viterbi_log(..., int64_t* state_sequence /*T, out*/);

void   hmm_backward_log(..., double* bwd /*T*K, out*/);   // standalone, for tests
```

**Binding requirements**, non-negotiable and already established:

- **NumPy typemaps**, never `VectorDouble` — `double* IN_ARRAY1, int DIM1` in,
  `ARGOUTVIEWM_ARRAY1/2` out. The default `std::vector<double>` typemaps cost
  ~50 ns per element, which on a `100,000 × 6` frame matrix is 30 ms of pure
  conversion against a 27 ms kernel. See `okf/bindings/marshalling-cost.md`.
- **The loop stays whole in C++.** One call per sweep, per sequence — never one
  call per time step. This is the standing rule, and it is what makes the
  delegation worth doing at all.
- Accept a **sequence-lengths** argument so a multi-sequence fit is one call
  rather than one per sequence.

## Definition of Done

- [x] `modules/math` gains the four functions above, with no fast-math flags
      that assume finiteness on that translation unit.
      `include/HmmLattice.h`, `src/HmmLattice.cpp`; `-fno-fast-math` /
      `/fp:precise` pinned in `modules/math/CMakeLists.txt`. `hmm_estep_log`
      is there too — the `lengths` form, one call per EM sweep — and the four
      single-sequence entry points stay beside it, so ChiSurf can delegate
      first and change how it loops separately.
- [x] NumPy-typemap bindings; keyword names fixed at the outset, since ChiSurf
      will call them by keyword. `ext/python/HmmLattice.i`; outputs are
      `INPLACE_ARRAY`, not `ARGOUTVIEWM`, because `xi_sum` is an accumulator
      and an EM fit reuses the lattices. `tools/check_swig_multilang.sh`
      passes for all four languages.
- [x] tttrlib tests: a two-state analytic lattice checked against a
      brute-force enumeration of all state paths (feasible at small `T`);
      an **all-`-inf` frame** returning `-inf` with no `nan` anywhere; a
      dead-state row staying zero; Viterbi agreeing with the arg-max of the
      exhaustive enumeration. `test/python/misc/test_hmm_lattice.py` —
      14 tests, plus the 10 recorded cases as subtests. Fixture vendored at
      `test/data/reference/hmm_lattice_numba_parity.npz` (`names` re-typed
      from an object array, so the test never passes `allow_pickle`).
- [x] Benchmarked at the four sizes in the table above, recorded in the PRD.
- [x] ChiSurf `core/math/hmm.py` delegates; its five numba kernels and the
      `import numba` are deleted; parity proven against a fixture recorded from
      the numba original **before** deletion (a live comparison becomes a skip
      the day numba leaves, and a skip reads like a pass).
      chisurf `0840f70f0` — the five are thin forwards; 152 tests green
      together (24 HMM, 8 new parity, the seam guard, the hmm plugin, the ml
      suite). The HMM suite went 8.35 s → 4.94 s, most of it the JIT warm-up
      that is no longer paid.
- [x] `chisurf/test/numba_import_allowlist.txt` loses its `core/math/hmm.py`
      line, and the entry in chisurf's numba-retirement concept is flipped.
      Allow-list 16 → 15.

**One constraint changed hands and is worth knowing.** ChiSurf's
`LOG_DOMAIN_FASTMATH` — the hand-picked numba flag set, and a test asserting
`nnan`/`ninf` were absent from it — is gone, because the flags it named no
longer exist there. The constraint did not disappear; it moved into
`modules/math/CMakeLists.txt`'s no-fast-math property on `HmmLattice.cpp`.
ChiSurf now asserts the **behaviour** (an all-`-inf` frame stays `-inf`) rather
than a flag set, so if a global flags change ever puts `-ffast-math` on that
translation unit, **ChiSurf's suite fails too**, not only tttrlib's. Two
suites in two repositories now depend on that one CMake property.

**Known dependency while the tttrlib side is uncommitted.** A clean tttrlib
checkout does not build a working ChiSurf HMM; `_require_lattice()` raises a
`RuntimeError` naming the missing functions rather than falling back to a
second implementation, which is the right failure — a silent fallback would
reintroduce exactly the duplicate this PRD removes. Recorded in chisurf
`okf/references/known-issues.md`, and closed by committing the tttrlib files
listed on board ticket `T-20260811-07`.

## Out of scope

The existing photon-stream `HMM` is untouched. This adds a second, simpler
primitive beside it; merging the two is not proposed and probably is not
desirable — the scaled per-burst recursion is right for photons and the log
lattice is right for uniform bins.

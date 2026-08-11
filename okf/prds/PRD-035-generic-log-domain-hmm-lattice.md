# PRD-035 — A generic log-domain HMM lattice, so the binned-trace HMM stops being a second implementation

**Status:** 🔵 Proposed
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

- [ ] `modules/math` gains the four functions above, with no fast-math flags
      that assume finiteness on that translation unit.
- [ ] NumPy-typemap bindings; keyword names fixed at the outset, since ChiSurf
      will call them by keyword.
- [ ] tttrlib tests: a two-state analytic lattice checked against a
      brute-force enumeration of all state paths (feasible at small `T`);
      an **all-`-inf` frame** returning `-inf` with no `nan` anywhere; a
      dead-state row staying zero; Viterbi agreeing with the arg-max of the
      exhaustive enumeration.
- [ ] Benchmarked at the four sizes in the table above, recorded in the PRD.
- [ ] ChiSurf `core/math/hmm.py` delegates; its five numba kernels and the
      `import numba` are deleted; parity proven against a fixture recorded from
      the numba original **before** deletion (a live comparison becomes a skip
      the day numba leaves, and a skip reads like a pass).
- [ ] `chisurf/test/numba_import_allowlist.txt` loses its `core/math/hmm.py`
      line, and the entry in chisurf's numba-retirement concept is flipped.

## Out of scope

The existing photon-stream `HMM` is untouched. This adds a second, simpler
primitive beside it; merging the two is not proposed and probably is not
desirable — the scaled per-burst recursion is right for photons and the log
lattice is right for uniform bins.

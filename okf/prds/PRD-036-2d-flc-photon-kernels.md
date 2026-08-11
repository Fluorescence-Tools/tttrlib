# PRD-036 — 2D-FLC photon kernels: the fluorescence-decay correlation belongs in the photon library

**Status:** 🟡 tttrlib side landed (2026-08-11, `opus-5/ac9f6757`, board ticket
`T-20260811-10`) — the kernels, the bindings and both test suites are in and
green. The ChiSurf delegation is not started. See **What the simulation
actually showed** below.
**Priority:** requested by the user (2026-08-11) — mark 2D-FLC a tttrlib
candidate and write it up against the original MATLAB.
**Depends on:** nothing new. Uses the NumPy-typemap pattern from
`ext/python/MaxEntTcspc.i` (see `okf/bindings/marshalling-cost.md`).
**Consumer waiting on this:** ChiSurf `chisurf/plugins/fcs/flc_2d/core.py`,
one of 13 files still importing numba
(`okf/subsystems/numba-retirement.md`, chisurf).
**Reference implementation:** `junk/2D-FLC-code` — the original MATLAB by
Toru Kondo (Schlau-Cohen lab, MIT), with *A Technical Note on 2D-FLC.pdf*.
Read and marked: `README`, `MatlabCodes/TK_Create2DFDC_04.m`,
`MatlabCodes/TK_MyMain_Simu_PhotonStream.m`.

## The claim

Building a 2D fluorescence-decay correlation (2D-FDC) matrix is a **photon-pair
histogram**: walk a macro-time-ordered stream, and for every reference photon
count the micro-time pairs falling in a lag window `dT ± ddT/2`. That is the
same shape as every correlator this library already owns, on the same data, and
it is the last compute in ChiSurf's 2D-FLC plugin that is not either NumPy or
already delegated.

Five kernels move: `_fdc_scan_log_kernel` (one matrix per lag, one photon
pass), `create_2d_fdc_numba_int` (the single-lag builder), `_log_bin_int` and
the two `_ceil_div_*` helpers it needs.

**What does not move**: the inversions (Tikhonov, MEM, the rate-matrix fit) are
already NumPy/SciPy in ChiSurf and stay there. This is about the photon pass.

## Why it belongs here rather than in ChiSurf

- It touches **macro and micro times together** and nothing else. That is this
  library's subject matter, and `TTTR` already carries both.
- It is a **serial pass with a binary search inside** (`searchsorted` for the
  lag window bounds), which NumPy expresses badly and which is exactly what the
  numba decorator is compensating for.
- ChiSurf's copy is the only reason `flc_2d` needs numba at all. The plugin's
  `api.py` came off the allow-list on 2026-08-11 once its last numba use — a
  thread count — was moved behind `core.py`; `core.py` is what remains.

## The reference, and what it fixes about the port

`TK_Create2DFDC_04.m` is the original. ChiSurf's kernel is already a faithful
port of it — same lin/log matrix pair, same log-tick construction
(`t_Imax^(j/(L-1)) * tStep - tStep`), same `dT ± ddT/2` window — so the C++ has
a *third* implementation to agree with, not just ChiSurf's.

Two deliberate differences to preserve rather than "fix":

- ~~ChiSurf builds only the **log-binned** matrix. The MATLAB also returns a
  linear one; it is recoverable by rebinning and nothing asks for it.~~
  **Wrong on both halves, corrected 2026-08-11 by the author of the line after
  checking the call sites.** Something does ask for it —
  `flc_2d/fit/helpers.py` returns `np.diag(mat_lin)` as the linearly-binned
  decay and `api.two_d_fdc` hands `mat_lin` to callers — and it is *not*
  recoverable, because log binning collapses many micro-time channels into one
  bin. This was the blocker that kept `flc_2d/core.py` on the numba allow-list
  after the log scan had landed. See `fdc_scan_axis` below.
- ChiSurf accumulates in **`int64` per chunk and sums**, so the result is exactly
  independent of how the photon stream is partitioned. Keep that: it is what
  lets the chunk count be a pure parallelism decision, and ChiSurf has a test
  pinning it.

## Everything is tested by simulation — this is the requirement, not a nicety

**USER REQUIREMENT.** Every kernel in this PRD is verified against a simulated
photon stream whose answer is known before the analysis runs. Not against
recorded numbers, and not against ChiSurf's output alone: a port checked only
against the thing it replaces cannot tell a faithful port from a shared mistake,
which is the trap PRD-035 hit when a fixture recorded from the code under
replacement encoded a live bug.

The simulator is prescribed by the reference. `TK_MyMain_Simu_PhotonStream.m`
generates a stream from:

- `NumOfState` states, each with an intensity (cps) and a fluorescence lifetime
  (ns);
- an interconversion **rate matrix** `K[n][m]` (1/s), `n → m`, whose
  eigendecomposition gives the equilibrium populations;
- an **IRF** sampled to draw the micro-time;
- a macro grid `Tstep = 1e-6 s` and TCSPC resolution `tstep = 0.004 ns`.

Its default case is the recovery target: **two states, lifetimes 1 ns and 2 ns,
10 s⁻¹ both ways, equal intensity**. ChiSurf already ports this as
`chisurf.plugins.fcs.flc_2d.api.simulate_stream`, with the same parameters, so
the ground truth is reproducible on both sides; this library should simulate
with its own `SimEngine` rather than transcribe the MATLAB generator.

Required tests, each stated as the thing that must come back:

1. **Two well-separated lifetimes are recovered.** Simulate 1 ns and 2 ns; the
   2D-FDC diagonal, inverted, must return two peaks at those lifetimes.
2. **The cross-peaks appear only when the states interconvert.** With the rate
   matrix set to zero the matrix must be diagonal within counting noise; at
   10 s⁻¹ the off-diagonal must grow with lag `dT` and be non-zero well before
   the relaxation time.
3. **The lag dependence recovers the rate.** Scanning `dT` across the
   relaxation time must give a cross-peak amplitude whose fitted relaxation
   matches the simulated `1/(k₁₂+k₂₁)` within the simulation's counting error.
4. **A single state produces no cross-peak at any lag.** The negative control;
   without it a kernel that fabricates correlation passes tests 1–3.
5. **Chunk-count invariance.** Identical matrices for any partition of the same
   stream, exactly — the counts are integers.
6. **Total pair count matches a brute-force double loop** on a small stream
   (ChiSurf has this test; the C++ needs its own).
7. **Photon order and gating**: an unsorted macro-time input is rejected, and
   micro-times outside `[tMin, tMax)` are dropped rather than clamped into the
   edge bin.

Tests 1–4 are the ones that make this a *method* test rather than an
arithmetic test, and they are the reason the simulator is in the requirement.

## Proposed surface

`modules/spectroscopy/fcs` (it is a correlation) or `modules/math`; free
functions, `int64` photon arrays in, `int64` matrices out:

```cpp
// One log-binned matrix per lag, one pass over the stream.
void fdc_scan_log(const int64_t* macro_times, const int64_t* micro_times, int n,
                  const int64_t* lags, int n_lags,
                  int64_t ddT_ticks, int64_t t_min, int64_t t_max,
                  int logt_imax, int n_chunks,
                  int64_t* out /* n_lags * L * L */);

// The single-lag builder, and the log-bin lookup they share.
void fdc_log(...);
int  fdc_log_bin(int64_t tau_ticks, const int64_t* logt_ticks, int n_ticks);
```

**Binding requirements**, already established and non-negotiable:

- **NumPy typemaps**, never `VectorDouble`/`VectorInt` — a photon stream is
  millions of elements, and the sequence-protocol conversion costs ~50 ns each.
- **The loop stays whole in C++**: one call per scan, never one per lag or per
  photon.
- Keep the chunked accumulator, and keep it `int64`.

## A trap this work must not re-create

ChiSurf's `env_bootstrap` rewrites `NUMBA_NUM_THREADS` from settings, and if it
does so after numba's pool has launched, the *next cold compile* raises. That
killed the whole 2D-FLC suite until `flc_2d/core.py` grew a `_sync_numba_threads`
guard (2026-08-11). Once these kernels are C++ the failure mode disappears with
the decorator — which is part of the argument for moving them, and worth stating
because it is invisible until an import order changes.

## Definition of Done

- [ ] Kernels in C++ with no fast-math assumptions about the accumulation.
- [ ] NumPy-typemap bindings; keyword names fixed at the outset.
- [ ] The seven tests above, all driven by a simulated stream with known
      ground truth, including the single-state negative control.
- [ ] Benchmarked against ChiSurf's numba kernel at a realistic photon count
      (1e6–1e7 photons, 100 log bins, ~20 lags); matching is success.
- [ ] ChiSurf `flc_2d/core.py` delegates, its five kernels and `import numba`
      are deleted, and `chisurf/test/numba_import_allowlist.txt` loses the line.
- [ ] The `junk/2D-FLC-code` markers point at this PRD (already done).


## What the simulation actually showed (2026-08-11)

The kernels are `modules/spectroscopy/fcs/{include/Fdc2D.h,src/Fdc2D.cpp}` —
`fdc_scan_log`, `fdc_log`, `fdc_log_ticks`, `fdc_log_bin` — bound through
`ext/python/Fdc2D.i` and, unlike most of this library, **registered in all four
bindings**: `IN_ARRAY`/`INPLACE_ARRAY` are implemented for R, Java and
JavaScript too (`ext/r/rarrays.i` and friends), so no per-language surface was
needed. Tests: `test/python/fcs/test_fdc2d.py` (10 tests, the deterministic
half) and `test_fdc2d_simulation.py` (6 tests, the method half).

**The rate comes back.** Fitting the coupling's decay along the lag axis:

| simulated relaxation | recovered |
|---:|---:|
| 100 windows | **98** |
| 50 windows | **52** |

with the four control curves behaving as the physics demands — a single state
flat at the noise floor (D ≈ 0.0014), two frozen states high and flat
(D ≈ 0.0846 at every lag), and two exchanging states decaying from ≈ 0.078 to
≈ 0.002, twice as fast when the dwell time halves.

**Three things the simulation taught that no amount of reading would have.**

1. **A single molecule with `k = 0` is not a two-state sample.** It starts in
   state 0 and never leaves, so the "frozen two-state" control produced numbers
   *identical* to the one-state control — which is how the mistake surfaced.
   The control now runs one molecule per state and concatenates the streams.
2. **Eight emitters in the focus destroy the signal** (D ≈ 0.0003 at every
   lag). Most pairs then come from *different* molecules, which are independent
   by construction. 2D-FLC is a single-molecule method and a simulation that
   forgets this measures nothing.
3. **A lag shorter than `ddT/2` puts each reference photon inside its own
   window.** The resulting self-pairs are a diagonal spike with no kinetic
   content, and they masked the decay entirely on the first attempt.

**One required test is deliberately not implemented as written.** The PRD asks
that the inverted diagonal show two peaks at the simulated lifetimes. The
inversions are explicitly out of scope — they stay NumPy/SciPy in ChiSurf — so
that test would exercise SciPy under this library's name. What is tested is the
property that *makes* an inversion possible: the matrix separates the two
lifetimes, measured as the partner's mean micro-time shifting with the
reference's. If the inversions ever move here, the peak test belongs with them.

**Not measured yet**: performance against the numba original. The PRD does not
set a target and the correctness work came first; a like-for-like timing is the
obvious next step, together with the ChiSurf delegation.


## The axis became an argument (2026-08-11)

The linear matrix above needed an answer, and the cheap one — a second kernel
with a `lint_bin_factor` — is not the one taken. ChiSurf's linear rule is
`ceil(tau / f)`, which *looks* like different arithmetic from the log path's
`searchsorted - 1` and is in fact **the same lookup** with edges
`[-1, 0, f, 2f, 3f, ...]`. Verified for every `f` in {1,2,3,5} and every `tau`
in 1..39 rather than assumed.

So `fdc_scan_axis` takes the bin edges from the caller, and `fdc_scan_log` is
that function with the log axis filled in — the path already validated against
nine recorded cases is untouched. Any binning expressible as ascending edges now
works, and there is no second copy of the loop to keep in step.

Two consequences worth stating rather than discovering:

- **Two axes meant two passes over the photons.** Measured rather than
  guessed, and it mattered: 1M photons at comparable bin counts (log 100,
  linear 101), one axis **144.8 ms**, two axes as two calls **329.1 ms** — a
  real 2.27x, not noise. So `fdc_scan_two_axes` now fills both in one walk;
  the axes share the photon loop and the window binary search. Two and not N
  because two is what callers need, and a ragged array-of-axes signature would
  cost every caller clarity to serve none; the internals take a list, so a
  third is a signature away.

  The saving shrinks as an axis gets finer — at 1501 linear bins the
  accumulator dominates and the extra pass is a smaller share of a much larger
  cost — so the win is at coarse-to-comparable binning, which is where callers
  live. Worth recording that the first A/B of this reported the second pass at
  4.6–5.9x, which was a `lint_bin_factor = 2` linear axis having **1501** bins
  rather than the ~20 assumed: a 1501² × 8-chunk accumulator is ~144 MB, so it
  measured a bigger job, not a slower lookup.
- **The two numba kernels do not share a `t_imax`, and this turned out to be a
  live defect rather than a curiosity.** The scan uses `span + 1`; the
  single-lag builder uses the span rounded *up* to a whole number of linear
  bins, and builds the **log** edges from that. Confirmed on one stream, gate
  `[1,40]`, 12 log bins: the two log matrices are identical at
  `lint_bin_factor = 1` and differ at 2, 3 and 5, with the same total count
  (6443) — pairs move between log bins rather than being lost.

  Two user-visible consequences. `api.two_d_fdc` derives `lint_bin_factor` from
  `max_bins`, so adjusting what reads as a resolution knob for the *linear*
  matrix silently shifts the axis of the **log** matrix, which is the one the
  lifetime inversion runs on — recovered lifetimes move and nothing warns. And
  `TK_Create2DFDC_04.m` sets `t_Imax = lint_Imax * lint_BinFactor` and uses it
  for `Mat_2DFDC_logt` as well, so **the builder is faithful to the published
  method and the scan is not**. `fdc_scan_log` followed the scan, so tttrlib
  inherits the deviation.

  **Settled 2026-08-11 by the user: the MATLAB is authoritative — where an
  implementation and it disagree, it wins.** So the scan's unconditional
  `span + 1` is the deviation and the builder was right. tttrlib now takes
  `lint_bin_factor` (default 1, which *is* the reference at factor 1) and
  derives the span the reference's way; `fdc_t_imax` exposes the formula. The
  ChiSurf side is to be fixed to match, not preserved.
  Recorded in ChiSurf `okf/references/known-issues.md` (`626900b6d`) and in the
  header of `Fdc2D.h`. `fdc_scan_axis` is what makes it fixable cleanly — a
  caller passes the decided axis instead of inheriting one.

## Verified against a third implementation, and measured

Both from the ChiSurf session, recorded here so they are not re-derived:

- **Parity**: `fdc_scan_log` against a fixture recorded from ChiSurf's numba
  before it was touched (`test/data/numba_parity/flc_2d_fdc.npz`, chisurf
  `74245dd35`) — nine cases (`ordinary`, `many_lags`, `chunked_7`,
  `lag_inside_window`, `narrow_gate`, `empty_gate`, `single_photon`,
  `two_photons`, `dense` at 1.05M pairs), **identical, every count**. That is
  the check a simulation cannot make, and the simulation is the check a fixture
  cannot make.
- **Performance**, 1M photons, 20 lags, 100 log bins, 8 chunks:

  | | numba | tttrlib | ratio |
  |---|---:|---:|---:|
  | 200k photons | 573.8 ms | 568.7 ms | 1.01x |
  | 1M photons | 2750.6 ms | 2419.7 ms | **1.14x** |

  **Quote the interleaved number, and know why.** Measuring the two
  *sequentially* first reported 0.79x — a 21% regression that does not exist.
  Interleaving A/B/A/B and taking best-of-4 gives 1.14x consistently. A
  sequential A/B on a machine with any thermal or scheduler drift measures the
  order as much as the code.


## A second ChiSurf defect, found by the same check — and not fixed here

`create_2d_fdc_numba_int` allocates `lint_imax × lint_imax`, fills it
correctly, and then **slices the last row and column off on return**
(`core.py:182-183`, `var_size = shape[0] - 1`). Measured against a **brute-force
double loop with no binning at all** — every pair whose two micro-times fall in
the gate and whose macro gap falls in the window — which is 6443:

| `lint_bin_factor` | true | in the linear matrix | lost |
|---:|---:|---:|---:|
| 1 | 6443 | 6443 | 0 |
| 2 | 6443 | 6443 | 0 |
| 3 | 6443 | 5789 | **654 pairs, 10%** |
| 5 | 6443 | 5469 | **974 pairs, 15%** |

The first version of this evidence used the *log* matrix as the denominator,
which is not a count either axis can supply — bin 0 is excluded on every axis
and spans different micro-times per axis, so a log total and a linear total may
legitimately differ at the low edge (a `tau = 1` photon is dropped by a 16-bin
log axis over 4096 and kept by a coarse linear one; measured elsewhere as 3480
vs 3495). Re-anchoring to the unbinned double loop separates the two effects:
the trim is the cause of the 10-15%, and the low edge is a separate, correct
asymmetry. Both are in ChiSurf `okf/references/known-issues.md` (`b8aff50b1`).

The lost pairs are exactly the trimmed row and column, and they are the
**longest micro-times** — so the linearly-binned decay loses its tail, which is
the part a lifetime fit leans on hardest. It hides because
`t_imax0 = span + lint_bin_factor` pads by one bin's worth of *ticks* while the
trim removes one whole *bin*; those coincide only at factor 1, which is what the
tests use.

**⛔ The paragraph that stood here was wrong, and it was mine.** I wrote that
"the MATLAB does no such trim — it returns `Mat_2DFDC_lin` whole", and that the
ruling therefore removed it. `TK_Create2DFDC_04.m:170-175` does exactly the same
trim, to *both* matrices:

```matlab
Var = size(Mat_2DFDC_lin) - 1 ;
Mat_2DFDC_lin  = Mat_2DFDC_lin(1:Var, 1:Var) ;
Mat_2DFDC_lint = Mat_2DFDC_lint(1:Var) ;
Mat_2DFDC_log  = Mat_2DFDC_log(1:logt_Imax-1, 1:logt_Imax-1) ;
```

MATLAB indexes bins `1..lint_Imax`, so `1:Var` is ChiSurf's `[:lint_imax - 1]`.
**ChiSurf matches the reference here.** Under *MATLAB is authoritative* the trim
**stays**, and removing it would be a divergence from the published method
rather than alignment with it. (The ChiSurf session did write the removal before
re-reading, and it made a real measurement worse: their
`test_one_d_fdc_matches_microtime_histogram` fell from 0.98 to 0.9695
correlation.)

I asserted this from another session's summary while stating I was reading the
reference first-hand — I had read its construction (38-44) and not its return.
The measurement (654 and 974 pairs) is real; only the conclusion was wrong.

**What is still open, and it is a question for whoever owns the method**: the
trim discards pairs that fell inside the gate, in the reference as much as in
the port. Keeping them is defensible and is a *deliberate change to the
published method*, which is a different decision from the axis one and has to be
made as such.

**tttrlib is not affected** — `fdc_scan_axis` returns all 6443 in every case,
because its bin count is derived from the axis it was handed rather than from a
padded span it then trims. Which is worth stating plainly for the delegation:
**ChiSurf switching to these kernels silently fixes a 10–15% loss**, so numbers
already published from the linear decay will change. That is a good change and
still a change, and it should be announced rather than discovered.

Both this and the axis coupling are in ChiSurf `okf/references/known-issues.md`
(`9f077a48b`, `626900b6d`). Neither has been fixed on either side; they share
the same `t_imax`/bin-count derivation and should be decided together.


## Both ChiSurf defects are filed as bugs, and their fix needs simulation evidence

User ruling, 2026-08-11: *"mark as bug in chisurf, needs further testing vs
simulations to be validated."*

**One of the two survived that filing.** The scan kernel's
`t_imax = span + 1` is a genuine defect against the reference and is filed as
`### 🐛 BUG` in chisurf `okf/references/known-issues.md`; it is now fixed there
(`d9ef9bc32`), with the simulation evidence still owed. The linear matrix's
trim is **withdrawn** — the reference trims too (see above), so ChiSurf already
matches it and there is nothing to fix. Whether the method *should* keep those
pairs is back with the user as a deliberate-divergence question.

**The validation bar is the part worth restating.** For both fixes, a
regenerated fixture proves nothing: it agrees with the new code by
construction. What is required is a simulation whose answer is known before the
analysis runs, exercised **at `lint_bin_factor > 1`**, which is the only regime
where either defect shows:

- for the axis, that recovered lifetimes and the recovered relaxation rate still
  match the simulated ones once the log axis has moved;
- for the trim, *if* the user decides to keep the highest bin: that the
  linearly-binned decay built from the restored tail recovers the simulated
  lifetimes — a brute-force pair count shows the photons came back, not that the
  decay they form is right, and the tail is what a lifetime fit leans on
  hardest. This is contingent on a decision that has not been made.

`test/python/fcs/test_fdc2d_simulation.py` is the worked example on this side,
including the two traps it documents: a single molecule at `k = 0` never leaves
its starting state (so it is a one-state sample wearing a two-state
configuration), and several emitters in the focus destroy the correlation
because most pairs then come from different molecules.

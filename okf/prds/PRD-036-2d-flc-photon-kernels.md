# PRD-036 — 2D-FLC photon kernels: the fluorescence-decay correlation belongs in the photon library

**Status:** 🟢 Done. tttrlib side landed 2026-08-11 (`opus-5/ac9f6757`, board
ticket `T-20260811-10`); the ChiSurf delegation landed with chisurf's numba
removal (`f1290e84b`, 2026-08-12 — `flc_2d/core.py` calls `fdc_*`, parity
fixture green). Proven against the original author's MATLAB and benchmarked —
see **The tick quantization was the reference's all along** (Octave proof,
permanent fixture) and the three benchmark sections (dynamics resolution,
microsecond immobilized, microsecond diffusing) below.
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

- [x] Kernels in C++ with no fast-math assumptions about the accumulation.
      (`Fdc2D.cpp`, int64 accumulators throughout.)
- [x] NumPy-typemap bindings; keyword names fixed at the outset.
      (`Fdc2D.i`, `IN_ARRAY` typemaps — and, unusually for this library,
      all four languages, not just Python.)
- [x] The seven tests above, all driven by a simulated stream with known
      ground truth, including the single-state negative control.
      (`test_fdc2d.py` 10 deterministic cases + `test_fdc2d_simulation.py`
      6 method cases. Test 1 is deliberately replaced by the
      separability property that makes the inversion possible — the
      inversions stay in ChiSurf — see *What the simulation actually
      showed*.)
- [x] Benchmarked against ChiSurf's numba kernel at a realistic photon count
      (1e6–1e7 photons, 100 log bins, ~20 lags); matching is success.
      (1.14x at 1M photons, interleaved A/B best-of-4 — the sequential
      number is a thermal artefact, see below.)
- [x] ChiSurf `flc_2d/core.py` delegates, its five kernels and `import numba`
      are deleted, and `chisurf/test/numba_import_allowlist.txt` loses the line.
      (Done with chisurf's numba removal, `f1290e84b`, 2026-08-12; board
      ticket `T-20260811-14` closed by it.)
- [x] The `junk/2D-FLC-code` markers point at this PRD (already done).


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

## The tick quantization was the reference's all along (2026-08-16)

Proof-of-parity, demanded by the user ("need proof!"), delivered by running the
original `TK_Create2DFDC_04.m` itself (Octave, `junk/2D-FLC-code` in chisurf)
against the library on a shared 3000-photon stream, `tStep = 1` so MATLAB ticks
and library ticks are the same integers:

- **Linear matrices: identical**, 3 lags, before any change.
- **Log matrices: NOT identical** — 352–457 of ~85k pairs (~0.5%) in different
  bins at both `lint_bin_factor` 1 and 8, same totals, at exactly the edges
  where the two tick conventions differ.

The .m file keeps its log edges **real-valued** (`t_Imax^(j/(L-1)) - 1`,
line 44) and compares the integer micro-time tick against them directly, so
the effective integer edge is `floor(v)`. The kernels — and, it turned out,
chisurf's numba original before them (`int(np.floor(v + 0.5))`, its line 131)
— quantized to *nearest*, and every existing check was blind to it: the
brute-force reference binned on the kernel's own ticks, the parity fixture
was recorded from the same round convention, and the simulated streams' bins
were wide enough to swallow a one-tick edge move. Only the original author's
code could see it.

**Fixed:** `build_log_ticks` floors. The prototype-first discipline was
followed — the hypothesis was proven in Python (caller-supplied axis with
floored edges via `fdc_scan_axis`: 6/6 identical) before the one-line C++
change, then re-proven through the production `fdc_scan_log` path (9/9
identical, log at two factors × three lags plus linear).

**The proof is now permanent:** `test/data/reference/fdc2d_matlab_tk_create2dfdc04.npz`
records the .m's own outputs on the seeded stream, and
`TestAgainstTheOriginalMatlab` pins the library against them — the one
fixture in this suite recorded from the *authoritative* implementation rather
than from the code under test. Consequences elsewhere: the round-pinning edge
test was rewritten to floor; the two-axes totals test no longer had its
asymmetry (that "tau = 1 lands in bin 0" behaviour was the round artifact, not
the method — it now pins the invariant against a hand-built asymmetric axis);
chisurf's `flc_2d_fdc.npz` parity fixture was re-recorded through the
delegated path (its job is call-site pinning; correctness is anchored by the
MATLAB fixture here), and its `test_fdc_parity.py` docstring says so.

**Citations added** (DOIs verified via Crossref, not guessed): Ishii & Tahara
JPCB 117(39) 11414–11422 and 11423–11432, 2013 (doi:10.1021/jp406861u,
doi:10.1021/jp406864e — the method papers); Kondo, Gordon, Pinnola,
Dall'osto, Bassi & Schlau-Cohen, PNAS 116(23) 11247–11252, 2019
(doi:10.1073/pnas.1821207116 — the single-molecule application this code was
written for); original implementation T. Kondo (Schlau-Cohen lab, MIT),
`TK_Create2DFDC_04.m`. In `Fdc2D.h`, the fcs README, and chisurf's
`flc_2d/{__init__,api,core}.py`.

## The full MATLAB corpus, and where each piece lives (2026-08-16)

`junk/2D-FLC-code/MatlabCodes` holds 46 `.m` files. The audit below is the
answer to "is all the MATLAB code reflected in the cpp?" — the honest split is:
**the photon pass is C++ and proven; the inversions and fits are deliberately
chisurf Python; three workflow items are ported nowhere yet.**

| MATLAB | What it is | Where it lives now |
|---|---|---|
| `TK_Create2DFDC_04.m` | the 2D-FDC pair pass (log + linear matrices, axis, gate, trim) | **tttrlib `Fdc2D`** (`fdc_scan_log`/`fdc_log`/`fdc_scan_axis`/`fdc_scan_two_axes` + `fdc_t_imax`/`fdc_log_ticks`/`fdc_log_bin`) — proven identical to the .m, fixture-pinned |
| `TK_Create1DFDC_01.m` | the zero-lag 1D decay coincidence (diagonal of dT=0) | tttrlib kernels via chisurf `fit/helpers.create_1d_fdc` (docstring cites the port) |
| `TK_Histgram1D.m` | fixed-width micro-time histogram | chisurf `fit/helpers.histogram_1d` (trivial; `np.bincount` class of work) |
| `TK_MyMain_Simu_PhotonStream.m` | the two-state photon-stream simulator | tttrlib `SimEngine` (PRD-036's decision: simulate with our own, not a transcription); chisurf `simulate.py` wraps it with the MATLAB default case |
| `TK_RateEq_MakeExpMatrix.m` | master-equation generator, `p(t) = expm(G t) p(0)` | chisurf `fit/kinetics.make_generator_matrix` (docstring cites the port) |
| `TK_mi_ModelFunction.m` | the four `mi` prior types for MEM | chisurf `fit/mem_1d.py` (all four, by name) |
| `TK_FitF_1DMEM_01/02`, `TK_FitF_1DMEM_MinimizeQ_01/02` | 1D MEM inversion | chisurf `fit/mem_1d.py` + `api.lifetime_spectrum(method=...)` |
| `TK_FitF_2DMEM_07`, `TK_GFitF_2DMEM_05` | 2D MEM (single and global over lags) | chisurf `fit/mem_2d.py`, `fit/global_mem.py` (global: "invert several lag matrices jointly") |
| `TK_FitF_MinimizeQ_09`, `TK_GFitF_MinimizeQ_04` | Q-objective minimizers | chisurf `fit/mem_2d.py` (entropy-refresh + regularizer ramp, by docstring) / `fit/global_mem.py` |
| `TK_ExpMultiDeco_For2FLC.m` | multi-exponential basis / decomposition convention | chisurf `fit/ilt.py` (cites the convention) |
| `TK_FitF_GaussianMulti.m` (+ its `TK_MyMain_Fit_` driver) | Gaussian-mixture components over the MEM output | chisurf `fit/gaussian.py` |
| `TK_FitF_CorrelationDecay_RateMat_05`/`_16_NotRatio` | rate-matrix fit of the correlation decay | chisurf `fit/kinetics.py` (`fit_rate_matrix`, variable projection) |
| `TK_MyMain_Search_RiseIRF_1DMEM`/`_2DMEM` | IRF-rise scan over 1D/2D MEM | chisurf `fit/helpers.search_rise_irf` |
| `TK_DisIntLife2Dmap.m` | discretise lifetimes onto the lifetime–lifetime map | chisurf `fit/ilt.py` map assembly (`api.two_d_spectrum`) |
| `TK_MyMain_Analyze*`, `TK_MyMain_CorFit*`, `TK_MyMain_Fit_*`, `TK_MyMain_GFit_*`, `TK_MyMain_Fig*`, `TK_MyMain_OpenAllFiles_01`, `TK_MyMain_ConcatenateMeasureTime`, `TK_MyMain_Exp_FFT_FWHM`, `TK_MyMain_Create2DFDC_cor_02`, `TK_MyMain_Create2DFDC_cor_SeparateData_v01` | workflow drivers (open, build, fit, plot) | the plugin itself: `api.py`, `cli/`, `backend/services.py`, the GUI — the drivers are what a plugin replaces |
| `TK_FitF_Reproduct1DFDC(.m/_02)`, `TK_FitF_Reproduct2DFDCand2DFLC_03`, `TK_GFitF_Reproduct2DFDCand2DFLC_03` | **forward "reproduct": rebuild decays/maps from fitted parameters, to validate a fit visually** | **not ported.** The model evaluation exists inside each fit, but no standalone reproduce-from-parameters entry point exists. Natural home: chisurf `api.py` |
| `TK_MyMain_Create2DFDC_cor_SeparateData_BootStrap_v02` | **split-data bootstrap error estimation on the 2D-FDC** | **not ported.** Nothing in either repo does bootstrap errors on the matrices. Natural home: chisurf `api.py`/`fit/` |

The two unported rows are recorded honestly rather than silently dropped; both
are analysis-layer conveniences, not photon-pass numerics, so neither blocks
anything PRD-036 shipped. `examples/correlation/plot_fdc_2d.py` (tttrlib) now
demonstrates the whole chain the C++ owns: simulate → `fdc_scan_two_axes` →
coupling vs lag → fitted relaxation (0.91 s fitted against 1.00 s simulated on
the seeded stream) — with the papers cited in its docstring.

## The dynamics benchmark: how fast, how complex (2026-08-16)

User question: "benchmark the method, ie, how good can it resolve fast
dynamics, and complex dynamics." Answered on simulated experiments with the
answer fixed in advance (`examples/correlation/plot_fdc_2d_dynamics_resolution.py`,
three seeds per point, 300k photons / 1500 s per stream, 10 ms macro window,
80 ms lag window):

**Fast dynamics — two-state sweep over three decades of relaxation time.**
Fitted (3-seed mean) vs true:

| true (s) | 0.01 | 0.02 | 0.05 | 0.1 | 0.2 | 0.5 | 1.0 | 2.0 | 5.0 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|
| fitted (s) | — | — | 0.038 | 0.085 | 0.181 | 0.451 | 0.936 | 1.754 | 3.80 | 7.99 |

Resolved within 25% from **50 ms to 10 s**, and nowhere below the lag-window
width (80 ms) — at 10 and 20 ms the coupling is gone before the first usable
lag. The floor is the *window*, a method property, not a defect of the pass;
shrinking the window moves the floor down proportionally.

**Complex dynamics — three-state chain, two relaxation times (1 s, 10 s).**
The seed-averaged coupling curve needs and supports two exponentials: fitted
**0.86 s and 7.9 s** (both within 25%); the single-exponential control fits
**4.96 s** — neither mode, as it must. Per-stream fits recover the fast mode
every time (0.68 / 0.72 / 1.03 s) but leave the slow mode noise-limited
(4.6 / 12.4 / 16.1 s): a decade-separated second timescale at this photon
count needs seed-averaging or a longer stream. That is the honest boundary of
"complex".

Two debugging notes worth keeping: the first version's lag grid straddled only
the slow mode (started at 1 s) and the two-exponential fit was degenerate on
it — a dynamics benchmark must cover *all* the timescales it claims to
resolve. And the fit's parameter vector is (a1, tau1, a2, tau2, b): slicing
taus as `popt[1:3]` silently compares tau1 with the *amplitude* a2; the taus
are at `popt[[1, 3]]`. Both errors produced plausible-looking wrong numbers,
and only printing the raw parameter vector caught them.

## Microsecond dynamics on a single FRET molecule (2026-08-16)

User question: "can it recover microsecond dynamics, ie, sth fast? 200ns -
10 us with single molecule experiments? simulate fret experiment."

**Yes.** `examples/correlation/plot_fdc_2d_microsecond_fret.py` reshapes the
experiment the way the µs regime demands: T3-mode TTTR (macro clock = laser
period, 25 ns at 40 MHz), one immobilized FRET molecule's donor channel at
500 kcps, two conformational states E = 0.2 / 0.8 (τ_D = 3.2 / 0.8 ns at
τ0 = 4 ns), equal exchange rates, lag window ddT = 2 clocks (75 ns effective
span). Recovery (3 seeds per point, 5 s streams, same log-linear estimator
as the slow benchmark):

| true | 200 ns | 500 ns | 1 µs | 2 µs | 5 µs | 10 µs |
|---|---|---|---|---|---|---|
| fitted | 155 ns | 396 ns | 820 ns | 1.67 µs | 4.24 µs | 8.36 µs |
| error | 22% | 21% | 18% | 17% | 15% | 16% |

All within 25%, seed spread ±3%, a consistent −15…−22% bias that belongs to
the simple estimator (a weighted exponential fit with baseline would shave
it; kept identical to the slow benchmark for comparability). Floor is again
the window: 200 ns sits 2.7× above the 75 ns span. Caveats stated in the
example: Poisson emission only — no afterpulsing, dead-time pile-up or dark
counts, which is precisely what real µs-lag correlations fight — and an
immobilized continuous stream rather than aggregated bursts.

**The debugging story worth keeping.** The first attempt produced flat D
curves: no coupling at any lag. Ground truth via the engine's own state log
(`set_state_log(True)`) showed the kinetics were PERFECT — mean dwell 1.9985 µs
against 1/k = 2 µs, states alternating correctly — so the defect was that the
photons carried no state information: the micro-time histogram was uniform.
Cause: a unit slip in the *caller*, not the engine — `SimIntegrator.dt` and
the rate matrices are in seconds while `microtime_resolution` and
`laser_period` are in nanoseconds (deliberate, matching TCSPC practice, but
half-documented). Passing `25e-9/256` as the resolution made the decay
pattern span 2.5e-8 ns instead of 25 ns: flat pattern → uniform micro-times
→ no lifetime axis → nothing to correlate. `SimIntegrator.h`'s `dt` comment
now states the units and the split explicitly. Diagnostic sequence that
found it, reusable next time: state log first (is the physics in the
stream?), then raw pair covariance vs separation (is it in the photons?),
then the D curve.

## Diffusing molecules: the diluted regime, a new statistic, and the diffusion ceiling (2026-08-16)

User question: "now do with diffusioning molecules 2 ms diffusion time."

Configuration: open volume, surface-flux injection (`from_dict` +
`population`, after discovering a closed box just lets molecules random-walk
away and never return — rate collapses 76→3 kcps over 5 s), ~40 molecules in
a 1.5/3 µm box, D = 11.25 µm²/s (τ_diff = w0²/4D = 2 ms at w0 = 0.3 µm),
~0.15 focus occupancy, 254 kcps donor channel, same T3/FRET µs setup.

**Three findings, all measured:**

1. **The TV statistic has a shot-noise pedestal.** In the diluted regime
   (~half the pairs same-molecule) the total-variation coupling D showed a
   flat 0.018 at every lag — present verbatim in a single-lifetime control
   (no lifetime information at all) and in a micro-time-shuffled null:
   order √(K/4N) (K=144 cells, N≈36k pairs), i.e. a statistic property, not
   physics. The immobilized benchmark never saw it because its signal was
   10× taller. The **pair micro-time covariance** — Cov(bin1, bin2) over
   pairs, computed from the same `fdc_scan_log` matrix — has per-pair noise
   and no positive bias: TV fit on the 1 µs case 0.52 µs (wrong), covariance
   1.04 µs (right). The diffusing example uses the covariance and documents
   why; the immobilized examples keep the TV (valid there).
2. **Engine window ≠ T3 clock.** Windows of dt = 2.5 µs (100 laser periods)
   with macro ticks reconstructed as `round((window·dt + arrival)/25 ns)` —
   the sync-divider picture — cut wall time ~25× (5 s stream in ~9 s) with
   diffusion per window at 2.4% of w0 and kinetics still continuous-time.
   Ground truth first: the raw pair covariance tracks exp(−t/τ) cleanly
   (0.87/0.905, 0.75/0.74, 0.42/0.407 at 200/400/1000 ns for a 1 µs
   relaxation) before any statistic is trusted.
3. **Diffusion is the ceiling, as the window is the floor.** Sweep (3 seeds,
   5 s streams, covariance fit): 200 ns→193 (3%), 500→575 (15%),
   1 µs→1.15 (15%), 2→1.93 (4%), 5→6.4 (27%, marginal), 10 µs→9.9 (1%).
   A 5 ms relaxation — beyond τ_diff = 2 ms — is NOT resolved: each seed
   fits 2.6–4.8 ms ≈ τ_diff; molecules leave the focus before the kinetics
   finish. Between the 75 ns window span and the 2 ms diffusion time, the
   method sees everything thrown at it.

Also found while wiring the diffusing sample: `plot_lifetime_fcs.py`'s unit
comments are wrong — it labels `dt` "(ms)" and D "(um^2/ms)" but the engine
takes `dt` in seconds and D in µm²/s (from_dict passes both through
verbatim); the example's own simulation is self-consistent, only its
annotations mislabel. Not fixed there in this pass (another agent's file to
touch politely).

Example: `examples/correlation/plot_fdc_2d_microsecond_fret_diffusion.py`
(~4 min runtime; the table regenerates from fixed seeds).

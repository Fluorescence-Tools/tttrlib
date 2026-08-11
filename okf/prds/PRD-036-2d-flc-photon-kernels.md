# PRD-036 — 2D-FLC photon kernels: the fluorescence-decay correlation belongs in the photon library

**Status:** 🔵 Proposed
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

- ChiSurf builds only the **log-binned** matrix. The MATLAB also returns a
  linear one; it is recoverable by rebinning and nothing asks for it.
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

# PRD-005 — Photon Simulator (grid fields, ISM/CLSM, RMF trajectories)

> **PRD #:** 005 · **Status:** Draft · **Created:** 2026-07-03 · **Owner:** tpeulen
> **Related:** PRD-004 (single-frame FLIM PTU CLSM), PRD-001 (cross-language test parity)
> **Plan:** [PLANS/plan-005-photon-simulator.md](../PLANS/plan-005-photon-simulator.md)

## Summary

Add an **additive** photon-simulation subsystem to tttrlib: an OpenMM-style engine that simulates
diffusing/immobile single fluorophores with photophysical states, generates a TCSPC photon stream,
and emits it as a `TTTR` object (default container **PTU**). Fields (excitation and per-detector
detection) are **3D voxel grids**, enabling separate excitation/detection profiles, **ISM** (many
offset detectors), and **CLSM** raster imaging with frame/line/pixel markers consumable by
`CLSMImage`. Optionally exports the full molecule trajectory in **RMF** (HDF5) format. The engine is
a clean-slate reimplementation of the legacy ChiSurf "Burbulator" C++ simulator, and reproduces it
**bit-for-bit when the SPC-132 encoder is selected** (used as a scientific-correctness gate).

## Problem / motivation

The scientific community around tttrlib needs a maintained, cross-platform photon simulator to
generate ground-truth TCSPC/FCS/FLIM/smFRET/CLSM data for developing and validating analysis
algorithms. The existing simulator ("Burbulator", ~2006–2010) lives inside the ChiSurf `acq` plugin
as a Windows-era C++ DLL loaded via a hand-written ctypes shim (`libburbulator.dylib` + `csrc/*`).
It is:

- **ChiSurf-only** and not part of the released, cross-platform library other tools build on.
- **Limited**: analytic detection volumes only, point-molecule FCS only, no separate
  excitation/detection profiles, no ISM, no scannable CLSM imaging, dead pulsed-TAC/IRF path,
  unreachable anisotropy engine.
- **Fragile**: the ChiSurf streaming path reseeds every batch and resets the time origin, so a long
  acquisition is actually a series of independent re-equilibrated short runs.

tttrlib already owns the consumption side (`TTTR`, `CLSMImage`, `Correlator`, decay/PDA, `TTTR::write`
for PTU/HT3/SPC-130, vendored HighFive for HDF5). Putting the *generation* side next to it makes the
simulator cross-platform, testable in every binding, and directly round-trippable against tttrlib's
own readers/analysis.

## Goals

- **Engine.** Port the scientific core — Brownian dynamics of single emitters in an open volume,
  photophysical/FRET state transitions, inhomogeneous-Poisson emission, background, macro/micro-time
  (TAC) encoding by inverse lookup — into new tttrlib C++ classes.
- **Grid fields.** Excitation and each detection profile are **3D voxel grids** (trilinear
  interpolation). Analytic PSF/CEF shapes exist only as **grid-builder helpers**, never as runtime
  fields. Excitation is one grid; detection is **one grid per detection (routing) channel** — so
  **ISM is implicit** (offset detection grids on separate channels), with no detector-array class.
- **Emitter input.** Primary input is a **multi-channel integer 3D grid** (from a TIFF read in
  Python): `ch0` = emitter count/voxel, `ch1` = species/type, further channels reserved (initial
  state, mobile flag) — "single emitters". Plus low-level `addFluorophore`/`setPositions` and
  open-volume population for stationary-focus FCS. Per-particle static/mobile flag.
- **Scanning (CLSM).** A **discrete scanner driven by a per-pixel dwell-time array** (the inverse of
  the pixel dwell times a CLSM reader derives) emits frame/line/pixel markers so the output
  reconstructs through `CLSMImage`.
- **Output.** Photon `TTTR` in a **selectable encoder** (PTU default; HT3; SPC-132), plus an optional
  **RMF/HDF5 trajectory** of *all* molecules (positions, state, id, time), ChimeraX/IMP-compatible.
- **API & ergonomics.** An OpenMM-style object model (Sample / ExcitationField / DetectorArray /
  Scanner / SimulationSettings / PhotonSimulator). **Pythonic** bindings, examples, and docs.
  The whole engine is also **JSON-configurable** (`SimEngine::from_json` / `default_json`) — seeds,
  RNG backend/scope, species, kinetics, background, box, population, excitation and per-channel
  detection fields, and discrete emitters — for reproducible, file-driven runs.
- **Performance.** Fast by design: the per-window molecule loop is parallelised with an explicit
  **std::thread pool** (not OpenMP — OpenMP is avoided in the simulator). Parallelism is
  **deterministic and thread-count-independent**: each molecule's RNG stream is (re)positioned from
  (base seed, molecule id, window), so output does not depend on thread count or processing order.
  The RNG **backend** is selectable (`SimRngKind`: xoshiro256++ default / PCG32 / Philox4x32 /
  MT19937) as is the **scope** (`SimRngScope`: PerMolecule default = reproducible across thread
  counts; PerThread = faster, thread-count-dependent). Because PerMolecule reseeds per molecule per
  window, cheap-seed generators win in-engine (PCG's stream-select seeding costs only +1.9%; MT's
  624-word init is ≈41× and not recommended). xoshiro/PCG both pass BigCrush, so no need to split
  RNGs by purpose. Full numbers + rationale: [PRD-005-rng-benchmark.md](PRD-005-rng-benchmark.md).
  Threading is threshold-gated (serial when few molecules, e.g. FCS; parallel for many-emitter CLSM).
  Remaining SIMD opportunity (grid interpolation, parallel merge/sort) is a follow-up.
- **Reproducibility (scoped to the encoder).** The **SPC-132 encoder** (`data2spc_tac` port) is
  byte-identical to the legacy encoder for **identical abstract photon records**. The
  diffusion/emission **engine** uses a **correct symmetric Gaussian** (the legacy `randomNorm` is
  positive-only on LP64 — a `sizeof(long)`-dependent bug — see Risks) and is validated
  **statistically** (distributions, decay τ / diffusion-time D / ground-truth recovery), not against
  legacy bytes. The engine is deterministic given a seed; cross-platform equivalence is statistical.

## Non-goals

- Modifying any existing tttrlib class, behaviour, or SWIG interface (the public API is **frozen**;
  this subsystem is **additive** and, once released, its own API is **locked**).
- Adding an IMP or libRMF dependency (RMF is written per-spec via the vendored HighFive).
- GPU acceleration; widefield/camera (EMCCD/sCMOS) detectors; non-raster scan trajectories.
- A separate "legacy mode" code path — legacy output is a *parameterization* of the one engine.
- Byte-identity for the PTU/HT3 encoders, for the interpolated-grid runtime, or for the
  diffusion/emission engine (all validated statistically). **Only the SPC-132 encoder is byte-exact**,
  and only against identical abstract photon records.
- Reproducing the legacy positive-only-Gaussian diffusion bug (the new engine is scientifically correct).

## Proposed approach

New C++ classes (auto-globbed `include/*.h` + `src/*.cpp`), SWIG-wrapped with a Pythonic layer.
**All new classes carry the `Sim` prefix** to keep tttrlib's global namespace clean:

- `SimSpecies`, `SimSample` — species photophysics + emitter population (grid / discrete / open-volume).
- `SimGrid` (+ static analytic fillers: 3D Gaussian, Gaussian-Lorentzian…) — the voxel field type.
  *(implemented)* Excitation is one `SimGrid`; **detection is one `SimGrid` per detection (routing)
  channel** — no separate detector-array class is needed, and **ISM is implicit**: multiple channels
  with laterally-offset detection grids (the offset lives in each grid's origin) give the ISM detector
  array for free.
- `SimScanner` — `none()` (FCS) or `fromDwellTimes(dwell[ny,nx], …, SimMarkerConfig)` (CLSM).
- `SimSettings` — `dt`, seeds, channels, `ch_conversion`, pulsed/CW, TAC params, `SimDecaySpec`
  (per-(species,channel) lifetimes + IRF → builds the inverse-lookup `F`/`lookup`), `SimRecordEncoder`.
- `SimRandom` — the legacy MT19937 (Cokus variant) ported with **corrected signed semantics**
  (explicit `int32_t` in `random4nrm` → a correct symmetric Gaussian; the tempering/state/seed logic
  is otherwise verbatim) and get/set-state. *(implemented)*
- `SimMicrotimeEncoder` — encodes abstract photon records to hardware TCSPC records; the SPC-132
  path is a faithful `data2spc_tac` port (byte-identical dev-time gate). PTU/HT3 output goes through
  the existing `TTTR::write`, not this class. (Generic name — "SPC" is Becker & Hickl-specific.) *(implemented)*
- `SimEngine` — assembles the above; `step()`/`run()` → marker-annotated `TTTR`; `getState`/
  `setState`; `write(path)` per encoder; `set_trajectory_reporter(stride)` + trajectory getters +
  `write_trajectory_hdf5(path)` (generic HDF5). *(implemented)*
- **RMF trajectory** — written the IMP way via a **Python helper over the standalone RMF library**
  (`import RMF`; ChimeraX/IMP-compatible `.rmf3`), consuming the engine's trajectory arrays. tttrlib
  C++ stays RMF-free; no IMP dependency. *(implemented — `examples/simulation/rmf_trajectory.py`)*

Legacy reproduction is scoped to the **SPC-132 encoder**: given identical abstract photon records
(`data_T`, `data_t`, `data_N`, `data_species`, `data_molecule`), the ported `data2spc_tac` emits
byte-identical `.spc`. This is integer/time math (no transcendentals in the RNG-driven CW path), so it
is straightforwardly cross-platform. The diffusion/emission **engine** (grid fields, correct Gaussian)
is validated **statistically** — fine-grid convergence, decay/FCS/ground-truth recovery — not against
legacy bytes.

## Milestones

- **M0** — Freeze legacy SPC-132 golden matrix from the current DLL; scaffold `test/python/simulation/`
  with x86 + ARM CI.
- **M1** — RNG core: MT19937 port with corrected signed Gaussian; `random0i1e/0e1e/randomUInt`
  streams match legacy exactly (integer path), `randomNorm` is symmetric/correct.
- **M2** — Engine + SPC-132 encoder: port `smdif_ov3` + `data2spc_tac`. Encoder byte-identical vs
  legacy for identical abstract records (x86 + ARM). Engine validated statistically.
- **M3** — Grid fields (`Grid3D`, `ExcitationField`, per-detector `DetectorArray`, `grid_builders`);
  ISM offsets; fine-grid convergence to the exact-eval statistics.
- **M4** — Emitter input (multi-channel INT-TIFF grid, `addFluorophore`, mobile flag).
- **M5** — CLSM scanning (`Scanner::fromDwellTimes`) + markers → `CLSMImage`; PTU/HT3 write/read;
  smiley/star shape examples for visual comparison.
- **M6** — RMF trajectory reporter (all molecules) → HDF5 via HighFive; ChimeraX-load check.
- **M7** — Pulsed inverse-lookup TAC/IRF from `DecaySpec`; revive anisotropy (`rotdiff`).
- **M8** — SWIG bindings + Pythonic layer + docs + examples (`ism/`, `simulation/`, `microscopy_flim/`).

## Risks

- **Byte-identity is a dev-time correctness assertion, not a product guarantee.** During
  implementation we compare against the legacy engine bit-for-bit where it is meaningful — the
  **SPC-132 encoder** (identical abstract records → identical `.spc`) and the integer RNG stream — to
  prove the port is scientifically faithful. It is a **test harness**, not a shipped feature: the
  released engine is validated statistically (distributions, τ/D/ground-truth recovery).
- **The legacy RNG is not portable and is buggy on LP64.** `random4nrm`'s `(long)` cast makes the
  Gaussian positive-only where `sizeof(long)==8` (macOS/Linux) — biased diffusion — and signed
  elsewhere (Windows/32-bit). The port **corrects** this (explicit `int32_t`, symmetric Gaussian); we
  therefore do NOT byte-match the legacy *diffusion* path, only assert the corrected engine's
  statistics. The integer generators (`randomUInt/0i1e/0e1e`) are matched exactly as a stream check.
- **Grid-only vs legacy analytic eval.** Interpolated grids differ from legacy's analytic focus
  evaluation; the grid runtime is validated statistically (fine grid → converges). Byte-level checks
  during dev use the encoder + fixed-record fixtures, not the interpolated field path.
- **Frozen public API.** New code must not touch existing headers/sources/`.i`. PTU write already
  works (via `write_ptu_header` + HydraHarp-T3 events), so no change to `TTTR::write` is required.
- **Locked new API.** The new simulation API cannot change after release; design reviewed against ISM,
  CLSM, FCS, smFRET, and trajectory use-cases before shipping.
- **RMF-per-spec.** Implementing RMF over HighFive without libRMF risks incompatibility; validated by
  opening output in ChimeraX and against the RMF spec.

## Unit contract & locked API (consumed by BurstNet — PRD-007)

**Two independent unit systems.** The engine is unit-agnostic but the following quantities share one
**abstract macro-time unit** (BurstNet treats it as **milliseconds**): `SimSettings.dt`, `SimSpecies.D`
(box/length units² per macro-time unit), the `k_rad`/`k_nrad` rates (per macro-time unit), the
brightness `q` and `background` (photons per macro-time unit), and the outputs `macro_window` (window
index) and `arrival_time` (within-window offset). The **micro-time axis is a separate unit system in
ns**: `microtime_resolution`, `laser_period`, and each `SimDecay`'s `dt`/`t0`; the output `micro_time`
is a micro-time channel index. The macro and micro axes never mix.

**Locked output contract** (relied on by BurstNet; do not break):
- `emitting_species == n_species` marks a **background** photon (stable sentinel).
- `micro_time` is a **forward-time** channel index sampled from `SimDecay` (no legacy inversion).
- `event_type == 0` = photon, `== 1` = marker; `run()` without a scanner emits only photons.
- Per-molecule RNG keyed by `(base, id, window)`; `step()` is reproducible and resumable and preserves
  molecule identity/labels across calls (verified) — deterministic shards for offline caching.
- Background photons carry a micro-time from `SimSample::set_background_decay(s)` (PRD-007 G1).

## Prior art — PyBroMo (inspected `junk/PyBroMo`, not copied)

PyBroMo (OpenSMFS) simulates Brownian diffusion + PSF excitation + photon emission for smFRET and
writes Photon-HDF5. Design ideas to adopt in the remaining phases (none copied verbatim):

- **Numerical / measured PSF import + cylindrical (r,z) fields.** PyBroMo's default PSF is a rigorous
  vectorial-EM computation (PSFLab, Nasse & Woehl 2010) stored as a **2D (r,z) grid** exploiting
  cylindrical symmetry, interpolated as `PSF(√(x²+y²), z)`. Adopt: (a) an **importer** so `SimGrid`
  can be filled from an external numerical/measured PSF, and (b) an optional **cylindrically-symmetric
  (r,z) field** representation for memory efficiency vs. our full 3D grid. (Reinforces the grid-only
  field decision; adds a realistic-PSF source beyond analytic fillers.)
- **Boundary conditions.** PyBroMo uses **periodic** or **mirror (reflective)** wrapping to conserve
  particle count. Offer these on `SimSample` as alternatives to the legacy open-volume surface
  injection — simpler and standard for FCS. (New scope item.)
- **smFRET convenience.** Emission split donor/acceptor by FRET efficiency `E` (`em_rates_from_E_DA`).
  Our state/`k_rad` model is more general; add an optional `E`-based per-molecule/per-state channel
  split as a smFRET-friendly parameterization layered on the general engine.
- **Detector dark counts** as a distinct per-channel constant background term (we have background; name
  the dark-count semantics explicitly).
- **Photon-HDF5 output option.** PyBroMo/FRETBursts standard. Consider **Photon-HDF5** (via HighFive)
  as an additional photon output alongside PTU/HT3/SPC-132, and as the trajectory/emission-trace store
  (chunked, compressed `/trajectories` + `/timestamps`) — relevant to the RMF-vs-HDF5 trajectory choice
  (P6): RMF for structure/ChimeraX, Photon-HDF5 for smFRET-analysis interop.
- **Chunked streaming + deterministic per-particle seeding** (`iter_chunks`, `get_seed(seed, ID, EID)`)
  — confirms our `step()`/window streaming and per-molecule keyed RNG `(base, id, window)`.

These update P3 (PSF import + r,z fields), P4/Sample (boundary conditions, `E`-split), P6 (Photon-HDF5
trajectory/photon option), and the Non-goals (Photon-HDF5 no longer strictly excluded — reconsider).

## Addendum — ALEX (alternating laser excitation)

Micro-second ALEX alternates the green/red laser on the **macro-time / diffusion timescale**
(unlike PIE, which is a micro-time construct). Modelled additively, without changing existing
behaviour: `SimEngine` holds a **vector** of excitation grids (one per laser), `SimSpecies` gains a
per-laser brightness matrix `q_alex` (empty ⇒ scalar `q` broadcast), and `SimIntegrator.alex_period`
drives an exact integer-window round-robin `laser_for_window(T0)` selecting the excitation grid and
`q` row per macro-window. The laser index is a pure function of `T0` (no RNG), so single-laser /
`alex_period=0` output is byte-identical to the pre-ALEX engine. The focus AABB is unioned over all
laser grids so coasting never skips a molecule near any laser's focus. Optional laser-switch markers
(`alex_markers`) are emitted at each switch (excluded from the `n_ph_max` photon budget). The
alternation is encoded in macro-time and recovered downstream by `TTTR.alex_to_microtime` + a
donor-detector auto-split; see `examples/simulation/alex_smfret.py` and the ALEX notebooks. Related
fix: `to_tttr`/`SimMicrotimeEncoder` now carries the simulated micro-time faithfully and skips
marker events (previously segfaulted / mangled micro-time filters).

## References

- Prior art (inspected, not copied): `junk/PyBroMo/` (OpenSMFS PyBroMo) — `pybromo/psflib.py`
  (numerical PSFLab PSF, (r,z) interpolation), `diffusion.py` (Box, particles, `wrap_periodic`/
  `wrap_mirror` boundaries, chunked trajectories), `timestamps.py` (E→D/A emission, background/dark),
  `storage.py` (HDF5 `/trajectories` + `/timestamps`, Photon-HDF5).
- Legacy scientific reference (to be reimplemented, not linked):
  `chisurf/plugins/core/acq/tcspc_devices/simulation/csrc/` — `smdif_ov3.cpp` (engine), `focus.cpp`
  (PSF/CEF), `data2spc_tac.cpp` (SPC-132/TAC), `rotdiff.cpp` (anisotropy), `mt19937cok.cpp`/
  `mtrandom.h` (RNG), `smdif_misc.cpp` (samplers/sorts).
- Reuse in tttrlib: `include/TTTR.h` (array ctor, `append_events`, `write`), `src/TTTR.cpp`
  (`write_spc132_events`, `write_hht3v2_events`, `write_ptu_header`), `include/CLSMImage.h` +
  `src/CLSMImage.cpp` (marker model), `thirdparty/HighFive` (HDF5).
- CLSM marker blueprint: `test/python/clsm/test_CLSM_bh_markers.py`.
- ISM / example conventions: `examples/ism/`, `examples/microscopy_flim/`.
- RMF format spec (ChimeraX/IMP), implemented per-spec via HighFive.

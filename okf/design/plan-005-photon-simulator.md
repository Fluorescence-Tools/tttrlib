# Plan 005 — Photon Simulator subsystem

> **Plan for:** [PRDs/PRD-005-photon-simulator.md](../prds/PRD-005-photon-simulator.md) ·
> **Created:** 2026-07-03 · **Owner:** tpeulen

Implementation plan for the additive photon-simulation subsystem described in PRD-005. This is the
tttrlib-side plan; the downstream ChiSurf `acq`-plugin swap onto tttrlib is tracked at the end.

## Guardrails

- **Additive only.** No edits to existing tttrlib headers, sources, or `.i` files. New files only.
- **New API is locked at release** — design reviewed against ISM / CLSM / FCS / smFRET / trajectory
  use-cases before shipping; prerelease drift allowed until then.
- **Legacy = parameterization, not a mode.** One engine; legacy output is a specific configuration.
- **Byte-identity = dev-time correctness assertion, not a shipped guarantee.** Used while building to
  prove faithfulness: SPC-132 encoder byte-identical vs legacy for identical abstract records, and the
  integer RNG stream matched exactly. The diffusion/emission engine is **corrected** (symmetric
  Gaussian; legacy is positive-only-buggy on LP64) and validated **statistically**.

## Architecture

All new classes are `Sim`-prefixed to keep tttrlib's global namespace clean.

```
tttrlib (new, namespace tttrlib)
  SimSample ── SimSpecies[]     physics + emitter population (grid / discrete / open-volume)
  SimGrid excitation            shared 3D excitation field, trilinear                    [done]
  SimGrid detection[channel]    one detection grid per routing channel; offset ⇒ ISM (implicit)
  SimScanner                    none() (FCS) | fromDwellTimes(dwell[ny,nx], SimMarkerConfig) (CLSM)
  SimSettings                   dt, seeds, TAC/encoder (PTU|HT3|SPC132), SimDecaySpec (F/lookup)
  SimRandom                     MT19937 (Cokus), corrected signed Gaussian, get/set-state   [done]
  SimXoshiroRandom/SimCounterRandom  selectable per-molecule RNG (xoshiro256++ default / Philox)  [done]
  SimThreadPool                 explicit std::thread pool (no OpenMP); parallel per-window loop  [done]
  SimMicrotimeEncoder           records -> TCSPC bytes; SPC-132 = byte-identical data2spc_tac  [done]
  SimGrid (+ static fillers)    voxel field + analytic PSF/CEF fillers (convenience only)
  SimEngine                     step()/run() → TTTR;  optional RMF trajectory
  SimTrajectoryWriter           RMF-structured HDF5 via HighFive (no IMP dep)
                    │
                    └── reuses (unmodified) TTTR, TTTR::write (PTU/HT3/SPC132), CLSMImage, HighFive
```

## Phases

- **P0 — Scaffolding & goldens.** Freeze the legacy SPC-132 golden matrix by running the current
  `libburbulator` DLL (reference platform) over a config matrix (focus shape, ±background,
  ±photophysics, CW+pulsed, multi-species/channel, several seeds). Land `test/python/simulation/`
  and a CI job matrix on x86 and ARM.
- **P1 — RNG core.** `SimRandom`: MT19937 (Cokus) ported with **corrected signed** `random4nrm`
  (`int32_t`) → symmetric Gaussian; get/set-state. Dev-time check: integer generators
  (`randomUInt/0i1e/0e1e`) match the legacy stream exactly; `randomNorm` is symmetric and matches a
  reference legacy build compiled with 32-bit-long (signed) semantics. Cross-platform transcendental
  determinism (vendored fdlibm) is optional/future — not required now (engine is statistical).
- **P2 — Engine + SPC-132 encoder.** *(core done)* `SimEngine` ports `smdif_ov3` (grid-field
  diffusion/emission/photophysics + open-volume surface injection) and `SimMicrotimeEncoder` ports
  `data2spc_tac`. **Verified:** encoder byte-identical vs legacy for identical records; engine count
  rate/channel-split/within-window ordering correct; open-volume population equilibrates; full
  engine→encoder pipeline runs. Remaining: pulsed-TAC path (P7), broader statistical suite (P3).
- **P3 — Grid fields.** `SimGrid` (trilinear) *(done)* + static analytic fillers; one excitation grid
  + one detection grid per routing channel (ISM = offset detection grids, implicit). Assert fine grid
  → converges to the exact-eval statistics (decay histogram, FCS curve, count rate).
- **P4 — Emitter input.** *(done)* `SimSample::set_emitter_grid` (multi-channel INT grid: ch0=count,
  ch1=species, ch2=mobile; TIFF read Python-side → int array), `add_fluorophore`/`set_positions`,
  per-particle mobile flag; open-volume `set_population`. Verified emitter-grid expansion.
- **P5 — CLSM scanning.** *(done)* `SimScanner` (per-pixel dwell-time raster, beam-scan field offset)
  emits frame/line/pixel markers (`event_type`) → build `TTTR` from engine arrays → `CLSMImage`
  reconstructs. **Verified:** scanned star reconstructs pixel-perfect (corr 0.991, 37805× contrast);
  example `examples/simulation/clsm_star_scan.py`. Remaining: FLIM micro-times (now 0), PTU/HT3
  on-disk round-trip, bidirectional/multi-frame.
- **P6 — Trajectory output.** *(done)* C++ strided all-molecule reporter
  (`set_trajectory_reporter` → `trajectory_frame/id/species/x/y/z`, verified MSD=6·D·dt ratio 1.004)
  + generic HDF5 writer (`write_trajectory_hdf5`, round-trips). **RMF** is written the IMP way — a
  Python helper over the **standalone RMF library** (`import RMF`, no IMP dep) consuming the trajectory
  arrays → ChimeraX-compatible `.rmf3`; verified write+read-back (`examples/simulation/rmf_trajectory.py`).
  Remaining: Photon-HDF5 photon/trajectory option (PyBroMo/FRETBursts interop).
- **P3b/P4b — PyBroMo-informed field/boundary additions (deferred enhancements).** `SimGrid` importer
  from a numerical/measured PSF + optional cylindrically-symmetric (r,z) field; periodic/mirror
  boundary conditions on `SimSample` (alternative to open-volume injection); optional smFRET `E`-based
  donor/acceptor emission split.
- **P7 — Micro-time (FLIM) + anisotropy.** *(done)* **Anisotropy** revived (rotdiff): per-molecule
  dipole, x-pol photoselection (∝3·ox²), emission dipole = r0-cone tilt + rotational diffusion over
  the excited-state (micro-time) delay, parallel/perp channel split (l1/l2). **Verified** Perrin:
  immobile r≈r0, τ=θ → r0/2 (0.201), fast rotation → 0.03. Example `examples/simulation/anisotropy.py`.
  **FLIM done:** `SimDecay` per-species decay **pattern**
  (arbitrary density; multi-exp + arbitrary IRF-pattern helpers) sampled by O(1) alias → per-photon
  `micro_time()` on a configurable micro-time channel axis (`n_microtime_channels`,
  `microtime_resolution`). **Verified:** arbitrary pattern (bi-exp ⊛ non-Gaussian IRF) recovered at
  corr 0.9989; example `examples/simulation/flim_decay_pattern.py`. ("TAC" renamed to micro-time —
  TAC is the analog hardware.) Remaining: revive anisotropy (`rotdiff`) as a species option.
- **P8 — Bindings, docs, examples.** SWIG `ext/python/PhotonSimulator.i` (+ siblings) `%include`d from
  `ext/python/tttrlib.i`; `%pythoncode` Pythonic helpers (`Sample.from_tiff`, kwarg ctors, numpy I/O,
  `__repr__`). Docs + runnable examples under `examples/{ism,simulation,microscopy_flim}/`.

## Tests / acceptance (headless, per PRD-001 shared-golden style)

- SPC-132 byte-identity goldens vs legacy on x86 + ARM; same-seed determinism repeatable.
- Grid convergence to exact-eval; per-detector ISM sub-images; CLSM shape recovery through `CLSMImage`.
- PTU/HT3 round-trip preserves markers; decay τ and diffusion-time D recovered within tolerance.
- RMF file validity / ChimeraX load; docs build.

## Build / run notes

- arm64 conda env uses an **editable** tttrlib install bound to `/Users/tpeulen/dev/tttrlib`; C++/`.i`
  changes rebuild on import or via the SWIG `python_wrapper` target. New `src/*.cpp` are auto-globbed
  by the root `CMakeLists.txt`. C++17, std-only (no Boost/Eigen); OpenMP/HDF5 already available.

## Downstream — ChiSurf `acq` plugin swap  *(core done)*

**Done:** Qt-free `simulation/core/algorithms.py` maps `simulation_params` → `tttrlib.SimEngine` +
`SimMicrotimeEncoder` → SPC-132 `uint32`; `simulation/core/streaming.py` `TttrlibSimulator` streams
batches on the device queue + writes `m###.spc`. `SimulationDevice.__init__` now selects the tttrlib
backend (Burbulator DLL only as fallback). **Verified headless** through the chisurf package:
`start_measurement` → `read_fifo` streams words → SPC file decodes to the requested photon count
(80k). The old `setup_dialog` still drives it (same `simulation_params` dict) — no GUI change needed.
Remaining (enhancements): `backend/services.py` RPC seam + `manifest.json` rpc_methods; AutoForm
`simulation.view.json` GUI; re-point `simulation_cli.py`; then delete `csrc/`, `libburbulator.dylib`,
`burbulator_dll_wrapper.py`, `setup_dialog.py` once the DLL fallback is no longer wanted.

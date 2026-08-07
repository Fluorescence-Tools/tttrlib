# PRD-008 — Simulator OpenMM-style API, manual & use-case coverage

> **PRD #:** 008 · **Status:** In progress · **Created:** 2026-07-04 · **Owner:** tpeulen
> **Related:** PRD-005 (photon simulator), PRD-007 (BurstNet integration & gap list)
> **Plan:** `~/.claude/plans/zazzy-swimming-badger.md`

## Summary

Reorganize, rename, document, and complete the tttrlib `Sim*` photon-simulation subsystem into a
sustainable, OpenMM-style toolkit that a FRET expert can drive **from the manual alone**. The
physics engine (PRD-005/PRD-007) is correct and now performant; this PRD does **not add new physics**.
It: (1) renames the core classes toward OpenMM roles (keeping the flat-namespace `Sim` prefix),
(2) adds a **thin** Python convenience layer, (3) fills the documentation vacuum with a full Sphinx
manual + a **routing-channel cookbook**, and (4) covers the standard smFRET/MFD/CLSM use-case matrix
with runnable example JSON + commented Python + statistical tests.

Because nobody uses the Sim subsystem yet, this work **explicitly overrides tttrlib's "no API
changes" convention — for the `Sim*` subsystem only.** The rest of tttrlib is untouched. ChiSurf's
acquisition plugin, which drove the old names, is updated in the last phase.

## Problem / motivation

- **Docs vacuum.** No user-facing documentation references the simulator — only PRDs. The C++ headers
  carry good one-line Doxygen, but none of it surfaces in Sphinx, and there is no tutorial, API
  reference, or cookbook. A newcomer cannot discover how to configure even a basic smFRET run.
- **Raw-SWIG ergonomics (PRD-007 G3).** The Python surface is the bare SWIG wrapping: every array
  argument needs a `VectorDouble`, there are no kwargs constructors, no numpy in/out, no `to_tttr()`.
  This is the opposite of an OpenMM-like feel.
- **Thin test/example coverage.** ~4 tests and ~4 examples exist (diffusion, anisotropy, CLSM/FLIM,
  trajectory). Missing: FCS, dynamic-FRET kinetics, proximity-ratio histograms, MFD, PIE, numeric
  PSFs, independent-molecule mode — i.e. most of what a FRET lab actually does.
- **Naming that does not read like a toolkit.** `SimSample`/`SimSettings`/`SimEngine` do not map to
  the mental model of "a system, a propagator, and a driver."

## Design decisions

1. **OpenMM roles, `Sim` prefix (settled):**
   - `SimSample` → **`SimSystem`** — the model (species, kinetics, box, population, background, fields).
   - `SimSettings` → **`SimIntegrator`** — the propagator/platform (dt, RNG, stop conditions, coasting/
     independent/active-margin knobs, micro-time axis).
   - **`SimEngine`** kept — the driver (owns system+integrator, `run()/step()/run_scan()`, outputs).
   - **`SimGrid`** and **`SimMicrotimeEncoder`** kept (descriptive, not abstract).
   - Keep: `SimSpecies`, `SimDecay`, `SimScanner`, `SimThreadPool`, RNG backends. Add: `SimState`
     (immutable snapshot). tttrlib has a flat C++ namespace, so all names stay `Sim`-prefixed.
2. **Thin Python convenience layer only.** No physics reimplementation — kwargs/numpy ctors,
   `from_json`/`to_json`, a `reporters` list (photon-stream / trajectory / TTTR / SPC), `to_tttr()`
   (PRD-007 G7), PSF builders, unit helpers.
3. **No new C++ physics — cover use cases by "smart routing channels."** PIE, MFD dual-color,
   per-channel lifetimes, E-based FRET, and PIE+anisotropy are all expressible with the existing
   engine by composing **routing channels + one SimSpecies per photophysical state + per-species
   `SimDecay` + per-channel brightness `q` + `k_rad`/`k_nrad`**. This *configuration model* is the
   intellectual core of the manual. Numeric/measured PSFs are field *fillers* (voxel-grid builders),
   not a new field type.

## The routing-channel configuration model (manual centerpiece)

`n_channels` = the product of the physical routings present: **spectral** (green/red/…) ×
**polarization** (∥/⊥) × **PIE window** (prompt/delay). Every advanced observable is encoded, not
engineered:

| Use case | How it maps onto the existing engine |
|---|---|
| **FRET / proximity ratio** | per-species per-channel `q` sets the donor/acceptor (green/red) split; `q_green ∝ (1−E)`, `q_red ∝ E·…`. Static states → distinct species → an E-histogram with peaks. |
| **Dynamic FRET / exchange** | transitions between FRET-state species via **`k_nrad`** (spontaneous); intensity-/photo-induced transfer/bleaching via **`k_rad`** (∝ local excitation). |
| **Per-channel lifetime (MFD)** | model each emitting state as its own species with its own `SimDecay` and channel-routing `q` — donor-excited→green with donor τ; acceptor-via-FRET→red with acceptor τ. |
| **PIE** | place a species' decay in the **prompt vs delayed** micro-time window via `SimDecay.t0` within the `laser_period`; the directly-excited acceptor is a species emitting in the delay window / red channels. |
| **Anisotropy** | a parallel/perp channel pair per spectral band; `r0`/`D_rot`/`l1`/`l2` per species (already implemented). |
| **ISM** | multiple detection `SimGrid`s with lateral `x0/y0` offsets. |
| **CLSM / FLIM** | `SimScanner` raster + per-species `SimDecay`; reconstruct with `CLSMImage`. |

## Scope of changes (this PRD)

**C++ (`include/Sim*.h`, `src/*.cpp`):** the rename above; `SimState`; **Doxygen on every public
member**; fix `default_json()` (add the micro-time + throughput keys it already reads) and the
`SimSpecies::D` unit comment (µm²/macro-unit, not µm²/s). **PSF fillers** on `SimGrid`:
`gaussian_lorentzian(w0, zR, …)` (confocal MDF with a z-expanding waist) and
`from_radial(rz, nr, nz, r_step, z_step, …)` (ports PyBroMo's radially-symmetric `NumericPSF`,
bilinear on `r=√(x²+y²)`); both exposed in the JSON `grid_from` (`"gaussian_lorentzian"`, `"radial"`).

**Schema:** `examples/simulation/sim.schema.json` (draft-07) — the documented, validatable definition
of the JSON single entry point; `default_json()` conforms to it.

**Python (`ext/python`):** the thin convenience layer.

**Docs (`doc/`):** a conceptual guide (unit contract; geometry; the routing-channel model; FRET/E;
`k_rad` vs `k_nrad`; anisotropy; PSF/CEF; PIE/MFD), an autodoc API reference, and a **cookbook** —
one page per use case with the physics, the config, the implications (e.g. how the exchange rate
shifts and broadens the E-histogram), and a runnable snippet.

**Tests/examples:** the use-case matrix, each as `examples/simulation/configs/<case>.json` +
`examples/simulation/<case>.py` + a statistical test in `test/python/simulation/`.

## Use-case matrix

single molecule (FCS / count-rate) · single-molecule kinetics (dynamic FRET / exchange) · proximity
ratios (static FRET E-histogram) · anisotropy (steady-state + Perrin) · MFD (lifetime + anisotropy +
intensity) · MFD dual-color · CLSM/FLIM imaging · PSF gallery (Gaussian, Gaussian-Lorentzian,
numeric/measured pybromo) · PIE · PIE + anisotropy. (Open slot for µsALEX, TCSPC-IRF fits, …)

## Unit contract (carried over, PRD-005/PRD-007 G4)

Two unit systems that never mix. **Macro-time:** one abstract unit shared by `dt`, `D` (length²/unit),
`k_rad`/`k_nrad` (per unit), `q`/`background` (photons/unit), and the outputs `macro_window`/
`arrival_time` — the ms convention is common. **Micro-time (ns):** `microtime_resolution`,
`laser_period`, each `SimDecay.dt`/`t0`, `D_rot` (rad²/ns); output `micro_time` is a channel index.

## Roadmap & status

1. **C++ reorg/rename + inline docs** — *in progress.* Done + verified: `SimSample→SimSystem`,
   `SimSettings→SimIntegrator` (SimEngine kept) across files/tokens/guards/includes/SWIG + Python
   tests/examples; standalone + module builds + 18 sim pytests green. `default_json()`/unit fixes.
   PSF fillers (`gaussian_lorentzian`, `from_radial`) + JSON wiring, verified. `sim.schema.json`
   written + conformance-checked. *Remaining:* `SimState`; Doxygen polish.
2. **Thin Python convenience layer** — pending.
3. **Sphinx manual + cookbook** — pending.
4. **Use-case JSON + examples + tests** — pending.
5. **ChiSurf update** to the renamed API (`chisurf/plugins/core/acq/.../algorithms.py` + manifest
   schema) — pending; deferred to last.

## Verification

- C++ + SWIG build clean; the 4 existing sim tests pass under the new names.
- Each use-case test statistically validates its observable (count rate; E-histogram peak
  positions/widths; anisotropy r / Perrin; lifetime recovery; FCS G(τ) diffusion time; CLSM
  reconstruction correlation; PIE prompt/delay separation).
- Every example JSON validates against `sim.schema.json` and round-trips (`from_json`→`to_json`).
- Every commented Python example runs headless and reproduces its docs figure/number.
- `doc/` builds under Sphinx with the API reference populated from the new docstrings.
- Acceptance spot-check: a FRET expert, reading only the manual + schema, can configure a 2-state
  dynamic-FRET dual-color PIE simulation.

## Non-goals

New physics engines; changing the locked unit contract or the byte-exact SPC-132 correctness gate;
GFRD/first-passage stepping (PRD-007 G2 future); touching non-`Sim*` tttrlib API.

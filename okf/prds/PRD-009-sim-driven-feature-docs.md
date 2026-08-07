# PRD-009 — Documenting every tttrlib feature by simulated example

> **PRD #:** 009 · **Status:** Proposed · **Created:** 2026-07-04 · **Owner:** tpeulen
> **Related:** PRD-008 (simulator OpenMM API), PRD-005 (photon simulator),
> PRD-007 (BurstNet integration). **Bug log:** `PRDs/simulator-bugs-found.md`.

## Summary

Use the tttrlib photon **simulator as a ground-truth data source** to document
*every* tttrlib analysis feature by runnable example. Each capability
(correlation/FCS, burst analysis, FLIM/decay fitting, PDA, imaging/CLSM,
anisotropy, localization, TTTR I/O, …) gets a self-contained example that (1)
simulates data with **known** parameters, (2) runs the real tttrlib analysis, and
(3) shows that the recovered quantity matches the simulated input. The example is
simultaneously a **tutorial**, a **figure for the manual**, and a **regression
test** — because the ground truth is known, "does the docs example still
reproduce the number?" is a real assertion, not a smoke check.

## Motivation

* tttrlib's features are currently documented mostly against **real measurement
  files**, which have no ground truth — an example can only show *that* a number
  comes out, not that it is *correct*. Simulated data closes this gap: the input
  E, lifetime, diffusion coefficient, anisotropy, or fluorophore position is known
  exactly, so every example can assert `recovered ≈ simulated`.
* Building the first such example (smFRET burst analysis, PRD-008) already
  surfaced **real bugs** in shipping code — `to_tttr` time scale and channel
  mapping, and the `cusum_sprt` burst detector (see `simulator-bugs-found.md`).
  Ground-truth pipelines are an effective bug-finding harness.
* One coherent, cross-referenced example set — sim in, analysis out — is far more
  approachable than per-feature snippets on unrelated data files, and it exercises
  the simulator (PRD-008) as a first-class citizen.

## The ground-truth validation principle

Every example follows the same three-step shape and ends with a checkable claim:

```
config (known params) --> SimEngine.run() --> to_tttr()  # ground truth in
      --> <tttrlib analysis>                              # feature under test
      --> assert recovered ≈ simulated                    # ground truth out
```

The assertion tolerance is set from counting statistics, so the same file serves
as `examples/…/*.py` (gallery + manual figure) and `test/python/…/*.py`
(regression). A thin helper may share the config between the two.

## Feature matrix

Each row = one simulated example + one validation test + one manual cookbook entry.

| tttrlib feature | Simulated input | Recovered / validated |
|---|---|---|
| **TTTR core I/O** | any sim → `to_tttr` | channels, macro/micro-time, `n_valid_events`, round-trip write/read |
| **Correlation / FCS** | one species, known `D`, `w0` | `G(τ)` diffusion time `τ_D = w0²/4D`, `N` from `G(0)` |
| **Burst analysis** *(done, PRD-008)* | two-state smFRET, known `E` | bimodal proximity-ratio histogram at the simulated `E` (cumulative + sliding search) |
| **FLIM / decay fitting** (fit2x) | species with known lifetime(s) | recovered `τ` (mono/multi-exponential), IRF handling |
| **PDA** | static/dynamic FRET mixture | photon-distribution-analysis `E`/shot-noise decomposition |
| **Imaging / CLSM** | `SimScanner` raster of known emitters | reconstructed image / FLIM matches the emitter map |
| **Anisotropy** | known `r0`, `D_rot` | steady-state `r` and time-resolved decay vs Perrin |
| **Localization** | discrete emitters at known positions | recovered positions vs ground truth |
| **PIE / MFD** | routing-channel PIE config | prompt/delay gating, per-channel lifetimes, 2D MFD plots |

## Deliverables (per feature)

1. `examples/simulation/<feature>.py` — commented gallery example: build the config,
   run, analyse, plot; **explain each analysis parameter** (e.g. burst-search
   `min_photons`/`background_cps`/`S/B`/`alpha`/`beta`).
2. `test/python/…/<feature>.py` — the same pipeline with a statistical assertion
   that the recovered value matches the simulated input.
3. A cookbook subsection in `doc/simulator-guide.rst` (or the relevant feature
   guide) linking the example and stating the physics + expected result.
4. An example JSON in `examples/simulation/configs/` where the config is reusable.

## Roadmap

1. **Foundation (done / in progress).** Simulator API + manual (PRD-008); smFRET
   burst-analysis example (cumulative + sliding search) as the template; fix the
   bugs the process reveals.
2. **Core analysis features.** FCS/correlation, FLIM/decay fitting, anisotropy —
   each has a clean closed-form ground truth (`τ_D`, `τ`, Perrin) ideal for
   assertions.
3. **Higher-level features.** PDA, CLSM/FLIM imaging, localization, MFD/PIE.
4. **Cross-links.** Wire every example into the manual and the gallery; add a
   "validated against simulation" badge/section to each feature guide.

## Verification

* Every example runs headless and its assertion (`recovered ≈ simulated`) passes.
* Examples are in the sphinx-gallery build; heavy ones are gated via the gallery
  blacklist with a fast-mode `n_ph_max`.
* New bugs found are logged in `simulator-bugs-found.md` and fixed or tracked.

## Non-goals

Replacing measurement-based examples (both have value); changing analysis APIs
beyond bug fixes; simulator physics changes (PRD-008 established none are needed).

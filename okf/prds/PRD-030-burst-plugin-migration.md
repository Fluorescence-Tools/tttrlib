# PRD-030 — Migrate chiSurf burst plugins to `tttr` CLI calls

> **PRD #:** 030 · **Status:** ⚪ Draft · **Created:** 2026-08-08 · **Owner:** tpeulen
>
> **Blocked by:** [PRD-026](PRD-026-mfd-sim-to-ndx-pto-pipeline.md) (C++ port)
> and [PRD-027](PRD-027-modular-algorithm-registry.md) (modular registry).

## Summary

Every burst-related chiSurf plugin becomes a thin shim around the compiled
`tttr` binary. The computation moves from Python/GUI to the C++ CLI; the
plugins retain the GUI, API, CLI, and RPC interfaces but delegate the
physics to `tttr burst ... --output out.pto`.

The end state: `tttr burst` delivers **all** burst results into a `.pto`
that were previously produced by burst plugins in chiSurf. The plugins read
the `.pto` artifacts and present them to the user.

## What migrates

| chiSurf plugin | CLI equivalent | Artifact |
|---|---|---|
| `burst_selection` | `tttr burst` (primary search) | `.bur` in `.pto` |
| `burst_bva` | `tttr burst --companions bva` | `.bv4` in `.pto` |
| `burst_2cde` | `tttr burst --companions 2cde` | `.2c4` in `.pto` |
| `burst_mle_analysis` | `tttr burst --companions mle` | `.bg4`/`.br4` in `.pto` |
| `burst_fcs_correlator` | `tttr burst --companions fcs` | `.td4` in `.pto` |
| `burst_h2mm` | `tttr burst --companions hmm` | `.bh4` in `.pto` |
| `burst_fusion` | `tttr burst --companions fusion` | `.fu4` in `.pto` |
| `burst_irf_bg` | `tttr burst --companions irf` | IRF curves in `.pto` |
| `burst_background` | `tttr burst --companions bg` | bg artifacts in `.pto` |

## What stays in chiSurf

- GUI (wizards, parameter forms, plots)
- API (Python objects wrapping `.pto` reads)
- CLI shim (calls `tttr burst`, reads result)
- RPC (forwards to `tttr` or reads `.pto`)
- ndx integration (reads `.pto`, renders)

## Plugin shim contract

Each plugin:

1. Writes a detector setup JSON (its existing GUI state)
2. Calls `tttr burst input.ptu --setup setup.json --output out.pto --companions all`
3. Reads the `.pto` via `PtoFile` + `ProvenanceGraph`
4. Presents the artifacts to the user (tables, plots, gating)

The plugin does **not** compute anything — it configures, invokes, and
displays.

## Dependencies

- PRD-027 must be implemented first: the `tttr burst` CLI must dispatch
  companion analyses through the registry, so a new companion (e.g. a
  plugin-provided BVA variant) appears without recompiling.
- PRD-026 C++ port must be complete: the compiled pipeline must produce
  the same `.pto` artifacts as the Python prototype.

## Non-goals

- Removing chiSurf's Python burst code before the C++ replacement is verified.
- Changing the `.pto` format or `_mmfdb_*` tag vocabulary.
- Breaking any existing chiSurf plugin API.

# PRD-026 — Simulated MFD burst pipeline to an ndx-conformant `.pto`

> **PRD #:** 026 · **Status:** 🔵 Proposed · **Created:** 2026-08-08 · **Updated:** 2026-08-08 · **Owner:** tpeulen

## Summary

Give the compiled `tttr` CLI a closed, piped loop for polarization-resolved
multi-parameter fluorescence detection (MFD): simulate an MFD photon stream
from a JSON config, write it to a TTTR file, run burst selection over it, and
land the burst table in a `.pto` container that the ndx viewer
(`chisurf/modules/ndxplorer`) opens. It must be possible to pipe one subcommand
into the next so the whole thing reads as one command chain:

```
tttr sim mfd/2col-2pol.json -o sim.ptu | tttr sm - --setup mfd.json \
    --output bursts.pto
```

The single biggest fact this PRD turns on is that every element already exists
in the library — the C++ simulator, the C++ burst search, the C++ PTO writer,
and a chiSurf-compatible detector-setup JSON — so this is wiring and
conformance, not new physics.

## Requirements

### R1. `tttr sim` subcommand

* Reads a tttrlib `SimEngine` JSON config (the strict schema in
  `examples/simulation/sim.schema.json`; unknown keys are rejected by the
  engine). `SimEngine.from_json` (C++ `SimEngine::from_json`, `run()`,
  `encode` → `TTTR::write`) is the entry point.
* `tttr sim CONFIG.json -o OUT.ptu` constructs the engine, runs it, writes the
  photon stream to `OUT` as a TTTR container (PTU/SPC).
* Uniquely, the CLI must reflect the simulator's *own* progress, so
  `--progress` works the same here as everywhere else — this is where the
  `ProgressTicker` in `modules/util` threads through to the sim loop.

### R2. Pipeable input

* Every subcommand that takes an input TTTR file must accept `-` to mean
  **stdin** (`tttr sm -`, `tttr convert - out.ptu`, `tttr image import -` if it
  exists), so the output of one subcommand feeds the input of the next without
  landing on disk. Where a container (PTO) drives its own seekable input this
  is a real constraint — see *Notes*.

### R3. MFD detector setups, chiSurf-shared

* A detector `detector_setups.json` describing the MFD instrument:
  * `green` → routing channels `[0, 8]`,
  * `red` → routing channels `[1, 9]`,
  * channel 0/1 are the perpendicular branches, 8/9 the parallel ones
    (interleaved `chs[::2]` = parallel, `chs[1::2]` = perpendicular —
    `chisurf/core/.../tttr_channel_definition_tttr_io.py:216-217`).
* This file is read by `sm` and `image export` (already wired: `--setup`,
  `--setup-name`, `--detector`) and is byte-compatible with chiSurf's
  `~/.chisurf/detector_setups.json` (MMFDB is chiSurf's primary store; JSON is
  the interchange form the CLI reads).
* The simulated photon stream must be emitted on exactly these routing
  channels, so the MFD channel mapping matches the setup (engine 0..3
  remapped via `_ENGINE_TO_MFD = [0, 8, 1, 9]`).

### R4. Two population-unresolved polarization-resolved populations

* The sim config drives **two species** resolved by polarization: each labelled
  FRET/polarization mode as its own species with a polarized `q` vector and a
  photoselection `k_rad`, per `chisurf/core/fluorescence/burst/simulate.py`
  (`SmfretParameters.config()`). The two populations differ in lifetime /
  anisotropy (r0, D_rot, decay) so burst-level anisotropy distinguishes them.

### R5. Bursts land in an ndx-conformant `.pto`

* `tttr sm ... --output out.pto` (PTO extension) writes the burst table as a
  `.pto` artifact ndx's `pto_reader.read_container` opens:
  * `_mmfdb_artifact.row_grain == "burst"`,
  * `_mmfdb_artifact.data_format == "dstore"` (a columnar `TTTRSTOR` table),
  * `_mmfdb_operation.operation_type == "burst_selection"`, with
    `settings_json` / `settings_hash` for run identity,
  * the `.bur` 2N+1 interleave preserved in the store (ndx
    `deinterleave_bursts`), plus provenance `_mmfdb_edge` from the source
    photon-stream artifact.
* Written via the existing C++ writer in `modules/io/pto`
  (`pto_add_store` / `pto_update_store`, the same API the Python bindings use).
  ndx picks the most recent `row_grain="burst"` artifact.

### R6. One reproducible command chain

* The PRD ships a checked-in, reviewed MFD sim config and detector setup so the
  pipeline is `Documented : Example : Pipeline` reproducible in CI: simulate →
  burst → open the `.pto` in ndx. The output's provenance tags must let a
  re-run with the same settings replace the artifact in place rather than
  accumulate.

## Non-goals

* No new simulation physics (FRET rates, photoselection, anisotropy) — the
  engine already models all of these; the PRD only wires them through JSON.
* No ndx viewer work — ndx must open the produced file unchanged.
* Not replacing chiSurf's Python authoring flow; the CLI `detectors` wizard
  already covers setup authoring and is unchanged here.
* No MMFDB-as-SQLite; the `.pto` stays a self-contained EBML container (the
  `_mmfdb_*` in the file are the vocabulary terms, not a database).

## Open questions

1. **stdin + seekable containers:** PTO is read as a whole EBML document; can
   `tttr pto`/`tui` accept `-`, or is stdin support only for the photon-stream
   formats now? Proposal: `-` works for TTTR/PTU/SPC inputs first; PTO stdin is
   out of scope unless a reader already supports it.
2. **Channel remap location:** should the sim→routing remap (`[0,8,1,9]`) live
   in the sim config itself (channels already numbered 0..3 with a remap
   table) or in the CLI after write? Must not hardcode the mapping inside the
   compiled binary — it belongs in the JSON (consistent with "detector
   knowledge is JSON, not code").
3. **PTO writer surface in `io_pto`:** confirm `pto_add_store` can carry the
   exact `_mmfdb_*` tag set ndx expects (row_grain/operation_type/settings
   hash/edge) from C++ without the Python wrapper, or whether a thin C++ shim
   is needed.

## Acceptance criteria

1. `tttr sim mfd.json -o sim.ptu --progress -` exits 0, writes a TTTR file on
   routing channels 0/8/1/9, and emits valid begin/progress/finish JSONL.
2. `tttr sm - --setup mfd_detectors.json --detector green -o bursts.pto`
   reads the piped stream, finds bursts on channels {0,8}, and exits 0.
3. `tttr pto ls bursts.pto` shows a burst artifact (`row_grain=burst`,
   `data_format=dstore`, operation `burst_selection`) and a provenance edge to
   the source stream.
4. ndx (`modules/ndxplorer`) opens `bursts.pto` and renders the burst /
   anisotropy scatter of the two populations — the ground-truth E/anisotropy
   from the sim config separating the two clusters.
5. Re-running R2 with identical settings replaces the burst artifact in place
   (one artifact, not a pile).
6. Two-channel JSONL contract unchanged (verified with the existing
   `python3 -c 'json.loads'` per-line check).

## Notes

* The CLI's detector-setup loader (`detector_setup.{h,cpp}`) already
  accepts-and-ignores chiSurf's extra setup keys (`tttr_reading`,
  `polarization_resolved`, `g_factor`/`l1`/`l2`, `channel_luts`, ...), so the
  canonical chiSurf MFD setup file works verbatim.
* Bottleneck for review is the writer path (R5/Q3), not the simulator; the sim
  and burst halves are already exercised by the C++ target.
* Related: `okf/specs/tttr-runner.md` (runner contract), `okf/design/tttr-cli-progress.md`
  (progress), `okf/specs/pto-mfdb.md` (container layout — normative in chiSurf repo).

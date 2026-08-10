# PRD-026 — Simulated MFD burst pipeline to an ndx-conformant `.pto`

> **PRD #:** 026 · **Status:** 🟢 Done · **Created:** 2026-08-08 · **Updated:** 2026-08-10 · **Owner:** tpeulen
>
> **C++ port status (2026-08-10): the burst-table half is done and verified.**
> `tttr sim` → `tttr sm --setup` → `.pto` → ndX runs end to end on simulated
> MFD data. The table is detector-setup-driven and matches ChiSurf's
> `generate_burst_dataframe` **cell for cell**: 23/23 columns on the
> two-detector setup, 37/37 on the four-detector one, 27/27 with PIE windows
> and micro-time gates. ndX opens the container, joins the companions and maps
> the provenance graph; the two simulated species come out as two separated
> proximity-ratio populations (medians 0.21 / 0.79).
>
> **The MLE half landed 2026-08-10** — `--mle` fits one lifetime per burst per
> detector and recovers the simulation's ground truth (3.87 ns for a 3.8 ns
> species, 1.71 ns for a 1.6 ns one). BVA and FRET-2CDE are real computations
> too. What is left is stdin (R2) and the registry dispatch of PRD-032; see
> *Remaining work*.
>
> **Blocker status (2026-08-09):** [PRD-027](PRD-027-modular-algorithm-registry.md)
> is **resolved** — the `register_operation` ABI is live (`tttrlib_operation_v1`
> struct, `register_operation` callback, `PluginHost::operations()`, registry
> splice). A `.dll` can register a new algorithm type without recompiling.
>
> **Design requirement (2026-08-09), now implemented:** ``tttr sm`` emits
> columns only for the detectors the ``--setup`` file defines; a single-color
> anisotropy experiment with parallel/perpendicular detectors produces
> ``Duration (parallel) (ms)`` style columns.

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
  * the list is **interleaved**: `chs[::2]` is parallel and `chs[1::2]` is
    perpendicular (`chisurf/core/.../tttr_channel_definition_tttr_io.py:216-217`),
    so these lists put **0/1 parallel and 8/9 perpendicular**.

  > **Corrected 2026-08-10.** This clause used to say "channel 0/1 are the
  > perpendicular branches, 8/9 the parallel ones" in the same breath as the two
  > lists above, which contradicts them under its own interleave rule. The lists
  > are what the code reads, so the prose was the wrong half. It is worth being
  > explicit because the failure is invisible: swapping the arms leaves every
  > photon count identical and inverts every anisotropy — the shipped example
  > setup had exactly that, `green: [8, 0]` against a simulation emitting
  > green-parallel on routing 0.
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

## C++ port design: detector-setup-driven column generation

### The problem with the current C++ code

`cmd_sm.cpp` hardcodes a green/red split by routing channel parity (`ch == 0`
→ green, `ch == 1` → red). This is wrong for any experiment that is not a
2-channel green/red setup. A single-color anisotropy experiment has
parallel/perpendicular channels, not green/red. A 4-channel MFD setup has
green-parallel, green-perpendicular, red-parallel, red-perpendicular.

### The chiSurf pattern (correct)

chiSurf's `burst.py` (`generate_burst_dataframe`, line 443) iterates over the
detector setup's named detectors and generates columns dynamically:

```python
for d in detectors:  # d = detector name string
    det_cols += [
        f"First Photon ({d})", f"Last Photon ({d})",
        f"Duration ({d}) (ms)", f"Mean Macrotime ({d}) (ms)",
        f"Number of Photons ({d})", f"{d.capitalize()} Count Rate (KHz)",
    ]
```

Column names are parameterised by the detector name — `Duration (green) (ms)`,
`Duration (parallel) (ms)`, etc. Sentinels (`-1.0`) are written for detectors
with no photons in a burst.

### What the C++ port must do

1. **Thread `DetectorSetup` through to the PTO output.** Currently
   `resolve_setup_channels` flattens named detectors into a channel list and
   discards the names. The named detector definitions must be preserved.

2. **Generate per-detector columns from the setup.** For each named detector
   in the setup, emit these 7 columns (matching chiSurf):
   - `First Photon ({name})`
   - `Last Photon ({name})`
   - `Duration ({name}) (ms)`
   - `Mean Macro Time ({name}) (ms)`
   - `Number of Photons ({name})`
   - `{Name} Count Rate (KHz)` (capitalized)
   - `Mean Microtime ({name}) (ns)`

3. **No hardcoded green/red.** A setup with detectors named `parallel` and
   `perpendicular` produces `Duration (parallel) (ms)` etc. A setup with
   `green_par`, `green_perp`, `red_par`, `red_perp` produces four sets.

4. **Photon matching uses both routing channels AND micro-time ranges.**
   `DetectorDef::channels` + `DetectorDef::micro_time_ranges` define the
   mask. A photon belongs to detector `D` if its routing channel is in
   `D.channels` AND its micro time falls in any of `D.micro_time_ranges`
   (empty ranges = accept all, matching chiSurf's `_micro_time_mask`).

5. **Fallback when no setup is provided.** If `--setup` is not given, emit
   only the aggregate columns (First/Last Photon, Duration, Mean Macro Time,
   Number of Photons, Count Rate) — no per-detector columns. This is the
   single-color or "I just want bursts" path.

### What mmfdb.dic says

The naming dictionary uses the pattern `({detector_name})` consistently:
`Duration (green) (ms)` is one instance; `Duration (parallel) (ms)` is
another. The dictionary's role is to define the **column template**, not
to enumerate every possible detector name. A CI check validates that the
parenthesised suffix matches a detector in the setup.

## Prototype implementation status (2026-08-08)

> Kept as the record of what the Python prototype proved. Note that the
> prototype's own column names are **not** conformant — it writes
> `Mean Macro Time (green) (ms)` where the format is
> `Mean Macrotime (green) (ms)`, and it splits green/red by channel parity, the
> thing the C++ port exists not to do. ChiSurf's `generate_burst_dataframe` is
> the reference, not this.

### What exists

| Component | Location | Status |
|---|---|---|
| Generic burst pipeline | `prototype/burst_pipeline/pipeline.py` | ✅ Working — `BurstPipeline` + `mfd_config()` |
| Compute operations | `prototype/burst_pipeline/operations.py` | ✅ burst search, IRF (gaussian/skewed/experimental), MLE (Fit2x VV/VH), BVA, 2CDE |
| Provenance tracking | `prototype/burst_pipeline/provenance.py` | ✅ `ProvenanceNode`, `ProvenanceGraph`, settings_hash |
| PTO builder | `prototype/burst_pipeline/pto_builder.py` | ✅ Auto-writes full `_mmfdb_*` tag set |
| Processing list | `prototype/burst_pipeline/processing_list.py` | ✅ Read .pto → ordered steps → replay any step |
| Central naming dictionary | `okf/nomenclature/mmfdb.dic` | ✅ mmCIF format, 100+ entries |
| Operation registry (C++) | `modules/registry/src/OperationRegistry.cpp` | ✅ 8 operations with I/O specs |
| ndx integration | `chisurf/.../settings/mmfdb_dic.py` | ✅ mmCIF loader wired into equation validator |
| IRF convolution docs | `okf/design/sim-irf-convolution.md` | ✅ Documented dt matching, background_decay, VV/VH |
| agy export tool | `tools/agy-export` | ✅ Executable, documented |

### What passes

- **18 provenance tests** — completeness, settings hash, lineage integrity, round-trip reconstruction, column conformance, ndx equations
- **21 value-recovery tests** — fit failure NaN handling, physical values, ground truth recovery, anisotropy columns, FRET efficiency
- **5 nomenclature tests** — constants match, equation outputs defined, all refs resolve, prototype columns in dictionary, symbol table
- **Tau recovery**: peaks at 3.75 ns (truth 3.8) and 1.55 ns (truth 1.6) — bimodal distribution resolves both species

## C++ port — what landed, 2026-08-10

The reproducible chain, checked in and covered by
`test/python/misc/test_cli_sm_burst_table.py`:

```
tttr sim examples/simulation/configs/mfd_2col_2pol.json \
     --channels 4 --routing-channels 0,8,1,9 -o mfd_sim.spc
tttr sm mfd_sim.spc \
     --setup examples/simulation/configs/mfd_detector_setups.json \
     --output bursts.mmfdb.pto --csv bursts.tsv
tttr pto ls bursts.mmfdb.pto
```

**Naming, settled 2026-08-10 and now normative in the profile spec:** `.pto` is
*the container* and claims nothing about its contents; `.mmfdb.pto` is a `.pto`
that also carries the PTO.MFDB profile. The tag goes on the stem, never the
suffix — a `.pto.mmfdb` would stop being a container to everything that
dispatches on the extension. The name is a courtesy for people and directory
listings; a reader decides conformance from `_mmfdb_container.profile` inside
the file. See `doc/formats/pto.rst` and `doc/formats/pto-mfdb.rst`.

| Acceptance criterion | State |
|---|---|
| 1. `tttr sim` writes routing channels 0/8/1/9, JSONL progress | ✅ via `--routing-channels`; the mapping is on the command line, not in the binary (open question 2) |
| 2. `tttr sm --setup ... -o bursts.pto` exits 0 | ✅ (stdin `-` still not wired — R2 open) |
| 3. `tttr pto ls` shows `row_grain=burst`, `data_format=dstore`, `burst_selection`, provenance edge | ✅ |
| 4. ndX opens it and renders the two populations | ✅ 3778 rows, 26 columns after the companion join; PR medians 0.21 / 0.79 |
| 5. Re-running with identical settings replaces in place | ✅ by `_mmfdb_operation.settings_hash` |
| 6. Two-channel JSONL contract unchanged | ✅ |

Three things the port settled that the design section did not say:

* **`data_format` is `dstore`, not `bur`/`bg4`/`bv4`/`2c4`** (this resolves open
  question 3). Those extensions name a *file layout*, and an object inside a
  container is not one; `put_table` writes what ChiSurf's `put_table` writes.
  ndX already accepted both because it matches on `operation_type` too.
* **The `.bur` 2N+1 interleave and its trailing blank column are NOT written**,
  which contradicts R5 as originally worded. They exist so a `.bur` can be
  merged with its companions *by counting rows*; in a container the relation is
  a declared edge, and ChiSurf's own container writer calls
  `deinterleave_bursts` before writing. Writing them would mean every reader
  strips them again.
* **A companion's run identity has to include its parent's.** Keyed on its own
  settings alone, a BVA table computed over one burst list is silently
  overwritten by a BVA table computed over another — and the row counts still
  line up whenever the two searches happen to find the same number of bursts.
  The parent's `settings_hash` is now part of the companion's settings.

## The MLE half — landed 2026-08-10

`tttr sm --mle --irf <spec>` fits one lifetime per burst per named detector
through `fit23`, over that detector's parallel and perpendicular arms jointly.
The placeholder tables are gone; these are measurements.

**Ground truth recovered:** the 3.8 ns species comes back at **3.87 ns** and the
1.6 ns species at **1.71 ns**, as two resolved modes of one distribution,
separated by proximity ratio. Pinned by
`test_mle_recovers_the_simulated_lifetimes`.

Four decisions, each of which replaced something that looked fine:

1. **`--irf` is required.** There is no default prompt. A lifetime fitted
   against the wrong instrument response is wrong by roughly its width and
   nothing in the output says so, so the one behaviour ruled out is choosing
   quietly. `delta`, `gaussian:FWHM[,T0]`, or a file; `--irf delta` has to be
   typed.
2. **Only `tau` is fitted.** The first attempt fitted tau and rho with r0 held
   at 0.38, and it recovered 3.69 ns for the 3.8 ns species — which looked like
   success and was luck. On a synthetic burst of the same size the same
   configuration returned 6.68 ns for a 3.8 ns truth, and moved by a factor of
   two on a change of start value with a 2I\* that still looked reasonable. A
   hundred photons do not determine an anisotropy. r0 = 0 with rho held is also
   the precondition for the kernel's own well-conditioned path.
3. **`gamma` is measured per burst, not guessed.** It is the *background
   fraction of that burst*: the kernel renormalises the background pattern to
   the burst's own total, so the pattern supplies only a shape and gamma
   supplies the magnitude. Measured as (the detector's off-burst rate) x (the
   burst's duration) / (its photons). Left at 0 it subtracts nothing and biases
   every lifetime up by ~50% on a 13% background.
4. **The fit runs on a rebinned axis** (`--mle-bins`, default 128). A burst is
   ~100 photons over 4096 raw TAC channels, so the raw axis is almost all zeros
   and the convolution is 4096 long for no gain: 14 s → 0.26 s.

The background pattern itself is *measured*, not assumed — the detector's
photons that no burst contains are exactly its background, and they are already
in hand.

Still open on this half: an IRF read from the container rather than from a flag,
`--mle` for a four-arm setup (it runs, but each single-channel "detector" then
has no perpendicular arm, so the anisotropy machinery is inert), and a
`split-by-state` fit against H2MM.

## Remaining work — closed 2026-08-10

1. ~~**R2, stdin.**~~ **Done.** `tttr sm -`, `tttr convert -` and `tttr
   correlate -` read stdin, so the chain needs no intermediate file from the
   user. It is **spooled to a temporary file, not streamed**, and the
   distinction is worth recording rather than hiding: every container reader in
   this library seeks — a PTU reads its header then jumps to the record block, a
   PTO reads a directory at the end — and a pipe cannot seek. Streaming would
   mean a second decoder per format or a silent failure on the formats that
   jump. The temporary is removed on every exit path (`InputPath` is a type for
   exactly that reason) and a test asserts nothing is left behind.

   Two things the piped path had to be taught, both found by running it:
   `-` is the shell's word for stdin and not a name a reader can resolve later,
   so it must not land in the burst table's `First File` column nor as the stem
   of the container's objects — a piped input is called `stdin`. And the source
   checksum has to be taken over the bytes actually read, since `-` cannot be
   opened at all. Piped and direct output are now identical except for the
   source column, which differs truthfully.

   `tttr sm` also gained `-o` as a short form of `--output`. Acceptance
   criterion 2 is written with `-o`, `tttr sim` has always taken it, and a chain
   that spells the same thing two ways is a papercut in the one place this
   pipeline is meant to be typed by hand.

4. ~~**PRD-032, dispatch half.**~~ **Done.** `TTTR::burst_search` resolves its
   mode through a table (`BurstSearchDispatch.h`), and a search a plugin
   contributes is reachable through it too. Three searches the registry
   advertised were silently returning sliding-window bursts through the old
   fallback — see PRD-032. `OperationRegistry.cpp`'s literal remains, and is
   PRD-032's criterion 1, not this PRD's.

5. ~~**Column order under windows.**~~ **Done.** `DetectorSetup::windows` is a
   vector in file order, like `detectors` beside it already was. A `std::map`
   sorted them alphabetically, so a setup declaring `prompt` then `delayed`
   produced the `S <window> <detector>` block in the opposite order — every
   value correct and the positions wrong, which is invisible until another tool
   reads the table positionally. Pinned by
   `test_window_columns_come_out_in_file_order`, which uses exactly that pair
   because sorted and file order disagree on it.

# PRD-034 — .pto as its own TTTR sink: native photon tables and header definitions

**Status:** 🔵 Proposed
**Depends on:** PRD-020 (targeted reads), PRD-021 (record streams), PRD-015 (conformance suite)
**Spec to amend:** `doc/formats/pto.rst`

## The claim

A `.pto` must be able to hold a photon stream **natively** — its own photon
table and its own header definitions — so that `tttr.write("run.pto")`
produces a self-contained file and `TTTR("run.pto")` reads it back, with no
vendor format inside. Today a photon stream in a `.pto` is an **embedded
vendor file** reached as `run.pto|m001.ptu`: the container is a wrapper, not
a sink.

## What exists (measured 2026-08-10)

More than expected, and less than it looks:

- **The spec already names the thing.** The example object in
  `doc/formats/pto.rst` (Objects section) is literally
  `PtoKind "photons"` + `PtoEncoding "dstore"`, and dstore is "the default
  for anything tabular". The design direction is pre-drawn.
- **Half a reader exists.** `holds_photons()` accepts
  `dstore` + `kind=="photons"`, and the loader reads `macro_time`,
  `micro_time`, `routing_channel`, `event_type` columns into
  `append_events` (`modules/io/pto/src/io_pto.cpp:2583-2598`). **But it
  applies no header whatsoever** — no macro-time resolution, no micro-time
  resolution, no bin count, no provenance. The TTTR that comes back is
  dimensionless numbers.
- **Positioning refuses native photons.** `build_cues` fails with
  *"not a record stream this build can index"* (`io_pto.cpp:2617`), and
  `pto_read_events`' seek path is record-stream only. PRD-020's targeted
  reads stop at the embedded-vendor boundary.
- **There is no writer.** `FileFormat.can_write` exists
  (`modules/io/base/include/TTTRFormat.h:193`); nothing fills it for pto.
  `tttr.write("out.ptu")` and `("out.spc")` work; `("out.pto")` does not.
- **The tag system was built for this and is waiting.** `PtoType`
  (`io_pto.h:49-61`) documents itself as covering *"PicoQuant's twelve
  header types, plus the two object references"* — including `Bytes`, which
  can carry the `tyBinaryBlob` that the PTU reader today **drops with an
  error** (`modules/io/pq/src/io_pq.cpp:114`).

## What is in a PTU header — the checklist a self-contained sink must cover

A PTU tag is `(Ident, idx, type, value)` in twelve types — structurally the
rows `TTTRHeader` already keeps as JSON, and nearly a `PtoTag`. The groups,
from the reader (`io_pq.cpp`):

1. **Record decode** — `TTResultFormat_TTTRRecType`, `BitsPerRecord`,
   `NumberOfRecords`. In a native table these become *provenance*, not
   decode instructions: dstore columns need no record type.
2. **Clocks** — `MeasDesc_GlobalResolution` (macro clock),
   `MeasDesc_Resolution` (micro bin), `MeasDesc_BinningFactor`, and the
   derived micro-time bin count (`32768 / binning`, `io_pq.cpp:154`).
   *Without these a photon stream is not data.*
3. **Acquisition** — `MeasDesc_AcquisitionTime`, `MeasurementMode`,
   `SubMode`, `StopReason`/`StopAfter`/`StopOnOvfl`.
4. **Hardware, per-channel via `idx`** — `InputCFDLevel`,
   `InputCFDZeroCross`, `InputOffset`, `InputRate`, …
5. **Imaging** — `ImgHdr_PixX/PixY/LineStart/LineStop/Frame`. These drive
   the C++-side CLSM auto-configuration; lose them and an imaging
   measurement stops reconstructing.
6. **Provenance and free tags** — `FileTime`, `CreatorName`/`Version`,
   `Comment`, arbitrary `usr_*` tags in any of the twelve types, including
   binary blobs.

## Design

1. **The photon table is a dstore object**, `kind = "photons"`, columns
   normative and pinned to TTTR's in-memory dtypes (`TTTR.h:396-402`):
   `macro_time` u64, `micro_time` u16, `routing_channel` i8, `event_type`
   i8. The names the loader already uses become the spec. No bit-packed
   records, no overflow events — `macro_time` is absolute, which is the
   native table's advantage over every vendor record stream.
2. **Header definitions are PtoTags targeting the photons object's UID** —
   one tag per header row, `(name, idx, PtoType, value)`, PTU's twelve
   types mapping onto `PtoType` including `Bytes` for blobs. One mechanism,
   already specified, typed and targeted; a JSON sidecar would be a second
   header system and is rejected. A small closed set is **required and
   normative** (the sink is unusable without them):
   `macro_time_resolution`, `micro_time_resolution`,
   `number_of_micro_time_bins`, and `source_record_type` (provenance).
   Everything else is open fidelity: every tag the source header carried is
   preserved verbatim, imaging tags included.
3. **Write.** `TTTR::write("run.pto")` through the same above-core hook the
   read path uses (the format table carries a pointer io_pto fills in —
   `TTTR.cpp:1497-1510`; `can_write` becomes true). Events stream into
   `FileData` last-child-first-sized exactly as the spec's over-wide-VINT
   streaming already prescribes; header tags follow. Writing into an
   *existing* container appends a second photons object — `tttr pto add`
   semantics, one measurement per object.
4. **Read.** Bare `TTTR("run.pto")`: exactly one photons object → open it;
   several → require the `|` selector and fail with the candidate list.
   The required tags configure the TTTR header; all remaining tags land in
   `TTTRHeader`'s JSON so nothing narrows. An imaging stream's `ImgHdr_*`
   tags reach the CLSM auto-configuration unchanged.
5. **Targeted reads without cues.** `pto_read_events(first_event, n_events)`
   over a native table is a dstore **row slice** — columnar storage makes
   the event index the seek position, so the cue machinery (which exists
   for embedded record streams that must be decoded to be counted) is
   simply not needed on this path. `build_cues` on a native object becomes
   a documented no-op, not an error.
6. **Bindings.** u64 `macro_time` crosses JS/R as the 64-bit work already
   solved (BigInt arrays; R strings for scalars) — and the 53-bit rule from
   PRD-020 applies to any macro time read as a scalar Number. Conformance
   cases (PRD-015) cover all four languages.

## Acceptance criteria

1. **Round trip**: PTU → `TTTR` → `write(".pto")` → `TTTR`: all four event
   arrays bit-identical, every header tag preserved `(name, idx, type,
   value)` — including one imaging PTU whose CLSM image reconstructs
   identically from the `.pto`.
2. **Blobs survive**: a `tyBinaryBlob` tag rides through as a `Bytes` tag
   instead of today's stderr error-and-drop; the pq reader keeps it.
3. **Selection**: bare open works for a single-stream container; a
   multi-stream container demands a selector and names the candidates in
   the error.
4. **Ranges**: `pto_read_events(first, n)` on a native table equals slicing
   the full read, and is not slower per event than the embedded-PTU path
   with cues.
5. **Cross-language**: conformance-suite cases pass in Python, R, Java and
   JS, including a macro time above 2^53 handled per the 53-bit rule.
6. **Docs**: `pto.rst` gains the normative photons-object section — column
   names, dtypes, required tags — and `tttr convert` accepts `.pto` as a
   target.

## Non-goals

- **Retiring the embedded original.** Embedding the vendor file byte for
  byte stays the fidelity anchor; the native table is the *decoded* stream
  living beside it, not a replacement for it.
- **Compression** — the container does not compress; unchanged.
- **A new record format** — no bit-packing; the columns are the format.

# PRD-034 — .pto as its own TTTR sink: native photon tables and header definitions

**Status:** 🔵 Proposed
**Depends on:** PRD-020 (targeted reads), PRD-021 (record streams), PRD-015 (conformance suite)
**Consumer waiting on this:** chisurf PRD-98 (acquisition writes its photons
straight into a `.pto` as they arrive) — see *Writing a file that is still
being measured* below.
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

## Writing a file that is still being measured

An acquisition is the case the sink exists for, and it is *not* the
write-once case above: the photon count is unknown at open, the run may last
hours, and the process may be killed. The format already anticipates all
three (`doc/formats/pto.rst`) —

- `FileData` is written last behind an **eight-octet over-wide size VINT**,
  a placeholder rewritten when the length is known (spec line 484);
- a `Segment`'s size is likewise **eight octets, rewritten as the file
  grows**, "what lets the number be raised without moving anything after
  it";
- each `SeekHead` is **padded to an 8 KiB reserve** precisely "because the
  commit protocol depends on being able to rewrite one where it lies";
- and the truncation rule is explicit: **bytes after the end of the
  `Segment` are not part of the file** — an abandoned write from a session
  that died before the commit that would have claimed them.

Read together, those give the guarantee an acquisition needs, but only if
the writer *takes* it: a run that streams for an hour and never commits
loses the hour, because everything it wrote lies outside the `Segment`.
So this PRD adds **checkpointing** as a first-class writer operation, not
an implementation detail:

7. **`PtoFile` can commit a still-growing object.** A checkpoint raises the
   `FileData` size VINT, re-stamps `PtoRowCount` to the events written so
   far, raises the `Segment` size, and rewrites the `SeekHead` in its
   reserve — in that order, so that no intermediate state is a file a
   reader misreads. Cost is a few hundred bytes of seek-and-write,
   independent of how much data preceded it, which is what makes a
   per-second checkpoint reasonable. The photon table's own row-slice
   reads (design item 5) then work on the committed prefix while the
   writer is still appending, since a row slice needs only `PtoRowCount`
   and the column layout.

The writer lock already in `PtoFile::open` (exclusive advisory, released
when the process ends *however* it ends) is what keeps a second writer out
of a file being measured into; a read-only open deliberately takes no lock,
so a live viewer during acquisition is the case the lock design already
allows.

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
6. **A killed writer keeps its committed photons**: stream events into an
   open photons object, checkpoint, write more, then `SIGKILL` the process.
   The file opens, `PtoRowCount` and the readable events equal the last
   checkpoint exactly, the uncommitted tail is invisible, and a subsequent
   writer reclaims it as `Void` per the spec's abandoned-write rule. A
   reader opening the file *between* checkpoints, while the writer holds
   the lock, sees a consistent shorter file rather than an error.
7. **Docs**: `pto.rst` gains the normative photons-object section — column
   names, dtypes, required tags, and the checkpoint order — and
   `tttr convert` accepts `.pto` as a target.

## Non-goals

- **Retiring the embedded original.** Embedding the vendor file byte for
  byte stays the fidelity anchor; the native table is the *decoded* stream
  living beside it, not a replacement for it.
- **Compression** — the container does not compress; unchanged.
- **A new record format** — no bit-packing; the columns are the format.

---

## Progress

### 2026-08-11 — the reader applies the header (design item 4, partial)

`apply_photon_header` reads the three required tags off the object's UID and
configures the `TTTRHeader`. Closes the gap this PRD opened with: the loader
read the four columns and applied *no* header at all, so a native photons
object came back as dimensionless integers and every derived quantity was
wrong or impossible. `test/python/test_pto_photons_native.py`, 7 cases.

The container-level spec section ("Photon streams, natively") landed with it,
including the vocabulary ruling: the two clock tags are MMFDB dictionary terms
and are spelled the dictionary's way; the bin count has no MMFDB term and goes
in PTO's own namespace rather than borrowing `_mmfdb_setup.n_bins`, which
means *correlator bins for an FCS setup* and would have been a silent semantic
collision.

### 2026-08-11 — the writer (design item 3) — **acceptance criterion 1 met**

`tttr.write("run.pto")` produces a self-contained native container.
`can_write` is true for PTO for the first time.

**Mechanism.** `FileFormat` gained `write_from` / `write_context`, the mirror
of the existing `read_into` hook, and `IORegistry::set_writer` sets it
together with `can_write` so the flag cannot outlive the writer. `TTTR::write`
dispatches through it *before* the record-type validation, deliberately: a
container that stores decoded columns has no record type, and demanding one
would refuse a write that is perfectly well defined.

**Verified.** Round trip PTU → `.pto` → `TTTR` on a real 870 161-event imaging
PTU: all four event arrays bit-identical **and same dtype**, and the three
header quantities preserved. Also SPC-130 (607 866 events) and HT3
(11 605 946) — the sink is not PTU-shaped, since it stores decoded events
rather than any record layout. `test/python/test_pto_write_native.py`, 13
cases.

**A defect found by writing the tests, not by review.** `write("run.pto|green")`
inferred the container from the *whole* string, found no extension on
`.pto|green`, fell back to the **source** container and wrote a PTU into a file
literally named `run.pto|green` — the wrong format under the right name, no
error, `write` returning `True`. The extension is now taken from
`subfile_path()`, and a selector handed to a format with no objects is refused
by name instead of folded into the filename.

**Still open in this PRD**, in the order they block something:

1. **Checkpointing (design item 7, acceptance criterion 6).** The writer is
   write-once: it builds the whole store in memory and commits once. An
   acquisition needs to commit a still-growing object — raise the `FileData`
   size VINT, re-stamp `PtoRowCount`, raise the `Segment` size, rewrite the
   `SeekHead` in its reserve, in that order. **This is what chisurf PRD-98
   requirement 3 actually needs**; the writer alone lets it write a finished
   measurement, not stream into one.
2. **Full header fidelity (design item 2, acceptance criteria 1–2).** Only the
   three *required* tags are written. The PRD asks for every source header tag
   preserved `(name, idx, type, value)` — imaging `ImgHdr_*` included, which
   means a CLSM image does **not** yet reconstruct from a `.pto` — and for
   `tyBinaryBlob` to ride through as a `Bytes` tag instead of being dropped.
3. **Bare open of a multi-object container (design item 4, criterion 3).**
   Several native objects currently *stack* in name order, which is the
   documented behaviour for embedded vendor files. The PRD wants a selector
   demanded and the candidates named. Changing it is a behaviour decision for
   the embedded path too, so it was not taken unilaterally.
4. **Targeted reads over a native table (item 5, criterion 4)**, and
   **conformance cases in four languages (item 6, criterion 5)**.

### 2026-08-11 — full header fidelity — **acceptance criteria 1 and 2 met**

Every row of the source header now rides through, `(name, idx, type, value)`
restored exactly. Measured on a 111-tag imaging PTU: **111 preserved, zero
value or type mismatches**, 13 `ImgHdr_*` among them. All twelve PTU types
round trip, verified per type including `tyBinaryBlob`, `tyFloat8Array`,
`tyEmpty8` and a non-scalar `idx`.

`PtoTag` needed no new fields: `index` and `source_type` were put in the format
for exactly this ("so a PicoQuant header can be written back bit-exact") and
nothing had wired them up. Rows go under `_pto_source_header.`, because writing
`ImgHdr_PixX` bare would claim an authority nobody holds — the rule the
vocabulary section already sets.

**`tyBinaryBlob` is no longer dropped.** The PTU reader read the blob's length,
printed `ERROR: PTU tyBinaryBlob not supported` and seeked past it, so the data
was gone before any container saw it. `add_tag` had handled the type all along.
*Unverified against a real file:* no PTU in the test set carries a blob, so the
pq reader's half is exercised only synthetically.

**A CLSM image reconstructs identically from a `.pto`** — 868 815 counts, pixel
for pixel, geometry equal. That needed a fourth required tag, which the PRD had
already specified and the first pass had not implemented: `source_container_type`
/ `source_record_type`. A marker convention belongs to the source format (PTU
stores marker *indices* decoding as 2^idx, HT3 stores the channel), and a native
table records neither in its columns. Without it the geometry was right, all
1001 markers were present, and the image reconstructed to **zero frames**.

### 2026-08-11 — streaming acquisition (design item 7) — **acceptance criterion 6 met**

`PtoPhotonStream` writes photons as they arrive, for acquisitions larger than
RAM.

**Why it is not one growing object.** A `dstore` payload writes its column
blobs and *then* a directory describing them, so appending rows would overwrite
the next column and move the directory. It cannot grow in place, and rewriting
the payload per checkpoint is O(total) work every second on a file measured for
an hour. A stream therefore writes a sequence of committed chunk objects under
one name (`run/000000`, `run/000001`, …). **No format change**: the reader
already stacks several photons objects into one measurement in name order, and
zero padding makes that order numeric.

Measured, in subprocesses because peak RSS is a high-water mark:

| events | file | peak RSS | chunks |
|---|---|---|---|
| 30 M | 361 MB | 189 MB | 60 |
| 60 M | 723 MB | 195 MB | 120 |
| 120 M | 1450 MB | 197 MB | 240 |

The file grows 4×, the process does not — memory is bounded by the checkpoint
interval, not the run. A 1.45 GB acquisition in a 197 MB process.

Crash and concurrency behaviour come from the format rather than from a
protocol: an uncommitted chunk lies outside the `Segment` and is invisible.
Verified by `SIGKILL` on a live writer — the file opens holding **exactly** the
last checkpoint, monotonic and without gaps — and by a reader opening the file
mid-acquisition through a lock-free read-only open and getting a consistent
shorter measurement.

**A defect this found in the reader.** Stacking shifted each object's macro
times to continue after the previous one, which is right for embedded vendor
files (each restarts its clock at zero) and wrong for a native table, whose
`macro_time` is absolute by specification. Chunked acquisition made it visible:
the event count matched and every photon after the first chunk was in the wrong
place. Shifting is now applied only to embedded objects.

**Generalised past PTO, as asked.** The interface is the abstract
`TTTRStreamWriter` (`modules/io/base`), which owns the error reporting, the
auto-checkpoint policy and the equal-length check so no backend can forget it,
and states the contract every implementation owes — bounded memory, a durable
checkpoint, a no-op checkpoint that succeeds, `close()` checkpointing first.
`FileFormat::make_stream_writer` + `IORegistry::set_stream_writer` register a
factory, kept separate from `can_write` because writable and streamable are
different questions: most vendor formats write a record count in a header
before any record, so they can be written and cannot be streamed into.
`make_stream_writer(filename)` resolves by extension.

**Still open:** PTO is the only backend, so the abstraction is stated but not
yet proven by a second implementation. Targeted reads over a native table
(item 5, criterion 4), cross-language conformance (item 6, criterion 5), and
demanding a selector for a multi-object container (criterion 3) remain.

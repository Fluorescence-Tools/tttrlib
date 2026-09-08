# `io/pto` — PTO, the PhoTon cOntainer, as tttrlib uses it

**The container lives in ptolib.** `thirdparty/ptolib/ptolib.h`
(https://github.com/tpeulen/ptolib, vendored; refresh with
`tools/sync_ptolib.sh`) holds the EBML framing, objects, tags, annotations,
cues, the two-index atomic commit, in-place update, compaction, the writer
lock, and embedding a DataStore as a `dstore` payload. IMP.bff carries the same
header, so a container written by either library is read by the other. The
implementation is compiled once, in `modules/core/src/DataStore.cpp`.

This module adds what needs a photon library behind it.

**Two names, two claims.** `<name>.pto` is *the container*: an EBML document with
`DocType "pto"`, saying nothing about what is in it. `<name>.mmfdb.pto` is a
`.pto` that **also** conforms to the PTO.MFDB profile — the container-level
`_mmfdb_container.profile` tags are present and every kind, encoding, grain and
relation in it is a term from the MMFDB dictionaries. The profile tag goes on the
*stem*, never the suffix (`.mmfdb.pto`, not `.pto.mmfdb`), so the file is still
recognised as a container by everything that dispatches on the extension. The
name is a courtesy; conformance is decided by reading the tags. See ptolib's
`docs/pto.rst` (container) and `doc/formats/pto-mfdb.rst` here (profile).

## Contents

- **`io_pto.h` / `io_pto.cpp`**: re-exports ptolib's types under `tttrlib::io`
  (`PtoObject`, `PtoTag`, `pto_add_store`, `pto_read_store`, `pto_bundle_files`,
  `is_pto_file`, ...) and defines `PtoFile`, which is `pto::File` plus the
  photon-aware members: `build_cues` (indexes a record stream), the inspection
  embeds, `add_sidecar_file`, and the two hooks the container leaves open —
  `classify` (the format sniffers, so `attach` and `pto_bundle_files` recognise
  a photon file by its bytes) and `external_payload_path` (the
  `_mmfdb_artifact.file_path` sidecar convention). Also `pto_read_events`,
  `PtoPhotonStream` (an acquisition streamed into a container), and the
  `IORegistry` registration that makes `TTTR("run.pto")` and
  `tttr.write("run.pto")` work.

The standalone `pto` inspector and the `ptoview` TUI ship with ptolib now
(`pto ls|tree|info|tags|cat|extract|verify|columns|head ...`); `tttr pto` and
`tttr tui` in `modules/cli` remain.

## Features & Conventions

- **Bundling files**: `pto_bundle_files` (`tttrlib.pto_bundle` in Python) puts a
  folder of files into a container, one object each, and `PtoFile::disassemble`
  puts the folder back. What each file *is* comes from `pto_classify_path`
  rather than from the caller: a file some photon format claims by extension is
  offered to the content sniffers and recorded under that format's own name
  (`spc-qc`, not `spc`), a file no photon format claims is never sniffed, and
  anything unrecognised is an `attachment` encoded `raw`. A directory is walked
  recursively with each object named relative to it, and a `.set` is tied to the
  `.spc` beside it with `pto.sidecar_of`. The CLI is `tttr pto pack` /
  `tttr pto add`; see `examples/tttr/plot_pto_bundle_files.py`.
- **Files from other writers**: a container with a single index and every
  object in one `Attachments` element (IMP.bff's `.drot.pto` libraries before
  they were written through ptolib) opens read-only; `compact()` makes an
  editable copy.
- **Relative Sidecar Links**: Support for referencing external TTTR photon streams (`.ptu`, `.spc`, `.sm`) via relative links stored in `_mmfdb_artifact.file_path` without duplicating raw binary data inside the container file.
- **IRF & Parameter Provenance**: Embedding of per-detector scatter-derived IRF decay curves (`irf_curve` / `curve_point`) and complete JSON operation parameters (`_mmfdb_operation.settings_json`).
- **Provenance Reconstruction**: Automated pipeline reconstruction (`reconstruct_analysis_from_pto`) to verify 100% reproducibility of single-molecule burst selection, decay parameters, and companion tables directly from the stored container graph.

## Dependencies

- Depends on `io/base`, `core` (which compiles ptolib), `io/store`.

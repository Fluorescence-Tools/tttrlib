# `io/pto` — PTO Binary Container Format I/O

This module implements reader and writer support for the PhoTon cOntainer (`.pto`) and its profiles.

**Two names, two claims.** `<name>.pto` is *the container*: an EBML document with
`DocType "pto"`, saying nothing about what is in it. `<name>.mmfdb.pto` is a
`.pto` that **also** conforms to the PTO.MFDB profile — the container-level
`_mmfdb_container.profile` tags are present and every kind, encoding, grain and
relation in it is a term from the MMFDB dictionaries. The profile tag goes on the
*stem*, never the suffix (`.mmfdb.pto`, not `.pto.mmfdb`), so the file is still
recognised as a container by everything that dispatches on the extension. The
name is a courtesy; conformance is decided by reading the tags. See
`doc/formats/pto.rst` (container) and `doc/formats/pto-mfdb.rst` (profile).

## Contents

- **`io_pto.h` / `io_pto.cpp`**: Core C++ API (`PtoFile`, `PtoObject`, `PtoTag`, cues, streaming reads/writes, compaction, metadata handling, and `add_sidecar_file` relative sidecar link references for TTTR photon streams).
- **`pto_read.h` / `pto_read.c`**: Pure C reader implementation for low-level or embedded consumption.
- **`pto.c`**: Low-level C utilities for PTO binary container inspection.
- **`ptoview.cpp` / `pto_tui.hpp`**: TUI view and inspection helpers.

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
- **Relative Sidecar Links**: Support for referencing external TTTR photon streams (`.ptu`, `.spc`, `.sm`) via relative links stored in `_mmfdb_artifact.file_path` without duplicating raw binary data inside the container file.
- **IRF & Parameter Provenance**: Embedding of per-detector scatter-derived IRF decay curves (`irf_curve` / `curve_point`) and complete JSON operation parameters (`_mmfdb_operation.settings_json`).
- **Provenance Reconstruction**: Automated pipeline reconstruction (`reconstruct_analysis_from_pto`) to verify 100% reproducibility of single-molecule burst selection, decay parameters, and companion tables directly from the stored container graph.

## Dependencies

- Depends on `io/base`, `util`, `io/store`.

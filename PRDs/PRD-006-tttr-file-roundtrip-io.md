# PRD-006 — TTTR file round-trip I/O for all supported containers

> **PRD #:** 006 · **Status:** In Progress · **Created:** 2026-07-03 · **Owner:** tpeulen

## Summary

Make every TTTR container that tttrlib can **read** also **writable**, so that
`TTTR::write()` supports full read → write → read round-trips for all 8
container types, with clearly documented lossiness per trip. Add a round-trip /
transcode test matrix, clean up the grown read/write code structure in
`TTTR.cpp`, and document the file types plus a conversion-support table in the
Sphinx docs.

## Problem / motivation

tttrlib reads 8 container types but writes only 2½:

| # | Container | Read | Write (today) |
|---|-----------|------|----------------|
| 0 | `PTU` (PicoQuant unified) | ✓ (PHT2/3, HHT2v1/2, HHT3v1/2, Generic T2/T3) | header ✓, events only HHT3v2 / Generic-T3 |
| 1 | `HT3` (HydraHarp legacy) | ✓ | **header writer is commented out** → `write()` silently emits a headerless file |
| 2 | `SPC-130` (Becker & Hickl) | ✓ | ✓ |
| 3 | `SPC-600_256` | ✓ | ✗ |
| 4 | `SPC-600_4096` | ✓ | ✗ |
| 5 | `PHOTON-HDF5` | ✓ | ✗ |
| 6 | `CZ-RAW` (Zeiss ConfoCor3) | ✓ | ✗ |
| 7 | `SM` (MFD single-molecule) | ✓ | ✗ |

Consequences:

- No way to convert measurements into an open format (Photon-HDF5) or between
  vendor formats except SPC→PTU-T3.
- T2 data (`pq_ptu_hh_t2.ptu`) cannot be re-saved at all.
- `TTTR::write()` fails **silently or partially** for unsupported combinations
  (prints to `stderr`, still returns `true` in the headerless-HT3 case).
- The reading code in `TTTR.cpp::read_file()` has grown into a long
  if/else with per-format fix-ups inlined (CZ channel back-fill, BH `.set`
  sidecar parsing, SM/HDF special paths), making it hard to extend safely.

## Goals

1. **Write support for every readable container** (see matrix below), so that
   `read(A) → write(A) → read(A)` preserves everything the record format can
   represent.
2. **Transcode support**: `read(A) → write(B)` for any writable B, with
   documented, tested lossiness (bit-depth clipping, dropped micro times,
   dropped markers).
3. **Honest error reporting**: `TTTR::write()` returns `false` (and does not
   leave a half-written file behind) for unsupported combinations.
4. **Internal clean-up** of the read/write paths: one clearly named helper per
   container, uniform dispatch, no behaviour change for existing reads.
5. **Test matrix** covering same-format round-trips and lossy transcodes.
6. **Docs**: a "File formats" page explaining each container + a
   read/write/conversion support table.

## Non-goals

- No public API removals or signature changes (hard constraint; new methods
  with defaults are fine).
- No new third-party dependencies (std-only; HDF5/HighFive are already deps of
  the optional `BUILD_PHOTON_HDF` path).
- Writing PTU **imaging** header reconstruction beyond preserving tags that
  were read (CLSM marker semantics are untouched).
- Bit-exact file duplication. Round-trip fidelity is defined on the **decoded
  event stream** (macro times, micro times, routing channels, event types),
  not on the raw byte stream (overflow-record packing may differ).

## Target support matrix

Event-stream fields per writable (container, record) pair. "n-bit" = value is
preserved iff it fits; larger values are a documented lossy clip.

| Container | Record type | Macro time | Micro time | Channel | Markers/event type |
|---|---|---|---|---|---|
| PTU / HT3 | HHT3v2, Generic T3 | ✓ (overflow records) | 15 bit | 6 bit | ✓ (`special` bit) |
| PTU | HHT2v2, Generic T2 | ✓ (overflow records) | **dropped** (T2 has none) | 6 bit | ✓ |
| PTU | PHT3 | ✓ (16-bit nsync + overflows) | 12 bit | 4 bit (1–14) | partial (marker → dtime=0 convention) |
| PTU | PHT2 | ✓ | **dropped** | 4 bit | partial |
| SPC-130 | SPC130 | ✓ (12 bit + overflow records) | 12 bit (ADC, stored inverted) | 4 bit | **dropped** (written as photons) |
| SPC-600_256 | SPC600_256 | ✓ (17 bit + mtov) | 8 bit (inverted ADC) | 3 bit | dropped |
| SPC-600_4096 | SPC600_4096 | ✓ (24 bit + mtov) | 12 bit (inverted ADC) | 8 bit (inverted) | dropped |
| PHOTON-HDF5 | — | ✓ 64 bit | ✓ 16 bit | ✓ 8 bit | **dropped** (photon-only format) |
| CZ-RAW | CONFOCOR3 | ✓ (32-bit deltas) | **dropped** | **single channel** (header field) | dropped |
| SM | SM | ✓ 64 bit (big-endian) | **dropped** | ✓ 32 bit | dropped |

Same-format round-trips of *real files* are exact on all fields the source
file itself populates (e.g. an SPC-130 file's micro times are 12-bit by
construction, so SPC→SPC is lossless).

## Proposed approach

### 1. Internal restructuring (no behaviour change)

- `TTTR::read_file()` becomes a thin dispatcher; per-container logic moves to
  private helpers (`read_records_file()` for all record-stream formats,
  existing `read_hdf_file()` / `read_sm_file()` kept), with the CZ channel
  back-fill and BH `.set` sidecar handling extracted into named private
  helpers instead of inline blocks.
- Writing mirrors this: `TTTR::write()` validates the (container, record)
  pair, dispatches to one `write_<format>_events()` per record type and one
  `TTTRHeader::write_<container>_header()` per container.
- Record encoders live next to their decoders' semantics: each
  `write_*_events()` is the exact inverse of the corresponding
  `RecordProcessor<T>` in `include/TTTRRecordReader.h`.

### 2. New writers

- **Events**: `write_hht2v2_events`, `write_pht3_events`, `write_pht2_events`,
  `write_spc600_256_events`, `write_spc600_4096_events`,
  `write_cz_events` (32-bit macro-time deltas), `write_sm_events`
  (big-endian 8-byte time + 4-byte channel, 26-byte trailer).
- **Headers**: implement `TTTRHeader::write_ht3_header` (fill
  `pq_ht3_Header_t` + channel headers + TT-mode header from tags, defaults for
  transcodes), `write_sm_header` (re-serialize the big-endian tagged header),
  `write_cz_confocor3_header` (128-byte `cz_confocor3_settings_t`).
- **Photon-HDF5**: `TTTR::write_hdf_file()` writing
  `/photon_data/{timestamps,detectors,nanotimes}` +
  `timestamps_specs/timestamps_unit`, `nanotimes_specs/{tcspc_unit,
  tcspc_num_bins}`; `#ifdef BUILD_PHOTON_HDF`.
- `valid_container_record_pair()` extended for CZ, SM, PHOTON-HDF5.
- When a header lacks a record type for the target container (transcode),
  `write()` picks the container's canonical record type (PTU/HT3 → HHT3v2,
  SPC-* → their single type, CZ/SM → theirs) instead of erroring.

### 3. Tests (`test/python/tttr/test_TTTR_roundtrip.py`)

- **Same-format round-trips** on reference data: SPC-130, SPC-600_256, PTU
  HH-T3, PTU HH-T2, HT3, Photon-HDF5, SM, CZ-RAW → write to temp file, re-read,
  `assert_array_equal` on macro/micro/channel (and event types where carried).
- **SPC-600_4096**: no reference file exists → synthetic TTTR built from
  arrays, write → read → compare (validates encoder/decoder inversivity).
- **Lossy transcodes** asserting exactly what survives: SPC-130→PTU-T3
  (lossless upgrade), PTU-T3→SPC-130 (12-bit micro clip), T3→T2 (micro
  dropped), any→SM / any→CZ (macro-only), any→Photon-HDF5→back (lossless).
- **Failure paths**: invalid pair returns `False`, no file left behind.

### 4. Docs

`doc/file_formats.rst` (wired into the user-guide toctree): one section per
container explaining origin, structure (header + record layout, bit widths),
typical instruments; a support matrix (read/write/lossiness) and a conversion
guide with Python examples (`data.write("out.ptu")` after adjusting
`header.tttr_container_type` / `tttr_record_type`).

## Acceptance criteria

- [x] All 8 containers write; `write()` returns `false` on unsupported pairs
      without leaving partial output.
- [x] Round-trip tests green for all containers with reference data; synthetic
      round-trip for SPC-600_4096.
- [x] Full pytest suite: no regressions vs baseline (681 passed; 1 fail + 25
      errors from missing aberior/bh data are pre-existing).
- [x] Read path restructured; existing reads validated by the untouched
      pre-existing read tests (one intentional decode fix, below).
- [x] Docs page with file-type explanations + conversion/support/metadata
      table builds (`doc/file-formats.rst`), plus a tested gallery conversion
      script (`examples/tttr/plot_tttr_file_conversion.py`).
- [x] No public API signature changes (SWIG interfaces only gain methods).

## Implementation notes (2026-07-03)

- **Event writers added** (each the inverse of its `RecordProcessor<>`):
  HHT3v1, HHT2v1, HHT2v2/Generic-T2, PHT3, PHT2, SPC600-256, SPC600-4096,
  CZ-RAW (32-bit deltas), SM (big-endian + 26-byte trailer). The SPC-130
  writer now also writes marker records and zero-initializes record bits.
- **Header writers**: `write_ht3_header` implemented (was fully commented
  out), `write_sm_header` + `write_cz_confocor3_header` added; SPC-600
  containers truncate (headerless). The HT3 writer derives `SyncRate` from
  the global resolution on transcodes so macro calibration survives.
- **Photon-HDF5 writer** (`TTTR::write_hdf_file`): spec-conformant v0.5
  layout per the phconvert reference (root attrs, `/description`,
  `/acquisition_duration`, `/setup` with mandatory fields — preserved from a
  Photon-HDF5 source header where present — and `/identity`).
- **Decode fix (SPC-600_256)**: the reader used inconsistent overflow
  multipliers (4096 for counted overflows, 65536 for the per-record mtov
  bit) for a 17-bit macro time field. Real data (mt values up to ~131071
  right before overflows) proves the wrap is 2^17 = 131072. Both paths now
  use 131072; decoded macro times are monotonic and `bh_spc630_256.spc`
  round-trips exactly. The pinned reference (`test/data/reference/
  bh_spc630_256.npz`) was regenerated. (phconvert hard-codes shift 12 in its
  shared helper — that disagrees with its own 17-bit field spec.)
- **Decode fix (SPC-600_4096)**: records are 6 bytes, but the batch reader
  loaded only 32 bits, leaving the macro time bytes `mt1/mt2` undefined.
  The processor now decodes from the raw record bytes (`process_bytes`).
- **Writer robustness**: all writers guard the overflow computation against
  non-monotonic macro times (previously a uint64 underflow could emit
  billions of overflow records — observed as a 33 GB file).
- **Read path restructured**: `read_file()` is a thin dispatcher
  (HDF/SM/record-stream); BH `.set` sidecar and CZ channel back-fill are
  named helpers; the duplicated 12-way record-type switch in
  `read_records()` collapsed into `dispatch_process_records_batch()`.
- v1 HydraHarp records (HHT3v1/HHT2v1) are written natively — v1 HT3/PTU
  files round-trip without a forced record upgrade.
- **SF-compressed HT3** (Suren Felekyan's HT3 conversion,
  `PQ_RECORD_TYPE_SF_HT3` = 14): read + write. SF files carry a plain
  HydraHarp v1 header; the overflow record's lowest 24 bits hold the number
  of additional overflows (`+ (1+count) × 1024`). Auto-detected on HT3 read
  by scanning the first 64k records (256 KB) for non-zero overflow payloads
  (observed within the first 3 records of real SF files) —
  spec-conforming v1 overflow records have all-zero payloads, and when all
  payloads are zero both decodes are identical, so detection is safe (
  validated: instrument-written v1 files `pq_ht3_clsm.ht3` / `mGBP_IRF.ht3`
  have 100% zero payloads with long overflow runs; SF files have
  ~85% non-zero payloads and zero runs). Both `pq/ht3` test files
  (`pq_ht3_sf-compression.ht3` **and** `pq_ht3v1.0_hh_t3.ht3`) turned out to
  be SF-compressed — previously they were silently mis-decoded as plain v1
  (total measurement time ~18× too short). The decoder is validated against
  an independent numpy reference decode in the tests. Writing SF from a
  plain v1 source shrinks the file (overflow runs collapse) with identical
  decoded arrays.
- Tests: `test/python/tttr/test_TTTR_roundtrip.py` (21 tests: 11 same-format
  round-trips incl. synthetic SPC600-4096, 10 transcode/metadata/failure
  cases). New settings keys: `cz_raw_filename`, `ht3_v1_filename`,
  `ht3_sf_filename`; `sm/data.sm` + `cz/fcs/*.raw` downloaded into the data
  set (hashes verified against `settings.json`).

## Risks / notes

- `SPC-600_4096` is documented as a 6-byte record but the union/processor uses
  32 bits; the writer must invert **the reader as implemented** so synthetic
  round-trips hold. If real 4096-mode data surfaces later, both sides get
  fixed together.
- HT3 header: reader warns for `FormatVersion != 1.0`; writer emits the
  version tags read from the source, defaults `HydraHarp` / `2.0` (HHT3v2) for
  transcodes.
- PTU `tyWideString`/`tyBinaryBlob` tags are still not written (pre-existing
  limitation); tags of these types read from a source PTU are skipped with a
  warning, which keeps SymPhoTime-generated files transcodable.
- Writing appends via `"ab"` after header write today; the new dispatch keeps
  that but truncates first on failure-free open (`"wb"` for header + append
  events) so re-writing an existing path does not concatenate.

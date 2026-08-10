# PRD-028 — A data standard for decay curves, FCS, PDA, and PCH in `.dstore` / `.pto`-mmfdb

> **PRD #:** 028 · **Status:** ⚪ Draft · **Created:** 2026-08-08 · **Owner:** tpeulen
>
> **Sibling:** `chisurf/okf/prds/prd-xx-data-standard.md` — the consumer-side
> PRD that reads these tables and feeds them to the fit/analysis GUIs.

## Summary

tttrlib's `.dstore` format and the `.pto`-mmfdb container carry **burst tables**
richly: every column has an mmfdb/flrCIF item identifier, units, and a
described grain. But the other spectroscopic outputs — TCSPC decay curves
(VV/VH/VM), FCS correlation curves, PDA histograms, photon-counting histograms
(PCH) — have **no standard representation** at all. They leave the library as
raw `double*` arrays through C-style pointers, and every consumer (ChiSurf,
ndx, the web UI) re-invents how to name, store, and read them.

Define a **data standard** for each of these curve types as a `.dstore` table
schema with mmfdb-compatible column names, metadata, and grain — the same
machinery that burst tables already use, extended with new flrCIF categories.
The result: a decay curve, an FCS curve, a PDA histogram written by tttrlib is
self-describing and round-trips through `.dstore`, HDF5, and `.pto` exactly,
identically to a burst table.

## Problem / motivation

### What the library produces today

| Output | How it leaves the library | Stored in `.dstore`? | mmfdb item names? |
|---|---|---|---|
| Burst table | `DataStore` with named columns | yes | yes |
| Decay curve (VV/VH) | `DecayFitProblem::data` — flat `double[]`, channel-major | no | no |
| FCS curve | `CorrelatorCurve` — `x_axis[]`, `correlation[]`, `corr_normalized[]` | no | no |
| PDA histogram | `Pda::get_S1S2_matrix()` — flat `double[]`, `(Nmax+1)²` | no | no |
| PCH | `Pda::get_1dhistogram()` — `double[]` via callback | no | no |

Every consumer that wants to persist a decay curve or an FCS curve today
either:

- keeps a sidecar file in a private format (ChiSurf's `.h5` data-frame layout),
- or holds the arrays in memory and loses them on exit.

Neither is self-describing. A reader that opens the file cannot tell what the
columns mean without out-of-band knowledge.

### What is missing from the flrCIF dictionaries

The mmfdb dictionary (`okf/nomenclature/mmfdb.dic`) defines three categories:
`mmfdb_burst_column`, `mmfdb_constant`, `mmfdb_derived_column` — all
burst-grain. There are no categories for:

- **Decay curve columns** — channel index, parallel intensity, perpendicular
  intensity, magic-angle intensity, IRF, model curve, residuals.
- **FCS curve columns** — correlation time, correlation amplitude, normalised
  amplitude, cross-correlation pairs.
- **PDA histogram columns** — S1 count, S2 count, probability density, 1D
  projection bin, species index.

Without these categories, there is no canonical name for any column in any of
these tables. The data standard cannot land until the vocabulary exists.

## Goals

- Define a **`.dstore` table schema** for each curve type: column names,
  dtypes, units, grain, and the mmfdb item each column maps to.
- Each schema **round-trips** through `.dstore`, HDF5, and `.pto` using the
  existing `read_table` / `write_table` / `table_columns` vocabulary
  (PRD-023). No new I/O code — just described columns.
- Each schema is **self-describing**: a reader that knows nothing about the
  producer can open the table, read the column metadata, and understand what
  it holds.
- New **flrCIF categories** are defined for the column items, in the mmfdb
  repository. tttrlib's `mmfdb.dic` is the reference; the canonical source is
  the mmfdb project.
- The schemas are **versionable**: each carries a `schema_version` in the
  store's group metadata, and the reader is tolerant of appended columns.
- ChiSurf and ndx consume these tables natively (sibling PRD).

## Non-goals

- **A new file format.** These are `.dstore` tables with described columns,
  written through the existing I/O. No new container.
- **Standardising the algorithm internals.** How a decay is computed or an FCS
  curve is correlated is the algorithm's business. This PRD standardises the
  *output table*, not the computation.
- **Replacing `DecayFitProblem` in memory.** The C++ class keeps its flat
  arrays for performance. The standard is a serialisation target — a function
  that copies the problem's data into a `DataStore` and back.
- **Photon-HDF5 compliance.** Photon-HDF5 is a community interchange format
  with its own schema. These tables are the tttrlib/mmfdb-native representation;
  a Photon-HDF5 exporter is a separate converter.

## Part 1 — TCSPC decay curves

### What a decay curve table holds

A TCSPC decay measurement is a set of histograms over micro-time bins, one per
detection channel. The standard two-channel polarisation-resolved measurement
has parallel (VV) and perpendicular (VH); a magic-angle (VM) measurement has
one channel. An IRF and a model curve may accompany the data.

### Table schema: `decay_curve`

| Column | dtype | units | mmfdb item | description |
|---|---|---|---|---|
| `micro_time` | f64 | ns | `_mmfdb_decay_curve.micro_time` | Bin centre time |
| `vv_counts` | f64 | counts | `_mmfdb_decay_curve.vv_counts` | Parallel-channel intensity |
| `vh_counts` | f64 | counts | `_mmfdb_decay_curve.vh_counts` | Perpendicular-channel intensity |
| `vm_counts` | f64 | counts | `_mmfdb_decay_curve.vm_counts` | Magic-angle intensity (optional) |
| `irf` | f64 | counts | `_mmfdb_decay_curve.irf` | Instrument response function |
| `model` | f64 | counts | `_mmfdb_decay_curve.model` | Fitted model curve (optional) |
| `residuals` | f64 | dimensionless | `_mmfdb_decay_curve.residuals` | Weighted residuals (optional) |
| `background` | f64 | counts | `_mmfdb_decay_curve.background` | Background pattern (optional) |

**Group metadata** (stored as a one-row companion group `meta` or as the
store's label):

| Key | Type | Description |
|---|---|---|
| `schema_version` | str | `"1"` |
| `n_bins` | int | Number of micro-time bins |
| `n_channels` | int | 1 (VM), 2 (VV/VH) |
| `dt` | float | Bin width in ns |
| `polarization` | str | `"vv_vh"`, `"vm"`, `"none"` |
| `excitation_period` | float | ns |
| `g_factor` | float | Detection g-factor |
| `fit_start` | int | First bin in fit range |
| `fit_stop` | int | Last bin in fit range (exclusive) |
| `acquisition_time` | float | Total acquisition time, s |

**Grain:** `curve_point` — one row per micro-time bin.

**Layout decision: wide, not long.** The table is one row per bin, with
channels as columns — not one row per (bin, channel) pair. This matches
`DecayFitProblem`'s channel-major layout, keeps the table narrow (at most 8
columns), and a consumer reads one channel with a single column-subset read.

If more than three channels are needed (e.g. a 4-detector setup), the schema
extends with `ch3_counts`, `ch4_counts`, etc. — appended columns, not a
schema break.

### API

```cpp
// Write a DecayFitProblem's data into a DataStore
DataStore decay_to_store(const DecayFitProblem& problem,
                         const DecayFitCorrections& corrections);

// Read a DataStore back into a DecayFitProblem
void decay_from_store(const DataStore& store, DecayFitProblem& problem);
```

From Python:

```python
store = tttrlib.decay_to_store(problem)
tttrlib.write_table("measurement.pto|decay_vv_vh", store)
```

## Part 2 — FCS correlation curves

### What an FCS curve table holds

An FCS correlation curve is a set of (correlation time, amplitude) pairs on a
multi-tau log-linear grid. The curve may be an autocorrelation or a
cross-correlation; a measurement may produce several curves (different channel
pairs, different time windows).

### Table schema: `fcs_curve`

| Column | dtype | units | mmfdb item | description |
|---|---|---|---|---|
| `correlation_time` | f64 | s | `_mmfdb_fcs_curve.correlation_time` | Lag time (centre of the bin) |
| `correlation` | f64 | dimensionless | `_mmfdb_fcs_curve.correlation` | Normalised correlation amplitude |
| `correlation_raw` | f64 | counts² | `_mmfdb_fcs_curve.correlation_raw` | Unnormalised amplitude (optional) |
| `stderr` | f64 | dimensionless | `_mmfdb_fcs_curve.stderr` | Standard error per point (optional) |

**Group metadata:**

| Key | Type | Description |
|---|---|---|
| `schema_version` | str | `"1"` |
| `n_casc` | int | Number of cascades |
| `n_bins` | int | Bins per cascade |
| `macro_time_resolution` | float | s per macro-time tick |
| `correlation_type` | str | `"autocorrelation"`, `"cross_correlation"` |
| `channel_pair` | str | e.g. `"0:1"` — which detector channels |
| `acquisition_time` | float | s |

**Grain:** `curve_point`.

**Multi-curve:** a measurement with several curves (e.g. green-green and
green-red cross-correlation) writes **one group per curve** under a parent
group — the same tree structure PRD-019 already supports:

```
fcs/
  green_green/     ← fcs_curve table
  green_red/       ← fcs_curve table
```

### API

```cpp
DataStore fcs_to_store(const CorrelatorCurve& curve,
                       const CorrelationCurveSettings& settings);
void fcs_from_store(const DataStore& store, CorrelatorCurve& curve);
```

## Part 3 — PDA histograms

### What a PDA table holds

A PDA (Photon Distribution Analysis) histogram is a 2D joint probability
distribution of photon counts in two detection channels (S1, S2). A 1D
projection onto a derived axis (FRET efficiency, stoichiometry) may accompany
it.

### Table schema: `pda_histogram` (2D)

| Column | dtype | units | mmfdb item | description |
|---|---|---|---|---|
| `s1_count` | i32 | counts | `_mmfdb_pda_histogram.s1_count` | Photon count in channel 1 (green) |
| `s2_count` | i32 | counts | `_mmfdb_pda_histogram.s2_count` | Photon count in channel 2 (red) |
| `probability` | f64 | dimensionless | `_mmfdb_pda_histogram.probability` | P(S1, S2) — model or observed |

**Group metadata:**

| Key | Type | Description |
|---|---|---|
| `schema_version` | str | `"1"` |
| `n_max` | int | Maximum photon count dimension |
| `n_min` | int | Minimum photon count threshold |
| `bg_ch1` | float | Background rate, channel 1, kHz |
| `bg_ch2` | float | Background rate, channel 2, kHz |
| `histogram_type` | str | `"model"`, `"experimental"`, `"residual"` |

**Grain:** `histogram_cell` — one row per (S1, S2) pair.

**Layout decision: long format.** The 2D histogram is stored as `(Nmax+1)²`
rows of `(s1, s2, probability)` — a sparse-friendly long table, not a dense
matrix. This lets a reader subset on `s1 >= threshold` without reading the
full matrix, and keeps the column count constant regardless of matrix size.

### Table schema: `pda_projection` (1D)

| Column | dtype | units | mmfdb item | description |
|---|---|---|---|---|
| `bin_centre` | f64 | dimensionless | `_mmfdb_pda_projection.bin_centre` | Projection axis value (E, S, etc.) |
| `probability` | f64 | dimensionless | `_mmfdb_pda_projection.probability` | P(axis) |
| `axis` | str | | `_mmfdb_pda_projection.axis` | Which axis: `"fret_efficiency"`, `"stoichiometry"` |

**Grain:** `histogram_bin`.

## Part 4 — PCH (photon-counting histograms)

### What a PCH table holds

A photon-counting histogram is the distribution of photon counts per sampling
window (bin) in one channel — the signal that FCS complements with its
time-domain view.

### Table schema: `pch`

| Column | dtype | units | mmfdb item | description |
|---|---|---|---|---|
| `photon_count` | i32 | counts | `_mmfdb_pch.photon_count` | Number of photons in a sampling window |
| `frequency` | f64 | dimensionless | `_mmfdb_pch.frequency` | Number of windows with this count |
| `model_probability` | f64 | dimensionless | `_mmfdb_pch.model_probability` | P(n) from a PCH model (optional) |

**Group metadata:**

| Key | Type | Description |
|---|---|---|
| `schema_version` | str | `"1"` |
| `sampling_window` | float | Sampling window size, s |
| `channel` | int | Detection channel |
| `acquisition_time` | float | s |

**Grain:** `histogram_bin`.

## Part 5 — flrCIF categories to define

These categories are defined in the mmfdb repository (`mmfdb_flr_ext.dic` or a
new `mmfdb_spectroscopy.dic`), not in tttrlib — but this PRD blocks on their
existence.

| Category | Items | Owner |
|---|---|---|
| `mmfdb_decay_curve` | `micro_time`, `vv_counts`, `vh_counts`, `vm_counts`, `irf`, `model`, `residuals`, `background` | mmfdb |
| `mmfdb_fcs_curve` | `correlation_time`, `correlation`, `correlation_raw`, `stderr` | mmfdb |
| `mmfdb_pda_histogram` | `s1_count`, `s2_count`, `probability` | mmfdb |
| `mmfdb_pda_projection` | `bin_centre`, `probability`, `axis` | mmfdb |
| `mmfdb_pch` | `photon_count`, `frequency`, `model_probability` | mmfdb |
| `mmfdb_artifact` (extension) | new `data_format` values: `dcy`, `fcs`, `pda`, `pch` | mmfdb |

The `data_format` extensions give the `.pto`-mfdb provenance system a short
identifier for each curve type, just as `bur`, `bg4`, `irf` identify burst
tables today.

## Part 6 — provenance integration

Each curve type is an **artifact** in the `.pto`-mfdb container. Its provenance
tags follow the existing contract:

| Tag | Value for a decay curve |
|---|---|
| `_mmfdb_artifact.artifact_kind` | `table` |
| `_mmfdb_artifact.data_format` | `dcy` |
| `_mmfdb_artifact.row_grain` | `curve_point` |
| `_mmfdb_operation.operation_type` | e.g. `tcspc_decay_acquisition` |
| `_mmfdb_edge.relationship_type` | `derived_from` (parent: photon stream) |

When the algorithm registry (PRD-027) is in place, the `operation_type` and
settings schema come from the live registry — a decay fit's output table is
provenance-tracked automatically.

## Part 7 — round-trip guarantee

The same invariant PRD-023 Part 3 states for burst tables holds here:

> A table written to `.dstore` and read back is identical to the original,
> column for column, dtype for dtype, metadata for metadata.

Plus:

- A curve written to `.pto` as a `dcy` artifact reads back through
  `read_table("file.pto|decay_vv_vh")` identically.
- A curve written to HDF5 reads back identically, with the same exception
  set as PRD-023 (bool → uint8).
- Column metadata (mmfdb item, units) survives all three formats.

## Criteria

1. `decay_to_store` / `decay_from_store` exist and round-trip a
   `DecayFitProblem` with VV/VH channels, IRF, and model without loss.

2. `fcs_to_store` / `fcs_from_store` exist and round-trip a `CorrelatorCurve`
   with its settings.

3. A PDA 2D histogram can be written as a `.dstore` table with the
   `pda_histogram` schema and read back into the same `(S1, S2, P)` arrays.

4. A PCH can be written and read back with the `pch` schema.

5. Every column in every schema carries an mmfdb item identifier in its
   column metadata, matching the flrCIF category definitions.

6. The flrCIF categories (`mmfdb_decay_curve`, `mmfdb_fcs_curve`,
   `mmfdb_pda_histogram`, `mmfdb_pda_projection`, `mmfdb_pch`) exist in the
   mmfdb dictionary and every item name in this PRD matches.

7. A decay curve written to `.pto` as a `dcy` artifact is readable by
   `read_table("file.pto|group_name")` and the provenance tags
   (`data_format`, `row_grain`, `operation_type`) are set.

8. The round-trip invariant holds across `.dstore`, HDF5, and `.pto` for all
   four curve types.

9. The schema version is stored in group metadata; a reader that encounters
   an unknown version reads the columns it knows and ignores the rest.

10. ChiSurf can open a `.pto` containing a `decay_curve` group and display the
    decay without any tttrlib-specific glue code — only the standard
    `read_table` vocabulary and the mmfdb item names. (Verified by the sibling
    chisurf PRD.)

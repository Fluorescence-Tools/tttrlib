# `core` — Core TTTR Data Structures and Operations

This module provides the central `TTTR` photon container class, channel routing, microtime calibration, and event filters.

## Contents

The photon-stream data model, the histograms every analysis bins with, the
columnar `DataStore`, and **the registry**.

- **`TTTR.h` / `TTTR.cpp`** — the photon container: macro times, micro times,
  routing channels, event types; reading and writing every container through
  the `io_*` modules; selections, ranges and the burst-search entry points
  (defined in [`spectroscopy/burst`](../spectroscopy/burst)).
- **`TTTRHeader.h` / `TTTRHeader.cpp`** — the header: tags, resolutions,
  per-vendor setup metadata, JSON round trip.
- **`TTTRRecordReader.h`** — the per-record-type decoders (header-only,
  compile-time stride), shared by the format modules.
- **`TTTRStream.h` / `TTTRStream.cpp`** — decoding a buffer with a carried
  state, for streaming and partial reads.
- **`RecordStreamWriter.h` / `RecordStreamWriter.cpp`** — writing records back
  out in a container's own encoding.
- **`TTTRMask.h` / `TTTRMask.cpp`** — a bit mask over event indices.
- **`TTTRSelection.h` / `TTTRSelection.cpp`** — index selections by channel,
  micro-time gate, event type and time window.
- **`TTTRRange.h` / `TTTRRange.cpp`** — the (start, stop) form a burst table
  is written in.
- **`Channel.h` / `Channel.cpp`** — a *named* detector: routing channel plus
  micro-time gate, which is what CLSM and burst analyses take.
- **`Histogram.h` / `Histogram.cpp`, `HistogramAxis.h` / `HistogramAxis.cpp`,
  `HistogramNd.h`** — 1-D, N-D, weighted, linear and log axes, threaded fill.
- **`DataStore.h` / `DataStore.cpp`** — the columnar table: dtype-preserving
  columns, dictionary-encoded strings, bit-packed masks, zero-copy NumPy/R
  views. The files that *save* one are [`io/store`](../io/store),
  [`io/hdf5`](../io/hdf5), [`io/csv`](../io/csv) and [`io/pto`](../io/pto).
- **`MicrotimeLinearization.h` / `MicrotimeLinearization.cpp`** — per-channel
  DNL correction (look-up table) and micro-time shifts.
- **`FileCheck.h` / `FileCheck.cpp`** — what a file is: the content sniffers
  and `inferTTTRFileType`.
- **`Registry.h` / `Registry.cpp`** — **the registry**. One table
  (`register_algorithm` / `register_algorithm_json`) that every algorithm, fit
  model, objective, prior, correlation method and pipeline operation registers
  itself in, next to its own code, when its library loads; `registry_json()`
  assembles the categories from what registered, plus the file-format,
  table-format and plugin catalogs. See [`modules/README.md`](../README.md).

## Dependencies

- Depends on `util`, `io/base`, `plugin` and the format modules
  (`io_pq`, `io_bh`, `io_cz`, `io_sm`, `io_ps`, `io_be`, `io_fl`, `io_hdf5`),
  nlohmann/json.

## Examples

- `examples/tttr/plot_write_read_roundtrip_simulated.py` (+ `.ipynb`): a simulated photon stream written as PTU / HT3 / SPC-130 with `TTTR.write` and read back record-for-record — no instrument file needed. The decoders are photon-for-photon identical to phconvert and ptufile (`benchmarks/check_reading.py`).

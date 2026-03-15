.. _beginner_examples:

# Beginner Examples for tttrlib

This directory contains small, runnable examples that mirror the tested core features of `tttrlib`.
They are safe to copy-paste into your own analysis.

Make sure the demo data bundle is available (see `test/test_settings.py`) and set `TTTRLIB_DATA` accordingly.

## Examples

* `plot_01_read_tttr.py`
  Load a TTTR file (PTU), print basic info (macro/micro times, routing channels), and make first diagnostic plots.

* `plot_02_slice_join_append.py`
  Take small slices, join/append photon streams, compare macro-time timelines (shifted vs unshifted), and get a micro-time histogram.

* `plot_03_selections_bursts_traces.py`
  Do count-rate based LOW/HIGH selections, run burst search, build per-burst micro-time histograms (log scale), and plot 1 ms intensity traces (first 3 s) with burst regions shaded.

* `plot_04_write_convert.py`
  Write subsets to a new file, make a full native copy, and convert between vendor formats by reusing a header template.

* `plot_05_clsm_zeiss980.py`
  Reconstruct a confocal (CLSM) image from Zeiss LSM 980 TTTR data, show an intensity projection, and plot a per-pixel mean micro-time (pseudo-lifetime) map.

## How to run

From this directory:

```
python plot_01_read_tttr.py
```

Each script will either run and generate figures/output files, or exit with a clear message if the demo data cannot be found.

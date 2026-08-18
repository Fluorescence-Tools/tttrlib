# `cli` — Command-Line Interface and TUI Tool

`tttr` command-line executable tool and terminal user interface (TUI) for inspecting TTTR photon files and PTO containers.

## Contents

- **`main.cpp`, `src/cli_main.cpp`, `include/tttr_cli.h`**: entry point,
  subcommand table and shared option parsing.
- **`src/cmd_common.cpp`**: helpers every subcommand shares (input resolution,
  including `-` for stdin, and container/format naming).
- **`include/cli_progress.h` / `src/cli_progress.cpp`**: the JSONL progress
  events `--progress` emits for a calling client.
- **`include/detector_setup.h` / `src/detector_setup.cpp`**: reading ChiSurf's
  `detector_setups.json` — named detectors, their channels and micro-time
  gates, which is where `tttr sm`'s column names come from.
- **`src/cmd_convert.cpp`**: `tttr convert` — rewrite a file into another
  container.
- **`src/cmd_correlate.cpp`**: `tttr correlate` — FCS, four columns out.
- **`src/cmd_formats.cpp`**: `tttr formats` — what can be read and written.
- **`src/cmd_detectors.cpp`**: `tttr detectors` — what a setup file declares.
- **`src/cmd_image.cpp`**: `tttr image` — CLSM reconstruction and export.
- **`src/cmd_sim.cpp`**: `tttr sim` Monte Carlo TTTR photon stream simulation subcommand.
- **`src/cmd_sm.cpp`**: `tttr sm` single-molecule burst search, writing the burst
  table as JSON, as a delimited file (`--csv`), or into a container
  (`--output out.mmfdb.pto`). The container is a plain `.pto` carrying the
  `PTO.MFDB` profile — hence the `.mmfdb` on the stem; the suffix stays `.pto`
  so it is still a container to everything that dispatches on one.
  The columns are **generated from the `--setup` detector file**, not hardcoded:
  the aggregate set (`"First Photon"`, `"Last Photon"`, `"Duration (ms)"`,
  `"Mean Macro Time (ms)"`, `"Number of Photons"`, `"Count Rate (KHz)"`,
  `"Confidence (sigma)"`, `"First File"`, `"Last File"`) plus six columns and a
  mean micro time per named detector, plus a rate per PIE window per detector.
  The names, sentinels and arithmetic are ChiSurf's `generate_burst_dataframe`,
  verified against it cell for cell. BVA and FRET-2CDE companion tables are
  computed from the `--donor`/`--acceptor` streams and written only when those
  streams can be named.

  `--mle` adds one fitted lifetime per burst per detector (`fit23`, ChiSurf's
  `.bg4`/`.br4` column set). It **requires `--irf`** and has no default prompt:
  `delta`, `gauss:FWHM[,T0]`, `sgauss:FWHM[,T0[,SKEW]]` (skew-normal, default
  skew 1.5, positive tailing to later times), or a path to a measured response
  of one number per line. A lifetime fitted against the wrong instrument
  response is wrong by roughly its width and nothing in the output says so, so
  the one behaviour ruled out is choosing quietly. Only `tau` is fitted — a
  hundred photons do not determine an anisotropy — and `gamma`, the burst's
  background fraction, is measured from the photons no burst contains rather
  than guessed.

  The container it writes carries the **pipeline document** that produced it
  (`_mmfdb_workflow.definition`), and a run can be driven by one:
  `--write-pipeline FILE` emits the recipe without reading the data,
  `--pipeline FILE` runs one (a `.json`, or a `.pto` that carries one). See
  [`doc/pipelines.rst`](../../doc/pipelines.rst).
- **`src/cmd_pto.cpp`**: `tttr pto` subcommands. Read side: ls, cat, extract, tree,
  info, tags. Write side: `pack -o out.pto PATH…` builds a container from files
  and directories, `add FILE PATH…` bundles more into an existing one.
- **`src/cmd_tui.cpp`** (+ `include/pto_tui.hpp`): `tttr tui` — interactive
  container navigation.

## Dependencies

- Depends on `util`, `core`, `simulation`, `fcs`, `clsm`, `superres`,
  `localization`, `burst`, `decay`, `io`, `io/store`, `io/pto`, `io/image`,
  `io/csv`, nlohmann/json, cxxopts.

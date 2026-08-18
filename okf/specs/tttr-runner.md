# tttr — the compiled runner

`tttr` is a small C++17 command built with cxxopts and the project's
`tttrlib_add_module` machinery. It replaces `bin/tttrlib` entirely -- that script is **deleted** (2026-08-18); the upstream bioconda recipe, which installed `$SRC_DIR/bin/*` and tested `tttrlib --help`, must install nothing from `bin/` and test `tttr --help` instead (the in-tree `recipes/cli` already does). One binary
next to `libtttrlib.dylib` (or `.so`), resolving its library via an `@rpath`/
`$ORIGIN` relative install RPATH so it finds the lib from a conda prefix without
`DYLD_LIBRARY_PATH`.

## Building / installing

```
cmake --build cmake-build-release --target tttr -j 8
cmake --install cmake-build-release --prefix <env>
```

The build tree mirrors the install layout so a run doesn't need the install
step at all: the executable lands in `<build>/bin/tttr` and
`<build>/lib/libtttrlib.dylib` is staged as a symlink, which is exactly what the
install RPATH `@loader_path/../lib` expects. A conda prefix pointed at the
build tree with two symlinks gets every rebuild with no reinstall:

```
ln -sfn <build>/bin/tttr           <env>/bin/tttr
ln -sfn <build>/libtttrlib.dylib   <env>/lib/libtttrlib.dylib
```

`@loader_path` is the real (symlink-resolved) path, so the staged `bin/` +
`lib/` layout is what makes the symlink work — a bare `modules/cli/` binary with
the aggregate dylib at the build root will not resolve (`../lib` points at
`modules/lib`). The per-module dylibs shipped in `site-packages/tttrlib/` come
from the Python package's own wheel build, not this tree, so they are not
symlinked here.

The flat install is `bin/tttr` + `lib/libtttrlib.{dylib,so}`. (The repo's
`include/tttrlib` install step aborts on the conda symlink farm — see caveats,
below — so a full `cmake --install` still needs `install` to copy `bin` + `lib`
by hand, which is exactly what the symlink loop above replaces.)

## Dispatch

First argument is the subcommand; the rest are parsed by it. Each subcommand
owns its cxxopts schema in `modules/cli/src/cmd_*.cpp`; `cli_main.cpp` is a
six-line switch with no shared state.

```
tttr convert    INPUT OUTPUT [-c CONTAINER] [-r RECORD]
tttr correlate  FILE [-c CH] --ch1 A --ch2 B [--method ...] [--progress ...]
tttr image      export FILE [--channels GROUPING] [--setup ...] [--progress ...]
tttr formats                       (list supported TTTR containers)
tttr sm FILE [--config JSON] [--setup ...] [--detector ...] [--output JSON] [--progress ...]
tttr detectors  FILE [--add [--name N]]   (inspect or author detector_setups.json)
tttr pto         FILE (ls|info|tree|tags|cat|extract ...)
tttr tui         FILE.pto          (full-screen terminal UI; needs a tty)
```

## Common: --progress

See [/design/tttr-cli-progress.md](/design/tttr-cli-progress.md) for the two-layer
architecture. Briefly: `--progress CHANNEL` turns on the machine-readable sink.

* `--progress -`        JSONL on stderr  
* `--progress stdout`   JSONL on stdout  
* `--progress FILE`     JSONL to `FILE`  
* `none` / absent       no JSONL; a tty bar only when stderr is a tty

Each line is `{"event","job","phase","done","total","fraction","seconds"}`;
`begin` and `finish` are guaranteed so a client can bracket a job.

## Detector setups (chiSurf compatibility)

The instrument definition is a JSON file, not compiled data — the binary holds
no detector knowledge, it only routes routing channels. The schema matches
`chisurf/core/data_io/detector_setups.py` so one file is shared verbatim:

```json
{
  "last_used": "SmRun",
  "setups": {
    "SmRun": {
      "detectors": {
        "Green": { "chs": [0, 1], "micro_time_ranges": [[0, 1023]] },
        "Red":   { "chs": [2, 3] }
      },
      "windows": { "prompt": [0, 1023], "delayed": [1024, 4095] }
    }
  }
}
```

* `chs` — routing (= detector) channel list, the same units as `sm --channels`
  and as TTTR routing-channel records.
* `windows` / `micro_time_ranges` — accepted, used only as metadata here.
* Everything chiSurf writes but the CLI doesn't act on (LUTs, shifts,
  calibration, `_owner`, `_is_public`, ...) is accepted and ignored, so a real
  chiSurf file parses. The CLI never writes those keys back, so round-tripping a
  chiSurf file through `tttr detectors` strips them — the wizard only touches the
  three keys above.

### Authoring

`tttr detectors FILE --add [--name NAME]` launches an interactive prompt
(detector name, channels csv, optional `lo-hi` gates; one window block; a blank
name ends the detector loop) and writes a chiSurf-shaped file, setting
`last_used`. `tttr detectors FILE` lists what is in a file. This is the
terminal analogue of chiSurf's detector-setup wizard — it lives in the CLI,
not a separate binary.

### Consuming (the three shared flags)

`sm` and `image export` both take:

```
--setup FILE         detector_setups.json
--setup-name NAME    choose a setup (default: last_used, then first)
--detector NAME      a named detector; without it the whole setup is used
```

* `sm` — a `chs` list (single detector) or the union of all detectors in the
  setup is fed to `--channels`'s routing filter (`get_tttr_by_channel`). With no
  `--setup`, the old `--channels` flag still works and wins. The result JSON
  records `detector_setup`, `setup_name` and `detector` in `parameters`.
* `image export` — per-detector channel lists become per-TIFF image groups (`-`
  joined), so one setup produces one TIFF per detector; `--detector NAME`
  exports just that one group. `--channels` still wins when given.

## Installation

conda: `lib/libtttrlib.dylib` and `bin/tttr` land at the prefix root; the
binary's `@loader_path/../lib` RPATH finds the lib inside the prefix so it runs
from any cwd with no env tweaks. Verified by running `tttr sm ... --setup`
from the conda env with `DYLD_LIBRARY_PATH` unset.

## Caveats

* `tttr convert` cannot emit PTO — `TTTR::write` has no PTO writer and produces
  a corrupt file. Use the Python bindings to build PTO fixtures, not the CLI.
  (tracked in /okf/log.)
* chiSurf may have migrated setups into MMFDB and deleted the JSON; the CLI only
  reads the JSON file form. If your setups live in MMFDB, export one
  `detector_setups.json` from chiSurf for the CLI to use.

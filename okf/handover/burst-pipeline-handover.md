# Handover — Burst pipeline: prototype done, C++ port unblocked

> **Date:** 2026-08-09 · **Repos:** [tttrlib] · **Previous:** burst pipeline
> prototype + PRD-027 blocker · **Next:** C++ port of PRD-026

## State of the work

### Done

| Item | Where | Notes |
|---|---|---|
| Python burst pipeline prototype | `prototype/burst_pipeline/` | `pipeline.py`, `operations.py`, `provenance.py`, `pto_builder.py`, `processing_list.py` |
| Provenance tests | `okf/testing/test_provenance.py` | 18 tests: completeness, hash, lineage, round-trip, columns, ndx equations |
| Value-recovery tests | `okf/testing/test_value_recovery.py` | 21 tests: NaN-on-failure, physical values, ground-truth tau, anisotropy cols |
| Nomenclature tests | `okf/testing/test_nomenclature.py` | 5 tests: mmfdb.dic conformance |
| Central naming dictionary | `okf/nomenclature/mmfdb.dic` | mmCIF format, 100+ entries |
| ndx integration | `chisurf/.../settings/mmfdb_dic.py` | mmCIF loader wired into equation validator |
| `to_tttr` fix (SWIG) | `ext/python/Sim.i:326` | defaults to engine settings, no more dt mismatch |
| IRF simulation docs | `okf/design/sim-irf-convolution.md` | dt matching, background_decay, VV/VH |
| `agy-export` tool | `tools/agy-export` | executable, documented in `okf/handover/agy-export.md` |
| **PRD-027 blocker: resolved** | `modules/plugin/`, `modules/registry/` | `register_operation` ABI live |

### The PRD-027 work (full detail)

The generic operation plugin type is live:

1. **`tttrlib_operation_v1`** struct in `tttrlib_plugin.h` — name, category,
   settings_schema, inputs_json, outputs_json, row_grain, can_replay, execute
2. **`register_operation`** appended to `tttrlib_host_v1` (append-only,
   struct_size gated — backward compatible with v1 plugins)
3. **`PluginHost::operations()` / `operations_json()`** with full journal
   rollback (vector-size snapshot in `Journal`, resize in `roll_back`)
4. **Registry splices** plugin ops into `registry("operation")` alongside the
   8 built-in entries (burst_selection, tcspc_calibration, mle_green,
   mle_red, bva, kde_cde, burst_fusion, burst_fcs)

A `.dll` can now register a new algorithm type — no recompilation.

### Partial C++ port work in `cmd_sm.cpp`

- Added `Duration (green) (ms)`, `Duration (red) (ms)`,
  `Mean Macro Time (green) (ms)`, `Mean Macro Time (red) (ms)` to the PTO
  burst table.
- Fixed pre-existing scoping bug: pointers `rout_ptr`/`macro_ptr`/`micro_ptr`
  were freed outside their declaration scope.
- Verified: `tttr sm examples/tttr/conv1.spc --output out.pto` produces a
  17-column burst table.

**⚠️ This green/red hardcoding is the WRONG approach — see next section.**

## Critical: detector-setup-driven columns (do NOT continue as green/red)

The `tttr sm` CLI currently hardcodes `ch == 0` → green, `ch == 1` → red.
This is wrong. From the chiSurf `burst.py` pattern:

- **Detector names come from the `--setup` JSON**, not from channel parity.
- Column names are `f"Duration ({detector_name}) (ms)"` — detector-name
  parameterised.
- A single-color anisotropy experiment has detectors named `parallel` and
  `perpendicular` (or `vv`/`vh`, `green_par`/`green_perp`, anything).
- The `DetectorSetup` struct already exists in `detector_setup.h` with named
  `DetectorDef{name, channels, micro_time_ranges}` — it's parsed but then
  flattened to a channel list, discarding the names.
- `BurstFilter` has named `Channel` objects with `(rout, mt_start, mt_stop)`
  matching — the mechanism exists.

**The C++ port must thread `DetectorSetup` through to the PTO output and
generate columns per named detector.** Full design is in PRD-026 under
"C++ port design: detector-setup-driven column generation".

## PRD chain

```
PRD-027 (modular registry)         🔵 Proposed → 🟡 In Progress (ABI done)
  └→ PRD-026 (burst pipeline)      🟡 In Progress (prototype done, C++ next)
       └→ PRD-029/030 (plugin mig) ⚪ Draft (depends on C++ port)
PRD-032 (migrate existing algos)   🔵 Proposed (follow-through after 027)
```

## Test inventory

| Test file | Count | What |
|---|---|---|
| `okf/testing/test_provenance.py` | 18 | Provenance completeness, hash, lineage, round-trip, columns, ndx equations |
| `okf/testing/test_value_recovery.py` | 21 | NaN handling, physical values, tau ground truth, anisotropy, FRET E |
| `okf/testing/test_nomenclature.py` | 5 | mmfdb.dic conformance |

Run with: `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python3 -m pytest okf/testing/`

## Build

- Working build dir: **`build_new/`** (Makefile)
- `cd build_new && make -j$(sysctl -n hw.ncpu)`
- **WARNING:** do not run parallel builds in `build/` and `build_new/`
  simultaneously — SWIG output corruption (posted on agent-board).
- The `tttr` binary runs with `DYLD_LIBRARY_PATH=build_new build_new/bin/tttr`
- Python module: `PYTHONPATH=build_new/ext`

## Open items for the next agent

1. **C++ port of cmd_sm**: rewrite to detector-setup-driven columns
   (PRD-026 design section). Replace placeholder companion tables with real
   C++ computations (BVA via `BVA` class, IRF via `_gaussian_prompt` port,
   MLE via `DecayFit`).
2. **PRD-032**: migrate burst_search dispatch from hardcoded if/else to
   registry table lookup; delete `kOperationRegistry` literal.
3. **Re-run all tests** after C++ port; extend conformance so the
   `tttr sm --setup` output matches the Python prototype bit-for-bit.
4. **`cmd_sm.cpp` companions** currently write `v_2I_g=10, v_tau_g=3.8` etc.
   placeholders — must become real fit results with `MLE Fitted` and
   NaN-on-failure semantics (prototype precedent in
   `prototype/burst_pipeline/operations.py::compute_mle`).

## Contacts / board

- Coordination: `okf/agent-board.md` (tttrlib + chisurf share it via symlink).

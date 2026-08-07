---
type: Status
title: JavaScript binding — what is still open
description: The honest gap list for the Node.js binding, ordered by what would bite first.
tags: [javascript, nodejs, status, gaps, prd-016, prd-015]
status: stable
stale_after: 2026-11-06
generated: { by: "claude-code/claude-opus-5", at: 2026-08-06T07:20:00Z }
sources:
  - id: prd-016
    resource: PRDs/PRD-016-javascript-node-api-bindings.md
    title: PRD-016 — milestones M5 and M6
    author: human:tpeulen
  - id: counts
    resource: test/python
    title: Measured test volume, 2026-08-06 (141 files, 1507 test functions)
    last_modified: 2026-08-06
---

# Verified state

Built and green at 2026-08-06T07:34Z: `16` suites / ~60 tests passing under
`node --expose-gc --test test/js/`, including the `std::map` conversion and the
new lifetime/GC cases. `BurstFeatureExtractor.get_burst_channel_photons()`
returns a plain `Object`, confirmed directly.

Two things went wrong on the way there and are worth keeping:

* **`SwigValueWrapper`.** The first map typemap did not compile — SWIG returns a
  by-value map as `SwigValueWrapper<T>`, which has no `begin()`/`end()`. Fixed by
  binding through the wrapper's `operator T&` before iterating.
* **A vacuous test.** The first test asserted the result was *not* a SWIG proxy,
  which passed on a binary with no map support at all: there the value is an
  empty generic `SwigObject` with no `get()`/`size()` and no entries. It now
  asserts the positive (`counts.constructor === Object`). **A negative assertion
  about marshalling can pass because the feature is missing entirely.**

An earlier failure was environmental rather than ours: `modules/plugin/` was
being wired into `TTTR.cpp` and `Registry.cpp` concurrently, which broke the
build with `fatal error: 'PluginHost.h' file not found` and left the module
dylibs inconsistent, producing spurious *"Container type SPC-130 not supported"*
failures. Resolved — `modules/plugin/CMakeLists.txt` exists and is referenced.

# Not verified

## Only macOS arm64 has ever been built

Linux and Windows are untried. The `build_test_js_lnx` CI job is written but has
never run — treat its first run as part of the work, not as a regression check.

# Gaps

| Gap | Detail | Size |
|---|---|---|
| **Test coverage is not at parity** | The Python suite has 1507 test functions across 141 files; this one has ~60.[^counts] It covers the same *subsystems* and asserts the same canonical cross-language values, which is what catches a broken binding — but it is not the file-for-file parity the phrase suggests. | Large |
| **No prebuilt binaries, nothing published** | PRD-016 M5 wants `prebuildify` binaries for linux-x64/arm64, darwin-x64/arm64, win32-x64 loaded by `node-gyp-build`, plus an npm release. Package metadata is in place; the pipeline is not.[^prd-016] | Medium |
| **The copy fallback has never executed** | When a host refuses an external `ArrayBuffer` the binding copies instead. No available runtime refuses, so the path is untested. Building with `-DTTTRLIB_JS_COPY_ARRAYS` and running the suite would cover it. | Small |
| **No sanitiser run** | PRD-016 asks for the "drop the owner, then read the view" case under ASAN. `test/js/lifetime.test.mjs` covers the shared_ptr and GC side in ordinary builds; nothing has been run under a sanitiser. | Small |
| **Columnar HDF5 not wrapped** | `ext/js/tttrlib.i` deliberately omits `Hdf5Table.i` so it matches the *committed* Python module. `readHdf5()` / `writeHdf5()` are already in `index.js` behind a feature check. | One line |
| **The full conformance list** | PRD-016 M6 wants PRD-015's case list green. The canonical reference values pass; that list does not exist yet. | Depends on PRD-015 |

# By design, not gaps

| | |
|---|---|
| No directors | SWIG's Node-API backend generates none, so `PdaCallback` cannot be subclassed — the same limitation R has. PDA's built-in models are unaffected. |
| No Promises | Everything is synchronous. Use a `worker_thread`. |
| Not the browser | A native addon for Node. WASM is a separate question; PRD-016 records the cost. |
| shared_ptr lifetime | See [shared_ptr design](/bindings/shared-ptr-design.md). |
| `std::set` unsupported | `SetInt32` is an unused template — nothing in the wrapped API returns or takes a `std::set`, so this costs nothing today. |

# Also worth knowing

Uncommitted at the end of the session, in the working tree only — **all of it
verified green**, just not committed:

* the `std::map` typemaps in `ext/js/jsarrays.i`
* `test/js/lifetime.test.mjs` and the map test in `test/js/analysis.test.mjs`
* the "What is still open" section in `doc/javascript-package.rst`
* the CHANGELOG entries for both
* this bundle

The committed state is `b4e8e7d0`, which was verified at ~60 tests passing, all
four SWIG wrappers generating, and the web viewer reading three formats — but
**without** the map conversion and lifetime tests, which are the work above.

[^prd-016]: PRD-016 — milestones M5 and M6
[^counts]: Measured test volume, 2026-08-06 (141 files, 1507 test functions)

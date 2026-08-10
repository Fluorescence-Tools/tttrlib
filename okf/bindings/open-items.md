---
type: Status
title: JavaScript binding — what is still open
description: The honest gap list for the Node.js binding, ordered by what would bite first.
tags: [javascript, nodejs, status, gaps, prd-016, prd-015]
status: stable
stale_after: 2026-11-06
generated: { by: "claude-code/claude-opus-5", at: 2026-08-10T00:00:00Z }
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

# Verified state (2026-08-10)

Green on macOS arm64 in **two** builds: the default zero-copy one and a
`-DTTTRLIB_JS_COPY_ARRAYS` one, 41 suites each, plus 96/96 conformance cases.

The lesson from the last round repeated itself almost exactly, so it is worth
stating as a rule rather than an anecdote. The old `array outputs report their
copy mode` test asserted that `arraysAreZeroCopy()` returns a *boolean* — which
is true of a build that copies every array, a build that copies none, and a
build where the function is a stub. **A test that describes a property instead
of exercising it passes in every world.** Its replacement asserts aliasing
against the mode, so the copy build and the zero-copy build must disagree, and
both were run to confirm they do.

Also green in a **STATIC-module** build (`-DTTTRLIB_MODULE_TYPE=STATIC`), which
is what the npm prebuilds ship: 44/44 suites, 19/19 conformance areas, 52
registry entries across 11 areas. The registry count is the assertion that
matters there — a static archive can drop the translation units whose only
purpose is a registration side effect, and the result loads perfectly and
advertises nothing.

## The registry key-order trap

**The JavaScript binding is the only consumer in the project that depends on
registry key order.** `burstSearchByName` builds a *positional* call from
`Object.keys(params_schema.properties)`, because JavaScript has no `**kwargs`;
Python, which calls the same registry, uses them and is immune.

This broke exactly once and is worth recognising on sight: PRD-032's
`TTTR::burst_search_algorithms_json` parsed the category into an
`nlohmann::json` and dumped it again. That type is a `std::map`, so every object
key came back alphabetically sorted, and `burstSearchByName` began passing
`max_false_alarm_rate` where `p0` belongs. PRD-032's own verification reported
"0 entries changed" — it compared *parsed* JSON, and parsing is the step that
discards order.

Two consequences to keep:

* any registry assembly must use `nlohmann::ordered_json`, including
  intermediate parses that look like plain plumbing;
* a diff of parsed JSON cannot see this class of change. The regression test
  that can is in `test/js/registry.test.mjs`: `required` preserves declaration
  order, so it must be a *subsequence* of the property keys.

# Earlier verified state

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
| **Four of five platforms have never been built** | The prebuild pipeline exists and is verified end to end — but only on darwin-arm64, locally. linux-x64, linux-arm64, darwin-x64 and win32-x64 have never been compiled at all, and `prebuild_js`, `pack_js`, `publish_npm` and `asan_js_lnx` have never run. **Their first run is part of the work, not a regression check.** | Medium |
| **Nothing published** | `publish_npm` is written and fires on a release, but the `NPM_TOKEN` repository secret does not exist and the name `tttrlib` has never been claimed on the registry. | Small |

## Reproducing the two-build check

```sh
cmake -S . -B build-js-copy -G Ninja -DBUILD_PYTHON_INTERFACE=OFF \
  -DBUILD_JAVASCRIPT_INTERFACE=ON -DBUILD_LIBRARY=ON -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="-DTTTRLIB_JS_COPY_ARRAYS"
cmake --build build-js-copy --target tttrlibJs
TTTRLIB_ADDON=$PWD/build-js-copy/js-pkg/tttrlib.node \
TTTRLIB_DATA=$PWD/tttr-data node --expose-gc --test test/js/
```

**Delete the extra build tree afterwards, or pass `TTTRLIB_ADDON` on every run.**
`loadAddon()` in `ext/js/pkg/index.js` searches every `build*/js-pkg/` at the
repository root and takes the **most recently modified** one. A second build
tree therefore captures the default test run silently: an ASAN tree left behind
turned all 41 suites red with no indication that the addon was not the one being
tested, and a copy-array tree would have been worse — it passes, so the suite
would quietly have been measuring the fallback path and reporting it as default.

## Closed since this note was written (2026-08-10)

Three of the six gaps above were already closed, or were closed by the work on
2026-08-10. Recorded because the note asserted them for four days after they
stopped being true, which is worse than not having listed them.

| Was | Now |
|---|---|
| **The copy fallback has never executed** | It has. The full suite builds and passes against `-DTTTRLIB_JS_COPY_ARRAYS`, and the zero-copy and copy builds were verified to *disagree* on the aliasing test — so the two modes are genuinely distinguished rather than both passing by accident. |
| **Columnar HDF5 not wrapped** | Wrapped. `ext/js/tttrlib.i` includes `Hdf5Table.i`, as do all four bindings; `readHdf5`, `writeHdf5` and `TableFormat_Hdf5` are exported. |
| **The full conformance list** | Green. `test/js/conformance.test.mjs` runs all **96 cases across 19 areas** with zero `unsupported` declarations — PRD-016 M6, and the PRD's real acceptance criterion. |
| **No prebuilt binaries, nothing published** | Half closed, and split above into what is actually still missing. The pipeline exists: `scripts/prebuild.mjs` (CMake → `prebuilds/<triple>/`, self-containment verified), `scripts/pack.mjs` (merge five triples, version from `pyproject.toml`, refuses a partial set), `node-gyp-build` loading in `index.js`, and the `prebuild_js` / `pack_js` / `publish_npm` CI jobs. A packed tarball installs into an empty project on darwin-arm64 and answers the PRD's acceptance queries. |
| **No sanitiser run** | The `asan_js_lnx` job is written — Linux, `LD_PRELOAD` of `libasan.so`, `detect_leaks=0`, and an explicit `TTTRLIB_ADDON` so it cannot silently test an uninstrumented addon. It has not run yet. The macOS blocker stands and is not ours: preloading ASAN into Node on macOS arm64 hangs before any JavaScript executes, with no addon loaded. |

A fourth item was not on the list because nobody knew it was there: the
`signed char` accessors leaked their buffer on every call in all four bindings.
See the CHANGELOG. It was found by asking what the marshalling layer actually
guarantees, which is the question this note exists to keep open.

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

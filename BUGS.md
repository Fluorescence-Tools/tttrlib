# Known bugs

Found from outside the library, with a reproduction each. Anything fixed moves
to the changelog and leaves here.

## SIGSEGV: `TTTR(path).header` on a temporary — the header outlives its owner

**2026-08-11.** Reading the header off a TTTR that is not bound to a name is a
hard crash, on every format:

```python
import tttrlib
h = tttrlib.TTTR("any.ptu").header
h.macro_time_resolution          # Fatal Python error: Segmentation fault
```

Exit code 139. Binding the TTTR first is fine and is the workaround:

```python
t = tttrlib.TTTR("any.ptu")      # keep it alive
h = t.header                     # 2.5e-08
```

`get_header()` hands back a raw `TTTRHeader*` and the SWIG proxy does not keep
the owning `TTTR` alive, so the temporary is collected at the end of the
expression and the proxy is left pointing into freed memory. Nothing about the
call site looks dangerous, which is what makes it worth fixing rather than
documenting: `obj.attr.subattr` is ordinary Python, and the same shape works for
every other tttrlib property.

Found while writing `test/python/test_pto_photons_native.py` (the test now binds
the object and says why). **Not specific to `.pto`** — reproduced above on a
PicoQuant PTU, and the same lifetime question applies to any accessor returning
a pointer to a member.

Fix is a SWIG `%feature` keeping a reference to the parent on the returned
proxy — the same treatment other container-to-member accessors need; worth
auditing them together rather than patching this one.

## FIXED — TCSPC MaxEnt is half-landed: the lifetime axis is here, the FRET distance axis is not

> **Fixed 2026-08-10, entry moved to the changelog** (removal = fix landed,
> not a concurrent-write loss). `solve_tcspc_mem_fret` +
> `tcspc_build_fi_distances` now live beside the lifetime solver, sharing one
> `run_mem_from_design` engine — the two differ only in the design matrix,
> which was the entry's point. Parity with ChiSurf's `solve_fret_mem`:
> `max |dp| < 1e-8` on identical input
> (`test_maxent_tcspc.py::TestTcspcMemFret`), plus a no-reference
> ground-truth recovery test. ChiSurf's `solve_fret_mem` can now delegate the
> way `solve_lifetime_mem` does.
>
> **Still open from this entry:** FCS MaxEnt
> (`chisurf/core/models/fcs/maxent.py`) remains a third MEM implementation
> with no home here; closing it means exposing the engine with a pluggable
> design matrix, at which point all three are one solver and two matrix
> builders. This stub can be deleted once both sessions have seen it.

## Enhancement: automated performance measurement via GitHub Actions with docs auto-update

**2026-08-08.** tttrlib needs a **continuous performance measurement** pipeline:

1. **Benchmark suite.** A set of benchmarks covering the hot paths — file I/O
   (PTO/PTU/HDF5 read), burst search, FCS correlation, decay fitting, CLSM
   assembly, DataStore read/write. These run as a dedicated benchmark target,
   not part of the unit test suite.

2. **GitHub Actions runner.** A workflow (triggered on push to `dev`/`main`
   and on PRs) runs the benchmark suite on a fixed runner environment and
   captures timing metrics. The runner must be consistent (same OS, same
   hardware class) so numbers are comparable across runs.

3. **Auto-update docs.** The performance metrics are written into the
   documentation automatically — a performance table or page (e.g.
   `doc/performance.rst` or `okf/specs/performance-baseline.md`) is
   regenerated with the latest numbers on every push. The update happens
   through a **push hook**: the workflow commits the updated metrics back to
   the branch (or opens a PR with the updated numbers), so the docs never
   drift from measured reality.

4. **Regression detection.** A benchmark that regresses beyond a threshold
   (e.g. >10% slower than the baseline) fails the CI check and blocks the
   PR. The baseline is versioned alongside the benchmarks.

5. **Relates to the GIL/non-blocking rule.** The benchmark suite should also
   verify the "logging does not degrade performance" constraint (from the ndx
   verbosity issue) and the GIL-release requirement — a test that runs a
   long tttrlib call from a thread and asserts the main thread's heartbeat
   did not stall.

### Concrete deliverables

- `.github/workflows/benchmark.yml` — runs on push/PR, executes benchmarks,
  writes metrics, detects regressions.
- `test/benchmarks/` — the benchmark scripts (Python, using `pytest-benchmark`
  or a standalone timing harness).
- `doc/performance.rst` — auto-generated performance table, updated by the
  workflow.
- A baseline file (`test/benchmarks/baseline.json`) with the reference
  numbers; updated only when a benchmark change is intentional.

## Enhancement: stabilise the ABI so development gets faster (modular compile, incremental rebuild)

**2026-08-08.** Related to PRD-027 (modular algorithm registry) and PRD-018
(ABI stability). The goal is a development loop where editing one algorithm
does not recompile the world.

Today every C++ source file compiles into one aggregate library. Touching one
function in `decay` recompiles `burst`, `fcs`, `hmm`, `pda`, `clsm`, and the
SWIG wrapper that links them all — even though nothing they depend on changed.
ccache helps but does not solve it: a header change in `core` still invalidates
every translation unit that includes it.

What to do:

1. **Stable ABI boundaries (PRD-018 completion).** Finish the work PRD-018
   started: `TTTRLIB_API` visibility markers on the aggregate library, frozen
   public headers, `SOVERSION` on the shared lib. Once the ABI is stable,
   modules can link against a prebuilt `libtttrlib_core` instead of recompiling
   it every time. The plugin C ABI (`tttrlib_plugin_init_v1`) is already
   designed for this — the gap is the intra-library C++ boundary.

2. **Modular compilation (PRD-027 Part 4).** When
   `TTTRLIB_MODULAR_ALGORITHMS=ON`, each algorithm family compiles into its own
   shared library (`libtttrlib_fcs`, `libtttrlib_decay`, etc.) that links
   against `libtttrlib_core`. Editing a decay fit recompiles only
   `libtttrlib_decay`; the core, FCS, HMM, and PDA binaries are untouched.

3. **Unity / precompiled headers.** The heaviest headers (`TTTR.h`,
   `DataStore.h`, `DecayFitModel.h`) are included by dozens of TUs. A PCH or
   unity build for the aggregate target would cut compile time significantly
   for full rebuilds (the ones ccache can't help with).

4. **Dependency isolation.** Audit the module dependency graph
   (`modules/CMakeLists.txt`). If `fcs` transitively pulls in `decay` headers
   through a chain it doesn't actually need, sever it. Fewer header deps = less
   recompilation on any change.

5. **Verify with a benchmark.** Measure: time to rebuild after touching (a)
   one `.cpp` in `decay`, (b) one header in `core`, (c) clean build. Record
   before and after. Target: (a) drops from "relink everything" to "recompile
   one TU + one module lib"; (b) does not recompile modules that don't include
   the changed header.

This is the build-system half of the modular algorithm story. Without it,
PRD-027's `TTTRLIB_MODULAR_ALGORITHMS=ON` compiles correctly but the
development loop is no faster than today.

## DONE — tttrlib naming must align with mmfdb / flrCIF

> **Closed 2026-08-10.** The rule and the reasoning are now normative in
> [`okf/specs/mmfdb-is-the-vocabulary.md`](okf/specs/mmfdb-is-the-vocabulary.md);
> what follows is the original entry with the outcome against each point.

**2026-08-08.** The names used throughout tttrlib — class names, methods,
parameters, object kinds, tag keys, file format identifiers — must be
consistent with the vocabulary used in **mmfdb** (`/Users/tpeulen/dev/mmfdb`)
and the **flrCIF** dictionary standard that mmfdb defines and exports.

What was found and done:

* **Audit the public API surface against mmfdb** — done for everything that
  reaches a *file*. tttrlib was emitting **eighteen terms mmfdb does not
  declare**: `bva`, `kde_cde`, `mle_green`, `mle_red`, `burst_fcs`,
  `hmm_photon_by_photon`, `tcspc_calibration`, `pda_histogram`,
  `companion_of`, `histogram_bin`, and seven `…4` data formats. All reconciled.
* **Align object kinds and tag keys** — done. Every `operation_type`,
  `row_grain`, `data_format` and `relationship_type` the writer or the registry
  publishes is now an mmfdb term, checked in CI from both sides.
* **Rename, don't alias** — done, and the distinction that made it tractable is
  worth keeping: a registry entry's **`name`** is tttrlib's own identifier and
  was *not* renamed; its **`operation_type`** is the controlled term and was.
  A conformance test that compares the key against the vocabulary forces the
  two to be equal, which is how the local names got into the dictionary.
* **Document the canonical vocabulary in one place** — done, and the one place
  is **mmfdb**, not here. `okf/nomenclature/mmfdb.dic` is deleted; its 113
  genuinely-new items were migrated into `mmfdb_workflow_ext.dic`. A test
  asserts this repository contains no `.dic` at all.

Not covered by this entry and still open: the *Python/C++ identifier* surface
(class and method names) was not audited against flrCIF — only the names that
cross into a file. That is a larger and much lower-risk piece of work, since an
identifier is not a term.

## Enhancement: all tttrlib functions must be non-blocking and release the GIL

**2026-08-08.** Any long-running tttrlib function — file I/O, photon
decoding, burst search, CLSM assembly, convolution, fitting — should be
**non-blocking** and **release the GIL** in the Python bindings so
concurrency isn't killed. Same principle applies to other language bindings
(Julia, R, JS) wherever they have an equivalent global lock or event loop.

What to do:

* **Python (SWIG bindings).** Ensure every C++ function that may take more
  than a trivial amount of time is wrapped with `Py_BEGIN_ALLOW_THREADS` /
  `Py_END_ALLOW_THREADS` — either via `%inline`/`%template` directors or by
  annotating the SWIG interface files (`%feature("allowthread")` /
  `PYTHON_THREAD_BEGIN` / `PYTHON_THREAD_END`). Audit every binding entry
  point, not just the obvious ones.
* **Other bindings.** Julia (`ccall` is fine, but long calls should yield),
  R (release the R eval lock for lengthy C++), JavaScript (Web Workers /
  async for wasm). Same goal: the host runtime stays responsive while the
  C++ runs.
* **Verification.** Write a test that runs a long tttrlib call from a thread
  while the main thread keeps a heartbeat alive, and asserts the heartbeat
  did not stall. This guards against regressions.

**2026-08-10, the Python half is DONE.** The wrapper is generated with SWIG
`-threads`: all ~4,600 wrapped calls release the GIL around the C++ action
(typemap code keeps it; the RAII guard reacquires during exception unwinding
before the `%exception` handlers touch the Python C-API — verified in the
generated code). The two `%extend` methods whose C++ bodies call the Python
C-API (`localization.fit2DGaussian_array` / `model2DGaussian_array`) are
`%feature("nothread")`. **Composition warning for whoever maintains
`TTTRLIB_NOGIL`:** SWIG inserts its BEGIN/END_ALLOW inside `$action` even in
a custom `%exception`, so a guard of one's own there releases an
already-released GIL — a FATAL Python error, not a no-op; the macro now
carries only the exception translation and says so, and `tttrlib_gil_release`
is `PyGILState_Check()`-tolerant. Verified: the entry's heartbeat test
(`test/python/test_gil_release.py`) passes, and the full fast suite is green
under `-threads` (2501 passed). **Still open:** the R / Java / JS bindings,
and the non-blocking (async) surface beyond GIL release.

## [chisurf] The built-in games appear to have disappeared

**2026-08-08.** **Repo:** `../chisurf`. The Games hub
(`chisurf/plugins/misc/games/`) still ships Pong, Tetris, Breakout,
Minesweeper, and Number Quest — but they are **hidden from the default
menus** by the `plugins.show_demo` flag (`demo: true` in the manifest). To a
user who does not know the flag exists, the games have simply vanished.

The games are not deleted; they are gated. But the gate is invisible and the
default is "hidden," which reads as "gone."

Fix: surface the games in a **ribbon** inside the Games hub tool — a row of
game cards shown regardless of `plugins.show_demo`, since a user who opens
the Games hub has explicitly chosen to play. The flag should keep the games
out of the production/analysis menus but not out of the dedicated gamespace.
The Doc Review Quest (PRD-91) should be a first-class entry in that ribbon.

**2026-08-10, verified resolved in substance — not by this session, so the
entry stays for its author to close.** The hub's manifest is `demo: false`,
`menu_hidden: false` (discovery confirms it lands in
`Tools:Miscellaneous:Games` with `show_demo_plugins` OFF); the hub lists all
six games unconditionally in a navigation panel — a side list of icon+name
cards rather than the literal ribbon, which reads as a design preference,
not a gap; the five arcade games are `demo: true` and hub-only, exactly the
menu split asked for; and Doc Review Quest became **Lumis Quest** (PRD-91
renamed it) and is a first-class hub entry. Pinned so it cannot silently
regress: `games/test/test_manifest.py::test_the_gamespace_survives_the_demo_flag`
fails if the hub is ever demo-gated or menu-hidden, a game leaks into the
production menus, or a game leaves the hub list.

## FIXED — [chisurf] License tracker: enumerate dependencies and compare to most permissive possible

> **Fixed 2026-08-10** (removal = fix landed, not a concurrent-write loss).
> `build_tools/license_tracker.py` generates `doc/licenses.md` (matrix on
> top) + `doc/licenses.json` (machine-readable) from `pyproject.toml`
> resolved against installed metadata, with overrides for packages shipping
> none, plus tttrlib's bundled compiled components; **no JS/wasm asset is
> bundled anywhere in the tree** (the entry's assumption, checked). The
> answer: most permissive possible is **GPL**, binding constraint
> **PyQt5/sip** — and those are GPL **v3**, so the stated `GPL-2.0` needs a
> deliberate call (2.0-or-later or 3.0 resolves it; 2.0-only cannot combine
> with v3-only deps). `--check` mode + `test/test_license_tracker.py` fail
> when a new dependency arrives unclassifiable, which is the CI regression
> guard the entry asked for. This stub can be deleted once both sessions
> have seen it.

## FIXED — ndx UI/UX: show filename, not full path, in title/header

> **Fixed 2026-08-10** (removal = fix landed, not a concurrent-write loss).
> ndX never titled its window from the opened file at all — the path on
> screen was the header's working-path field. `open_files` now titles the
> window `ndX - <filename>` (` (+N)` for multi-selection, unchanged on
> append), matching the convention the four external launchers already used,
> and the full path(s) land in the header Path field's tooltip.
> `ndxplorer/tests/test_window_title.py`, 5 cases.

## FIXED — ndx UI/UX: buttons are hard to recognize

> **Fixed 2026-08-10** (removal = fix landed, not a concurrent-write loss).
> Every icon kept its glyph and gained a one-word label: the seven emoji-only
> buttons of the main window (`📁 Browse`, `📊 Data`, `🎨 Contrast`,
> `🔄 Update`, `🧹 Clear`, `📷 Screenshot`, `💾 Save`) and the six
> single-LETTER buttons of the axis rows — `u`/`r` are now `Set`/`Auto`, and
> the three `Auto` buttons, which had no tooltip at all, say what they
> auto-range. Buttons that already paired an icon with a word were left
> alone. Sizes are unchanged (expanding rows absorb the short labels); the
> full UI test set passes with the new texts.

## FIXED — ndx UI/UX: clean up slider position display

> **Fixed 2026-08-10** (removal = fix landed, not a concurrent-write loss).
> (1) was already true of the AutoForm playback panel — the Step slider's
> value box renders to the right of the slider. (2) the persistent
> `slice X/Y · N points` info row is removed from `playback.view.json`; the
> same live readout now answers on hover of the Step row (the row, the
> slider and the value box all carry it, refreshed on every step and after
> panel rebuilds). (3) the panel loses its one always-on secondary label,
> which was the noise. Test:
> `test_playback_panel.py::test_the_readout_is_a_hover_not_a_row`. This stub
> can be deleted once both sessions have seen it.

## ndx (ndxplorer) must be a fully autoform application

**2026-08-08.** **Repo:** `chisurf/modules/ndxplorer` (handled here because
ndx lives in the ChiSurf tree). Motivation: ndx should be portable to a webapp
later. For that to be possible it must not depend on imperative GUI wiring —
every form, table, and control should be **driven by a schema/declarative
description** (an "autoform") rather than hand-built widget code.

What blocks the port today:

* Widget construction is likely hand-coded against the desktop framework
  (Qt/enaml). A webapp cannot reuse that; it needs a schema it can render.
* Data binding is probably imperative (signals/slots). A webapp needs the form
  state to be a serialisable object the frontend can read and write.
* Layout and field definitions are embedded in Python GUI code rather than in
  a description layer.

To unblock: extract a declarative form definition (field names, types,
constraints, layout) that a desktop **and** a web renderer can consume, and
have ndx render its UI from that definition. No business logic in widget
code.

## ChiSurf issues

**2026-08-08.** Issues reported against `../chisurf` are also tracked here
until the project has its own tracker. Tag the report with `[chisurf]` and
note the affected path under `chisurf/`.

## FIXED — ndx is too chatty via logging; reduce verbosity

> **Fixed 2026-08-10 in two passes** (removal = fix landed, not a
> concurrent-write loss).
>
> **The level, everywhere it was set:** ndX standalone defaults to `WARNING`
> (`-v` for INFO, `--debug` unchanged) — but embedded ndX was re-raised to
> INFO by chisurf itself, from three places, all now `WARNING`:
> `chisurf/__init__.py`'s import-time default, its settings-fallback, and
> the shipped `settings_chisurf.yaml` (`log_level: 20` → `30`; the comment
> says how to get INFO back). The stale `log_level: 20` in
> `~/.chisurf/settings_chisurf.yaml` on this machine was updated too — it
> was a copy of the old default, not a choice. A settings file that states
> a level is still honoured.
>
> **The performance constraint, measured:** the plot-update hot path makes
> 13 log calls; the f-string sites among them now use lazy `%` args.
> Interleaved-median benchmark on a 50k-point `update_plots`: **2 µs (0.8%)
> logging overhead** with logging enabled vs removed — the hot path is
> unchanged, which is what the constraint demanded. This stub can be
> deleted once both sessions have seen it.

## FIXED — ndx DataFrame Editor is slow to open `tes_chisurf_mfd.pto`

> **Fixed 2026-08-10** (removal = fix landed, not a concurrent-write loss).
> The profile said the entry's suspects were innocent: container enumeration
> and table decode take 0.12 s cold. The seconds were the EDITOR — chitable
> had retired `DataFrameSource`, ndX's import of it raised, the ImportError
> was swallowed by the standalone-fallback machinery, and every ChiSurf user
> silently got the per-cell `QTableWidget` editor (~5 s at burst-table size,
> scalar `df.iloc[i, j]` per cell). The chitable branch now adapts the frame
> through `ArraySource` with edit write-back (~0.16 s for 4.6k x 20); the
> fallback hoists the per-cell frame access and bounds the resize scan for
> standalone installs; and a guard test fails loudly if the chitable branch
> ever rots into the fallback again
> (`test_dataframe_editor.py::test_chisurf_branch_is_alive_when_chisurf_is_importable`).
> This stub can be deleted once both sessions have seen it.

## `disassemble` does not create the directories an object's name implies

**2026-08-07.** An object name is written out as a *relative path* — which is
useful, and is what ChiSurf now relies on to address a container like a folder
(`m000.pto/countrate_All 0.2000#60/bursts`). But the writer does not create the
directories the name implies, so the first name containing a separator fails:

```
PtoMfdbError: could not disassemble into /tmp/unpack:
    cannot create /tmp/unpack/countrate_All 0.2000#30/bursts
```

Worked around by walking `objects()` and `mkdir(parents=True)`-ing each name's
parent before the call. Either the writer should do that, or it should say that
a name is a flat identifier and reject a separator — the present behaviour
accepts the name and then fails on it, which is the one option that teaches
nothing.

## A container's objects have no identity beyond `(kind, name)`, so a reader cannot tell two runs apart

Found driving ChiSurf's burst pipeline end to end over a `.pto` built from ten
`.spc` files: search, change one setting, search again. Each run writes an
object of kind `burst_table` named `bursts` — correctly, because the older
result is meant to stay reachable. But `PtoFile.objects()` gives a reader
nothing to *choose* between them with except tags it has to know to look up
(`_mmfdb_operation.settings_hash`), and `find(name)` resolves a name that is
not unique.

The consequence in a reader that does the obvious thing: ndX read every
`bursts` object and concatenated them side by side, lining up three unrelated
analyses of 4621, 2318 and 1099 rows against each other and padding the short
ones — no error, a plot of a mixture of three searches.

```python
import tttrlib
f = tttrlib.PtoFile(); f.open("m000.pto")
names = [(o.uid, o.kind, o.name) for o in f.objects()]
# [(…, 'burst_table', 'bursts'), (…, 'burst_table', 'bursts'), (…, 'burst_table', 'bursts')]
f.find("bursts")   # one uid, and nothing says which
```

Worked around on the reader side (take the last object of the right
`operation_type`, then only tables whose parent is that one). Two things would
make that unnecessary, and the second matters more:

* **`objects()` should promise write order.** The workaround leans on it and
  the header does not say it holds.
* **A container should be able to say which object is current for a given
  `(kind, name)`** — a `superseded_by` edge, or a `current` flag the writer
  moves. Every reader otherwise re-implements "newest wins" and they will not
  agree; a reader that guesses wrong shows old numbers with no sign of it.

Related and smaller: `Measurement.metadata()` returns `""` for a container
written by `Measurement.create()`, so nothing at the file level says what the
measurement *is* while every object below it is richly tagged.

## FIXED — Tags are appended, never replaced, and nothing dedupes an edge

> **Fixed 2026-08-10, entry moved to the changelog** (removal = fix landed,
> not a concurrent-write loss). As the entry proposed, with the semantics the
> two call sites agreed on: `add_tag` now skips a tag identical in every field
> (two *different* parents both still land); new `PtoFile::set_tag(tag)`
> replaces by `(target, name, index)`; new `clear_tags(target, name)` removes
> one name from one object. `cmd_sm.cpp`'s local implementation now delegates
> to the API; ChiSurf's read-and-skip workaround can be deleted once it pins a
> tttrlib with this. Tests:
> `test_pto.py::test_adding_the_same_fact_twice_records_it_once`,
> `::test_set_tag_replaces_what_was_stated_before`,
> `::test_clear_tags_for_one_name_leaves_the_rest`. This stub can be deleted
> once both sessions have seen it.

## Concurrent agents silently lose each other's documentation

**2026-08-10.** Two instances working the same checkout produced three
observable kinds of damage in the shared prose files, none of which any test
catches because none of it is code:

1. **`okf/log.md` had six byte-identical duplicated sections** and five colliding
   headings (three `## 2026-08-10 (9th entry)`, two `(8th entry)`, …). A
   read-modify-write of a whole file by two writers appends both copies; the
   ordinal in the heading is chosen by counting existing entries, so two writers
   counting concurrently pick the same number. Cleaned by dropping only the
   *exact* duplicates and renumbering the day; both are safe because neither
   changes a byte of anyone's content.
2. **A `BUGS.md` entry was lost entirely** — the `pto_update_store` /
   `PtoRowCount` finding, written earlier the same day, was absent a few hours
   later. Restored from the session that wrote it. Nothing would have noticed:
   a missing bug report has no failing test.
3. **A source-of-truth decision was reverted and re-applied**, recorded in the
   log by the instance that did it: a `data_format` enumeration reduced to one
   row by one instance was "restored" by the other before it found the upstream
   mmfdb package and reverted itself.

**2026-08-10, later: the same hazard in the index.** Committing the mmfdb
vocabulary work found two more shapes of it, both of which `git commit -a` would
have swallowed:

4. **The index carried a staged change from another session** — a revert of
   `region_table` and the `spot`/`region` row-grain distinction, which that
   session had already undone in the working tree. Staged but stale, and
   invisible unless you run `git diff --cached` before committing.
5. **One file held two sessions' work, interleaved and unsplittable.**
   `mmfdb_flr_ext.dic` carried this session's five additions and another's
   396-line `_mmfdb_object` category. Hand-splicing a dictionary somebody is
   actively editing risks corrupting it, so the commit names both rather than
   quietly claiming one.

   Also: **do not branch off the default branch under a live collaborator.**
   The usual advice is to branch before committing; moving the branch pointer
   while another session works in the same checkout is worse than a local commit
   that is easy to reset.

Worth stating because the mitigation is not "be careful": (2) is invisible, (1)
is only visible if somebody reads the whole file, and (4) is only visible if you
look at the index rather than the diff you expect. What would actually help:

* **`okf/log.md` entries keyed by something not counted** — a timestamp or a
  session id rather than an ordinal, so two writers cannot collide by
  construction.
* **Append-only writes for the log**, never read-modify-write of the whole file.
* A guardrail test that fails on a duplicated `##` heading or a duplicated
  section body in `okf/log.md` — trivial, and it turns (1) from invisible to
  loud.
* **`git reset` then stage explicit paths, always, in a shared checkout.**
  Never `git commit -a`, and read `git diff --cached` before every commit
  rather than trusting that the index holds what you put there.

The code side is already handled: the registry/dictionary conformance tests
reported the half-applied vocabulary rename precisely, in both directions, which
is exactly what they are for.

## FIXED — `pto_update_store` leaves `PtoRowCount` at the old value

> **Fixed 2026-08-10, entry moved to the changelog.** Not lost to a concurrent
> write this time: the removal *was* the fix landing. Implemented as the entry
> proposed — `uint_elem_fixed` at 8 octets, the offset recorded in the slot on
> write **and on parse** (the `tttr sm` re-run is two processes, so the patch
> has to work on a reopened file), patched in `pto_update_store`. Two adjacent
> holes closed with it: a relocating `update` and `compact` both rewrote object
> headers without `PtoRowCount` at all. Tests:
> `test_pto.py::test_an_update_corrects_the_row_count_the_header_claims`,
> `::test_compact_keeps_the_row_count`. This stub can be deleted once both
> sessions have seen it.

## FIXED — Local macOS builds compile against conda's HDF5 headers and link Homebrew's library

> **Fixed 2026-08-10, entry moved to the changelog** (removal = fix landed, not
> a concurrent-write loss). As the entry proposed: with a conda env active and
> no explicit `HDF5_ROOT`, the configure now pins `HDF5_ROOT` to
> `$CONDA_PREFIX` whenever that prefix ships `H5public.h` and disables the
> config-package path, so headers and library come from the same prefix. An
> explicit `HDF5_ROOT` still wins. Verified: fresh configure resolves both
> halves to the conda prefix at 1.12.2, `build_new` relinks without the
> Homebrew dylib warning, and the two named tests pass without
> `HDF5_DISABLE_VERSION_CHECK`.
>
> **Same day, the mirror image bit chisurf:** an install into `envs/arm64`
> built with `HDF5_ROOT` hardcoded to *base* mambaforge linked base's
> `libhdf5.200` — an install name the env cannot resolve, so `import tttrlib`
> died at chisurf startup. A cross-prefix HDF5 inside a conda env is now a
> configure `FATAL_ERROR` (override: `TTTRLIB_ALLOW_HDF5_PREFIX_MISMATCH=ON`;
> conda-build exempt). This stub can be deleted once both sessions have seen
> it.

## FIXED — `pch_mixture` indexes `avg_numbers` by the length of `brightnesses`, and reads past the end

> **Fixed 2026-08-10** (removal = fix landed, not a concurrent-write loss).
> As the entry prescribed: mismatched lengths now throw
> `std::invalid_argument` naming both sizes, surfacing as `ValueError` — the
> same shape `sample_from_cdf` uses. The entry's repro raises instead of
> returning the stable-but-wrong one-species histogram.
> `test_pch_fida.py::test_mixture_rejects_mismatched_species` pins both
> directions of the mismatch and that equal lengths are untouched. ChiSurf's
> `strict=True` zip in front of the delegation can now be dropped. This stub
> can be deleted once both sessions have seen it.

## A `background` with no `background_decay` puts every background photon in micro-time channel 0

**2026-08-10.** Not an engine defect — the mechanism exists and this is its
*default*, which is the part worth arguing about.

`SimEngine` draws a background photon's micro time from the optional
`background_decay` pattern, and writes **channel 0** when the config does not
declare one (`SimEngine.cpp`, `uint16_t micro = 0`). So a config with
`"background": [0.02, ...]` and no `background_decay` produces:

```
$ tttr sim <config without background_decay> --channels 4 -o s.spc
>>> np.bincount(micro[rout == 0], minlength=4096)[:4]
array([19557,   330,   319,   302])      # 13.6% of the channel, all in bin 0
```

Uncorrelated background in TCSPC is **flat** in micro time; a spike at zero is
what *scatter* looks like. So the default silently models the wrong thing, and a
lifetime fit on such a file fits a large fake scatter component — the channel's
mean micro time comes out 2.91 ns where the emission is 3.37 ns.

Declaring the distribution fixes it, and is what
`examples/simulation/configs/mfd_2col_2pol.json` now does:

```json
"background_decay": {"pattern": [1.0, 1.0, ...], "dt": 0.008}
```
```
>>> np.bincount(micro[rout == 0], minlength=4096)[:4]
array([328, 338, 322, 306])              # flat, as uncorrelated background is
```

**The open question is the default**, and it is a decision rather than an
investigation: an unconfigured background is more honestly *flat over the laser
period* than a delta at zero, because flat is what the physics is and zero is a
different physical claim. Changing it would move every existing result that used
a background without a decay, so it needs a deliberate call — but until then,
every config that sets `background` should set `background_decay` too, and
nothing warns when one is given without the other.

**2026-08-10, the warning half is done** (the default is untouched — that
decision stays open). `SimEngine::from_json` now prints a `WARNING:` to
`std::clog` when `background` has any nonzero rate and `background_decay` is
absent, naming the consequence (a lifetime fit reads the spike as scatter) and
the flat-pattern cure. Verified through `tttr sim` in both directions;
`test_config_trajectory.py::test_background_without_decay_warns` pins it.

## Not a bug, recorded so it is not re-derived: `.pto` round-trips CLSM markers exactly

`Leica_SP5.ptu` packed into a `.pto` and read back through the container gives
byte-identical event types, marker routing counts and `CLSMImage` geometry:

```
event_types  {0: 6596261, 1: 118288}   markers {1: 59133, 2: 58924, 4: 21, 6: 210}
CLSMImage    n_frames=1 n_lines=7921 n_pixel=256 counts=443139     # both
```

The `no complete frames; salvaging 1 frame(s) with 7921 line(s)` warning that
comes with it is **not** a container problem — the raw `.ptu` produces it too.
It is a marker-configuration question in `CLSMImage` (this file's frame marker
appears 21 times and is not being used), and it makes the intensity image of a
standard fixture a 256×7921 stripe instead of 31 frames of 256×256.

---

## FIXED — `Column.numpy()` hands out a view that does not keep the `DataStore` alive

> **Fixed 2026-08-07.** Both halves of the suggested fix, because the first
> alone does not close it:
>
> * The owner is now an object that is **not** an ndarray
>   (`_DsBuffer` in `ext/python/datastore_support.py`), so numpy's chain
>   collapse stops at something that owns the buffer. The report is right that
>   the collapse then works in the library's favour: the root of every derived
>   view is the owning object.
> * **Every** way of getting a column carries the store, not just the two that
>   went through `DataStore.py`. `store.column(0)` and
>   `store.column_by_name("x")` are wrapped C++ with no link back at all, and
>   still dangled with the first half in place — they now get the same
>   `TTTRLIB_DS_KEEP_ROOT` append as `group`/`add_group`/`ensure_group`.
>
> The owner is the `Column` proxy rather than the `DataStore` the report
> suggests, and reaches the store through `Column._store`; with the second half
> above that link now always exists, so the chain
> `array → _DsBuffer → Column → DataStore` holds for every accessor.
>
> The reproduction below is a test —
> `test/python/test_datastore_paths.py::test_a_csv_column_survives_the_store_it
> _was_read_from`, run eight times as filed — beside one per accessor and one
> asserting the root of a collapsed chain still owns the buffer.
>
> Zero-copy is unchanged: writing through `np.asarray(col)` still reaches the
> C++ buffer.
>
> Kept here rather than deleted because the analysis is the useful part, and
> the same trap is one `ARGOUTVIEW` away in any other binding.

**Found:** 2026-08-07 · **Severity:** silent wrong data · **Affects:** `DataStore`
(any store, `.dstore` and CSV-read alike) · **tttrlib 0.27.0, macOS arm64,
Python 3.12, numpy 2.x**

`Column.numpy()` returns a zero-copy view into the column's buffer — which is
the point of a store, and is documented as such. What is missing is the
ownership link: **no object in the returned array's base chain owns the memory
or references the store**, so the array is a dangling pointer the moment the
`DataStore` is collected.

It does not raise. It returns plausible numbers with occasional wrong ones.

### Reproduction

```python
import gc, tempfile, pathlib, numpy as np, tttrlib

d = pathlib.Path(tempfile.mkdtemp()); p = d / "t.csv"
expected = np.arange(1000, dtype=float) * 3.0
p.write_text("a\tb\n" + "".join(f"{v}\t{v * 2}\n" for v in expected))

def read():
    store = tttrlib.read_csv(str(p), delimiter="\t")
    # np.asarray, not np.array: with a matching dtype this does NOT copy.
    return {store[i].name(): np.asarray(store[i].numpy(), dtype=float)
            for i in range(store.n_columns())}          # <- store dies here

got = read()
gc.collect()
print((~np.isclose(got["a"], expected)).sum(), "values wrong")
```

Eight runs of exactly that: `1, 1, 0, 0, 1, 1, 1, 1` values wrong. Always at
**row 2**, which the file gives as `6.0`, read back as either `0.0` or
`6.001000000000001e-05` — a partially overwritten double, i.e. reused memory.

Downstream, the same defect read **84 of 154 rows** of a burst table's
`First Photon` column as `3.3e-319` instead of `2755`.

### What the base chain shows

```python
x = store[0].numpy()
y = np.asarray(x, dtype=float)      # matching dtype

x.flags["OWNDATA"]        # False
x.base.flags["OWNDATA"]   # False   <- nothing in the chain owns the buffer
x.base.base               # None
np.shares_memory(x, y)    # True
y.base is x               # False
y.base is x.base          # True    <- numpy COLLAPSES the chain
```

The last line is why the bug is intermittent rather than constant. numpy
shortcuts a view-of-a-view to the root, so a derived array does not even keep
the array it was derived from alive — and since the root does not own the
memory either, there is nothing anywhere holding the store. Whether a given
expression corrupts is then down to allocator timing, which is the worst
possible failure mode: `store[i].numpy()` alone looked correct in every trial,
and `np.asarray(store[i].numpy(), dtype=float)` — the same memory, one extra
temporary — corrupted in six trials of eight.

### Ruled out

* **Not a parser race.** With `threads=1`, and with the array copied
  immediately, 25 threaded reads and 10 single-threaded reads of the same file
  gave zero wrong values. The data written into the store is correct.
* **Not specific to the CSV reader.** It is a property of `Column.numpy()`; the
  reader only makes it easy to hit, because the store is usually a temporary.
* **Not the documented `Column`-proxy invalidation.** That is about a *proxy*
  going stale across a structural change, and is worked around by re-fetching.
  This is the *array*, after the store is gone, with no structural change at
  all.

### Suggested fix

Give the returned array an owner: set its `base` to the Python object that keeps
the store alive (the SWIG proxy for the `DataStore`, not for the `Column` —
the column is itself borrowed), so the buffer cannot outlive its owner. numpy's
chain-collapsing then works in the library's favour rather than against it,
because the root of every derived view is the owning object.

Until then the contract is "copy or keep the store", and it has to be *said* —
the current docstring advertises the zero-copy view without the lifetime that
makes it safe.

### Workaround in use downstream

`np.array(..., copy=True)` for anything that outlives the store, plus a test
asserting the arrays survive their store. Note that `np.asarray(x, dtype=...)`
is **not** a copy when the dtype already matches, which is exactly how this got
into shipped code.

**No longer needed.** `chisurf/core/datastore.py:207` carries the warning and
the copy rule; both can go once the downstream pins a tttrlib with the fix.
The copy is not free — it is the one on the largest array in the process.

---

# Coverage gaps

Not defects. Places where something works and is verified in **one** language,
recorded because "it compiles" is not "it passes" — and the R runner proved
that distinction on 2026-08-07, failing six of eight new conformance cases that
Python had green.

## CSV options are not in the conformance suite, in any language

`test/conformance/cases/csvfile.json` has three cases and all three go through
default options:

```python
tttrlib._write_csv_native(path, store, tttrlib.CsvWriteOptions())
tttrlib.read_csv_into(store, path, tttrlib.CsvOptions())
```

So the round trip, the digits and the column order are pinned in four
languages, and **every knob is pinned in Python only**:

| Not covered cross-language | Added |
|---|---|
| `nan_rep` — what a `NaN` is written as | 2026-08-07 |
| `metadata` / `comment` — the JSON Lines block | 2026-08-07 |
| `na_rep`, `true_string`, `false_string` quoting | earlier |
| `quoting`, `float_precision`, `float_decimals` | earlier |
| `na_values`, `text_columns`, `use_float32` | earlier |

Why it matters here specifically: the metadata block is the one CSV feature
whose *point* is that another program reads the file. A binding that built the
options struct wrongly would write a file this library reads back perfectly and
nothing else does — which is exactly the failure that has no local symptom.

**What closing it looks like.** The op signatures are the work, not the cases:
`csvfile.write` and `csvfile.read` take no options today, so they need an
options argument that four runners each build. Once they do, one case per knob
is cheap. Worth doing when the next CSV option lands rather than as its own
task — the ops only need generalising once.

Nothing is known to be wrong. This records that nothing is known to be right
either, outside Python.

---

# Enhancements

Not defects — things a downstream migration needs and cannot express today.
**Rewritten 2026-08-07 from evidence rather than prediction**: the first version
of this list was written before migrating any consumers, and the migration
disagreed with it. What follows is what actually cost time.

## Landed since this list was first written

`concat` / `append_rows` / `append_columns`, `take` / `compact`, the column
lifetime fix, `container_read_records` / `container_read_events` /
`decode_records`, na-ranges and column descriptions.

**And, since this list was rewritten: the blocker below is closed.** A `Column`
now supports `col[i]`, `col[a:b]`, `col[mask]`, `list(col)`, `col[i] = x` and
all six comparisons; `DataStore.copy()` exists in C++, so all four bindings have
it. Two things came out of implementing it that the proposal did not have:

* **`column == value` was not a missing feature, it was a wrong answer.** It
  returned SWIG's identity `False` rather than raising, so a selection built
  from it matched nothing and said nothing. `>` and the other three orderings
  raised a `TypeError` and were never dangerous. That reordered the work.
* **The proposed `self.numpy()[key]` cannot be implemented literally.** For a
  text or bool column `numpy()` is a *copy*, so an integer index would decode
  the whole column to read one row — measured at 38 s per thousand accesses on
  200 000 rows against 0.6 ms for the routed form. An integer key goes through
  `string_at` / `value_at`; only a slice or an index array goes through
  `numpy()`.

What remains from the list below: a readable spelling for row selection,
group-by over a dictionary column, `argsort` / `sort_by`, and
`rename_column` / `insert_column(position)`.

**The prediction was half right.** `concat` *was* the item that changed the
shape of the migration — but only for the **file layer**. With it, one downstream
plugin (burst fusion: core, driver and view-model) went from frames to stores
end to end, and the burst reader now returns a store. That half is done and it
worked as argued.

**It was wrong about the consumer layer**, which is where the remaining cost
actually is, and the blocker there was not on the list at all.

## ~~The blocker that matters now: a `Column` is not array-like~~ — CLOSED

*Kept for the record; this is what the migration hit.* At the time,
`np.asarray(column)` and `len(column)` worked and nothing else did:

```python
column[0]          # TypeError: 'Column' object is not subscriptable
column[1:3]        # TypeError
column > 1         # TypeError: '>' not supported
list(column)       # TypeError: not iterable
```

So every consumer that touched `frame[name]` as a value has to be rewritten to
take `np.asarray` first — not because the arithmetic changes, but because the
*handle* does not behave like the thing it replaced. Counted across the files
still holding frames in the downstream package:

| Idiom that needs a column to behave like an array | calls | files |
|---|---|---|
| `col.to_numpy(...)` | 31 | 10 |
| `col[i]` / `col[a:b]` | 17 | 4 |
| `col > x`, `col == x` (building a mask) | 5 | 2 |
| `col.map(fn)` | 3 | 2 |

That is ~56 mechanical edits whose only purpose is to insert a conversion. Every
one of them is a place a reader will later ask "why is this wrapped?".

**Element access and comparison would remove almost all of it.** A column that
supports `__getitem__`, `__len__`, `__iter__` and rich comparison returning a
bool array is the difference between "swap the reader" and "rewrite every
consumer". `map` is not needed — `np.asarray(col)` plus a comprehension is
honest — but indexing and comparison are used everywhere and have no readable
substitute.

## After that, in the order they were hit

| Operation | calls | files | Note |
|---|---|---|---|
| ~~**`DataStore.copy()`**~~ | 28 | 13 | **DONE.** The copy constructor did this already and nobody could find it; it is now a named method in C++, so all four bindings have it. |
| **row selection returning a store** (`loc`/`iloc` shaped) | 36 | 8 | `take`/`compact` cover it; what is missing is a *readable* spelling at the call site. |
| **group-by over a dictionary column** | 6 | 5 | Unchanged from the first list. |
| **`argsort` / `sort_by`** | 4 | 2 | |
| **`rename_column`, `insert_column(position)`** | — | — | `insert(0, "source", …)` prepends a provenance column before writing; `add` appends only. |
| **`to_numeric(column)`** setting the mask rather than raising | — | — | Mostly evaporated: the CSV reader already types columns, so what is left is coercing text that arrived from elsewhere. |

## What the migration confirmed about the file layer

Worth recording because it was the argument for all of this, and it held:

* an `int32` column survives an **outer join** where a frame must widen to
  `float64` to hold the `NaN` and cannot recover the dtype;
* columns line up **by name**, which is what a burst folder needs — two runs
  need not have written them in the same order;
* a dtype conflict is **refused and named** rather than promoted silently;
* ranged reads compose: `container_read_records` + `decode_records` from record
  0 with a carried state gave macro times **identical** to a whole-file read on
  a 174 438-event SPC-130 file.

## `.dstore` specifically

Nothing missing for the migration: `save_store` / `load_store` already keep
column order, dtypes, dictionary-encoded text, validity masks, labels, the group
tree and the row selection. Two notes from using it:

* **It is the right default for anything only this ecosystem reads** — measured
  downstream at a wash against uncompressed HDF5 on bulk I/O and dramatically
  faster than compressed. What keeps HDF5 in the picture downstream is that the
  burst and imaging files are *interchange* formats read by other programs.
* **The column-lifetime defect above is fixed**, and `.dstore` was where it
  would have bitten hardest: `load_store` is exactly the call whose result a
  caller lets go of after pulling arrays out of it.

---

# Proposal — give `Column` the array protocol

A concrete form of the blocker above, because "make it array-like" is not
actionable on its own and the interesting part is what it should do about
masks and text.

## The change

Four dunders and rich comparison, all delegating to the buffer the column
already exposes:

```python
class Column:
    def __getitem__(self, key):        # scalar for an int, ndarray otherwise
        return self.numpy()[key]

    def __setitem__(self, key, value): # numeric only -- see below
        ...

    def __iter__(self):
        return iter(self.numpy())

    # __len__ already exists; __array__ already works.
    # __eq__ __ne__ __lt__ __le__ __gt__ __ge__ -> np.asarray(self) OP other
```

Nothing new is computed: `numpy()` is a zero-copy view for a numeric column and
already materialises a text one. This is a *handle* change, not a data change.

## Why it is worth more than it looks

It is the difference between "swap the reader" and "rewrite every consumer".
Measured on the package migrating onto `DataStore`: **~56 call sites** exist
purely to insert a conversion — 31 `to_numpy`, 17 element accesses, 5 mask
comparisons, 3 `map`s — and every one is a place a later reader asks why the
wrapping is there. The arithmetic around them does not change at all.

## Three decisions worth making deliberately

**1. Should element access honour the validity mask?** Today `numpy()` ignores
it: a masked row still returns its stored value. Measured — a column with
`set_mask([1,0,1])` returns `[1., 2., 3.]`, and the `2.` is not a measurement.

The consistent answer is that the *array protocol* returns what is stored and
says nothing about validity, exactly as `numpy()` does, and that "value or
missing" stays an explicit question (`valid(i)` / `mask_numpy()`). The
alternative — `col[i]` returning `NaN` where masked — cannot work for an
integer or text column without changing its dtype, which is the whole reason
the mask exists. **Recommend: no masking, and say so in the docstring**, since
the silent-wrong-answer risk is real and one sentence removes it.

**2. Should `col[i] = x` write through?** The numeric view is writable, so
delegation works for numeric columns and *silently loses the write* for boolean
and text ones, which decode through a copy. A write that vanishes is worse than
one that refuses. **Recommend: implement `__setitem__` for numeric dtypes and
raise `TypeError` naming the dtype for boolean and text**, pointing at the
dictionary/`set_bool` route.

**3. What does comparison return for a text column?** `np.asarray` on a
dictionary column gives an object array of Python strings, so `col == "m000.spc"`
gives an elementwise bool array — which is what a caller wants and what the
frame did. It also decodes the whole column, so it is O(n) in Python. That is
acceptable for a comparison, and worth a note: a caller filtering a large text
column repeatedly should compare `codes()` against a dictionary index instead.

## What is deliberately *not* asked for

`map`, `isin`, `groupby` on the column. `np.asarray(col)` plus a comprehension
or `np.isin` is honest, reads fine, and does not grow a second table API inside
the column. The gap being closed here is the *protocol* a numpy user already
expects, not a dataframe surface.

---

# ~~Proposal — `write_csv` should say what a `NaN` is written as~~ — DONE

**Shipped as `nan_rep`.** Two defects were found while adding it and fixed in
the same change: the null/true/false texts were written unquoted, so an
`na_rep` containing a delimiter produced a file that did not read back; and
`quoting="never"` raised a bare `KeyError`. One thing the proposal got wrong:
it assumed `nan` round-trips as a NaN value. It does not — `nan` is one of the
reader's default `na_values`, so both spellings come back masked, and the
option is about what *other* programs read.

A second concrete request, from the same migration. Smaller than the array
protocol and it removes a whole class of workaround.

## The gap

`na_rep` controls what an **invalid** (masked) value is written as. It says
nothing about a float `NaN`, which is a *value*, so it goes out as the text
`nan`:

```python
store_from_arrays({"x": np.array([1.0, np.nan, 3.0])})
write_csv(None, s, na_rep="")      # -> "1\nnan\n3\n"
```

A frame's writer produces the empty field for both, and these files are read by
programs that were written against that. So a caller wanting the old text has to
**mask every non-finite float before writing**.

## Why that workaround is worse than it looks

It is not the cost — masking 12 columns of 500 000 rows is 19.5 ms against a
732 ms write, 3%. It is that **the mask is part of the table**, so doing it in
place means *writing a table changes it*:

```
before write: has_mask = False,  valid(1) = True
after  write: has_mask = True,   valid(1) = False
```

That shipped in the downstream package and was found only by measuring this. It
is fixed there by copying the store before masking — which is a whole-table copy
on every CSV write, to express one formatting choice.

## The change

```python
write_csv(..., nan_rep=None)   # None: as now, the shortest text ("nan")
                               # "":   the empty field, what a frame writes
                               # any:  that text
```

Independent of `na_rep`, because the two are genuinely different questions: a
masked cell says *not measured*, a `NaN` says *the number is not a number* —
a fit that diverged, a ratio with no denominator. The store keeps them apart on
purpose, and CSV has one blank field for both, so the writer is exactly the
place the caller has to be able to choose.

Suggested default `None` (unchanged), so no existing file changes.

## Why not solve it downstream

It is solved downstream, and the fix is a full copy of the table per write. The
information needed — "this float is NaN" — is already in the writer's hands as
it formats each value.

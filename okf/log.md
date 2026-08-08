# Bundle update log

## 2026-08-08

* **Replacement**: Replaced the click-based `bin/tttrlib` Python runner with a
  compiled C++ CLI, `tttr`, living under `modules/cli` (cxxopts + cxxopts, built
  via `tttrlib_add_module`, installed flat to `bin/tttr` + `lib/libtttrlib`). The
  dispatch table in `cli_main.cpp` is a six-line switch; each subcommand owns its
  own cxxopts schema in `modules/cli/src/cmd_*.cpp`.

* **Progress, two layers**: A general progress ticker now lives in the library at
  `modules/util` (`ProgressTicker`, `ProgressEvent`, `ProgressSink`) — see
  [/design/tttr-cli-progress.md](/design/tttr-cli-progress.md). It does no I/O; the
  CLI (`cli_progress.cpp`) installs one sink that fans each event to a tty bar on
  stderr and to machine-readable JSONL (`--progress CHANNEL`), so a GUI can build
  `cli -> progress -> gui`. `tick` events are throttled to 60 ms in the library;
  `begin`/`finish` and the final tick are never dropped. The lifecycle bug in
  `cmd_convert` (no `set_total`/`finish`, so events printed `total=0` and never
  closed) is fixed.

* **Detector setups, chiSurf-compatible**: The instrument definition is pure JSON
  (no compiled-in detector knowledge — the binary only routes routing channels).
  `detector_setup.{h,cpp}` read/serialize the chiSurf `detector_setups.json`
  schema; unknown chiSurf keys are accepted and ignored. `sm` and `image export`
  share `--setup FILE --setup-name NAME --detector NAME`; a single detector's
  `chs` filters `sm`, and per-detector `chs` become per-TIFF groups in image
  export. Authoring is terminal-based: `tttr detectors FILE --add [--name N]`
  prompts for detectors/gates/windows and writes a chiSurf-shaped file, setting
  `last_used`. (If chiSurf has migrated setups to MMFDB, export a JSON file for
  the CLI.) Full contract in [/specs/tttr-runner.md](/specs/tttr-runner.md).

* **PTO CLI fixes**: `cmd_image` and `cmd_pto` both shipped `argc -= 2; argv += 2`
  after stripping the subcommand, which made cxxopts read the input file as the
  program name — the positional was silently dropped. Now `argc -= 1; argv += 1`.
  `cmd_pto extract` disambiguates `FILE OBJECT DIR` (one object) from `FILE DIR`
  (extract-all into a directory) by treating a stoull-parseable or file-known
  uid/name as OBJECT.

* **Install caveats (not code bugs)**: `cmake --install` is blocked by
  `include/tttrlib` being a symlink into the repo; the workaround is to copy
  `bin/tttr` and `lib/libtttrlib.dylib` into the prefix by hand. The binary's
  `@loader_path/../lib` RPATH resolves from the conda env without
  `DYLD_LIBRARY_PATH` (verified). `tttr convert` cannot write PTO — `TTTR::write`
  has no PTO writer and emits a corrupt file; build PTO fixtures via the Python
  bindings. The LSP (clangd) in this workspace reports phantom
  "pp_file_not_found"/"no member named value in std::" errors because the include
  path isn't fed to the language server — the real targets build clean.

## 2026-08-07

* **Fold-in**: Moved the photon-simulator implementation plan from the
  gitignored `PLANS/` directory into [design/](/design/plan-005-photon-simulator.md),
  next to its PRD. `PLANS/` is gone; the `.gitignore` line that excluded it is
  gone too, for the same reason as `PRDs/` before it.

* **Reorganization**: Sorted the bundle into subfolders and folded the PRDs in.
  The flat layout (ten files at the root) became thematic directories —
  `bindings/`, `testing/`, `design/`, `handover/`, `prds/` — with `index.md` and
  this log staying at the root. The 25 PRDs that lived as untracked,
  `.gitignore`d planning state under `PRDs/` are now `prds/` and version-
  controlled alongside the rest of the bundle, because a PRD that ships is
  knowledge that belongs with the source, not local scratch. The `PRDs/` line
  left `.gitignore` for the same reason. Every internal link was updated for the
  move.

## 2026-08-06

* **Creation**: Added [Test workload tiers and the fast lane](/testing/test-workload-tiers.md).
  The Python suite is now tiered by measured cost (`slow`, `heavy`, `smoke`) and
  selected with `--lane fast|standard|full`, cutting the working run from 33
  minutes to 1.7 at the cost of 2% of the tests. The note records the two things
  that are not guessable: cost is concentrated to an extreme degree — 29 of 1833
  tests are 85% of the runtime, and the biggest file is in `clsm`, not in the
  simulation groups everyone names — and a first, cold-cache measurement inflates
  I/O-bound tests up to 20x, which put 39 wrong markers in before the audit
  caught it. Also records that `MODULE_TEST_GROUPS` had drifted in both
  directions and is now derived from the module `CMakeLists.txt` instead, and
  that three `TEST_DIR` declarations pointed at directories that do not exist.

* **Initialization**: Established this bundle. The repository root already held a
  plain file named `okf` — a Fiji-plugin handover note from 2026-08-04. Creating
  the directory needed that name, so the note moved to
  [Fiji plugin — where to continue](/handover/fiji-plugin-handover.md) and gained the
  frontmatter OKF conformance requires. Its body is unchanged.
* **Creation**: Added [tttrlib for JavaScript (Node.js)](/bindings/javascript-binding.md),
  covering the fourth language binding committed as `b4e8e7d0` — a Node-API addon
  wrapping the full Python surface, generated by SWIG from the same interface
  fragments as Python, R and Java.
* **Creation**: Added [SWIG Node-API backend traps](/bindings/swig-node-api-traps.md).
  Five behaviours that each cost real time and none of which announces itself:
  typemap locals silently dropped for template-generated classes, multi-pattern
  typemaps attaching locals to the last pattern only, the preprocessor reading
  backticks and the macro-end directive inside comments, `SWIG_exception_fail`
  being unusable in an `%extend` body, and `%feature("unref")` receiving `arg1`
  rather than `smartarg1`.
* **Creation**: Added [shared_ptr for the Node-API backend](/bindings/shared-ptr-design.md).
  Records that transcribing SWIG's own `boost_shared_ptr.i` compiles cleanly and
  fails on every method call, because the backend's generated constructor
  hard-codes the stored pointer and its type — and what the holder-table design
  does instead, including the null-deleter limitation it inherits.
* **Creation**: Added [PTU web viewer](/bindings/ptu-webapp.md), the reference
  application, with the measured results on three file formats.
* **Update**: Recorded the verified state in
  [What is still open](/bindings/open-items.md) after the `std::map` conversion compiled
  and the suite went green (16 suites, ~60 tests, `--expose-gc`). Two failures on
  the way are kept there: `SwigValueWrapper<T>` has no `begin()`, and the first
  map test passed vacuously because a missing feature and a correct one both
  satisfy a negative assertion.
* **Creation**: Added [What is still open](/bindings/open-items.md). Test coverage is ~60
  functions against Python's 1507; only macOS arm64 has been built; the CI job
  has never run; there are no prebuilt binaries; the copy-fallback path has never
  executed; nothing has been run under a sanitiser.

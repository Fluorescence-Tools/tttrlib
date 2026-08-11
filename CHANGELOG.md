# Changelog

## [Unreleased]

### Added
- **`tttr.write("run.pto")` — a `.pto` is now a TTTR *sink*, not a wrapper**
  (PRD-034). A photon stream goes in as its own four columns — `macro_time`
  u64, `micro_time` u16, `routing_channel` i8, `event_type` i8 — with no
  vendor file inside, and comes back with its clocks and bin count. `can_write`
  is true for PTO for the first time. Verified as a bit-identical round trip
  (same values *and* same dtype) on a 870 161-event imaging PTU, an SPC-130 and
  an 11 605 946-event HT3: the sink stores decoded events, so it is not
  PTU-shaped. `test/python/test_pto_write_native.py`.

  A second write **appends** rather than replaces — `write("run.pto|green")`
  names the object, so one container holds several measurements, one object
  each. Discarding what a container already held is a deletion nobody asked
  for, not a write.

  `FileFormat` gained `write_from`/`write_context`, the mirror of the existing
  `read_into` hook, plus `IORegistry::set_writer`, which sets `can_write` with
  it so the flag cannot outlive the writer. `TTTR::write` dispatches through it
  **before** the record-type validation, deliberately: a container storing
  decoded columns has no record type, and demanding one would refuse a write
  that is perfectly well defined.

  Still open in PRD-034, and stated in the spec so a file relying on any of it
  stays conforming: full header fidelity (only the three required tags are
  written, so imaging `ImgHdr_*` do not yet ride through and a CLSM image does
  not reconstruct from a native table), incremental checkpointing for a file
  still being measured into, and demanding a selector for a multi-object
  container instead of stacking.

- **The Python binding releases the GIL around every wrapped call** (SWIG
  `-threads`). A long correlation, burst search, file read or fit no longer
  freezes every other Python thread — a GUI heartbeat, a progress bar, a
  second reader all keep running, which is what the "non-blocking" rule in
  BUGS.md asks of the Python half. Typemap code still runs with the GIL
  held; the two `%extend` methods whose C++ bodies call the Python C-API
  are pinned `nothread`; and the pre-existing per-method `TTTRLIB_NOGIL`
  macro no longer takes its own guard — SWIG inserts its release inside
  `$action` even in a custom `%exception`, so a second release there is a
  fatal Python error (found as an abort in the `TTTR` constructor; the
  RAII guard is now `PyGILState_Check()`-tolerant as well). Verified by the
  heartbeat test (`test/python/test_gil_release.py`), inspection of the
  generated wrapper's exception path, and the full fast suite (2501
  passed).

- **`benchmarks/bench_ad_scaling.cpp`** — how far the AD advantage actually
  goes. Every conversion decision in PRD-010 was taken at N ≤ 18, where AD beats
  tuned central differences by 4–10×. That is a statement about a parameter
  count, not about AD. Measured out to 200 exponentials (N = 400): the advantage
  **peaks at ~13× around 8–16 exponentials and decays to 3.3× at 200**. Central
  differences stay near-linear (895× the objective against a theoretical
  2N = 800×) while AD goes superlinear — 257× where pure O(N) predicts ~160×,
  because a `Dual<double, GradVec<400>>` is 3.2 kB and one 1024-channel
  intermediate is 3.13 MB, far outside cache. AD still wins there, but at that
  size both are the wrong tool: for a sum of exponentials the analytic gradient
  is closed form and costs about one objective evaluation. Full table in
  `PERF.md`. Also corrects this PRD's premise that `FitNExp` is an `i_lbfgs`
  consumer — it never constructs a `bfgs`, optimising lifetimes coordinate-wise
  with Brent and profiling amplitudes out by EM.
- **`npm install tttrlib` — the JavaScript binding is packageable** (PRD-016
  M5, the last open milestone). Prebuilt binaries for linux-x64, linux-arm64,
  darwin-x64, darwin-arm64 and win32-x64, resolved by `node-gyp-build` from
  `prebuilds/<platform>-<arch>/`; no compiler, no `node-gyp` and no per-Node
  matrix, because Node-API's ABI is stable across majors.

  The work was not the packaging metadata, it was making the binary
  *relocatable*. The addon linked ~35 sibling module libraries plus HDF5 and
  libomp through absolute build-tree RPATHs, and the CMake comment claiming the
  modules "sit next to the addon" described a staging step that did not exist —
  it worked only on the machine that built it. Prebuilds are therefore built
  with `-DTTTRLIB_MODULE_TYPE=STATIC` (one 14 MB `.node` instead of 35 sibling
  libraries) and produced by `cmake --install --component js`, which is what
  rewrites RPATHs to `@loader_path` / `$ORIGIN`; a new `js` install component
  collects the remaining third-party libraries with
  `install(RUNTIME_DEPENDENCY_SET)`.

  `ext/js/pkg/scripts/prebuild.mjs` then **rejects** a prebuild whose dynamic
  dependencies reach outside its own directory, and `scripts/pack.mjs` refuses
  to assemble a package missing any of the five platforms — a partial package is
  indistinguishable from a complete one until someone on the missing platform
  installs it. New CI jobs `prebuild_js` (5 runners), `pack_js` (assemble, then
  install the tarball into a clean project and load it) and `publish_npm`
  (release-triggered, needs `NPM_TOKEN`).

  Verified on darwin-arm64: the packed tarball installs into an empty project
  and opens `bh_spc132.spc` (183657 events, macro-time sum
  `443406877425185n`), 44/44 JavaScript suites and 19/19 conformance areas pass
  against a STATIC build. The other four triples have never been built — the
  first CI run is part of the work, not a regression check.
- **AddressSanitizer over the JavaScript binding** (`asan_js_lnx`), which
  PRD-016 asked for and which the lifetime cases (a TypedArray outliving the
  object that owns its memory) need: those are use-after-free, not exceptions,
  so an ordinary run is green either way. It is a Linux job deliberately —
  preloading the ASAN runtime into Node on macOS arm64 hangs before any
  JavaScript executes, with no addon loaded, which is a platform problem.
- **`solve_tcspc_mem_fret` — the distance-axis MaxEnt inversion, p(R_DA).**
  The lifetime half (`solve_tcspc_mem_lifetime`) landed alone, and the half a
  FRET experiment actually runs MEM for stayed in ChiSurf's plugin as a second
  engine drifting against this one. The sibling now shares the engine by
  construction: one `run_mem_from_design` both solvers call, and the only
  difference is the design matrix — `tcspc_build_fi_distances` builds each
  column as the donor decay quenched at `k_FRET = (1/tau0)(R0/R)^6` (pairwise
  `e1te2` combination with the multi-exponential donor-only reference),
  mixed with the unquenched donor by the donor-only fraction. Verified
  against ChiSurf's `solve_fret_mem` to `max |Δp| < 1e-8` on the same input,
  and a decay simulated at one distance comes back as a distribution
  concentrated there (mean R 45.05 for truth 45, χ² 1.2). ChiSurf's
  `solve_fret_mem` can now delegate the way `solve_lifetime_mem` already
  does. Still open from the same entry: FCS MaxEnt as a third engine —
  closing that means exposing the engine with a pluggable design matrix.
- **`tttr sim` warns when `background` is set without `background_decay`.**
  An undeclared background decay writes every background photon into
  micro-time channel 0 — the signature of scatter, where uncorrelated
  background is flat over the laser period — and a lifetime fit on such a
  file fits a large fake scatter component. The default is unchanged (that
  is a deliberate-call decision recorded in BUGS.md); the config is now told
  what it is claiming and how to say the flat thing instead.
- **A burst search declares itself once** (PRD-032). The seven built-ins were
  described in a hand-authored JSON literal (`kBurstSearchRegistry`) and
  dispatched from a separate table in another file — two lists of the same
  algorithms with nothing keeping them in step. They had already drifted:
  `bocpd` and `coincident` were advertised with a `method` the dispatcher had
  never heard of, so calling them ran the sliding window and returned a
  plausible answer from the wrong algorithm.

  Each search now passes its description and its dispatch function to one
  `register_burst_search(descriptor, fn)` call, through the same
  `register_algorithm` path a plugin uses. The descriptor is registered first,
  so a search that is described but not runnable cannot exist. The literal is
  deleted, and `BurstSearchDispatch.cpp` is the mechanism only — it names no
  burst search and includes none of their headers.

  **Nothing a consumer reads changed**: verified against a capture of the whole
  registry taken beforehand — 0 entries removed, 0 changed, 63 added, being the
  9 descriptor fields × 7 searches. `params_schema` still carries the schema
  under the name ChiSurf, ndX and the web UI read, now alongside the
  descriptor's own `settings_schema`, and they are one object rather than two
  to keep in parallel.

- **The JavaScript binding's no-copy claim is now asserted, not described**
  (PRD-016 acceptance). `test/js/arrays.test.mjs` had one test about
  marshalling mode, and it asserted that `arraysAreZeroCopy()` returns a
  boolean — true of a build that copies everything, so it tested nothing. Two
  real cases replace it: an `ARGOUTVIEW` output (`Pda.get_amplitudes`, a view
  onto the object's live `std::vector`) is written through and read back, and
  an `INPLACE_ARRAY1` argument (`add_pile_up_to_model`) is checked for C++
  having written into the caller's buffer.

  The first asserts *against the mode* rather than for zero-copy, so it means
  something different in each build and is the first test here that does. The
  whole suite has now been run against a `-DTTTRLIB_JS_COPY_ARRAYS` binary —
  the Electron/V8-sandbox fallback path, which until now had never executed —
  and is green in both, with the two builds verified to disagree exactly where
  they should.

- **A `.pto` bundles files** — `pto_bundle_files` (`tttrlib.pto_bundle` in
  Python), `PtoFile::attach`, and `tttr pto pack` / `tttr pto add`. A folder of
  files goes into one container and comes back out of it as the same folder. The
  container already carried payloads; what was missing was a way to hand it the
  measurement as it actually arrives — an instrument file, a settings sidecar, a
  table, a note — without the caller declaring what each of them is.

  **The name proposes and the bytes dispose.** Only a file some photon format
  claims by extension is offered to the content sniffers, and the encoding
  recorded is that format's own name: an SPC-QC file bundles as `spc-qc`, not as
  the `spc-130` an extension table would have made it. Files no photon format
  claims are never sniffed — several formats recognise a container by little
  more than its record size dividing evenly, and a 40-byte PNG was cheerfully
  identified as photons until the name gated the sniff.

  A directory is bundled recursively with each object named by its path relative
  to it, so `disassemble` puts the layout back; and a `.set` is tied to the
  `.spc` beside it with `pto.sidecar_of`, because a Becker & Hickl reader handed
  the `.spc` alone silently reads half a header. `examples/tttr/plot_pto_bundle_files.py`
  bundles a measurement, reads the photon stream back **out of the container**
  without unpacking it, and takes the folder apart again.

- **`Deconvolution.h` — Richardson-Lucy and Wiener** (`modules/math`), over the
  vendored FFT. `richardson_lucy` is the Poisson maximum-likelihood restoration
  that fluorescence data actually calls for: the estimate stays non-negative and
  flux-conserving by construction, neither of which a linear filter promises.
  2-D and 3-D, with the PSF transformed once so the cost is independent of the
  kernel size. Numerically identical to the reference implementation to 1e-12
  over three PSF shapes, three iteration counts and both ranks; 1.3-2.6x faster
  per iteration.

  Biggs-Andrews acceleration is included and **off by default**, because
  measurement says what it is: a step-size change, not a better estimator.
  Thirty accelerated iterations land where four hundred plain ones do, which
  reaches the optimum in about five instead of twenty and sails past it just as
  fast -- and in Richardson-Lucy the iteration count is the regularisation.

  The "same"-mode crop offset, `(m - 1) / 2` per axis, is the compatibility
  surface: off by one and the output is the right image shifted by a pixel.
  `test_deconvolution.py` asserts it directly for symmetric and asymmetric
  kernels, because no other assertion would catch it.

- **`Cluster.h` — a k-d tree, and the two kernels HDBSCAN spends its time in**
  (`modules/math`). `KDTree` answers k-nearest-neighbour queries over a
  row-major `(n x d)` table; `core_distances(X, k)` returns the distance to
  every point's k-th neighbour, and `mutual_reachability_mst(X, k, alpha)` the
  minimum spanning tree of the graph whose weight is
  `max(core_i, core_j, d(i,j))`. Boruvka over the tree; Prim sits beside it as
  the obviously-correct kernel the fast one is checked against, and nothing
  dispatches to it. The module is in `math` because none of it knows what a
  photon is and a k-d tree over a table of doubles is wanted in several places
  at once.

  **Candidate edges are compared in distance space, not in squared distance.**
  The squared form is faster and was written first; it is wrong at the values
  this has to get right, because the threshold derives from a weight that is
  itself a square root and `sqrt(x) * sqrt(x)` is not `x`. A *tied* edge then
  reads as one unit in the last place too far and is skipped, the endpoint
  tie-break never sees it, and the kernel returns a different -- perfectly valid
  -- spanning tree. Removing the squaring also made Boruvka faster than Prim at
  every dimension measured up to thirty-two, which is why there is no
  dimension-based dispatch between them.

  **The edge order is a compatibility surface.** Mutual-reachability weights tie
  constantly — a core distance is the weight of every edge it dominates — so the
  minimum spanning tree is not unique and Boruvka and Prim would return
  different, equally valid trees. `edge_less` therefore orders by weight *and
  then by the sorted endpoint pair*, which makes the tree unique, and
  `Cluster.cpp` is compiled with `-ffp-contract=off` so that a fused
  multiply-add cannot break a tie the other way. A downstream caller keeps its
  own implementation of the same algorithm for environments without this
  library and the two are bit-identical; relaxing either rule breaks that
  silently.

### Removed
- **Eigen is no longer a dependency of tttrlib — at all.** It was a
  project-wide `FIND_PACKAGE(Eigen3 REQUIRED)`, and therefore an apt package on
  Linux CI, a Homebrew keg on macOS, a vcpkg port on Windows, and a `dnf`
  package inside the manylinux wheel builder, for the sake of two things: the
  batched GEMMs in `NeuralNet` and one struct member in `ImageLocalization`.

  Both are now tttrlib's own. `NeuralNet` uses `Mat.h`. The vectorized
  forward-mode AD gradient carried its N partial derivatives in
  `Eigen::Array<double, N, 1>` and now carries them in
  `GradVec<N>` (`modules/math/include/GradVec.h`) — a fixed-size double vector
  implementing exactly the operator set `autodiff::detail::Dual` calls on its
  `grad` member, and nothing else.

  Measured against Eigen on the real objective at its real free-parameter
  counts (`benchmarks/bench_gradvec.cpp`, new): parity at N=9, 13–16% slower at
  N=6 and N=12. Recorded rather than rounded away, and small against the
  3.95–5.44× the AD path wins over central differences to begin with. Two
  optimisations were tried and rejected on measurement (32-byte alignment,
  padding N to the SIMD width); returning a proxy from `scalar * grad`, so the
  multiply fuses with the accumulate that always follows it, is what closed most
  of the gap — and made the vectorized gradient bitwise identical to autodiff's
  own scalar `dual`.

  `QREigen.h` is unaffected and always was: it is tttrlib's own non-symmetric
  eigensolver, not Eigen.

### Changed
- **The maximum-entropy TCSPC bindings take NumPy buffers, not `VectorDouble`
  — 18–69× faster calls, no behaviour change.** `%template(VectorDouble)
  std::vector<double>` is declared library-wide, so every binding taking or
  returning one converted through the Python sequence protocol: one boxed float
  per element, **~50 ns each**, in and out. It made the wrapper, not the C++,
  set the runtime — `tcspc_shift_lamp` asked for a shift of *zero* channels
  cost 24.4 µs of the 24.8 µs a real shift cost, 98% conversion, on a
  512-channel decay. All nine MaxEnt entry points now use
  `double* IN_ARRAY1, int DIM1` in and `ARGOUTVIEWM_ARRAY1/2` out: 9.1 → 0.50 µs
  at n=64, 26.5 → 0.81 at 512, 335 → 5.55 at 4096, 1360 → 19.6 at 16384, with
  per-element cost down ~40× to 1.2–1.6 ns. Knock-on: `tcspc_quadpr_bound` at
  `n_tau = 60` goes 146 → 36.3 µs (a Python-driven 200-iteration MEM loop drops
  from 29 ms of seam to 7.3 ms) and a per-column design-matrix build goes
  1668 → 129 µs. Signatures, keyword names and defaults are unchanged, lists
  still work as input, and `test/python/decayfit` + `test_gil_release` stay at
  111 passed / 1 skipped. The rule this does **not** repeal is in BUGS.md: a
  loop stays whole in C++, and the seam is crossed once per analysis.
- **`DecayFit25` and `DecayFit26` moved onto the same bound mechanism as
  `DecayFit23`.** Both carried a thread-local `penalty` added to the objective
  in `targetf`. `DecayFit25`'s was dead — set to zero and never anything else.
  `DecayFit26`'s was *correct*, unlike `DecayFit23`'s (`-x[0]` below zero,
  `x[0]-1` above one, both positive outside the box), so this is a tidy rather
  than a bug fix — but it shared the other two problems: it duplicated a
  mechanism `i_lbfgs` already provides, and being added outside the model it is
  invisible to an analytic gradient, which sees only what the registered
  callback returns. Replaced by `set_bounds(0, 0.0, 1.0)`; the clamp in
  `correct_input` stays as the arithmetic guard.

  Verified as for `DecayFit23`: every in-range starting fraction gives an
  identical result and 2I*; a start at `f = -0.3` differs in the sixth decimal
  with the same 2I*, i.e. the same minimum by a marginally different path.
- **`gamma`'s hard clamp in `DecayFit23` is kept, deliberately.** Unlike `tau`
  it has no arithmetic failure outside its range (the model is finite at
  gamma = −0.2 and 1.5, measured), so the clamp is purely a modelling
  constraint and there is no correctness reason to remove it.

### Fixed
- **The SIMD capability constants told every binding the kernels were not
  compiled in.** `tttrlib.TTTRLIB_COMPILE_NEON` read `0` on an arm64 build
  whose NEON kernels were compiled, selected at runtime and measurably
  running at 1.87×. The value never came from the compiler that built the
  library: SWIG's own preprocessor evaluates `info.h` when it generates the
  wrapper, defines neither `__aarch64__` nor `__x86_64__`, and so froze both
  `TTTRLIB_COMPILE_NEON` and `TTTRLIB_COMPILE_AVX` at `0` **on every
  platform**. The two spellings disagreed and the wrong one was the one a
  person reaches for — `get_neon_enabled()` is a function and told the truth,
  the constant beside it did not, so anyone asking "was this built with
  SIMD?" concluded no and went looking for a build bug that did not exist.

  The macros are now inside `#ifndef SWIG`, so no wrapper generator can see
  them and no binding can read a fabricated answer; `get_neon_compiled()` and
  `get_avx_compiled()` are new functions beside the existing `_enabled()`
  pair, evaluated by the compiler that built the library. One guard covers
  all four bindings — Python, R, Java and JavaScript each `%include
  "info.h"`. `get_avx_enabled()` / `get_neon_enabled()` now route through the
  new predicates instead of repeating the macro test, so compiled and enabled
  cannot drift apart. Audited for the same shape elsewhere and there is none.
  `test/python/misc/test_capability_report.py`.

- **Python-only SWIG directives in the shared interface broke the R, Java and
  JavaScript wrappers.** `%pythonappend` is an *unknown directive* to those
  three backends, not a no-op, so wrapper generation stopped dead at the
  first one — while `pip install -e .` kept succeeding, which is why it went
  unnoticed. Introduced by the `TTTR.header` keep-alive fix, which put the
  directive in `ext/python/TTTR.i` and `ext/python/CLSM.i`, both parsed by all
  four backends. Now guarded with `#ifdef SWIGPYTHON`, the convention those
  files already used elsewhere. `tools/check_swig_multilang.sh` is green on
  all four again and is the check to run after touching any `ext/python/*.i`.
  Note the underlying lifetime hole is still open in the other three
  bindings; each needs its own equivalent.

- **`fconv_simd` / `fconv_per_simd` are deprecated aliases, and now say so.**
  They are one line each — a call to `fconv` / `fconv_per` — because the
  runtime scalar/SIMD dispatch lives inside those functions, which already
  pick the AVX or NEON kernel by CPU feature and problem size. So the `_simd`
  name promised a choice the caller does not have, and measuring one against
  the other (1.00× out to n=65536) was comparing a function with itself. The
  header marks both `@deprecated`, the docstrings no longer repeat the
  "AVX optimized, four lifetimes at once" claim that described `fconv`'s
  internals rather than theirs, and the Python bindings raise a
  `DeprecationWarning` naming the replacement. The two internal callers and
  the two SWIG wrapper bodies now call `fconv` / `fconv_per` directly, so the
  shims have no callers left in the library; they stay one release because
  both names are exported and ChiSurf may call them.
  `test/python/misc/test_capability_report.py::TestSimdAliasDeprecation`.

- **`PtoFile::disassemble` no longer writes outside the directory it is
  given.** An object name doubles as a relative path — that is the feature
  that lets ChiSurf address a container like a folder — and it was never
  checked, so a name containing `..` escaped the target. An object named
  `../victim/keep.txt` overwrote a file outside the directory and the call
  returned success with an empty `error()`. A `.pto` is an interchange
  format, so `disassemble` is exactly what a recipient runs on a file
  somebody else wrote.

  Gated at both ends, because the two guard different things: the writer
  (`emit_object` **and** `pto_add_store`, which lays down its own header and
  would otherwise have been a hole) refuses to store such a name, and
  `disassemble` re-checks **every name before writing anything**. The
  pre-pass is the point — checking inside the loop refused the hostile
  object correctly and still left the objects before it on disk, so a caller
  who saw the failure found a directory neither empty nor complete. Names
  are rejected, never sanitised: rewriting `../x` to `x` puts data somewhere
  the container did not ask for and the caller cannot predict. `\` counts as
  a separator on every platform, since a container written on Linux is
  unpacked on Windows and `\` is an ordinary filename character on POSIX.
  Legitimate nested names (`countrate_All 0.2000#30/bursts`, `a/b/c/d/deep`)
  are unaffected. Now normative in `doc/formats/pto.rst`
  (`_pto_object_names`); `test/python/test_pto_names.py`, which tests the
  reader against a container byte-patched after writing, because the writer
  will no longer produce one.

  This was the direct consequence of the previous fix for "`disassemble`
  does not create the directories an object's name implies": creating the
  parents is what made a traversing name *succeed* where it used to fail.
  That entry offered two options and the first was taken; the second —
  reject a separator — would have closed both.

- **`tttr pto extract FILE DIR` unpacks through that gate too.** It had its
  own copy of `disassemble`'s naming and loop — kept, per its comment, "so
  the progress count matches the object list" — so the library was fixed and
  the command a recipient actually unpacks a container with still wrote
  outside `DIR`. `disassemble` gained an optional per-path callback, the CLI
  passes its progress tick through it, and the duplicate is gone: one
  implementation, one gate. Extraction output is unchanged, including the
  uid-prefixed name two objects sharing one get. Covered by tests that run
  the built binary, because the defect was in the CLI and not in the library
  it links.

- **`write("out.pto|name")` no longer writes the wrong format under the right
  name.** The container was inferred from the whole string, found no extension
  on `.pto|name`, and fell back to the **source** container — so a PTU landed
  in a file literally called `out.pto|name`, with no error and `write`
  returning `True`. The extension now comes from `subfile_path()`, and a
  selector handed to a format with no objects to name (a PTU holds one
  measurement) is refused by name rather than folded into the filename.

- **`PtoFile::find(name)` returns the newest match, not the oldest.** Several
  objects may share a `(kind, name)` on purpose, since re-running an analysis
  keeps the earlier result reachable — so the most obvious call in the API
  was handing back the *stalest* analysis in the container, with nothing to
  say anything newer existed. New `find_all(name)` returns every match in
  write order for a reader that wants the history or wants to notice there is
  more than one. `objects()` returning write order is now documented as
  contract rather than left as an accident readers were quietly leaning on.
  Both rules are normative in `doc/formats/pto.rst`
  (`_pto_object_identity`), so two readers cannot disagree about which
  result a container is showing.

- **`TTTR(path).header` no longer segfaults — member proxies keep their
  owner alive.** `get_header` returns a pointer into the TTTR; on a
  temporary, the TTTR was collected at the end of the expression and the
  header proxy read freed memory — ordinary-looking Python, a hard crash.
  Every accessor of that shape now tags the owner onto the returned proxy:
  the header, the microtime linearizer, and the CLSM frame/line accessors
  (returned as tuples, so each element carries its owner).
  `test_header_lifetime.py` pins the temporary-expression case.

- **`pch_mixture` rejects a species count mismatch instead of reading past
  the end.** It indexed `avg_numbers` by `brightnesses`' length; the
  out-of-bounds read happened to hit zeroed heap, so a three-species
  argument list was silently fitted as ONE species with a stable,
  normalised, finite histogram — undefined behaviour indistinguishable from
  a correct answer. Mismatched lengths now throw `std::invalid_argument`
  naming both sizes (a `ValueError` in Python), the same refusal
  `sample_from_cdf` makes. ChiSurf's defensive `strict=True` zip in front
  of the delegation can be dropped.

- **`DecayFit23`'s hand-rolled `tau` penalty had its sign inverted over most of
  its range, and is gone.** The line read
  `fit_settings.penalty = (x[0] < kMinTau) ? -x[0] : 0.` — a term meant to push
  `tau` back above `kMinTau = 1e-3`. It only does that for `tau < 0`. Over the
  whole band `0 < tau < kMinTau` the term is *negative*, so crossing below the
  bound **improved** the objective. Measured on the real objective: stepping
  from `tau = 1.1e-3` to `9e-4` improved it by 9e-4, and the discontinuity put a
  spurious spike in the numerical gradient at the crossing (`d/dtau = 4999.5`,
  against `-0.0004` just above it).

  The bound now goes through `i_lbfgs::set_bounds`, the same soft exterior
  penalty every other bound in the fit uses. That also fixes a third problem the
  sign error was hiding: the hand-rolled term was added to the objective
  *outside* the model, so an analytic gradient over the model chain would miss
  it and be wrong by exactly `-1` in the `tau` component below the bound.
  `i_lbfgs` adds its bound penalty **and that penalty's gradient** to whatever a
  registered gradient callback returns, so routing bounds through it is what
  makes the AD conversion of these paths possible at all.

  Also: gamma's soft bound was applied only inside the branch that frees gamma,
  so the pre-fit ran under different rules than the main fit. Both bounds are
  now set once, unconditionally, before any minimisation.

  Verified end to end against the pre-change code: four ordinary starting points
  give **identical** `tau`, `gamma` and 2I* to every printed digit; starts below
  the bound and at negative `tau` still recover to the same minimum
  (2I* −924.527677 in both).

  The floors on `tau` and `rho` stay, and their role is now documented as what
  it actually is — a *numerical guard*, not the constraint. They cannot be
  removed: without the `tau` floor, `exp(-dt/tau)` overflows for `tau` in
  roughly `(-dt/709, 0)` and the model comes back non-finite. Measured at
  `tau = -1e-6`: every model bin `inf`, objective `NaN`.

  They are now **smooth** floors rather than hard clamps (`soft_floor` in
  `DecayFit.h`): exactly the identity at and above the floor, so no ordinary fit
  moves by even an ulp, and `m₀·exp((v−m₀)/m₀)` below it — C1 at the join,
  strictly positive for every finite input, and with a nonzero derivative, so
  the parameter map contributes no structural zero for an AD pass to inherit.
  Under it the failure direction flips from overflow to underflow: a wildly
  negative `tau` now gives a decay factor of `0` rather than `inf`.

  **What the smooth floor does not do, because it was worth measuring rather
  than assuming:** it does not restore a usable gradient below `kMinTau`. The
  objective is flat there because the data cannot resolve a lifetime that
  short — at `dt = 0.032` the factor `exp(-dt/tau)` is already `1.3e-14` at
  `tau = 1e-3` and underflows below — and the proof that the clamp was never the
  cause is that `d/dtau` is already exactly 0 at `tau = 1.1e-3`, *above* the
  floor. `tau_eff` keeps moving; the model stops caring. The restoring force in
  that region comes from `set_bounds`, not from the parameter map.

  A clamp is separately the wrong *constraint* mechanism because the objective
  goes flat outside a clamped box (measured: `d/dgamma` exactly 0 at gamma =
  1.0, 1.2 and 2.0) — the gap a soft bound fills. `gamma` keeps its hard clamp:
  unlike `tau` it has no arithmetic failure outside its range (the model is
  finite at gamma = −0.2 and 1.5, measured), so its clamp is purely a modelling
  constraint, and removing it is a behaviour change worth making on its own
  rather than bundled here.

  `DecayFit26`'s equivalent penalty is **correctly** signed and is left alone;
  `DecayFit25`'s is dead (always 0). Unifying those two onto `set_bounds` is
  worth doing but is not this change.
- **The Poisson likelihood paid the optimiser to drive a model bin to zero.**
  `Wcm` and `wcm_p2s` handled a model bin at or below `1e-12` by *skipping* it,
  a line commented "this is only for stability reasons". It was not stability.
  The term a near-zero bin contributes to the minimised objective is
  `-C·log(m)`, which is large and **positive** — at `C = 30` and `m = 1e-12`,
  about `+829`. Dropping it is a discontinuous *improvement* of exactly that
  size, handed out for pushing the bin one step further down, and below the
  floor the objective is perfectly flat, so nothing pulls it back. Measured: the
  objective falls **828.9** across the threshold and is identical for every
  negative model value.

  Both are now continued smoothly instead — `log` replaced by its tangent at the
  floor, which agrees in value *and* slope, so the objective is C1 across it,
  finite for every finite model value including negative ones, and strictly
  worse the further below it goes.

  **This moves two reference fits, and an earlier revision of this entry claimed
  it moved none.** That claim was wrong and the way it was wrong is worth
  keeping. Two checks were run: the new code is *bitwise* identical to the old
  above the floor (`test/cpp/test_decay_likelihood.cpp`, ten magnitudes — this
  part holds), and a sweep of 143,360 model bins across the clamped `DecayFit23`
  parameter box never produced a bin below `1.86e-07`. **The sweep used a flat
  non-zero background**, so it never explored the case the reference data
  actually is. It proved the floor unreachable for the inputs it chose, and that
  was read as unreachable in general.

  The reference fits that do reach it:

  | fit | before | after | why |
  |---|--:|--:|---|
  | `fit23` 2I\* | 23.802337 | 23.791124 | zero background, 58 photons — the tail underflows |
  | `fit23` tau | 0.74219 | 0.721353 | |
  | `fit25` 2I\* | 4.738831 | 3.887975 | the p2s path, where `wcm_p2s` discarded the **pair** whenever *either* channel underflowed |
  | `fit24`, `fit26` | unchanged | unchanged | background 0.2; the model never reaches the floor |

  `fit25`'s 0.85 shift is the largest and is unambiguous in origin: the only
  other change to that path was removing an addend that was always zero, so it
  is a bitwise no-op and every bit of the movement is the likelihood correction.

  **The old numbers are not the more correct ones.** They are the answer to a
  likelihood that silently discarded occupied bins. Re-pinned in
  `test/conformance/cases/decayfit.json` (all four languages),
  `test/python/decayfit/test_fit2x_compat.py` and `test_decay_fit_interface.py`.

  One detail worth not undoing: the multiply stays in `Wcm`'s loop body rather
  than moving inside the helper. With it inside, the compiler stops contracting
  it into the accumulate and every ordinary evaluation shifts by an ulp — a
  one-ulp drift in a change that is supposed to alter nothing above the floor,
  in functions pinned by cross-language reference tests.

  `twoIstar`/`twoIstar_p2s` are deliberately untouched: they are computed after
  the fit for reporting, never minimised.
- **Three modules declared third-party dependencies they do not use, and one
  used a dependency it did not declare.** `clsm` and `superres` asked for
  `tttrlib::eigen` (and `superres` also for `tttrlib::autodiff`) while including
  neither; `localization` asked for Eigen while actually including autodiff.
  Nothing caught it because the top-level `INCLUDE_DIRECTORIES` for
  `thirdparty/` puts the vendored headers on every module's include path
  regardless of what it declared, so `EXTERNAL_DEPS` is documentation until a
  module compiles with only what it asked for. Recorded as the remaining exit in
  `modules/MODULE-DEBT.md`.
- **The comment in `ImageLocalization.cpp` claimed a guard test that did not
  exist.** It said the vectorized gradient was "guarded by a unit test that
  compares [it] against scalar `dual`". There was no such test. There is now:
  `test/cpp/test_ad_gradient.cpp` differentiates the objective three ways —
  vectorized dual, autodiff's scalar `dual`, central differences — and requires
  agreement. This matters more than an ordinary missing test, because the
  `NumberTraits` specialization the vectorized path depends on is undocumented
  upstream: an autodiff bump that changed the contract would compile cleanly and
  silently produce wrong derivatives.
- **The burst-search registry re-ordered its parameters, and the JavaScript
  binding then passed them in the wrong positions** (PRD-032 fallout, found by
  PRD-016 M5). `TTTR::burst_search_algorithms_json` assembled the category by
  parsing `algorithms_json("burst_search")` into an `nlohmann::json` and dumping
  it again. That type is a `std::map`, so the round-trip sorted every object key
  alphabetically — including `params_schema.properties`, whose declaration order
  *is* the C++ argument order. `bayesian_blocks` went from
  `[L, m, p0, trigger_contrast, …]` to `[L, m, max_false_alarm_rate, …]`, and
  `burstSearchByName` — which must build a positional call, because JavaScript
  has no `**kwargs` — raised *"Illegal arguments for function
  burst_search_bayesian_blocks"*. Both parses now use `nlohmann::ordered_json`,
  as the rest of the registry already did.

  Python was unaffected (it calls with `**kwargs`), and the PRD-032 verification
  that reported "0 entries changed" could not have seen this: it compared parsed
  JSON, and parsing is exactly the step that discards key order. The invariant
  is now pinned by a test — `required` keeps declaration order, so it must be a
  subsequence of the property keys, which sorting breaks immediately.
- **`get_routing_channel`, `get_event_type` and `get_used_routing_channels`
  leaked their buffer on every call, in all four language bindings.** These are
  the three accessors declared `(signed char** output, int* n_output)`, and
  `signed char` was the one type in that block applied as `ARGOUTVIEW` — "C++
  owns this, do not free" — while every sibling type on the identical signature
  used `ARGOUTVIEWM`. All of them allocate through `get_array<T>`, which
  `malloc`s, so the promised owner did not exist and nothing ever freed the
  allocation: one byte per event per call. Measured at 39 MB leaked over 200
  calls on a 183,657-event file, against 0.4 MB after the fix.

  The values were always correct, which is why it survived four bindings and a
  conformance suite. `test/python/tttr/test_argoutview_ownership.py` measures
  the footprint instead, and was confirmed to fail on a rebuilt-with-the-bug
  binary (36.8 MB against a predicted 36.7) before being kept. The JavaScript
  leak check read only `get_macro_times` and `get_micro_times` — both always
  `ARGOUTVIEWM` — so it now reads the two affected accessors as well.

- **Stating the same fact twice no longer records it twice, and a fact can be
  restated.** The container's tag list was append-only, so every re-run of an
  analysis re-added its parent edge (`parents(uid)` returned the same source
  four times after three re-runs) and re-added every scalar tag, with readers
  silently taking the first. Both ChiSurf and `tttr sm` had grown the same
  read-filter-rewrite workaround — the sign the writer was missing an API.
  Three changes, matching the semantics those two call sites already agreed
  on: `add_tag` skips a tag identical in every field (two *different* parents
  are two facts and both still land); new `PtoFile::set_tag(tag)` replaces
  every tag with the same `(target, name, index)` and then appends — "the
  value IS x" as against `add_tag`'s "x is also true"; new
  `clear_tags(target, name)` removes one name from one object. `tttr sm` now
  uses `set_tag` directly; ChiSurf's read-and-skip dance can be deleted once
  it pins a tttrlib with this change.

- **A local macOS build no longer mixes conda's HDF5 headers with Homebrew's
  library.** With a conda env active and no explicit `HDF5_ROOT`,
  `FIND_PACKAGE(HDF5)` picked Homebrew's CMake config package (the library)
  while the env's include directory — already on the compile line through the
  Python, libtiff and OpenMP hints — supplied the headers of a different ABI.
  Nothing failed at build time beyond a link warning; the first Photon-HDF5
  *write* aborted the interpreter with `Headers are 1.12.2, library is 2.1.1`,
  taking the whole pytest run with it. The configure now pins `HDF5_ROOT` to
  the active conda prefix whenever that prefix ships `H5public.h`, the same
  policy the OpenMP fallback uses; an explicit `HDF5_ROOT` still wins.

  The same class of mismatch in the other direction — an explicit `HDF5_ROOT`
  naming a *different* prefix than the active env, which builds an extension
  whose `@rpath/libhdf5.*.dylib` the env cannot resolve (ImportError at first
  use) or resolves beside h5py's copy (abort at first write) — is now a
  configure-time `FATAL_ERROR` instead of a runtime surprise, with
  `TTTRLIB_ALLOW_HDF5_PREFIX_MISMATCH=ON` as the deliberate override.
  conda-build is exempt: its host prefix is the intended cross-prefix.

- **An updated store corrects the row count its object header claims.**
  `pto_add_store` wrote `PtoRowCount` once and `pto_update_store` never touched
  it, so re-running a burst search into an existing container left `tttr pto
  ls`, `pto info` and the TUI reporting the *previous* run's count (the store's
  own header was right, so readers of the payload were never misled). The count
  is now written as a fixed 8-octet element — the packed width could not hold a
  count that grew, the same trap `FileUID` hit — its offset is recorded on
  write *and* on parse, and an update patches it in place or writes it into the
  relocated header. Two adjacent holes closed by the same change: a relocating
  update and `compact` both dropped `PtoRowCount` (and the former also
  `FileMediaType`) from the headers they rewrote, so the count silently read 0
  after reopening. A container written before this change stays as it was
  until compacted: a packed count is left alone rather than corrupted.
- **A disconnected kinetic scheme is no longer rejected by `GopichSzabo::set_scheme`.**
  A repeated zero eigenvalue — the all-zero matrix that is the no-exchange
  limit a dynamic fit is compared against, or any scheme with a state that
  does not exchange — made `set_scheme` return `false` and the likelihood
  `-inf`, so an optimiser exploring towards slow exchange hit a wall exactly
  where the likelihood is best defined. The cause was in the shared QR
  eigensolver (`QREigen.h`): inverse iteration solved the same singular system
  from the same start vector for every copy of a repeated eigenvalue, so all
  copies came back parallel and the eigenvector matrix was rank deficient even
  when the true eigenspace is the whole space. Copies of an eigenvalue are now
  grouped and each iterate is orthogonalised against the vectors the group has
  already found, the way LAPACK's `dhsein` does; a residual check keeps a
  *defective* eigenvalue (a Jordan block, which genuinely has too few
  eigenvectors) failing the condition gate as before rather than being handed
  a fabricated basis. The no-exchange two-state likelihood now matches the
  closed-form mixture sum to 12 digits, and with no repeated eigenvalue the
  computation — every case that already worked, including BurstML's
  ~100-state matrices — is exactly the old one.
  `pto_add_store` wrote `PtoRowCount` once and `pto_update_store` never touched
  it, so re-running a burst search into an existing container left `tttr pto
  ls`, `pto info` and the TUI reporting the *previous* run's count (the store's
  own header was right, so readers of the payload were never misled). The count
  is now written as a fixed 8-octet element — the packed width could not hold a
  count that grew, the same trap `FileUID` hit — its offset is recorded on
  write *and* on parse, and an update patches it in place or writes it into the
  relocated header. Two adjacent holes closed by the same change: a relocating
  update and `compact` both dropped `PtoRowCount` (and the former also
  `FileMediaType`) from the headers they rewrote, so the count silently read 0
  after reopening. A container written before this change stays as it was
  until compacted: a packed count is left alone rather than corrupted.
- **`FileMediaType` is written, not only read.** It has been in the PTO
  specification and parsed on open since the format existed, and nothing but an
  embedded store ever wrote one — so every attachment came back with an empty
  media type. Objects now carry it, and `compact` carries it across.
- **OpenMP was off for the entire library on macOS.** `FIND_PACKAGE(OpenMP)`
  fails under AppleClang, which ships neither `-fopenmp` nor a runtime, and the
  top-level `CMakeLists.txt` then set `WITH_OPENMP OFF` — every `#pragma omp` in
  tttrlib became a comment and every parallel kernel ran serial, announced by a
  single `WARNING`. The configure now points `FindOpenMP` at the `libomp` that
  conda ships as a compiler dependency (and Homebrew as a keg), so an AppleClang
  build is parallel again. Measured on the clustering kernels: 8x.
- **`TTTRLIB_VEC_REDUCTION(var)` did not compile under OpenMP.** `_Pragma` takes
  a string literal and a macro parameter is not substituted inside one, so the
  emitted pragma named a variable literally called `var` and every reduction
  site failed with "use of undeclared identifier 'var'". Stringified through a
  helper macro (`modules/math/include/Mat.h`).
- **The `cli` module included `io_csv_writer.h` without declaring `io_csv`.**
  The module system grants a module only its own and its declared dependencies'
  include directories, so the undeclared edge surfaced as a "file not found" in
  `cmd_sm.cpp` rather than as a link error later.

### Added
- **`tttr sm --mle` fits one lifetime per burst per detector** (PRD-026's
  remaining half), through `fit23` over the detector's parallel and
  perpendicular arms jointly. Columns are ChiSurf's `.bg4`/`.br4` set in its
  historical order, including the two spaces in `2I*  (green)`. Verified against
  the simulation's ground truth: the 3.8 ns species comes back at **3.87 ns**
  and the 1.6 ns species at **1.71 ns**, from a bimodal distribution that
  resolves the two.

  Three decisions worth stating, because each replaced something that looked
  fine and was not:
  - **`--irf` is required; there is no default.** A prompt is a claim about the
    instrument, and a lifetime fitted against the wrong one is wrong by roughly
    its width with nothing in the output saying so. `--irf delta` is available
    and has to be typed. The synthetic prompts are `gauss:FWHM[,T0]` and
    `sgauss:FWHM[,T0[,SKEW]]` — a skew-normal,
    `exp(-z²/2)·(1 + erf(αz/√2))`, default skew 1.5, positive tailing to later
    times as a real prompt does; `T0` is the skew-normal's *location*
    parameter rather than its peak, which is how it is parameterised
    everywhere and what a fit to a measured IRF hands back. `gaussian:` and
    `skewed:` are accepted as aliases. A path is read as a measured response,
    one number per line.

    Checked by the physics rather than by parsing: the simulation convolves
    with nothing, so `delta` recovers the truth best (3.87 ns against 3.8),
    and a wider prompt takes more out of the decay, so the recovered lifetime
    falls monotonically with its width — 3.87 → 3.67 → 3.03 ns for delta,
    0.5 ns and 2.0 ns FWHM. A spec that parsed and was then ignored passes
    none of that.
  - **Only `tau` is fitted.** A burst of a hundred photons does not determine an
    anisotropy; the four-parameter fit moved the recovered lifetime by a factor
    of two on a change of start value while its 2I\* still looked reasonable.
    r0 = 0 with rho held is also the precondition for the kernel's own
    well-conditioned path.
  - **`gamma` is measured, not guessed.** It is the background fraction of the
    burst — a rate measured from the photons no burst contains, times the
    burst's duration. Left at 0 it subtracts nothing, which biased every
    lifetime up by ~50% on a 13% background.
  - The fit runs on a rebinned micro-time axis (`--mle-bins`, default 128): a
    burst is ~100 photons over 4096 raw channels, so the raw axis is almost all
    zeros and the convolution is 4096 long for no gain. 14 s → 0.26 s.
- **The detector setup's `g_factor`, `l1` and `l2` are read** instead of parsed
  and discarded. They are instrument constants and belong to the file; a
  g-factor left at 1 when the file says otherwise is a wrong number, not a
  missing one.

### Changed
- **Every burst-table column carries its unit** (chiSurf PRD-84). The
  `_mmfdb_column.units` attribute travels with the column, so a column-subset
  read gets it too, and a `Duration (ms)` stops being a millisecond only by
  virtue of its name. The rule is a port of ChiSurf's — two writers disagreeing
  about the unit of the same column is the failure the vocabulary exists to end
  — and a cross-writer test pins the two to the same answer column for column
  (40/40 on a four-detector burst table, 30/30 with PIE windows). Absent means
  *unknown*, not dimensionless: a photon index claims nothing.
- **The controlled vocabulary now comes from mmfdb, and is validated against
  mmfdb.** `okf/nomenclature/mmfdb.dic` is a *copy*, and it had drifted: it
  declared `bva`, `kde_cde`, `mle_green`, `mle_red`, `burst_fcs`,
  `hmm_photon_by_photon`, `tcspc_calibration`, `pda_histogram`, `companion_of`,
  `histogram_bin` and seven `…4` data formats — **eighteen terms mmfdb does not
  have**. The registry agreed with the local copy, the local copy agreed with
  the writer, and all three were wrong together, which is a closed loop of
  agreement that says nothing about the vocabulary the rest of the world reads
  these files in.

  New `test/python/test_vocabulary_matches_mmfdb.py` validates against
  **mmfdb's own dictionaries** — the installed package, `$MMFDB_DIC_DIR`, or a
  sibling checkout — and is the only check that can catch a term this repository
  invented, because it is the only one that reads a file this repository does
  not own. It skips loudly, naming where it looked, when mmfdb is absent.

  Reconciled: every `operation_type` is an mmfdb term. `registry_json()`'s entry
  **`name` is unchanged** — that is tttrlib's own identifier and what a caller
  dispatches on; only `operation_type`, the provenance term, moved. The existing
  `test_registry_matches_mmfdb.py` had been comparing the registry *key* against
  the vocabulary, which forces the two to be equal and is exactly how the local
  names got into the dictionary; it now compares the term.
  `data_format` is `dstore` on every entry — that is what a `.pto` artifact
  carries, where `bg4`/`bv4`/`2c4` were both non-conformant and a statement
  about what a table *is*, which is `operation_type`'s job.
  Two terms tttrlib genuinely needed and mmfdb lacked — `pda_burst_likelihood`
  and the `histogram_bin` row grain — were **added to mmfdb**, which is where a
  new term belongs.
- **Every term the container writer emits is now an MMFDB dictionary term**
  (chiSurf PRD-88). The profile defines no vocabulary of its own, so a word this
  invents is a word nothing can query — and it was inventing **four**: `bva`,
  `kde_cde` and `mle_<detector>` for operations the dictionary calls
  `burst_variance_analysis`, `burst_2cde` and `burst_lifetime_fitting`, plus
  `companion_of` for a relation `_mmfdb_edge.relationship_type` does not define
  at all (a companion is `derived_from` its burst table; that the two share a
  grain is what `row_grain` says). ChiSurf's writer checks every term before
  writing and so could never emit one; this one cannot make that check — it is
  C++, does not link mmfdb, and has no mmCIF parser — so the check now lives on
  the ChiSurf side, in `test_every_term_the_cli_writes_is_in_the_dictionary`.
- **Extending a container no longer gives its provenance graph two roots**
  (chiSurf PRD-88). `tttr sm` looked the photon stream up by **file name**, so
  writing into a container another tool had created added a *second*
  `tttr_photon_stream` whenever the name did not match, and hung the burst table
  off the root nothing else references. It reads as intact until somebody walks
  the lineage of an artifact the other tool wrote. The lookup is now on
  `_mmfdb_artifact.checksum`, so a byte-identical file under any name resolves
  to the primary already there; the name remains as a fallback for a container
  written before the checksum tag.
- **Documented the two container names.** `<name>.pto` is *the container* — an
  EBML document with `DocType "pto"` that claims nothing about its contents.
  `<name>.mmfdb.pto` is a `.pto` that **also** carries the PTO.MFDB profile.
  The profile tag goes on the stem and never on the suffix: a `.pto.mmfdb`
  would stop being recognised as a container by everything that dispatches on
  the extension, so it is read the way `.tar.gz` is. The name is a courtesy for
  people and directory listings; a reader decides conformance from
  `_mmfdb_container.profile` inside the file, and must accept a plain `.pto`.
  Normative in the profile spec, restated in `doc/formats/pto.rst`,
  `modules/io/pto/README.md`, `modules/cli/README.md` and `tttr sm --help`.
  (`modules/io/pto/README.md` had it backwards as `.pto.mfdb`.)
- **`tttr sm` writes a detector-named burst table, and it is ChiSurf's table**
  (PRD-026). The column set is generated from the `--setup`
  `detector_setups.json`, so a setup whose detectors are `green`/`red` produces
  `Duration (green) (ms)` and one whose detectors are `green_par`, `green_perp`,
  `red_par`, `red_perp` produces four sets — nothing in the binary knows what
  "green" means any more. Verified cell-for-cell against ChiSurf's
  `generate_burst_dataframe` on simulated MFD data: 23/23 columns identical for
  the two-detector setup, 37/37 for the four-detector one, 27/27 with PIE
  windows and micro-time gates. The columns that were missing entirely —
  `Confidence (sigma)`, `First File`, `Last File`, the per-detector
  `First/Last Photon (<d>)`, and the `S <window> <detector> (kHz) | lo-hi`
  window rates — are written; `--csv` now writes the whole table rather than
  five columns.
- **`.pto` burst artifacts follow the PTO.MFDB profile as ChiSurf writes it.**
  `_mmfdb_artifact.data_format` is `dstore` (it was `bur`/`bg4`/`bv4`/`2c4`,
  which name a *file* layout, not an encoding), `_mmfdb_operation.settings_hash`
  is written — SHA-256 of the canonical settings JSON, the same rule as
  ChiSurf's `_settings_hash` — and re-running with identical settings now
  replaces the artifact in place instead of adding a second one, while a
  changed search correctly writes a new one beside it.
- **The BVA and 2CDE companion tables are computed.** They were written with
  constants (`2I* = 10`, `Tau (green) = 3.8`, `Proximity Ratio Std = 0.05`,
  `FRET 2CDE = 10`) for every burst, which is indistinguishable downstream from
  a measurement. They now come from the `BVA` and `TwoCDE` classes over
  `--donor`/`--acceptor` detector streams, and are **not written at all** when
  those streams cannot be named. The two placeholder MLE tables (`mle_green`,
  `mle_red`) are gone until they are real fits.

### Fixed
- **The container held a lossy re-encoding of the measurement.** `tttr sm`
  embedded the *channel-filtered* stream, re-encoded as a `.sm` — the other
  channels gone, the vendor header gone, and nothing in the file saying so — in
  the one place the PTO.MFDB profile promises is not a re-encoding ("the
  instrument file is the truth; it goes in verbatim and comes back
  byte-for-byte"). The original file now goes in unchanged, with its real
  encoding term and a SHA-256 in `_mmfdb_artifact.checksum`, verified to round
  trip byte for byte. It is also smaller: 1.9 MiB of `.spc` where the
  re-encoded `.sm` was 4.6 MiB.
- **The MFD example setup had `green`'s polarization arms the wrong way round.**
  `chs[::2]` is parallel, and the file said `[8, 0]` while the simulation emits
  green-parallel on routing 0. Every photon count was right and every anisotropy
  derived from the file would have been inverted — the kind of error that
  changes no total and every conclusion.
- **`tttr sm` counted one photon too few in every burst.** A burst search
  returns *inclusive* start/stop indices, so a burst holds `stop - start + 1`
  photons; the burst table wrote `stop - start`, and derived its duration from
  `macro[stop - 1]` rather than `macro[stop]`, so every count, duration and
  count rate in a `.pto` burst table was slightly wrong.
- **`tttr sim` encoded the photon stream on the wrong micro-time axis.** It
  hard-coded `microtime_resolution = 0.004069` ns and `laser_period = 13.596`
  ns regardless of what the config simulated, so a 3.8 ns decay simulated on an
  0.008 ns axis read back as a 1.9 ns decay with nothing in the file to say so.
  It now takes the axis from the engine's own settings, which is the fix the
  Python `SimEngine.to_tttr` already had. `--routing-channels 0,8,1,9` maps
  simulation channels onto instrument routing channels, so the MFD mapping
  stays out of the binary.
- **The `tttr` binary in a build tree loaded whatever `libtttrlib` it found
  first, and could start on a stale one.** `BUILD_WITH_INSTALL_RPATH` was ON, so
  the build-tree binary carried only the *install* rpath — and
  `@loader_path/../lib` does not resolve in a build tree, where the library
  lands at `<build>/libtttrlib.dylib` rather than `<build>/lib/`. While that was
  the single entry it merely failed loudly and a `DYLD_LIBRARY_PATH` fixed it
  (which macOS SIP strips from anything but a shell). It became dangerous once
  a second entry was needed for `libomp` — conda's and Homebrew's both carry an
  `@rpath/libomp.dylib` install name and neither ships beside `libtttrlib` — as
  a conda prefix commonly holds a `libtttrlib.dylib` symlink into *another*
  build tree. The tool then started successfully on a months-old library, with
  the wrong subcommands and no error anywhere: `tttr sm --output x.pto`
  overwrote an existing container with the old JSON burst list. The build tree
  now gets the rpath CMake computes from what it actually linked, ordered ahead
  of any prefix, and the install rpath keeps `@loader_path/../lib` plus the
  OpenMP directory.
- **The macOS build lost OpenMP on any re-configure.** The AppleClang fallback
  set `OpenMP_CXX_FLAGS` as an ordinary variable after `FIND_PACKAGE(OpenMP)`
  had already cached it as `NOTFOUND`, so it worked exactly once, on a fresh
  cache. Touching any `CMakeLists.txt` in an existing build tree turned OpenMP
  back off — and not quietly: the compile flags survive in `CMAKE_CXX_FLAGS`,
  so the sources still emit `omp` calls and the link fails on
  `_omp_set_num_threads`. The fallback now writes the cache entries `FindOpenMP`
  documents, with `FORCE`.
- **New `test/python/misc/test_cli_sm_burst_table.py`** — the whole pipeline
  from `tttr sim` to the container, with the ChiSurf column arithmetic
  reimplemented in NumPy from its documented rules so the conformance is
  checked without ChiSurf being a dependency: column naming for three setups,
  every cell of every column, half-open micro-time gates, window columns,
  the profile tags, re-run identity, and that the companions vary rather than
  being constants. Its input is simulated, so it needs no data download.
- **New `test/tools/compare_burst_table_to_chisurf.py`** — the same diff against
  ChiSurf's *actual* `generate_burst_dataframe`, for the gap the
  reimplementation cannot cover (a rule read wrongly is read the same wrong way
  twice). Opt-in and outside the suite, like `pto_ebml_check.cpp` and for the
  same reason.
- **New `examples/simulation/configs/mfd_2col_2pol.json` and
  `mfd_detector_setups.json`** — a polarization-resolved two-colour MFD run on
  four channels with two species differing in FRET efficiency and in rotational
  freedom, and the chiSurf-compatible detector setup that reads it back as
  green/red or as four named arms. The reproducible chain PRD-026 asks for.
- **`StreamingCorrelator` was wrong from cascade 2 up, and is now exact against
  the batch correlator** (PRD-033, which declared it unusable). The cause was
  not the normalization it was attributed to: `get_correlation` read every
  cascade level at coarse lags `n_bins/2 .. n_bins/2 + n_bins - 1`, a constant,
  while the multi-tau axis it publishes requires level *b* to start at
  `x[b*n_bins] / 2^b` — 0, 8, 12, 14, 15, 15, ... converging to `n_bins - 1`.
  Only level 1 happens to equal `n_bins/2`, which is exactly why cascades 0 and
  1 agreed and nothing above did. Returning the value from a shorter lag than
  the label claims reads, for a decaying G(tau), as an inflated G, and the
  inflation grows with the cascade: the measured 1.19 / 1.25 / 1.12 / 2.39. Mean
  stream/batch ratio is now 1.0000 on every cascade out to lag 65520. Two
  smaller defects went with it: the coarse-bin width in the normalization used
  `2^(j/n_bins)` where the batch uses `2^((j-1)/n_bins)`, so every block-boundary
  lag was off by a factor of two; and `push_photon(mt1, w1, mt2, w2)`
  accumulated `w1` only, computing an autocorrelation and calling it a
  cross-correlation.
- **`StreamingBurstDetector` did not agree with the batch sliding-window burst
  search it mirrors** — the same PRD-033 that called the correlator broken
  called this one "✅ works". Four defects: every burst ended **one photon late**
  (the window that fails ends the burst at the preceding window's last photon,
  `i + m - 2` in the batch loop, not at the photon just pushed), so every burst
  carried an extra background photon; a burst of **coincident photons was
  reported as no burst at all**, because the rate was computed as `m / span`
  with a zero span guarded by `rate := 0` — m photons in one macro-time tick is
  the highest rate the detector can see, and it fell below the threshold; the
  detector **kept every photon's macro time**, unbounded, in a consumer meant
  for a live acquisition (now a ring buffer of the last `m`, O(m)); and a
  non-positive `macro_time_resolution` **silently returned the whole stream as
  one burst**, which is reachable by accident because a `TTTR` whose header has
  not been read reports `-1.0` (now rejected). `push_photon`'s return value,
  documented as "true if a burst just completed", was
  `... && &bursts_.back() != nullptr` — the address of a reference, always
  true — and now means what it says. Boundaries are compared index for index
  against the batch search over nine seed/parameter combinations.
- **`StreamingDecayHistogram` and `StreamingPhasor` checked, not assumed.** Both
  were claimed correct on the same evidence as the burst detector; both hold up
  — the histogram is exact against `np.bincount` and the batch microtime
  histogram, the phasor matches `DecayPhasor.compute_phasor_bincounts` to 1e-12
  and lands on the universal semicircle with the simulated lifetime.
- **New `test/python/streaming/test_streaming_correlator.py`** — axis equality
  with the batch correlator, Poisson flatness, per-cascade agreement (not
  aggregate: an aggregate tolerance is what hid this), cross-correlation,
  flush idempotence, and chunked-vs-whole equality.
- **The macOS build linked no OpenMP runtime.** Once `FIND_PACKAGE(OpenMP)` was
  taught to find Homebrew/conda `libomp` on AppleClang, every library target
  failed to link: the flags reach the compiler but nothing links the runtime,
  and `OpenMP_EXE_LINKER_FLAGS` is empty on that path and would not reach a
  library target anyway. `LINK_LIBRARIES(OpenMP::OpenMP_CXX)` after detection
  covers the modules, both whole-library targets, the SWIG modules and the
  tests.
- **The non-symmetric eigensolver returned wrong eigenvectors** (`QREigen.h`,
  used by `BurstML`; the same code was duplicated in `GopichSzabo`). Three
  independent defects, each enough on its own: the null vector was
  "un-permuted" by the elimination's row pivots, but row pivots permute
  equations, not unknowns, so the components came back scrambled; the null
  direction was read off a pivot that is rounding-level noise when the shift is
  an eigenvalue, so its value was noise; and the balancing back-transform
  scaled columns where it had to scale rows, and was applied to vectors that
  had never been balanced in the first place. Measured on the defining
  property: `||A v - lambda v|| / ||A||` was ~1 (that is, no information) for a
  symmetric 8×8, a general 10×10, a kinetic generator and a badly scaled
  matrix. It is now 1e-15 on all of them. Eigen*values* were always correct.
  Eigenvectors are now produced by inverse iteration on the Hessenberg form,
  and taken from the balanced matrix so the basis is well conditioned.
- **The QR iteration could stall silently.** With no exceptional shift, a
  matrix whose eigenvalues sit symmetrically about the trailing 2×2 — a cyclic
  permutation is the standard example — is a fixed point of the Wilkinson
  shift, so nothing ever deflated; the iteration then hit its budget, broke out
  and reported success with the remaining eigenvalues left at zero. It now
  applies LAPACK's exceptional shift every tenth iteration, and returns failure
  rather than zeros if a block still will not deflate.
- **`mat_solve` and `mat_inverse_inplace` called singular matrices regular.**
  The test was an absolute `1e-300` floor, which is not a rank test: a rank-1
  outer product with entries ~1e8 leaves pivots at rounding level, not at zero,
  and the solver returned components of size 1e24. The threshold is now
  `eps * max|A|`, as tight as an exact-zero test but scale invariant.
- **`mat_lstsq_minnorm`'s `rcond` was absolute, not relative.** Every singular
  value of a uniformly small system falls under a fixed `1e-14` cutoff, so the
  whole solution was zeroed — a system scaled by 1e-15 returned `x = 0` instead
  of `||x|| = 7.6e14`. The cutoff is now `rcond * sigma_max`, matching numpy's
  `lstsq` and LAPACK's `gelsd`.
- **The MaxEnt TCSPC active set fed its least-squares fallback a destroyed
  matrix.** `mat_solve` eliminates in place, so the copy taken *after* it
  failed held a half-triangularised matrix and a partly updated right-hand
  side. The copy is now taken before the solve.
- New `test/cpp/test_mat_linalg.cpp` and `test/cpp/test_qreigen.cpp` cover all
  of the above as properties (residual, orthogonality, minimum norm, rank
  detection, reconstruction) rather than stored numbers.

### Performance
- **`StreamingCorrelator` no longer pays for empty bins.** Its cost was one
  cascade step per macro-time bin (~19 ns), and a real acquisition has hundreds
  to thousands of empty bins per photon — a 100 s run at 10 ns resolution is
  10^10 of them. A run of empty bins is now skipped in closed form: level *b*
  emits `n0 / 2^b` times, so a run covers a difference of two divisions, and
  only the first emission can be non-zero. 80k photons over a 4.0 M-bin span:
  0.136 s → 0.063 s; over a 200 M-bin span: 0.069 s, where the old code would
  have spent about 4 s. The cost no longer grows with the length of the
  acquisition.
- **The non-symmetric eigendecomposition is 5.5× faster single-threaded and
  10× on eight threads** (n=100: 45.3 ms → 8.3 ms → 4.5 ms; n=150: 208 ms →
  13.0 ms threaded). Eigenvectors were 94% of the time and scaled as n⁴,
  because a dense LU of `A - lambda*I` ran for every eigenvalue. Inverse
  iteration on the **Hessenberg** form costs O(n²) per eigenvalue — a
  Hessenberg column has one entry to eliminate — so the basis is O(n³), the
  same order as the QR iteration itself. The Schur-vector accumulation was then
  switched off (nothing consumes it) and the per-eigenvector loop parallelised.
  `GopichSzabo` now shares this solver instead of its own O(n⁴)
  Faddeev-LeVerrier + Durand-Kerner path.
- **`mat_lstsq_minnorm` is 2.5–7.3× faster** (512×128: 201 ms → 27.7 ms). The
  one-sided Jacobi sweep works on a column-major copy so every rotation and
  inner product is contiguous, and column norms are carried through each
  rotation in closed form instead of recomputed.
- **`mat_inverse_inplace` gained an allocation-free overload** (1.4–2.1× in the
  Kalman burst search's per-bin loop), and `mat_power` reuses one scratch
  buffer instead of allocating per multiply.
- New tracked benchmark `benchmarks/bench_linalg.cpp` with a recorded baseline
  (`benchmarks/results/linalg_baseline.tsv`) and a `--check` mode that exits
  non-zero on a regression beyond 1.30×. Table and method in `PERF.md`.
- **Per-pixel reconvolution MLE (`fit_map`) is 2.8× faster** (456 ms → 161 ms
  on a 256×256 FLIM image). Two changes:
  1. **Allocation-free inner optimization loop** (`DecayFitNExp.cpp`): a
     `FitWorkspace` struct pre-allocates all scratch buffers (component arrays,
     EM probability/weight vectors) once per fit. The grid scan, Brent
     minimization, and EM iterations previously allocated ~6 vectors per
     `evaluate_profile` call — thousands of heap allocations per single fit.
     A new `compute_nll_only` / `evaluate_nll_ws` path skips the
     `ProfileResult` copy entirely when only the NLL is needed (the inner
     optimization loop).
  2. **Buffer-based `fit_batch_flat_buffers` SWIG binding**: NumPy arrays now
     cross the Python-C++ boundary as raw pointers via `IN_ARRAY2` typemaps,
     eliminating the element-by-element `std::vector` conversion that
     dominated the 46k-pixel image path.
  The CPU fitter now beats FLIMKit's MLX GPU by **5.3×** (was 1.25×).
- **`FitNExp` Python wrapper** (`ext/python/FitNExpWrapper.py`): instance-based
  convenience class holding IRF/timing/bounds state, with `__call__` (single
  curve), `fit_many` (batch), and `fit_map` (per-pixel image). Delegates to the
  optimized C++ `DecayFitNExp` API. The benchmarks reference `tttrlib.FitNExp`;
  this implements it.

### Added
- **A `DataStore` tree is reached with `/`, the way `pathlib` reaches a
  filesystem.** A store has been a tree since the data-groups work, and getting
  at a column was a four-link chain — `store.group("results")["Tau"].numpy()`.
  Now `store / "results" / "Tau"` composes a path and looks nothing up until it
  is used, so a path can be built before the group exists, held, passed on and
  resolved later. `store["results/Tau"]` is the same key space resolved at
  once, `store["meta/source"] = arr` writes and creates the groups it needs,
  and `walk()`, `paths()`, `rglob()` and `tree()` make an unfamiliar `.dstore`
  explorable at a prompt. `add_group("meta", {"source": ["run.ptu"]})` replaces
  the add-then-loop-then-`set_n_rows` callers were writing by hand.
  **A key without a separator is unchanged**: `store["Tau"]` is a column, a
  name that is only a group still raises, and a column is looked up *before*
  the tree — so a column genuinely named `Sg/Sr` still wins over the path.
- **Histograms take paths**, on the rule that a histogram fills from one table:
  `(store / "results" / "Tau").histogram(bins=100)` and
  `store.histogram("results/Tau", "results/E")`. Axes from two different groups
  raise and say why — the groups have different row counts and no row
  correspondence, so it is not something that can be filled. `weight=` and
  `profile(sample=)` take paths on the same rule.
- **`np.asarray(column)` and `np.mean(column)` work**, without spelling
  `.numpy()`.
- **`Column.set_attribute_json(key, value)` and `attribute_json(key)`**, so a
  description can hold structure rather than only strings. `set_attribute`
  stores a string whatever it looks like — `set_attribute("na", "[[2,4]]")`
  really stored the seven characters — which forced anything structured to be
  double-encoded and re-parsed by hand at the other end. The reader is the same
  split: `attribute()` unquotes so a caller reading `units` need not parse,
  `attribute_json()` does not, so the round trip is exact.

- **Rows that were never measured are recorded as ranges**, not as a bit per
  row, when they are contiguous — which the gap a `concat` leaves always is,
  because it is one file's whole contribution. Twenty files of a million rows
  cost about a kilobyte of description rather than 2.5 MB of bits. The saving
  is real and is not the main reason: **a range can say why and a bit cannot.**
  `run.why` is `absent in 'm002.hdf5'`, so a caller merging twenty files can
  report which ones contributed what instead of keeping the file list beside
  the table. `valid(i)` and `mask_numpy()` answer the same whichever way the
  column stores it; `has_missing()` is the new question ("are any rows
  missing"), because `has_mask()` asks about storage and is false for a column
  whose gaps are ranges. A scattered pattern — what `mask_non_finite()`
  produces — still becomes a bit mask, and the two are never mixed in one
  column. `add_na_range(first, last, why)` writes one by hand.
  HDF5 writes **both**, the ranges in the description and the mask as a
  dataset, because that format exists to hand a table to a reader that cannot
  be assumed to know what an `na` range is; CSV writes empty cells. `take()`
  and `compact()` keep the validity and drop the ranges — a gather reorders
  rows, so a range naming the source's rows says nothing true about the
  result's.

- **One vocabulary for a table in a file, whatever the file is.**
  `read_table` / `write_table` / `table_groups` / `table_columns` /
  `table_has`, keyed by a spec — `path` or `path|object` — with the format taken
  from the file exactly as `TTTR(filename)` already infers a container:

  ```python
  read_table("run.dstore",     group="results", columns=["Tau"])
  read_table("run.h5",         group="results", columns=["Tau"])
  read_table("run.pto|bursts", group="results", columns=["Tau"])
  ```

  These add **no capability**: every one is a call to a reader that already
  existed, chosen from the file. What they add is that the choosing happens once
  in the library instead of at every call site in four languages — three formats
  could hold a `DataStore` and each was reached by a different verb with a
  different spelling of the same argument, so swapping one for another meant
  rewriting call sites.
  Five, not six: there is no `table_remove`. Removing a group is `read_table` →
  `remove_group` → `write_table`.
  Group paths now come back in one form: HDF5's own listing gives `/results` and
  includes the root, the native format's gives `results` and does not, and one
  had to win or a path from one listing could not be handed to the other.
  The reader takes its format from the **content** — a `.dstore` named `.h5`
  still reads as a `.dstore` — and the writer from the **extension**, which is
  inherent since the file need not exist yet. CSV is one flat table with no
  tree, so `group` and a row range raise rather than being quietly ignored.
  In the conformance case list, with the same steps run against a `.dstore`, an
  HDF5 file and a store inside a PTO from all four languages: that is what
  "interchangeable" means, and without it this would be only a fourth spelling.
  `write_table(spec, store, group=...)` works on both tree formats and leaves
  the siblings alone whichever way each does it — HDF5 replaces the group in
  place, the native format and PTO read, replace and write back, and the caller
  is not told which because the resulting file is the same. The new
  **`table_format` registry category** publishes what each can be asked for, and
  the entry that matters most is not a capability but a cost:
  `rewrites_on_partial_write`, which a caller with a four-gigabyte file is
  entitled to know before they call rather than after.
- **Every partial read now works on both formats.** They had complementary
  holes — `.dstore` could take a column subset and a row range but not one
  group, HDF5 could take one group and neither of the others — so a caller who
  swapped an extension to get a subset read got the opposite, and the two were
  not interchangeable however alike they looked. `load_store(f, group=...)`
  reads one group of a `.dstore`, `read_hdf5(f, columns=..., first_row=...,
  n_rows=...)` reads part of an HDF5 table, and `store_has(f, group)` asks
  without reading. Each is native, not a whole read that is then sliced: a
  group is reached by a scan of the directory, and a row range is a hyperslab
  HDF5 resolves to the chunks it falls in.
  `hdf5_bytes_read()` joins `store_bytes_read()`, because "the columns you did
  not ask for were never read" is a claim about work not done and a wall clock
  on a warm page cache measures the cache. Measured on the same tree written
  both ways, **the same request now moves the same bytes through either
  format** — 800 400 for the whole file, 320 000 for one column, 2 000 for a
  fifty-row window, 160 000 for one group.
  Two things worth knowing: reading one group gives back the tree *below* it
  and not the tree above, which is what makes the result a store in its own
  right rather than a view; and `columns=` is matched **per node**, so a name
  in one group and not another leaves that group with fewer columns rather than
  making the read an error.

- **`write_csv(metadata="leading"|"trailing")` and `read_csv(comment=...)`.**
  CSV carries values and nothing else, so a table written to it lost its label
  and every column's units. The description now rides beside the data as JSON
  **Lines** — one object per line, each prefixed with a comment character — so
  any reader that skips comments sees exactly the table it saw before:

  ```
  #{"tttrlib":"table","version":1,"label":"acquisition","n_rows":4096}
  #{"column":"Tau","dtype":"float64","metadata":{"units":"ns"}}
  Tau,n
  ```

  JSON Lines rather than one blob so a line a later version does not understand
  is skipped instead of making the block unreadable, and `grep` still works.
  A column with nothing to say gets no line — nothing acquires a description by
  being written. `read_csv(comment="#")` puts it all back.
  The reader recognises comments as a **leading and a trailing block**, not line
  by line: that is what this writes, and it keeps the parser's hot loop free of
  a test per record. Its header used to say comment lines were out of scope
  entirely; that note now says which half is in and why.
- **`write_csv(nan_rep=...)`** — what a float `NaN` is written as. `na_rep`
  covers a cell the mask says was never measured; a `NaN` is a *value*, and the
  store keeps the two apart on purpose. CSV has one blank field for both, so
  the writer is where the choice has to be made. `"nan"` is the default and is
  what this always wrote; `""` is what a data frame's writer produces.
  The workaround it removes was masking every non-finite value before writing —
  and the mask is *part of the table*, so doing that in place means writing a
  table changes it. The cost was never the 3% of write time; it was a
  whole-table copy per write to express one formatting choice.
  ±infinity is deliberately not covered: it has an exact text that reads back
  as itself. Neither spelling survives this library's own round trip as a
  *value* — `nan` is one of the reader's default `na_values`, so both come back
  masked — which is measured, stated in the docstring, and the reason the
  option is about what other programs read.
- **A `Column` behaves like the array it wraps**: `col[0]`, `col[1:3]`,
  `col[mask]`, `list(col)` and `col[0] = x`. `np.asarray(column)` and
  `len(column)` already worked and nothing else did, so every consumer that
  touched a frame column as a *value* had to be rewritten to insert a
  conversion — ~56 call sites in the package migrating onto `DataStore` whose
  only purpose was that conversion. Nothing new is computed; the buffer was
  always reachable and what was missing was the protocol a numpy user expects.
  Three things are deliberate. **An integer index does not go through
  `numpy()`** for bool and text, whose array forms are copies — `numpy()[i]` on
  a 200 000-row text column costs 38 s per thousand accesses against 0.6 ms,
  because it decodes every row to read one. **Element access says nothing about
  validity**: a masked row returns what is stored in it, exactly as `numpy()`
  does, because NaN-where-masked cannot be done for an integer or text column
  without changing the dtype the mask exists to preserve. And **a write refuses
  where it would be lost** — bit-packed bool and dictionary-encoded text decode
  through a copy, so `col[0] = x` on those raises and names the route that
  works, rather than vanishing.
- **`DataStore.copy()`**, in C++ so all four bindings have it. The copy
  constructor did this already and nobody could find it, so callers were
  writing `take(range(n))` — which allocates an index array the size of the
  table and says nothing about the intent. Deep: every column owns its own
  buffer afterwards, and the tree, dtypes, dictionaries, validity,
  descriptions, labels and the row selection all come across.

### Changed
- **A column's description is stored as msgpack**, in both formats, rather than
  as JSON text. The reason is types: JSON has one number type, so an integer
  row index, a count and a flag all came back as doubles and had to be
  re-inferred — and a row boundary was exact only up to 2^53. It is also about
  a quarter smaller, needs no base64 for binary values, and is decoded on every
  open whether or not anyone asks for it. The interface is unchanged:
  `metadata()` still takes and returns JSON **text**, because a string crosses
  four bindings with no typemap and is what a human reads in a debugger. The
  `.dstore` format goes 2 → 3, and version 2 files still read — the slot is in
  the same place and holds text, so only the decode differs.

### Fixed
- **Two processes could open the same `.pto` for writing, and neither was
  told.** `PtoFile::open(writable=true)` used `"r+b"` and `create` used `"w+b"`,
  with no lock and no question asked, so both writers succeeded immediately.
  Each holds its own slot table, freelist and generation counter and nothing is
  visible until `commit()`, so they allocate from freelists computed before
  either committed: the last commit decides what the file says, and the loser's
  bytes stay in it, reachable through the winner's index. The observed damage
  was a burst table holding rows no single code path produces — `Duration
  (ms) = 0.0` beside 1951 photons, and a mean macro time of 7.1e12 ms in a 707 s
  measurement — with the photon streams clean and two independent decoders
  reading the table back identically.
  A writer now takes an **exclusive advisory lock** on the container
  (`flock(LOCK_EX | LOCK_NB)`, `LockFileEx` with `LOCKFILE_FAIL_IMMEDIATELY`)
  and a second one is refused with *"… is open for writing elsewhere"*.
  **Readers are untouched** — a viewer open during an analysis is the normal
  case, and the commit-on-write design already has the reader seeing the
  pre-commit state. **The refusal is immediate**, never a wait: a writer that
  blocks is indistinguishable from a writer that hung. `create` takes the lock
  *before* it truncates, so being refused cannot destroy the file. The lock is
  released by `close()`, by the failure path of a rejected `open` — it is taken
  before the container is parsed, so everything the parser rejects would
  otherwise stay locked — and by the kernel when a writer dies, which is what a
  sidecar lock file cannot promise. Filesystems with no locking (some network
  mounts) open as before rather than becoming unusable.
- **`write_csv` wrote its constant cells unquoted**, so `na_rep="a,b"` produced
  a file with a phantom column that did not read back — while the header three
  lines away quoted the same string correctly. The null, true, false and NaN
  texts now go through the same render a column name and a dictionary label
  already did.
- **`write_csv(quoting="never")` raised a bare `KeyError` naming nothing.** The
  C++ enumerator is `Never` — SWIG has to escape `None` — so a caller reading
  the C++ side typed the one spelling the Python wrapper rejected. Both work
  now, and an unknown value names the ones that do.
- **`column == value` returned `False` instead of a mask.** SWIG's default
  identity comparison, so `store.select(column == "m000.spc")` selected
  nothing, raised nothing, and looked like a run with no matching bursts —
  a wrong answer that is silent, which is worse than a missing feature.
  All six comparisons are now elementwise, like an array. A dictionary-encoded
  column compared against a single string takes the dictionary rather than the
  decoded labels: the same answer, one lookup plus an integer compare over the
  codes, measured **374× faster** on 500 000 rows and with the column never
  decoded. A label that is in no row matches nothing rather than raising.
  `Column` stays hashable — defining `__eq__` in Python would otherwise set
  `__hash__` to `None` and break a column used as a dict key somewhere with no
  connection to this.
- **HDF5 dropped a column's description entirely**, which made the two formats
  disagree about what a column *is*: a lifetime written in nanoseconds came
  back through HDF5 saying nothing about nanoseconds, so what a caller got
  depended on which format they had picked. It now rides as a byte attribute on
  the column's own dataset — next to the dictionary and for the same reason,
  that everything needed to read a column is on the column — holding the same
  msgpack the native format holds. A name HDF5 cannot store as a link, one with
  a `/` in it, now comes back whole for the same reason. Nothing acquires a
  description by being written: a column with none costs nothing.
- **A column's zero-copy array outlived its `DataStore` and read reused
  memory** (BUGS.md). Nothing in the returned array's base chain owned the
  buffer or referenced the store: the owner rode on an ndarray subclass, and
  numpy collapses a base chain through any array that does not own its data —
  subclass or not — so `np.asarray(col.numpy())` handed back an array whose
  base was the raw SWIG view, with the owner dropped on the way. It did not
  raise; it returned plausible numbers with occasional wrong ones. The filed
  reproduction read row 2 of a 1000-row CSV column as `0.0` or
  `6.001000000000001e-05` instead of `6.0` in six runs of eight, and downstream
  a burst table read 84 of 154 rows of `First Photon` as `3.3e-319` instead of
  `2755`.

  Two halves to the fix, and only both together close it. The owner is now an
  object that is *not* an ndarray, which is where numpy's collapse stops — so
  the root of every derived view owns the buffer, and the collapse works in the
  library's favour. And **every** way of getting a column now carries the store:
  `store.column(0)` and `store.column_by_name("x")` are wrapped C++ and had no
  link back at all, so they dangled even with the first half in place. Zero-copy
  is unchanged, and the reproduction from BUGS.md is now a test.

  `hist_support._OwnedView` is the same shape and, checked, does *not* have the
  same problem — its chain is two views deep so the collapse lands on a view
  that still carries the owner, and an axis's `edges` and a profile's `mean()`
  are ARGOUTVIEWM, where numpy owns the buffer outright.
- `Column.__array__` and `HistogramNd.__array__` take numpy 2's `copy=`
  keyword, which it passes and warns about on a signature that cannot.
- `column_names()`, `store_groups()`, `store_columns()`, `hdf5_table_groups()`
  and `read_hdf5_table_columns()` return real lists, so `== [...]` is true
  without wrapping every call in `list()`. `group_names()`/`group_paths()`
  already did.
- **A buffer of undecoded records can be decoded.** Every decoder sat
  behind `TTTR(filename)`, so a caller holding records from a card, a socket or
  a container it unpacked itself had to write the decoder a second time — and a
  copy with nothing holding it to the original is how two implementations come
  to disagree about an overflow run months later, on somebody's data, with no
  error anywhere. `TTTR.decode_records(buffer, record_type, state)` uses the
  same `RecordProcessor` specialisations the file readers dispatch to, so every
  record type is covered by construction. `TTTRDecodeState` carries the macro
  time overflow count across chunk boundaries, which is the whole reason the
  interface has a state rather than being one function; without it a stream
  decoded in pieces comes back with every macro time after the first boundary
  short, and nothing fails. The four encodings that need something the record
  stream does not carry — SM, BrightEyes-TTM, both FLIM LABS taggers — decline
  by name.
- **Seven containers can now be read in pieces**, not just PTO: PTU, HT3,
  SPC-130, SPC-600 (both), CZ-RAW and SPC-QC. `container_records(spec)` reports
  the record count from the header and the file size without decoding one,
  `container_read_records(spec, first, n)` reads a range undecoded, and the two
  compose with `decode_records` into a chunked reader — five lines in any of the
  four bindings, giving results identical to a whole-file read. `ranged_reads`
  in the `file_container` registry says which containers this applies to, and
  the ones it does not apply to decline by name rather than quietly reading all
  of it. `container_events` and the `first_record` / `n_records` reader
  parameters cover the common case without a loop; both report macro times
  counted from `first_record`, for the reason documented on them.
- **The whole Becker & Hickl `.set` sidecar, in every binding.**
  `read_set_file` / `parse_set` return every `#SP`, `#PR`, `#DI`, `#TR` and
  `#WI` parameter with its section, its group, the type letter the file
  declares and its value — 222 and 207 parameters for the two reference
  sidecars, against the five `read_bh_set_file` extracts for the header. Values
  stay text: a `.set` declares its types per parameter and a parser that guesses
  is wrong about one field in a hundred and silent about it. `bh_set()` in
  Python arranges them as `{section: {name: value}}`. `read_bh_set_file` keeps
  its own scope and still feeds only the imaging tags into the header.

### Fixed
- **A 64-bit integer returned as a scalar lost precision in JavaScript.** The
  arrays were always exact — a macro-time channel comes back as a
  `BigUint64Array` and sums past 2^53 without loss — but SWIG's Node-API
  backend routed `long long` and `unsigned long long` scalars through
  `Number`, an IEEE double. A PTO uid of `14523661926200792394` read back as
  `14523661926200793000`: off by 606, silently. Those scalars are `BigInt`
  now. Counts stay `Number`: `size_t` and `unsigned long` carry lengths that
  callers do arithmetic on and cannot reach 2^53.

  **Breaking for JavaScript callers** of anything declared `int64_t` or
  `uint64_t` — `SimEngine.n_photons()` is the common one. `Number(x)` converts
  where a plain number is wanted.

  R had the same symptom for a different reason: its `integer` is 32-bit and
  its `numeric` is an IEEE double, so a 64-bit float — the widest number R has
  — still carries only 53 bits of mantissa and three consecutive uids collapse
  onto one value. A **PTO uid is therefore a character string in R**: produced
  as one, accepted as one, never arithmetic. Scoped to the identifiers (`uid`,
  `target`, `primary`); rows, offsets and sizes stay numeric, being magnitudes
  a double holds exactly.
- **The R conformance runner passes for the first time** (80/80). Three bugs,
  all in `test/r/conformance.R` and none in the library, from those ops having
  been written without an R toolchain to run them against:
  `file.write_text` used `writeLines`, which appends a newline the other three
  runners do not, so a case pinning a byte range read one byte too many in R
  alone; and `pto_read_store`'s generated R dispatcher cannot be satisfied at
  all — it requires a wrapped `VectorString` while the typemap behind the
  wrapper it dispatches to coerces to `STRSXP`, so the proxy satisfies one and a
  character vector the other. The numbered overload (`pto_read_store__SWIG_1`)
  is called directly instead. Same class of SWIG-R codegen defect as the
  scoped-enum one already noted in `ext/r/tttrlib.i`.
- **`write_csv` — a `DataStore` written out as CSV**, in Python and JavaScript,
  to a file or to a string. Only the selected rows when the store is gated, the
  same as `write_hdf5_table`; column subset and order, three quoting modes, a
  configurable missing-value string, and threaded blocks written in order.
  Informed by Arrow's writer (`cpp/src/arrow/csv/writer.cc`) and independent of
  it: the same column-at-a-time dispatch, no sizing pass — this writer owns its
  sink and formats straight into the block buffer — and each distinct value of a
  dictionary-encoded text column is escaped once rather than once per row.
  Measured against `pyarrow.csv.write_csv` on two million rows: 3.4x on integer
  columns, 2.6x on measured floats and 2.5x on text at the default thread count,
  and parity single-threaded.
- **`write_csv` gained `float_decimals` and `keep_decimal_point`**, both for
  matching a layout another program fixed rather than for preserving a value.
  `float_decimals` is printf's `%.<n>f` — what a format specified as `%.6f`
  means — where `float_precision` counts significant digits and would write
  `1.23457e-05` for `0.000012`. `keep_decimal_point` writes an integral value
  as `12.0` rather than `12`, so an all-integral column still reads back as a
  float from a type-inferring reader such as pandas; both spellings are the
  same double, so what it preserves is the dtype, not the value. Defaults are
  unchanged and still match Arrow.
- **Doubles are written as the shortest text that reads back as the same
  double**, so `read_csv(write_csv(s))` returns the values bit for bit and `0.1`
  stays `0.1`. `std::to_chars` does this where the platform has it; macOS builds
  never do, because libc++ keeps the floating-point overloads in the dylib
  behind a macOS 13.3 availability guard and this library ships a 10.15 floor.
  `modules/io/csv/src/decimal_exact.h` covers that without vendoring a decimal
  library: Clinger's conditions decide values of fifteen significant digits or
  fewer, and an exact 128-bit integer comparison decides sixteen and seventeen.
  Both answer "cannot decide" outside their range rather than guessing, and the
  reader decides by the same two rules, so the two directions cannot disagree.
- **The analysis outputs reach JavaScript.** `SimEngine.photons()`,
  `BVA.result` / `.proximityRatioMean` / `.proximityRatioStd`, `TwoCDE.twoCde`,
  `HMM.viterbiPath()` / `.gamma()` / `.jitterPath()` / `.ffbsPaths()` and
  `NeuralNet.layerWeights()` / `.layerBias()` — the shorthand Python gets from
  `%pythoncode`, which no other backend reaches. The C++ underneath was already
  wrapped, so a simulation could be run from JavaScript but its photons could
  not be read: they are seven parallel accessors, and nothing said so.
- **A cross-language conformance suite.** `test/conformance/` holds one
  committed case list — 67 cases over thirteen areas — that Python, R, Java
  and JavaScript all run, every case in every binding. The expected values are *shared*, not four copies that
  happen to agree: change one and all four go red. It replaces hand-copied
  constants, and the four files that duplicated them are gone.
  `tools/conformance_update.py` generates expectations for review;
  `tools/conformance_matrix.py` publishes the coverage table. See
  `test/conformance/README.md`.
- **R and Java now wrap `DataStore`, `Hdf5Table` and the registry**, and
  JavaScript gains `Hdf5Table` — so a columnar store, its group tree and its
  HDF5 round trip are reachable from every binding rather than from Python
  alone. Java also gains eleven `Column.get_*_into` accessors, without which a
  column could not be read at all.
- **`CLSMImage.get_fluorescence_decay_v`** — the per-pixel decay block as a
  plain vector, so R can reach it. SWIG's R overload dispatcher matches against
  the C++ parameter list, output pointers included, and could only resolve the
  all-defaults call; `get_phasor_v` already existed for the same reason. The
  native `get_fluorescence_decay` is unchanged.
- **`DecayPhasor.phasor_of_bincounts`** — the phasor of a decay histogram
  through an array rather than a `std::vector<int>&`, which R cannot pass:
  SWIG's R dispatcher wants a typed S4 proxy and `VectorInt32()` returns a bare
  externalptr it will not match. The native method is unchanged.
- **`CLSMImage.get_decay_of_pixels_v`** — the masked decay as a plain vector,
  for the same reason as `get_fluorescence_decay_v`. Deliberately without
  default arguments: defaults make SWIG emit an overload set, and an overload
  set brings back the R dispatcher the wrapper exists to avoid.
- **`Correlator.get_x_axis_into` / `get_corr_normalized_into` for Java** —
  both getters were opaque pointers, so a correlation could not be read from
  Java at all.
- **`jarrays.i` gained 2-D and 3-D array marshalling.** A Java caller passes
  `double[][]` or `byte[][][]` and the typemap flattens it row-major, taking the
  dimensions from the array itself; ragged input is refused rather than
  truncated, and the in-place forms write back afterwards. Before this, every
  2-D or 3-D array parameter was an opaque pointer, so `Histogram.update`, the
  TIFF writers and the masked-decay getter could not be called at all.
- **A `DataStore` is a tree.** It gains named child groups, each a full store
  with its own columns, row count, selection, masks and label — because that is
  the shape the data has: an imaging run is a `results` table of one row per
  pixel plus a one-row `meta` saying where it came from. `add_group`,
  `ensure_group`, `group`, `group_names`, `group_paths`, `remove_group`,
  `clear_groups`, and `store.groups` in Python. `store["name"]` is still a
  column and always will be; groups have their own accessors so nothing has to
  guess which you meant. A store with no groups is unchanged in memory and on
  disk, and `find()` — the hot lookup — does not gain a single instruction.
- **A native store file, `.dstore`.** `save_store` / `load_store` write a store
  and read it back unchanged: column order, dtypes, dictionary-encoded text,
  validity masks, labels, the group tree, and the row selection, which is saved
  rather than applied. It has no external dependency, so a build with
  `BUILD_PHOTON_HDF=OFF` can persist a `DataStore` — which by any other route it
  cannot. Against *uncompressed* HDF5 it is a wash on bulk I/O and the docs say
  so; against compressed HDF5 a 1M-row write goes from 2.09 s to 0.008 s, and
  reading one column of four costs 0.0002 s rather than 0.008 s.
- **`hdf5_table_groups`, `hdf5_table_has`, `hdf5_table_remove`** — ask a file
  what tables it holds, or drop one. Silent on any input, including files that
  are not HDF5, because probing is a normal thing to do.
- **`Hdf5WriteMode`** on `write_hdf5_table`, and whole-tree HDF5 read and write.

### Fixed
- **A C++ exception could terminate the Node process** instead of becoming a
  JavaScript error. `BurstFeatureExtractor.i` ended its `%exception` block with
  a bare `%exception;`, meaning to scope it — but that clears the handler for
  everything SWIG parses afterwards, which left `BurstFeature`, `BVA`,
  `TwoCDE`, the HMMs, `NeuralNet` and `HmmSurrogate` with none. Python survived
  on SWIG-Python's built-in `std::exception` fallback; Node-API has no such
  fallback and aborted. The handler is now restored rather than cleared, which
  fixes R and Java over the same range.
- **The CSV reader returned a different number than it was given.**
  `mant * pow(10, exp10)` is one rounding too many: 32% of doubles came back a
  ulp out, and anything past about 1e-310 came back as zero, because
  `pow(10, -327)` underflows. Leading zeros were also charged against the
  nineteen digits the mantissa can hold, so `0.00035338058920092875` lost its
  last digit. Ordinary data reads at the same speed as before; only the values
  that need it take the slower exact path.
- **`readCsv`'s `naValues` and `textColumns` were silently ignored in
  JavaScript.** A SWIG `std::vector<std::string>` parameter does not accept a
  plain JavaScript array, and the assignment failed quietly.
- **A zero-length `TypedArray` was refused by the JavaScript binding** as having
  "the wrong element type". An empty `ArrayBuffer` has a null data pointer, and
  the borrow reported that null as a type error — so writing a zero-row table,
  or updating a histogram with no samples, was impossible.
- **Six enums were unusable from R.** SWIG's R backend emits one name for a
  namespace-scope `enum class` accessor and a different one in the table that
  reads it, so `ColumnType`, `Hdf5WriteMode`, `AxisKind`, `HistStorage`,
  `SuperResMethod` and `TiffDType` all failed on first use — two of them in the
  already-shipped R module. They are passed as integers now.
- **A `DataStore` group proxy could outlive the store that owned it** in R and
  in Java, reading freed memory after a garbage collection. The proxy now holds
  its root, as it already did in Python.
- **A text column went into HDF5 as one string per row**, throwing away the
  encoding at the file boundary — which is the one place a written store lost to
  the DataFrame it replaces. The dataset is now the `int32` codes and the labels
  are a `dictionary` attribute on it, so the file stays self-describing and a
  reader that ignores the attribute still gets valid category codes rather than
  nothing. Measured on a 1M-row burst table with one four-label text column:
  **96.0 MB → 60.0 MB**, against pandas' 72.6 MB — from above that file to below
  it — with the write 0.185 s → **0.037 s** and the read 0.191 s → **0.011 s**.
  Both older layouts still read: variable-length strings (what this wrote until
  now, and what every other producer writes) and fixed-width ones. Codes that do
  not index their dictionary are read as the integers they literally are, rather
  than as a text column whose every access is out of bounds.
- **A gated wide integer went through a `double` and came back changed.**
  Writing a store *with a row selection* to HDF5 gathered every column into a
  `vector<double>`, so an `Int64` or `UInt64` above 2^53 was written as a
  different number — `2**53+1` became `2**53`. Macro times, event indices and
  pixel numbers all land in that range. The same column written without a
  selection was correct, which is why it went unnoticed.
- **Writing a second group to an HDF5 file destroyed the first.**
  `write_hdf5_table` truncated the whole file on every call, so `/results`
  followed by `/meta` left only `/meta` — and both calls returned `true`.

### Changed
- **The columnar HDF5 table moved into `modules/io/hdf5/`**, beside Photon-HDF5,
  so there is one place to look for HDF5 rather than two directories. They stay
  two targets because they sit on opposite sides of `core` — a photon stream
  decodes into plain arrays and lives below it, a `DataStore` lives in it — and
  one target carrying both dependencies would be a cycle. Nothing else changes:
  same module names, same headers, same API.
- **`write_hdf5_table` defaults to `Hdf5WriteMode::Update`**, keeping groups it
  is not writing, instead of truncating the file. For any file this library has
  produced the two are indistinguishable; what changes is the two broken cases
  above, plus a file that exists and is not HDF5, which is now refused rather
  than destroyed. Pass `Hdf5WriteMode::Truncate` to mean the old behaviour.
- **HDF5 compression defaults to 0 rather than 4.** Level 4 costs roughly thirty
  times the write to save eight percent of the size, on files written once and
  read repeatedly.
- **`read_hdf5_table` throws when a group holds no table**, instead of returning
  an empty store. Zero rows is an answer; zero columns is a refusal, and the two
  were indistinguishable. `read_hdf5_table_columns` still returns an empty list
  for those cases — it is the cheap predicate and callers already read empty as
  "not ours".
- `DataStore::release()` now drops groups as well as columns, so `nbytes()`
  really does reach zero.
- **`PdaBurstLikelihood` — K-channel burst-wise PDA, so three-colour PDA is a
  tttrlib method rather than a NumPy loop in a downstream package.** `Pda` fits a
  binned 1-D projection of a dense `(hist2d_nmax+1)²` count matrix, and that
  representation does not generalise: a dense three-channel simplex at
  `hist2d_nmax = 300` is 217 MB and a four-channel one is 65 GB. The new class
  never forms a matrix — it evaluates `L(F | p, B)` per burst — so it is defined
  for any number of channels, has no photon cap, and is a maximum-likelihood
  objective. At K = 2 it is an alternative to `Pda`'s χ² histogram fit that stays
  correct where a bin holds a handful of bursts.

  The evaluation follows chisurf's `pda3c` factorisation: only the multinomial's
  leading `n!` couples the channels, and it depends on the *total* background
  count, so the nested sum over per-channel background becomes a matrix product
  over a background box. Everything burst-side — falling factorials, Poisson
  series, the multinomial constant — is computed once in the constructor and
  reused for every model point and every fit iteration. Two things go beyond the
  Python original, and both are algorithmic rather than a language change.
  **The background box is never formed.** Grouping by the total background count
  makes the coefficients a discrete convolution of the per-channel series, so
  the cost is `O(K·b·m_max)` instead of `O(∏_c b_c)` — identical algebra, but the
  box grows as the K-th power of the cutoff and the convolution does not (at
  K = 2 they are the same work). That also removes the chunking and its 64 MB
  memory budget. **And every transcendental leaves the inner loop:** both the
  burst factor and the model's `p_c^-b` are peak-shifted per channel, so there
  are `Σ_c b_c` exponentials per burst rather than `∏_c b_c` — 90 instead of
  3375 at a 15-wide box in three channels — and the per burst-and-point loop is
  multiply-add only.

  **Measured 14–920× faster than chisurf's NumPy path** on the same inputs,
  agreeing to ~1e-13 (10k bursts × 20 model points with background: 80 s → 87 ms).
  chisurf now calls it and is **140× faster end-to-end** at its
  `total_log_likelihood` entry point.

  It also fixes a case the Python fast path gets wrong. That path evaluates
  `L = Multinom(F;p) × correction`; where a channel has `p_c = 0` but collected
  photons the leading term is zero, so the product is `-inf` even though the
  burst is perfectly possible with those photons as background. `tttrlib`
  evaluates such bursts in a form that never divides by `p`. chisurf's own
  untruncated reference agrees with tttrlib.

  Two traps are carried across deliberately and pinned by tests: the background
  series may **not** be truncated on Poisson tail mass (the terms grow before the
  Poisson turns them over, exactly where the background explains the burst), and
  both halves of the factorisation must be peak-shifted before exponentiating —
  the model half overflows to `inf`, and `log(inf)` reads as a `+inf`
  log-likelihood, i.e. a perfect fit.
- **`Pda.get_1dhistogram_per_species()` — the projection, one row per species.**
  The S1/S2 → 1-D projection is a fixed linear map and the model is linear in
  the species amplitudes, so `amplitudes @ rows` reproduces `get_1dhistogram()`
  exactly (to 3e-17). A fit that varies only the fractions can therefore call
  this once and reweight, turning an O(`hist2d_nmax`²) re-evaluation per
  iteration into an O(n_species · n_bins) dot product — **measured 62× on a
  three-species amplitude scan at `hist2d_nmax = 200`.** Recall it when
  `probabilities_ch1`, `pF`, a background or the binning changes.
- **A fourth binding: JavaScript for Node.js** — `require('tttrlib')` gives the
  **same surface as Python**, not a subset: core, I/O, burst search, correlation,
  CLSM imaging, HMM decoding, PDA, the decay fits, the simulator, DataStore and
  the registry. It is generated by SWIG's Node-API backend from the *same*
  interface fragments as the Python, R and Java bindings, so the numbers cannot
  drift between them — `test/js/cross_language_reference.test.mjs` asserts the
  same canonical values as the Python, R and Java suites, and
  `tools/check_swig_multilang.sh` now generates all four wrappers on every
  change. Build with `-DBUILD_JAVASCRIPT_INTERFACE=ON`; Node-API's stable ABI
  means one binary per platform serves every Node >= 12.17. See
  [doc/javascript-package.rst](doc/javascript-package.rst).

  Four conventions are worth knowing before writing against it. **64-bit values
  are BigInt** (`BigUint64Array` for macro times, `BigInt64Array` for burst
  boundaries) because a JavaScript number is exact only below 2^53 — a
  `Float64Array` of macro times is rejected rather than silently rounded.
  **Multi-dimensional results are flat and row-major with a `shape` property**,
  and the same form is accepted back as input. **Every method has two names**,
  its C++ one and a camelCase alias, and `%attribute` members are properties
  (`header.micro_time_resolution`, `img.nFrames`) exactly as in Python. **Every
  call is synchronous**, so server code belongs in a `worker_thread`.

  Two things are unavailable, both for the same reason as in R: SWIG's Node-API
  backend generates no directors, so `PdaCallback` cannot be subclassed, and
  there are no Promises. One lifetime caveat is documented in
  `ext/js/js_shared_ptr.i` and the package README: an object constructed in
  JavaScript and handed to C++ that stores a `std::shared_ptr` is passed with a
  null deleter, so C++ holding it does not extend its life.
- **`std::map` returns become plain JavaScript objects.** Python's `std_map.i`
  gives a dict; the Node-API one wraps a map as an opaque proxy with
  `.get()`/`.size()`, the way the Java backend does — so
  `BurstFeatureExtractor.get_burst_channel_photons()` came back as a handle a
  caller had to loop over by hand. Keys become strings (JavaScript object keys
  are, and that is the shape either binding's JSON already produces) and values
  go through the same conversions as everywhere else, so a map of vectors yields
  TypedArrays. Out only: no wrapped API takes a map as a parameter.
- **Lifetime and GC tests** (`test/js/lifetime.test.mjs`) — a `shared_ptr` result
  outliving the object that produced it, surviving that object's collection,
  thousands of proxies of the same object releasing cleanly, `.slice()` detaching
  a view from C++-owned memory, and a crude leak check over 200 file reads. This
  is the worst of the binding's risks, because it fails by segfault rather than by
  exception. Run with `--expose-gc`; the GC-forcing cases skip without it rather
  than pass vacuously.
- **A minimal web viewer for TTTR files** — `examples/js/ptu-webapp/` opens a PTU
  (or HT3, SPC, Photon-HDF5, ...) and shows its header, time trace and micro-time
  decay in a browser, with no Python anywhere. One file of server, one page of
  front end, no framework and no build step. Every tttrlib call runs on a worker
  thread and photon arrays never leave it — binning and histogramming happen in
  C++, so a few thousand points cross the wire instead of a few hundred million.
  Container names come from `registry("file_container")` rather than a table that
  would go stale. Localhost only, with the data root containment-checked
  server-side.

- **TIFF I/O carries an axis order** — a TIFF is a flat page sequence, so six
  pages cannot say whether they are six frames or two frames in three colours.
  `TiffInfo` now exposes the first page's `ImageDescription` tag and `write_tiff`
  accepts one, which the Python layer uses to read and write ImageJ hyperstack
  metadata: `imwrite(path, array, axes="TCYX")` stores the split, `imread`
  restores the N-D shape, and the new `tiff_metadata(path)` reports axes, shape
  and dtype without decoding pixels. Arrays of more than three dimensions are no
  longer rejected. Files written this way are byte-compatible with what
  ImageJ/Fiji and `tifffile` read, and a description that disagrees with the page
  count on disk is ignored rather than used to reshape the pixels into the wrong
  grid. `imwrite` also takes `resolution=(x, y)` and `metadata={"spacing": …,
  "unit": …}`, which together give a z-stack a physical voxel size — ImageJ needs
  both halves, the tags for x/y and the description for z.
- **ImageJ/Fiji plugin rebuilt on SciJava** — the two IJ1 `PlugIn` classes became
  SciJava `Command`s, so every command is now macro-recordable, scriptable from
  Groovy/Jython, headless-capable and unit-testable. New commands: *Show TTTR
  Metadata*, *Batch Process Folder…*, and both a SCIFIO `Format` and an
  `IOPlugin` so `File ▸ Open` and drag-and-drop handle `.ptu`/`.ht3`/`.spc`
  directly. (Both are needed: `File ▸ Open` for images goes through
  `DatasetIOService`, which consults SCIFIO formats and ignores plain
  `IOPlugin`s, while drag-and-drop and scripts use `IOService`.) Reconstruction logic moved
  into `…imagej.core` with no ImageJ dependency at all.

  Images are now ImageJ2 `Dataset`s with **named axes** (`X`, `Y`, `CHANNEL`,
  `TIME`, and a custom `PIE` axis). Routing group and micro-time window are
  separate axes instead of being flattened into one channel index, so a group or
  a window can be addressed independently rather than parsed out of slice labels.

  **This makes the plugin Fiji-only**: SciJava commands are discovered through
  annotations, and a plain ImageJ 1.x install has no SciJava layer, so they do
  not appear in its menu. The Java *library* is unaffected.

- `CLSMImage::get_intensity_u32` / `get_intensity_from_masks_u32` and
  `CLSMFrame::get_intensity_u32` — 32-bit counter variants of the intensity
  getters. Available from Python (numpy) and Java; existing 16-bit signatures are
  unchanged.

### Fixed
- **PDA projected the model matrix transposed, mirroring every 1-D histogram.**
  `evaluate()` builds `S1S2[ch1][ch2]`, but `get_1dhistogram` read
  `s1s2[ch2][ch1]` and `compute_experimental_histograms` wrote the experimental
  matrix the same transposed way. The projection axis therefore came out
  mirrored — `E` where `1 - E` was meant — for the *model* only. Nothing looked
  broken, because a fit simply converged on `1 - probability_ch1` and left no
  trace in the residuals. All three now agree on row-is-channel-1, which is what
  the header always claimed; the convention is spelled out in the class
  documentation and in `doc/pda-guide.rst`. **A previously fitted
  `probability_ch1` from tttrlib should be re-checked: the correct value may be
  its complement.** Experimental 1-D histograms are unaffected (the storage and
  the read flipped together); code that indexes the raw experimental matrix must
  swap its indices.
- **PDA lost the outermost anti-diagonal of the S1S2 matrix — two off-by-ones.**
  The binomial scatter stopped at `red < Nmax` instead of `red <= Nmax`, dropping
  the all-channel-2 corner, and `conv_pF` asked `poisson_0toN` for `Nmax` kernel
  taps where 0..Nmax needs `Nmax + 1`, leaving the last tap zero. Every cell with
  `ch1 + ch2 == Nmax` was wrong. The model now matches a direct implementation of
  the definition to ~1e-17 (it was ~3e-6).
- **`Pda.compute_experimental_histograms` used a quarter of the data, then hung.**
  `get_time_window_ranges` returns interleaved `[start, stop, start, stop, ...]`,
  but the loop walked `tws[i], tws[i+1]` over the first half of the array, so
  every other window was empty and the rest were never reached. The returned
  index array held one meaningless value per window (the leftover photon-scan
  variable) rather than the documented start/stop pairs. Separately, an in-memory
  `TTTR` reports a macro-time resolution of `-1`, which made
  `ranges_by_time_window` compute a zero-length window and spin forever; a
  non-positive resolution now falls back to raw macro-time ticks and a window
  always advances by at least one photon.
- **PDA leaked every array it returned to Python.** `get_1dhistogram`,
  `compute_experimental_histograms` and their outputs were bound with
  `ARGOUTVIEW_*` typemaps, which wrap a malloc'd buffer in a numpy array that
  never frees it — so a fit leaked two arrays per iteration and the experimental
  call leaked three buffers plus a full copy of the file's routing channels.
  They now use the managed `ARGOUTVIEWM_*` typemaps, the time-window array is
  freed, and the routing channels are read in place instead of copied.
  `Pda::set_callback` also leaked the callback it replaced, including the default
  one the constructor allocates, and `Pda` had an owning raw pointer with no
  copy control, so copying it double-freed.
- **`str(Pda)` always raised `TypeError`** — five of its lines concatenated a
  string with a float.
- **`registry_categories()` returned an empty list in every binding.** It looped
  over `build().items()`, and a range-for lifetime-extends only the range
  expression — the `iteration_proxy` — not the `json` temporary the proxy points
  at, so the loop iterated a destroyed object. `registry_json()`, built from the
  same call, returned five categories throughout, which is what made the
  discrepancy visible.
- **`tools/check_swig_multilang.sh` had stopped working.** It still looked for
  headers under `include/` and `src/`, where the module rework no longer puts
  them, so every language's pass failed on `Unable to find 'TTTR.h'`. It now
  globs the module include directories the way `cmake/TTTRLibModule.cmake` does,
  and checks a fourth backend.

- **`CLSMImage::get_fcs_image` was unusable — three separate defects.** It
  dereferenced `clsm_other` unconditionally although that parameter is documented
  as optional (and guarded for null two lines earlier), so every call that did
  not pass a second image segfaulted; its declared default correlation method
  `"default"` is not one the correlator knows (`wahl`, `felekyan`, `laurence`),
  so it warned once per pixel and returned all zeros; and it copy-assigned
  stack-local `TTTR` selections into reused `shared_ptr`s, leaving the correlator
  reading freed memory and aborting on the first pixel with photons. Per-pixel
  FCS now works from every binding.

- **Every other scan line was mirrored on files without an `ImgHdr_BiDirect`
  tag.** `TTTRHeader::get_tag` reports a missing tag with a sentinel
  (`{"value": -1.0, "name": "NONE"}`), not null, so the `read_tag_int` /
  `read_tag_double` helpers in `CLSMImage.cpp` accepted it as a real value: every
  absent tag read as `-1`, and `bidirectional_scan = (-1 != 0)` became `true`.
  Python was unaffected only because its wrapper hardcoded the flag to `False`.
  Sum- and dimension-based tests cannot catch this — mirroring preserves both —
  so the ImageJ module now asserts row-to-row coherence.

- **Becker & Hickl SPC images reconstructed to zero frames outside Python.** BH
  carries the scan geometry in a `.set` sidecar and the marker layout in the
  `BH_SPC_ReadingRoutine` header tag; the mapping from that tag to
  `CLSM_BH_SPC130` plus its marker convention lived only in the Python wrapper.
  It now lives in `CLSMImageInfo::from_header`, so Python, R, Java and native C++
  reconstruct identically (`FocalCheck_A1_20x_8xzoom_750nm_m1.spc` →
  20×512×512, sum 1036407 in both Python and Java). `CLSMImageInfo` gained
  `use_pixel_markers`, `skip_before_first_frame_marker` and `reading_routine`.

- CLSM header auto-configuration applied geometry and markers under a single
  gate, so callers naming an explicit reading routine (Leica SP5/SP8) received no
  pixel dimensions. Geometry and markers are now gated separately.

- `CLSMImage.__init__` no longer duplicates the header resolution in Python: it
  had drifted from the C++ implementation, which is why the same file yielded
  different settings in Python than in the other bindings.

- **Leica SP5/SP8 and BH SPC-130 marker conventions moved into C++.** They were
  Python-only, so those reading routines produced no image from R, Java or native
  C++. Verified against the Python reference values: SP8 93×512×512 sum 2758188,
  SP5 230×256×256 sum 3486614. The old SP8 branch also read `ImgHdr_BiDirect`
  through `header.tag(...)["value"]`, hitting the same not-found sentinel and
  mirroring alternate lines on SP8 files lacking that tag.

- **Pixels above 65535 photons no longer wrap.** The intensity path accumulated
  into `unsigned short` in two places — `CLSMFrame::get_intensity` truncated a
  true `size_t` count at the cast, and `CLSMImage::get_intensity_from_masks`
  wrapped *during* accumulation, so the real value never existed. Both are now
  templated on the counter width. The ImageJ plugin additionally cast to signed
  `short`, displaying anything above 32767 as negative; it now uses the 32-bit
  path end to end into an `UnsignedIntType` dataset.

- **SWIG wrappers were never regenerated when an interface or header changed.**
  `ext/CMakeLists.txt` set `SWIG_MODULE_DEPENDS`, which is not a property UseSWIG
  reads, so the declared dependencies were silently ignored for **all three
  languages** — a cached build directory could ship a stale wrapper with no
  error. Now uses `SWIG_MODULE_<target>_EXTRA_DEPS` plus
  `USE_SWIG_DEPENDENCIES`, so swig computes implicit dependencies itself.

- ImageJ plugin: the routing-channel list was capped at a fixed `int[256]` buffer
  and the decay histogram at 65536 bins, both truncating silently. Both now query
  the exact size.

### Changed
- **PDA scratch buffers are reused between calls and the model matrix is no
  longer copied into `get_1dhistogram`.** A fit re-evaluates the same `Nmax`
  thousands of times, and each call allocated and zeroed two to three
  `(Nmax+1)^2` matrices — 1.4 MB per call at `Nmax = 300`. Measured on Apple
  silicon: `evaluate()` is ~1.07x faster for two species and 1.3x for ten,
  `get_1dhistogram` 1.30x. Species with zero amplitude and empty photon-count
  bins are now skipped outright, and the projection's bin cache is compact (one
  entry per visited cell, off-axis cells routed to a trash bin) so the inner
  loop is branchless with both streams contiguous. Note that the row-propagation
  kernel reads one element past each row's diagonal and so relied on the buffer
  being freshly zeroed; that slot is now zeroed explicitly per row rather than
  the whole matrix. `evaluate()`'s zero-padding warnings moved from `stdout` to
  `stderr`.

  Restructuring `conv_pF`'s background convolution kernel-tap-outermost — one
  contiguous axpy per tap instead of one short dot product per output cell — was
  tried and is **1.25x slower**: it stores every output element once per tap
  where the dot product keeps the accumulator in a register and stores once.
  Reverted, with a comment in the source so it is not attempted again.
- **`PDA_OPTIMIZED`'s multi-molecule FFT correction is documented and made
  safe**, not removed. It fires when `pF[0] < 1e-15` and changes the *model*, so
  the two implementations legitimately disagree there — a test now pins that.
  `pF[0] == 0` fed `log(0)` to a cast whose result is undefined; the order is
  now validated and capped. The doc comment claimed the opposite trigger
  condition. It would be better as an explicit opt-in flag than a silent
  threshold; that is left alone here because it is a model change, not a bug.
- `BurstFeatureExtractor`'s null-`BurstFilter` check throws and is translated in
  an `%exception` instead of calling `SWIG_exception_fail` inside the `%extend`
  body. A raise macro expands to language-specific wrapper code, which an
  `%extend` body is not. Python's behaviour is unchanged — the same `ValueError`
  with the same message — and the type's other methods gain the C++-exception
  translation `TTTR`'s already had.

- ImageJ plugin is built by Maven (`pom-scijava`) instead of raw `javac`/`jar`,
  and ships headless JUnit tests that run the commands through `CommandService`.
- The Java binding tests moved from `test/java` (`public static void main`) into
  the `tttrlib` Maven module as JUnit 5 (`ext/java/pkg/src/test/java`), run by
  `mvn -f ext/java/pkg test`. They now also cover BH SPC and Leica SP5/SP8.
- `ext/java/helpers.i` gained 2-D/3-D/4-D output-array marshalling macros;
  `get_mean_lifetime_into`, `get_fcs_image_into`, `compute_ics_into` and
  `get_fluorescence_decay_into` are exposed to Java through them.
- **Detectors and PIE windows, in chisurf's format.** The plugin reads and writes
  chisurf detector-setup JSON, so named detectors (routing channels with their own
  micro-time gates, G-factor and leakage) and named PIE windows carry across the
  two tools. *Open TTTR CLSM Image* driven by a setup produces one image channel
  per window × detector pair, each gated by the intersection of the two — not the
  cross product of the group and window fields, which cannot express two
  detectors gated differently inside one window. Parallel/perpendicular follows
  the imaging convention (even/odd *positions* in `chs`, or explicit
  `ch_p`/`ch_s`) and can be split into separate channels. Unmodelled keys
  (`mle_settings`, `fret_calibration`, LUTs) survive a round trip. New command
  **Detector Definition…**.

  Verified in both directions against chisurf's own code: files written here load
  through `chisurf.core.data_io.detector_setups.load_detector_setups` and
  `mle.setup.parse_detector_setup`, survive a chisurf rewrite, and read back
  unchanged. The channel list alternates VV, VH, VV, … so the split is by list
  position; chisurf's MLE parser used to split on channel-number parity, which
  silently swapped VV/VH for non-consecutive lists such as `[8,0,3]` — corrected
  in chisurf alongside this work.
- **Settings…** — defaults shared across commands (detector setup file plus the
  detector/window selection), remembered between sessions via SciJava's
  preference store and exportable as JSON.
- New ImageJ commands: **Lifetime Map** (IRF-corrected mean lifetime),
  **Fit Decay per Pixel…** (multi-exponential, parallel, with optional IRF),
  **Phasor Plot** (2-D g/s histogram plus the universal semicircle),
  **Pixel-wise FCS** (a correlation curve per pixel, lag on its own axis) and
  **Image Correlation (ICS)**.
- Documentation consolidated: the orphaned `docs/` tree (published nowhere, and
  the more detailed of two copies) was merged into the Sphinx tree under `doc/`
  and removed.
- **`SimCounterRandom::reset` now seeks in O(1) instead of burning draws** — the
  Philox path could not finish a run. Philox is *counter-based*: its output is a
  pure function of (key, counter), so an arbitrary position is reachable by
  setting the counter. `reset` instead walked there by generating and
  discarding `counter_start` values. Since `SimEngine` reseeds every molecule
  every window at `window * kWindowStride`, that burn grew with the window index
  and made a run **quadratic in window count** — ~1e10 draws at a few thousand
  molecules.

  The visible symptom was that
  `test_engine.py::test_rng_thread_count_independent[Philox]` never completed,
  which is why no full test-suite run had ever finished. It now takes 0.14 s,
  against 0.13 s for Pcg and 0.12 s for Xoshiro (whose resets were already
  O(1)); the whole suite runs in about five minutes.

  `Random::seek(draw_index)` positions the counter directly and reproduces the
  burned state **bit for bit**, verified at every offset class including block
  boundaries, so no simulation output changes. Guarded by
  `test_counter_rng_seek_is_constant_time`, which asserts the cost as a *ratio
  against Xoshiro* so the check cancels machine speed rather than hard-coding a
  wall-clock budget.

### Added
- **Becker & Hickl SPC-QC support (`SPC-QC` container, id 9).** The QC modules
  write `.spc` files whose record layout shares only its width with the classic
  SPC-130 one. The lower 28 bits are common to every event kind — 12-bit macro
  time (bits 0-11), 4-bit routing (bits 12-15), 12-bit ADC (bits 16-27) — and
  the top bits select photon / macro time overflow / marker / GAP. Unlike every
  classic SPC card the ADC value is **not** inverted. An overflow is the bare
  word `0x80000000` standing for exactly one wrap of 4096 units, with no count
  field. Both record layouts are supported: `BH_RECORD_TYPE_SPCQC_X04` (id 15,
  SPC-QC-104/004, 2-bit channel) and `_X06` (id 16, SPC-QC-106/006, 3-bit
  channel), chosen by a header flag. Reading, writing and auto-detection are
  wired up; `tttrlib.TTTR("file.spc")` picks the container by itself.

  The 4-byte header carries flags where the classic one has reserved bits:
  routing width, raw, markers, femto, six-channel, and only 22 bits of macro
  time clock. The femto flag is what lets the QC clock be expressed at all —
  2.048131 ns needs femtoseconds, where the classic 0.1 ns unit would round it
  to 2.0 ns.

  Since the detector is the input channel *and* the router signal, both are
  kept in `routing_channels` — routing low, input channel directly above it.
  B&H reserve four routing bits for this regardless (`MeasFCSInfo.chan`, "bits
  6-4 = input channel", so their `.sdt` calls a plain three-input measurement
  chan 0/16/32); tttrlib puts the input channel on the routing width the header
  declares instead, so the usual no-router case reads back as 0, 1, 2 and stays
  usable as a stream index, while a router still packs losslessly. The declared
  width is validated against the records, so an understated header cannot merge
  two detectors. Markers decode as markers with their type in the routing
  field, as on SPC-130, so CLSM reconstruction works unchanged. GAP records are
  photons — the flag only warns of a preceding FIFO overrun.

  Not supported: the QC "absolute time" FIFO mode, where the micro time instead
  holds the low 9 bits of a 4 ps absolute time. Nothing in the `.spc` header
  distinguishes it, so it cannot be detected from the file alone.

  New gallery example `examples/tttr/plot_tttr_spcqc.py` covers reading,
  conversion to PTU / Photon-HDF5 and back, and the byte-identical rewrite;
  `plot_tttr_file_conversion.py` gains a PTU -> SPC-QC leg.

  The layout was first derived from data and then checked against Becker &
  Hickl's published `SPC_data_file_structure.h`, which confirmed the field
  positions and supplied the parts the reference recordings do not exercise
  (markers, GAP, routing, QC-x06, the header flags). Where phconvert's BH
  reader disagrees with that spec, the spec was followed: phconvert packs the
  input channel into the *low* bits of its detector id and ignores the femto
  flag, applying femtosecond units unconditionally. Both halves of the record
  are pinned against SPCM's own output on all ten reference recordings:

  - *Micro times and channels* — histogramming the decoded micro times per
    channel reproduces the decay curves SPCM writes into the companion `.sdt`
    **bin for bin** (three channels with counts differing by 50×, so a channel
    permutation could not pass).
  - *The writer* — re-writing a reference measurement reproduces SPCM's record
    stream **byte for byte** over 13.1 million records (only the trailing
    overflow padding after the last photon is dropped). This is the check that
    a round trip cannot make: a writer that swaps two fields still round-trips
    through its own reader, and one did — the channel and routing fields came
    out transposed until this comparison caught it.
  - *Macro times* — a FIFO `.sdt` also carries a 1 ms intensity trace per
    channel. Binning the decoded macro times reproduces it to **54 of 3.1
    million bins** across the ten files, and nearly all of those are each
    channel's trailing partial bin, which SPCM truncates. This is a
    per-photon check: monotonicity and total duration alone would not catch a
    wrong overflow weight or a misplaced macro time field.

  The residual is a single global scale of 3.9e-8 — consistent across all ten
  files, so a property of the clock rather than of the decoding. It is below
  what the file can express: the header stores the macro time clock as a whole
  number of femtoseconds (2048131 on this module), one LSB being 4.9e-7, more
  than 12× coarser. Raw counter values are exact; only the conversion to
  seconds inherits this, worth ~4 µs per 100 s.

  One consequence of the format worth knowing: the QC modules run their TAC
  independently of the macro time clock, so the micro time resolution is *not*
  derivable from the `.spc` header (in the reference data the macro tick is
  2.048 ns while the TAC bin is 16 ps). tttrlib assumes the TAC range SPCM
  writes by default and replaces it with `SP_TAC_R`/`SP_ADC_RE` as soon as a
  `.set` sidecar is found, so check `header.micro_time_resolution` when reading
  a `.spc` that came without its `.set`.

- **HMM on a product alphabet: stream x micro-time bin.** `HMM.set_bursts_micro`
  (and `n_micro_bins` on `set_bursts_from_tttr` / `set_bursts_from_filter`) load
  each photon as the symbol `stream * n_micro_bins + bin`, so a state is
  constrained by *when* its photons arrive as well as by *where*. The engine's
  recursions were already alphabet-agnostic -- they read `obs[i*p + y]` and never
  ask what `y` means -- so this is a wider emission table, not a second code
  path, and `n_micro_bins == 1` remains bit-identical to `set_bursts`.

  What it buys is the one thing intensity alone cannot do: separate a **dark
  acceptor from real FRET**. Both move the donor/acceptor ratio; only transfer
  also shortens the donor lifetime. On simulated data where the two states have
  an *identical* ratio by construction, per-photon Viterbi accuracy against the
  known path is 0.53 (chance) on the stream alphabet and 0.77 with micro-time --
  reproducing the numpy prototype's 0.527 / 0.782.

  `HmmModel` gained `n_micro_bins` and `n_symbols()`; `n_streams()` now means the
  detector-stream count and `n_symbols()` the width of `obs`. They are equal
  unless a micro-time axis is set, which is why the split has to be carried
  explicitly -- a 128-column table is 128 streams or 4 streams x 32 bins and
  nothing in the numbers says which. `optimize` now rejects a model whose
  emission width does not match the loaded alphabet rather than reading past the
  end of its own table.

- **`HmmEmissionSpec`** (`include/HMMEmission.h`) -- builds that emission table
  from a per-state, per-stream **lifetime spectrum** plus a stream split:
  `P(stream|state) * f_{state,stream}(bin)`, with `f` a multi-exponential decay
  optionally convolved with an IRF pattern and mixed with a background shape.
  Evaluated through `SimDecay`, so the object that draws micro-times in the
  simulator is the object that scores them in the likelihood.

  It knows no physics: there is no Forster radius here, no linker width, no
  crosstalk matrix. A state is described by what *scoring* needs, and the map
  from a structure onto a lifetime spectrum lives outside the library and
  enters as a prior. That boundary is what keeps the header short.

- **Parameterised emission sampling: `sample(..., emission)`.** The sampling
  counterpart of `optimize(..., emission)`, and required on a product alphabet.
  Left free, the emission is drawn as a categorical over every column -- the
  degenerate family the parameterised M-step exists to avoid -- so the chain
  wanders out of a good lifetime fit and drags the sampled paths, and hence the
  transition counts, with it. The failure therefore shows up in the
  **transitions**, not the emission, which is what makes it confusing to find.

  Supplied, the stream split stays a conjugate Dirichlet draw and each lifetime
  gets one **univariate slice update**, scored through the same `q_of_tau` the
  M-step maximises so sampler and optimiser cannot drift apart. On 64 micro-time
  bins, same wall-clock:

  | emission | R-hat | ESS | 95% interval width |
  |----------|-------|-----|--------------------|
  | free     | 1.443 | 48  | 0.00444            |
  | **parameterised** | **1.025** | **186** | **0.00084** |

  The interval is 5.3x tighter and still covers the truth, so the free version's
  width was the wandering emission rather than genuine uncertainty.

  **Slice sampling was chosen on measurement, not principle.** Benchmarked as
  effective samples per log-density evaluation -- the fair unit, since a slice
  update costs several calls -- a well-tuned random-walk Metropolis wins
  (168 vs 128) and a badly-tuned one loses by 3.6x (17 vs 62). Across step sizes
  Metropolis spans 10x, slice 2x. Inside a Gibbs sweep the conditional's scale is
  unknown, differs per (state, stream) and drifts as the fit moves, so the
  untuned column is the one that applies -- and matching Metropolis's best would
  mean carrying dual-averaging adaptation per cell. `hmm_rand::slice_sample` is
  ~40 lines, std-only, and correct at any interval width.

- **`HMM.sample` -> `HmmPosterior`: blocked Gibbs over (pi, A, B).** EM returns
  one model; this returns a distribution over them, which is what a credible
  interval needs and what no post-processing can recover from a point estimate.
  Every step is conjugate, so there is nothing to tune: sample the photon-level
  path (FFBS), sample the tick-level bridge through each gap, count, then draw
  `theta ~ Dirichlet(counts + alpha)`. Restraint concentrations are used
  **as-is**, not as `alpha - 1` -- the MAP M-step wants the mode, a sampler
  wants the distribution.

  `HmmPosterior` carries the raw draws plus `mean`/`sd`/`quantile`, split-Rhat
  and ESS. On a 7500-photon dataset, 4 chains x 1000 draws seeded from an EM fit
  reach Rhat 1.005 / ESS 539 in ~40 s and recover the generating transition
  rates inside their 95% intervals.

  Three things that look like bugs and are not, each pinned by a test:

  - **The bridge is what makes the counts one-tick transitions.** FFBS gives the
    state at each *photon*, but `trans` is the one-tick matrix, so each gap
    needs an endpoint-conditioned draw through it. It is built without a dense
    dt-indexed cache: only the column `A^s e_v` is ever needed, so a backward
    recursion gives every intermediate power in `O(dt*n)` scratch. Materialising
    matrices instead would rebuild exactly the `n^2 * dt_max` allocation the
    engine's sparse cache exists to avoid.
  - **Diagnostics relabel; draws do not.** Two chains each holding a *stable*
    but opposite labelling gave a raw split-Rhat of 15.2 and a relabelled one of
    1.75. Both chains were correct -- the raw statistic was comparing "state 0"
    in one against a different state in the other. For exchangeable states,
    relabelling first is what makes Rhat mean anything.
  - **Chains disperse around the model, not from the prior.** A flat Dirichlet
    on a transition row starts a 2-state chain near `A01 = 0.5`, which for
    sticky data is a genuine second mode (fast switching, blurred emissions)
    that no practical number of sweeps escapes: chains seeded that way sat at
    `A01 ~ 0.3` against a truth of 0.005, while a chain started at the truth
    stayed there and mixed cleanly.

- **Analytic Gaussian IRF: `HmmEmissionSpec.irf_center` / `.irf_fwhm`.** A
  photon's micro-time is the sum of the memoryless excited-state time and
  everything the instrument adds (finite pulse width, detector jitter), so its
  density is an exponential convolved with a Gaussian -- the
  exponentially-modified Gaussian, after Tavakoli *et al.*'s Eq. 7. Setting a
  centre and FWHM computes each bin's probability as a **difference of CDFs**
  instead of convolving a sampled IRF pattern.

  This is exact at any resolution: a 32-bin table equals an 8192-bin one summed
  over the bins it merges, to round-off. The sampled-IRF route it replaces
  carries ~1.7e-3 aliasing error at 256 bins and needs ~4096 bins to reach 1e-6.
  The two converge as the grid is refined, which is what checks the closed form
  against something independent of itself. `irf` (a measured pattern of
  arbitrary shape) remains, and is now rejected if combined with `irf_fwhm` --
  setting both would convolve the Gaussian in twice, silently.

  Both the table build and the M-step objective now go through one kernel
  (`component_bins`), so a lifetime cannot be fitted against a no-IRF shape and
  then scored with an IRF-convolved one -- which would bias every lifetime by
  the IRF's offset.

- **Measured decay patterns: `HmmEmissionSpec.set_pattern()`.** A decay usually
  arrives as a *measured pattern* -- a donor-only reference, a scatter pattern --
  not as amplitudes and lifetimes, which is why `SimDecay` treats a pattern as
  its first-class representation. Any length is accepted and aggregated onto the
  emission axis, which is **exact** (a bin's probability is the sum of the source
  channels in it; verified against the analytic table to 1e-17), so an
  instrument-resolution decay can be handed over and coarsened by `build()`.

  A supplied pattern is the shape, not a starting guess: neither `fit_counts` nor
  `sample_counts` touches it, and only the stream split stays free. That is more
  robust than fitting a lifetime, since no decay parameter is left to be
  unidentifiable -- and it sidesteps the multi-exponential binning cost entirely.
  **No IRF is applied to a supplied pattern**, deliberately: a measured decay
  already contains the instrument response. Spectra and patterns can therefore be
  mixed in one spec without the IRF being applied inconsistently.

- **Refining physics against photons, via `HMM.evaluate`.** What gets refined is
  not the pattern -- that is the free categorical again -- but the physical
  parameter that generates it. `evaluate` returns one E-step's sufficient
  statistics, so an external optimiser can own the M-step:

      ev = eng.evaluate(model);  A = row_normalize(ev.xi);  R = refine(R, ev.gamma_obs)

  The new example `plot_hmm_distance_refinement.py` runs this on a **3-state**
  system where each state is a *distance distribution*: from a start of
  35/55/75 A against a truth of 42/52/64, 30000 photons recover
  **42.11 / 52.68 / 63.95 A** with the kinetics refined in the same loop
  (k01 0.00097 vs 0.00080, k12 0.00059 vs 0.00060) and a monotone
  log-likelihood. The engine never sees a Forster radius.

  `plot_hmm_blinking_acceptor.py` pushes the same loop to the hardest case: a
  **multi-exponential donor** quenched *homogeneously*
  (`1/tau_i,DA = 1/tau_i + k_FRET(R)`), three conformations that are each a
  **Gaussian distance distribution**, and an **acceptor blinking on 10 us** --
  faster than a burst, so it toggles within the observation. 30 exponentials per
  state, all generated from one scalar. The state space is a product
  (conformation x photophysics) with a single shared dark state, since three
  dark states would be emission-identical. Recovered: blink-off **0.0986/tick**
  against 0.10, distances **41.0 / 50.6 / 60.6 A** from a clustered 45/50/58
  start against a truth of 40/52/65, dark-state recall 0.91 / precision 0.72.
  The 65 A state coming back short is the measurement's identifiability limit,
  not the fit's: a dark acceptor and a distant one converge as R grows.

- **Corrected: per-burst coincidence detection does not work, and the previous
  AUC 0.87 was wrong twice over.** It came from bursts made coincident by
  concatenating photon lists -- so they held more photons by construction, and
  the detector was scored on how the test data were built -- and from a single
  dataset. Re-measured against `SimEngine` ground truth (`emitting_molecule()`
  names the emitter of each photon, so diffusion decides the overlap) and
  replicated over independent seeds, every statistic is at chance: duration
  **0.53 +- 0.04**, photon count **0.53 +- 0.04**, peak rate **0.47 +- 0.04** at
  5.3% coincidence, and 0.54/0.55/0.54 at 17.7% -- no better in the crowded
  sample, where the "clean" bursts are contaminated too. The hypothesised "rate
  step coincident with an E change" signature is also at chance.

  **Replication mattered more than the measurement.** At 5% coincidence a single
  run holds only a handful of coincident bursts and single-run AUCs ranged
  **0.36-0.63** across seeds -- one seed looks like a publishable detector, the
  next is anti-correlated. `SimEngine` is deterministic unless
  `SimIntegrator.seed_diffusion` / `seed_emission` are varied, so an earlier
  attempt at "pooling runs" was pooling identical copies.

  The recommendation changes accordingly. Discarding the longest half of all
  bursts moves contamination only 5.3% -> 4.8%, at ~25 clean bursts lost per
  coincident burst removed. Occupancy is the better lever but saturates
  (0.125/0.25/0.5/1.0 -> 2.5/5.3/9.3/17.7%): halving it buys ~1.9x when crowded
  and ~2.1x at the sparse end, and spurious switching is measurable even at the
  lowest occupancy tried. **There is no setting at which coincidence goes away**
  -- it is an acquisition-design problem, not an analysis one. The coincidence
  *rate* is predictable as `1 - exp(-lambda*tau)` with `tau` the **transit**
  time; the detected burst duration underestimates it by over an order of
  magnitude, since a burst is only the above-threshold part of a transit.

  New example `plot_hmm_coincidence.py` demonstrates the whole chain: static
  species that appear to switch, ROC curves that hug the diagonal, and the two
  levers on one cost axis.

- **Decided: no per-state brightness read-out.** Only the *ratio* between two
  states is identifiable, and `(photons in state)/(time in state)` was expected
  to be safe because state and position in the focus are independent, so the PSF
  envelope should cancel. Measured (true ratio 3.00, replicated over seeds), it
  does not:

  - with the **true tick-level path** the estimator is unbiased at every
    switching rate (3.00-3.09), so there is no information ceiling -- what is
    lost is attribution, since the state is known only at photons and a gap gets
    charged to the state at its start even when the chain switched inside it;
  - **the envelope does not cancel**, and the bias grows with focus depth:
    2.96 flat, 2.66 at 55x, 2.19 at 1e6x -- all at *slow* switching, where the
    attribution error is absent;
  - fast switching adds its own collapse: 1.99 flat, 1.65 with an envelope.

  The planned mitigation -- ship it behind a guard on switching rate -- would
  have caught only the second error and left a number that looks trustworthy and
  is 10-27% low. The tick-level bridge in `HMM.sample` does not rescue it
  either: it conditions on endpoint states but not on the gap having contained
  *no photons*, and using that evidence is exactly the rate-aware likelihood
  that would attribute the PSF transit to state changes. This also corrects an
  earlier note claiming the true path degraded with switching rate; that number
  came from the photon-level path.

- **Phasor diagnostic for a converged HMM fit** -- new example
  `plot_hmm_phasor_diagnostic.py`, and no new API: `HMM.evaluate` already
  returns the posterior-weighted per-state micro-time histogram (`gamma_obs`),
  and `DecayPhasor.compute_phasor_bincounts` already computes phasors. Compare
  the data phasor with the model's, per state.

  Measured on data with a multi-exponential donor, fitted once with a
  mono-exponential spec and once with the generating one, over 5 datasets: both
  recover E to **0.247 / 0.700** against a truth of 0.25 / 0.70 -- so the
  headline number gives no warning -- while the phasor distance separates them
  **4.6x** (0.0435 +- 0.0040 vs 0.0095 +- 0.0017, no overlap). E is set by the
  stream split; the misspecification lives in the decay shape.

  It is a *relative* diagnostic: the correct model does not score zero, because
  the distance carries sampling noise and because binning and truncation move
  the data and model points differently. For the same reason, distance from the
  universal semicircle is not a clean multi-exponentiality test here. The
  likelihood still ranks the models (+91 nats); the phasor adds *which* state is
  failing, from a statistic that never saw the model.

  Sharp edge worth knowing: `DecayPhasor` divides by `g_irf^2 + s_irf^2`, so
  passing `(0, 0)` for "no IRF" silently returns `nan`. The identity is
  `(1, 0)`, the phasor of a delta response.

- **Burst bootstrap for the maximum-likelihood path** -- new example
  `plot_hmm_bootstrap.py`, and again **no new API**: a replicate is the same
  dataset with its burst list resampled, and both loaders already accept a burst
  list with duplicate rows. On 313 bursts / ~32k photons that is ~7 ms per
  replicate with no photon data copied, so a 200-replicate interval costs
  seconds.

  Measured coverage of a nominal 95% interval (40 datasets x 100 replicates):
  burst bootstrap **94.4% +- 1.8%**, `posterior_sd_analytic` **~63%**. The
  analytic gap is what its own docstring predicts -- conditioning on the state
  path drops `Var(E[theta|y,path])`, making it a lower bound at roughly half
  width -- so it must not be quoted as an error bar.

  Two floors, both measured: coverage by burst count runs 90/94/93% at 20/40/120
  bursts, and by replicate count 89.4% at 40 versus 94.4% at 100 and at 250 --
  so **use at least 100 replicates**. States must be ordered canonically before
  summarising or the spread measures label switching instead of uncertainty.

- **PRD-011 rewritten** as `okf/prds/PRD-011-photon-hmm.md` (Done), replacing
  `PRD-011-physics-aware-h2mm.md`. The old framing -- "make H2MM physics-aware"
  -- was the source of most of its difficulties, and several premises were
  contradicted by measurement. Corrected: tttrlib owns MAP/Gibbs/`evaluate` and
  is fully self-validating (the draft said it "adds no inference engine beyond
  MAP"); "bit-identical" replaced by 1e-10 relative; "mode blending" deleted;
  the ELBO demoted from criterion to heuristic (and VB not shipped at all);
  nonparametric state counting moved from out-of-scope to working (4/4 vs BIC's
  3/4); `DecayState` replaced by `SimDecay`; "adopted by the research community"
  replaced by CI gates. Adds the sections the draft lacked entirely --
  multi-molecule coincidence, and the four features closed as will-not-build
  with the measurement behind each. The PRD index also gained rows 008-011,
  which had never been added.

### Fixed

- **`DecayPhasor` no longer fails silently.** The IRF correction divides by
  `g_irf^2 + s_irf^2`, so `(0, 0)` -- the natural way to write "no IRF" -- was a
  division by zero returning a quiet `nan` that propagated into every downstream
  result. Invalid arguments now raise `std::invalid_argument` (`ValueError` in
  Python) with a message naming the fix; the identity IRF phasor is `(1, 0)`,
  and it is now the **default** for `compute_phasor_bincounts`, which previously
  required all five arguments.

  Also guarded: non-finite `g_irf`/`s_irf`, an IRF phasor too small to invert,
  non-finite or non-positive `frequency`, negative `n_microtimes`, and a null
  `microtimes` pointer.

  **`compute_phasor` bounds-checks the caller's index selection.** It previously
  read `microtimes[idx]` for every entry of `idxs` with no bounds check -- an
  out-of-bounds read on a stale or mis-sized index vector.

  **`CLSMImage::get_phasor` no longer uses the "too few photons" sentinel as a
  calibration.** When the supplied IRF held too few photons, `compute_phasor`
  returned `{-1, -1}` and `get_phasor` fed it straight back in as the IRF
  phasor. That is not a division by zero -- its modulus is 2 -- so it sailed
  through and rotated every pixel by 225 degrees while halving it. It now raises.

  The "too few photons" sentinel `{-1, -1}` is unchanged for the
  `compute_phasor*` return values: that is a property of the data, not a
  mistaken call, and the two are now deliberately distinguished.

- **`HmmEval.posterior_sd_analytic()`** -- a closed-form posterior width, with no
  sampling. Conditional on the state path the posterior is Dirichlet, so the
  E-step's expected counts give one directly. Measured against `HMM.sample` on
  7500 photons: **364x cheaper** (0.04 s against 16.3 s), means agreeing.

  It is documented, and tested, as a **lower bound on the width** -- measured
  1.92x to 2.11x too narrow. The reason is structural:
  `Var(theta|y) = E[Var(theta|y,path)] + Var(E[theta|y,path])`, and only the
  conjugate term survives; uncertainty in the *path* is dropped, and there it was
  3x larger. The factor is not a constant to divide out -- it depends on how well
  the path is determined, and simulation-based calibration of the equivalent
  variational approximation measured 2.3-3.4x on other data. Use it for point
  estimates and a fast first look; use `HMM.sample` for an interval.

- **`HMM.evaluate` -> `HmmEval`** -- one E-step's log-likelihood, sufficient
  statistics (`xi`, `gamma_obs`, `prior_counts`) and score. All of it is already
  computed inside an EM map and then discarded, so this costs exactly one
  forward-backward pass. The statistics summarise the dataset at the *model's*
  dimension rather than the data's, which is what lets a host drive its own
  optimiser or sampler over an HMM submodel without re-touching photons. Counts
  are raw -- restraints never enter, since a consumer supplying its own prior
  would otherwise double-count.

  The score is Fisher's identity, `count / parameter`, so the gradient comes
  free from the same pass. **It is meaningful along the simplex, not off it**,
  and the distinction is not cosmetic: `prior` and `obs` match central finite
  differences entry by entry, but `trans` matches only in *within-row
  differences*. The `A^dt` cache is built by `matmul_norm`, which row-normalises
  after every composition -- a no-op for a row-stochastic matrix, but it makes
  the likelihood invariant to scaling a row, so the off-simplex component of the
  gradient is an artifact of that renormalisation. A finite-difference check
  must therefore perturb `(A_ij + h, A_ik - h)` rather than one entry alone.
  Both the correct behaviour and the artifact are pinned by tests.

- **Parameterised emission M-step: `optimize(..., emission)`.** The remedy for
  the failure documented below. Instead of freeing every column of `obs`, the
  M-step re-fits the `HmmEmissionSpec`'s decay parameters from the expected
  counts, and updates the spec in place so the fitted lifetimes can be read
  back. It factorises exactly, so it costs almost nothing: with
  `obs[i][k,b] = p_ik * f_ik(b)`, the stream split keeps the categorical
  M-step's closed form and only the lifetime needs a bounded golden-section
  search -- one scalar per (state, stream).

  On the configuration where free EM falls from 0.775 to 0.509 per-photon
  accuracy, this reaches **0.776 from a deliberately wrong seed** (8.0 / 0.8 ns
  against a truth of 4.0 / 2.0), and the fitted table has no interior holes by
  construction, since no lifetime spectrum can put a zero mid-decay.

  Restraints and constraints on `obs` do not apply -- the family is itself the
  constraint, and a prior on a *lifetime* belongs on the lifetime rather than on
  the table it generates. SQUAREM is disabled on this path because an
  extrapolated table need not be reachable from any parameters. Only
  mono-exponential components are re-fitted for now; a multi-component spectrum
  keeps its shape and contributes through the stream split.

- **`SimDecay::pdf(bin)`** -- the normalised density beside the alias table. The
  alias method samples in O(1) but cannot be read back as a density, so scoring
  previously had no way to reuse a simulator decay.

  **On counting statistics, since a fine micro-time axis invites the question.**
  Scoring needs no Poisson term, structurally rather than approximately: the
  likelihood is a product over photons of `P(symbol|state)` and no micro-time
  histogram is ever formed, so there are no bin counts to carry Poisson noise.
  Conditioned on the photon count `N`, a Poisson likelihood factorises into
  `Poisson(N)` times exactly the per-photon categorical this engine scores.
  Resolution is therefore a scoring-accuracy knob at no statistical cost. The
  one piece genuinely omitted is the information in `N` itself -- state
  **brightness** carries no likelihood weight, since macro-times only propagate
  the chain -- and recovering it needs a state-dependent Poisson rate per gap,
  a likelihood extension rather than a correction.

  The problem appears instead in **estimation**, and it is structural rather
  than a matter of resolution. Micro-time bins are not free parameters: they are
  one smooth decay of about two parameters, sampled onto the TAC grid. `fit()`
  re-estimates every column independently, which discards that and admits models
  no decay can produce -- an exact zero in the *interior* of an exponential.
  Measured on two states differing only in donor lifetime, per-photon decoding
  accuracy against the known path: free `fit()` sits at chance (0.52-0.53) while
  an `HmmEmissionSpec` table holds 0.784-0.797 across 16-1024 bins.

  Worse than a search failure: started at the **exact generating model**, free
  EM sometimes walks away from it -- three of sixteen runs over four macro-time
  seeds and four bin counts fell from ~0.78 to ~0.51, and *not* monotonically in
  bins (128 fine, 256 collapsed, 512 mixed, 1024 fine), so no bin count is the
  safe one. Dirichlet restraints remove every zero -- the `log(0)` hazard really
  does go -- and leave accuracy near chance, because the obstacle is the model
  family, not the sparsity. `fit()` and `optimize` are now documented as unsafe
  on a product alphabet, with the signature (interior zeros in a decay) pinned
  by tests; the fix is the parameterised M-step, still to come.

- **s2ISM** (`CLSMSuperRes.s2ism_reconstruction`) -- joint super-resolution and
  optical sectioning by adaptive maximum-likelihood deconvolution over a stack
  of axial planes, after Zunino et al., *Nat. Photonics* (2025). A real port of
  the reference `s2ism/s2ism.py` (`amd_update_fft`, `amd_stop`,
  `max_likelihood_reconstruction`): the detector array is treated as Nch images
  of one object seen through Nch PSFs and inverted jointly by multi-image
  Richardson-Lucy, with the sectioning coming from giving the object several
  axial planes with their own PSFs. A line-by-line numpy transcription of the
  reference is the test oracle and the port matches it to round-off (3e-15
  relative). Two details the parity test pins down: the PSF flip is numpy's
  (about index (N-1)/2 -- conjugating the spectrum instead is a circular flip
  about 0 and lands one sample off per axis), and the update carries no extra
  normalization because each plane's PSF is already normalized over
  (channel, y, x).

- **SOFISM** (`CLSMSuperRes.sofism_reconstruction`) -- super-resolution optical
  fluctuation image scanning microscopy, after Sroda et al., *Optica* **7**, 1308
  (2020), in the formulation restated by Beck et al., arXiv:2606.16508. It
  multiplies the two independent resolution mechanisms of ISM and SOFI: for
  every pair of detector elements the temporal cross-correlation of the
  fluctuations `C_ij(r,tau) = 1/(N_t-tau) sum_t dI_i(r,t) dI_j(r,t+tau)` is
  formed, whose effective PSF is the *product* of the two elements' PSFs; the
  pair acts as one virtual detector midway between them and is reassigned by
  `v_ij = (v_i + v_j)/2`. Autocorrelation terms are excluded by default, since
  their shot noise does not cancel. On a simulated blinking sample the measured
  spot narrows 9.30 -> 6.11 -> 4.74 px going confocal -> APR-ISM -> SOFISM, a
  factor 1.96, matching the paper's stated ~2x before Fourier reweighting.
  `CLSMSuperRes.fourier_reweight` applies the Wiener-type filter
  `W(k) = 1/(OTF^2(k) + eps)` of the paper's Eq. 3.

- **The optical models moved into the installed library.** `jones_vector`,
  `airy_psf`, `vectorial_psf`, `detector_grid` and `psf_volume` are now
  `CLSMSuperRes` methods rather than functions in `prototype/esrrf/simulate`,
  so a consumer outside this repository -- a GUI, a deconvolution -- imports
  them from tttrlib instead of reaching into a prototype directory. The
  prototype re-exports them, so the examples are unchanged.

- **A better ISM PSF model.** `prototype/esrrf/simulate` gains `detector_grid`,
  ported from BrightEyes-ISM `detector.rect_grid`/`hex_grid`, so the simulated
  array can be hexagonal rather than only square -- a 5-per-side hexagonal grid
  gives the 23 elements of the SPAD23G array the CW-SOFISM work uses. It also
  gains `airy_psf`, the exact scalar diffraction PSF, selectable with
  `model=airy`. Cross-validated against the analytic limit: the first zero
  lands at 0.61 lambda/NA and the FWHM at 0.51 lambda/NA. The point of having it
  is that the Gaussian approximation tracks the core closely but has no wings,
  and holds ~43x less energy beyond the first zero -- which is exactly where
  out-of-focus background and element-to-element crosstalk live, so any result
  judged on a Gaussian-simulated background inherits that error. The model
  Alongside it, `vectorial_psf` implements the Richards-Wolf integral
  (`model="vectorial"`), which matters because the examples run at NA 1.4 where
  the scalar approximation is not defensible: the longitudinal field is not
  small, and with linear illumination the focal spot is **elongated along the
  polarization axis by about a third** (FWHM 256 nm along x against 192 nm
  along y at NA 1.4) -- an asymmetry no scalar or Gaussian model can produce.
  Even for circular polarization the scalar model underestimates the FWHM by
  17%. Validated by convergence to the Airy pattern as the aperture closes
  (agreement to 3e-4 at NA 0.1). Detection uses the circularly averaged form,
  which is the right one for an incoherent sum over dipole orientation.
  The state entering the pupil is selectable: `'x'`, `'y'`, `'linear'` at any
  `angle_deg`, `'circular'`, `'left'`, `'right'`, `'unpolarized'`, the
  cylindrical vector beams `'radial'` and `'azimuthal'`, or an explicit
  `(Ex, Ey)` Jones pair for any elliptical state. Each carries a signature that
  the tests check at NA 1.4: linear elongates along its own axis and x/y are
  mirror images; circular, left-circular and unpolarized are indistinguishable
  in intensity; radial focuses *tighter* than circular (192 nm against 224 nm),
  its longitudinal lobe being the point of using it; and azimuthal has an exact
  on-axis zero, being a doughnut. Aberrations and the Zernike pupil of
  BrightEyes-ISM `PSF_sim` are still unmodelled.

### Fixed
- **eSRRF now reproduces NanoJ.** The RGC kernel deviated from
  `liveSRRF.cl` in four places, none of which showed at the image centre and
  all of which showed at the borders: the 2x gradient upsampling read a
  zero-padded buffer instead of clamping to the native width and height; `Gy`
  was fetched with `Gx`'s sub-pixel index shift (NanoJ shifts each gradient
  along its *own* axis); the `0 < vx < width` / `0 < vy < height` guards that
  keep out-of-frame samples out of both the numerator and `distanceWeightSum`
  were missing; and `getInterpolatedValue`'s bilinear extrapolation branch,
  which NanoJ takes wherever the bicubic 4x4 support does not fit, was not
  ported at all. The numpy oracle in `prototype/esrrf/esrrf_reference.py` had
  the same three last defects and so agreed with the C++ about being wrong; both
  are fixed, and the A/B test now demands a double-precision match over the
  whole field instead of a median with a 0.1 tolerance on the tail.

- **eSRRF photon reassignment.** The magnified flat position was accumulated in
  `int`, overflowing above ~2^31 (a 512x512 frame at magnification 8 wraps at
  128 frames). In `channel_mode="split"` the RGC field was picked with
  `event_index % n_channels`, which is not the photon's channel; it now comes
  from the frame block the photon belongs to, via the new
  `CLSMImage::get_channel_of_frame`. `CLSMSuperRes::write` discarded the
  container writer's return value and always reported success. `temporal_combine`
  divided by `n_frames - 1` for `TAC2` and produced infinities on a single frame.

- **`CLSMImage::get_photon_positions`** doubled the position along a line in its
  materialized-pixel path: the time offset is measured from the line start, so it
  already contains the pixel index that was being added to it.

- **ISM reconstructions were quantitatively wrong.** The inverse FFT applied no
  `1/N` normalization (pocketfft never normalizes on its own, contrary to the
  comment claiming it did), so every shifted detector channel came back scaled
  by the number of pixels and APR did not conserve photon flux. Adaptive pixel
  reassignment also ignored both of its documented knobs -- `usf` (so shifts were
  integer-pixel only) and `filter_sigma` -- and omitted the Hann apodization the
  BrightEyes-ISM reference applies before correlating. The estimator now
  reproduces `skimage.registration.phase_cross_correlation(..., normalization=None)`
  on apodized, optionally denoised input, including the upsampled-DFT peak
  refinement, and is exposed as `CLSMSuperRes.shift_vectors`.

- **The ISM subpixel shift no longer wraps around the frame.** It is a Fourier
  shift, which is periodic, so content leaving one edge reappeared at the
  opposite one -- with a bright structure at the left of the field, the far
  right margin carried 3-18% of the peak where it should carry nothing. The
  shift is now applied on a zero-padded canvas and cropped back, which also
  confines the interpolation's ringing (far-margin leakage drops to ~0.2%).
  Photons registered off the edge of the frame are now dropped rather than
  wrapped, so an APR sum is no longer exactly equal to the input total; the
  focus-ISM planes still partition the reassigned total exactly. Note that the
  BrightEyes-ISM reference defaults to `mode='interp'` (a compact-support
  spline) rather than the Fourier shift ported here; the boundary behaviour now
  agrees, the interpolation kernel still differs.

- **Focus-ISM follows the published algorithm again.** The separable
  least-squares core (solve the mixing fraction *B* exactly at each trial
  background width) was sound and is kept, but everything around it was
  invented: a confidence weight and a ridge shrinkage that biased *B*, an
  edge-aware smoothing of the *B* map, detector coordinates inferred from
  measured shifts rather than the known lattice, an unused APR pass, and a
  `threshold` parameter that was accepted and ignored -- where the reference
  assigns sub-threshold pixels wholly to background. It now follows
  `FocusISM_lib.focusISM`/`pixel_fit_2`: APR first, the in-focus width fitted
  from the calibration-patch fingerprint, `sigma_bound` as the reference's
  `sigma_B_bound`, and the threshold honoured.

### Removed
- `CLSMISM` is gone; every ISM algorithm now lives on `CLSMSuperRes`, which had
  been forwarding to it. `apr_reconstruction` returns `(1, ny, nx)` and
  `focus_reconstruction` returns `(3, ny, nx)` -- in-focus signal, background,
  APR sum -- so the `nz` parameter, which only replicated the same plane, is
  gone with it.
- The old `CLSMSuperRes.s2ism_reconstruction`, `.deconv_reconstruction` and
  `.generate_ism_psf`. The first was a variance-weighted APR carrying the name of
  a different published method -- the real s2ISM is now implemented, see Added;
  the second a hand-rolled Richardson-Lucy against
  a hardcoded Gaussian, not ISM deconvolution; the third imported from the repo's
  `prototype/` directory at call time and otherwise silently fell back to a
  Gaussian approximation. The PSF simulator remains available to examples as
  `prototype/esrrf/simulate.generate_ism_psf`.
- `CLSMSuperRes.frc_resolution` returned an FRC *curve*, not a resolution, and
  was not a port of anything. It is now `frc_curve` plus `frc_resolution`,
  ported from BrightEyes-ISM `FRC_lib`: Hann apodization before the transform
  (without it the spectral leakage from the frame edges correlates perfectly
  between the two halves and holds the curve up at every frequency), the
  reference radial binning and frequency axis, LOWESS smoothing or a sigmoid
  fit, and the fixed 1/7, 3-sigma and 5-sigma threshold criteria. Verified
  against the reference: the raw curve to 2e-15, the frequency axis exactly,
  and the resolution to nine digits for both criteria. The LOWESS is
  reimplemented rather than taken from statsmodels, which meant matching its
  window choice -- the r *nearest* points, where a symmetric window twice as
  wide oversmooths and shifts the resolution by 0.25%.

### Changed
- **`H2MM` is now `HMM`, and H2MM is what the algorithm is called.** The class,
  its headers (`HMM.h`, `HMMSurrogate.h`), its helper types (`HmmModel`,
  `HmmChannelMap`, `HmmStateSidecar`, `HmmSurrogate`), its constants
  (`HMM_UNASSIGNED`) and the test/doc tree all drop the `H2` prefix. Nothing
  else changes: `fit()` still runs the published H2MM algorithm of Pirchi et
  al., bit for bit.

  The rename is worth the churn because the class is about to hold *more than
  one* inference path — the H2MM maximum-likelihood EM, and a Bayesian
  physics-aware path beside it. Naming the container after one of its algorithms
  would have made the second one look like an intruder in its own class. No
  alias layer ships, because `H2MM` never appeared in a tagged release (the
  newest tag is `v0.26.2`; the `0.27.0` section below is untagged), so there is
  nothing to deprecate.

  `H2MM_C` keeps its name throughout — it is a different project.

- **`optimize` takes restraints and constraints, and they are different things.**
  `HmmRestraints` are **scored**: Dirichlet concentrations on π/A/B (exactly
  conjugate to the E-step's raw counts, so the MAP M-step is `counts + (α − 1)`
  with no approximation) plus arbitrary `DecayFitPrior`s on decay parameters.
  They contribute `log p(θ)` to the objective, so the fit trades them against
  the likelihood. `HmmConstraints` are **imposed**: pinned entries of π, A or B
  that hold exactly, are re-applied after every M-step *and* after SQUAREM's
  extrapolation, and never enter the objective at all.

  Keeping them apart is not pedantry. A hard constraint expressed as a very
  sharp prior would be only approximately satisfied and would dump a large term
  into the objective that swamps any comparison between models; a scored
  restraint imposed as a constraint could not be violated by evidence that
  contradicts it. They also arrive from different places — restraints
  deserialised from an external physics model, constraints asserted locally by
  whoever knows the experiment — which is why each round-trips through JSON on
  its own.

  Supplying restraints switches **every** convergence and SQUAREM accept test to
  the penalised objective `logL + log p`; comparing marginal likelihoods there
  would stop in the wrong place and accept steps that lower the posterior.
  `HmmModel` gains `logpost` so the distinction is observable from Python:
  restraints move it away from `loglik`, constraints leave it equal. With both
  absent the MAP path is **bit-identical** to plain EM, which is what makes the
  two paths sharing one loop safe rather than something to re-argue after every
  change — and it is tested on both the plain and SQUAREM paths.

- **One interface for every decay fit.** A fit is now built by registry name and
  called the same way whatever the model: `DecayFit2("fit23", setup, irf)`, a
  `DecayFitProblem` holding the measurement, a `DecayFitConstraints` saying what
  may move, and an outcome carrying `parameters`, `results` and `objective`. The
  per-estimator classes, their per-model `fit_matrix`, and `DecayFitData` are
  gone; `DecayFitNExp` is registered as `fit_nexp` and is reachable the same way
  (it never appeared in the registry before).

  Parameters, setup values and results each cross as one flat `double` array
  whose slots the registry names, so nothing counts positions. `FitRegistry.cpp`
  now states the **flattening rule** normatively, gains JSON-Schema `array`
  properties with a `count_from` link for variable-length blocks, a symmetric
  `results_schema`, an `objective` category replacing the two boolean flags that
  used to ride inside the parameter vector, and `n_patterns`/`supports_*`
  capability flags. `decay_fit_setup_vector(name, json)` builds a setup block
  from named values in **every** binding, so R and Java are not left assembling
  it by hand.

  New capabilities that fell out of the unification: `fixed` becomes an integer
  **link vector** (negative fixed, `0` free, `k>0` shares one value across every
  slot in group `k`); a parameter may carry a **prior**, of which a hard bound is
  the degenerate uniform case, serialised in the same JSON form ChiSurf uses;
  one `fit_batch` serves every model instead of five near-copies; `model_curve`
  returns the model *independent of the data* (which `evaluate` cannot, since it
  scales to the observed counts); and every model now reports `converged` and
  `iterations`, which only the multi-exponential fit did before.

  **Linked fits.** `fit_linked` fits many measurements *together* against one
  parameter vector, with link groups resolved across the whole of it, so a
  parameter that belongs to the instrument (a rotational correlation time, a
  g-factor, a timeshift) is informed by every measurement at once while a
  per-sample parameter stays free. `fit_many` fits rows independently, where a
  group spanning rows can do nothing because the rows never meet.

  Migration: the former Python API (`Fit23`…`Fit26`, the `fit23`-style helpers,
  `DecayFitData`, `DecayFit23.modelf`/`fit_matrix`) is kept as a **Python-only**
  compatibility layer that warns on use and is **removed in 0.29**. It is rebuilt
  on the new interface and reproduces the same numbers. R and Java took the clean
  break. See `doc/fit-guide.rst` and the `plot_decay_fit_interface` example.

### Fixed
- **A C++ throw from `TTTR`, `TTTRMask` or `HMM` aborted the interpreter.**
  Those three `.i` files had no SWIG `%exception` handler, so an exception
  raised to report a bad argument (a mismatched array length, an unreadable
  file, a foreign msgpack payload) unwound through the wrapper and terminated
  the process — no traceback, and no way to catch it. They now raise
  `RuntimeError` like the rest of the library.
- **`get_used_routing_channels` could return the channels the file had before
  you edited it.** `set_routing_channel_at` is public but
  `find_used_routing_channels`, which refreshes the cache it invalidates, was
  protected — so any code that rewrote channels (as the new HMM state split
  does) left the accessor silently reporting stale values with no way to fix it.
  `find_used_routing_channels` is now public; the cache itself stays protected.
- **A response sized for one channel was accepted on a multi-channel fit, and
  read out of bounds.** `DecayFitProblem::validation_error` treated any
  `n_bins`-long `irf`/`background` as "shared", but a polarisation-resolved model
  reads `n_channels * n_bins` samples straight out of the array — so a
  half-length response was an out-of-bounds read, not a shorthand. It was silent:
  the memory past a heap vector is usually mapped, so the curve came back with
  plausible numbers and the process died later in unrelated code. Found by
  bisecting exactly that: a crash in a different test file, four runs out of
  four. A shared response is now accepted only when there is one channel to share
  it with, and the message names both sizes.
- **`model_curve` validated nothing.** `fit` reaches the sizing check through
  `bind`/`is_usable`; `model_curve` bypassed it and handed raw pointers to the
  kernel. It now calls `require_valid()` first, so a mis-sized problem raises
  instead of corrupting the heap.
- **`fconv_per_cs` shifted its wrap-around tail one bin early.** The periodic
  convolution's tail loop applied a decay step *before* writing bin 0, but the
  value it held was already the continuation at bin `period_n` — which *is* bin 0
  of the next period. The three `_cs` kernels (scalar, NEON, and the two-channel
  NEON) all carried it; `fconv_per()` did not, because its main loop ends one bin
  earlier and compensates. The error is invisible whenever the decay completes
  within the excitation period and grows as it does not — 5.8e-5 of the peak at a
  lifetime of a fifth of the period, and larger for longer lifetimes or higher
  repetition rates. With the fix the recursion matches an exact circular
  convolution to 1.3e-15, which is what makes the machine-precision agreement
  between the two convolution backends a meaningful check rather than a tuned
  tolerance.
- **Priors set on a single-row fit were stored, serialised and then ignored.**
  `DecayFitConstraints::set_prior_json` reached the optimiser in `fit_linked` but
  not in `DecayFit2::fit` for `fit23`/`24`/`25`/`26`, because those models build
  their optimiser internally and never saw the constraints. A uniform prior — the
  documented way to express a hard bound — therefore did nothing on the primary
  path, silently, while the caller believed the bound held. Bounds now travel on
  `DecayFitContext` and are applied by every fit2x kernel, including `fit23`'s
  1-D Brent specialisation (where an unbounded fast path would have made a fit's
  answer depend on which internal route it took).
- **Cross-language `fit23` reference tests now start inside the basin they pin.**
  The 58-photon reference decay is sparse enough that the objective falls
  monotonically as the lifetime grows, so the historical answer is a *local*
  minimum with a basin of roughly 0.5–1.2; the old start of 2.1 sat close enough
  to the edge that a change of 1e-4 in the model flipped it to a converged,
  successful-looking fit reporting a lifetime in the tens of thousands. The
  reference values are unchanged. The runaway itself is now pinned by a test, and
  is documented as a failure mode in `doc/fit-guide.rst`.

### Added
- **`TTTR::set_routing_channel`** — the bulk companion to
  `set_routing_channel_at`. Relabelling a whole measurement through the
  per-event setter costs one binding call per photon, which at photon scale is
  the dominant cost of the operation. Refreshes `used_routing_channels`, so the
  accessor cannot go stale behind it.
- **`HmmChannelMap::allocate` and `HmmStateSidecar::set_arrays`** — the id
  allocation and the sidecar format, usable without an `HMM` engine. A caller
  that assembled its photon streams some other way (several source files, a
  burst table, a nanotime-split stream set) can now write *this* layout and
  *this* file rather than a second, subtly different one; `HMM::build_channel_map`
  is a thin wrapper over the former.
- **HMM state decoding that reports a distribution, not a winner.** Viterbi
  answers "what is the single most likely state sequence"; most burst analysis
  instead asks "how do the photons distribute over the states", and the argmax
  answers that badly — photons at γ = (0.7, 0.3) all land in state 0, so
  well-separated states are inflated and ambiguous or short-lived ones erased.
  On simulated data with 75/25 occupancy and overlapping emission profiles the
  Viterbi occupancy error is **0.081** against a ground truth the two new
  decoders reproduce to **0.0006**.

  `HMM::posterior` returns the per-photon posterior **γ** as a float32
  `(N, n_states)` matrix — the quantity the E-step already formed and threw
  away, and the same array the reference `H2MM_C` calls `gamma`.
  `HMM::sample_states` draws each photon's state from its own γ row (faithful
  marginal, but the independent draws shatter dwells — 15103 where the truth has
  1244 — so it must not drive dwell or transition statistics), and
  `HMM::sample_paths` does **FFBS** (forward filtering, backward sampling),
  drawing whole trajectories from `P(path | data)`, which reproduces the
  marginal *and* the dwell structure (1191 dwells against a true 1244).
  Averaging over draws is multiple imputation: the spread is the decoding
  uncertainty a single Viterbi path reports as zero. `viterbi` is untouched and
  stays the default.

  Both samplers use a **counter-based** RNG keyed by
  `(seed, draw, photon index)` rather than a shared stream, so output is
  bit-identical at any thread count. The forward filter is shared across FFBS
  draws, so extra draws are nearly free (8 draws over 200 k photons: 3.3 ms,
  against 1.6 ms for γ alone and 110 ms for the fit).

  A decode can be persisted two ways, which agree photon for photon:
  `split_routing_channels` rewrites each photon's routing channel so every
  `(stream, state)` pair has its own id and writes one PTU holding every photon
  — self-describing, so per-state decays and FCS become ordinary `Channel`
  selections; and `state_sidecar` writes a msgpack sidecar leaving the source
  file untouched, with no id budget. The split **compacts the whole id space**,
  not just the new ids: source channels are usually sparse (1, 12, 30 for three
  detectors is ordinary) and those gaps are dead weight in a field a few bits
  wide, so the used ids compress to `0..k-1` and the `(stream, state)` pairs are
  allocated immediately after, densely, one step apart. Everything then lies in
  one run from 0 — a file whose detectors sat at 1, 12 and 30 needs 3 bits
  instead of 5, so a split that would have required PTU round-trips through an
  SPC-600/256 record. Both directions are recorded in the map, which travels in
  the sidecar. Photons no decoder assigned move to the compressed form of the
  channel they were on, so afterwards those ids hold *only* unassigned photons —
  and a tool that hard-codes channel numbers will be wrong about a split file.

  `set_bursts_from_tttr` / `set_bursts_from_filter` now record the source photon
  index (`get_photon_index`), which both persistence paths need and which was
  previously discarded.
- **`TTTRMask::to_msgpack` / `from_msgpack` / `write_msgpack` / `read_msgpack`**
  (Python: `to_bytes` / `from_bytes`). `to_json` emits one JSON integer **per
  event**, so a 10 M-photon mask is ~20 MB of decimal text; the msgpack form
  carries the already bit-packed words verbatim as a `bin` field, at `size/8`
  bytes plus a header. msgpack is now the convention for tttrlib outputs that
  scale with the photon count — it costs no new dependency, since nlohmann/json
  is already vendored. `to_json` stays for small masks and compatibility.
- **Two convolution backends behind one call**, `dfa::convolve` /
  `dfa_convolve(rates, weights, irf, n_bins, shift_bins, method)`. `Recursive`
  (the default) is the SIMD time-domain recursion the library already used;
  `Spectral` multiplies the closed-form periodic spectrum by the response's.
  Benchmarked in `benchmarks/bench_convolution.py` and recorded in `PERF.md`:
  the recursion is **1.7× to 6.2× faster**, the gap widening with the number of
  rates, so the frequency domain is *not* the fast path — it is there for the
  three things the recursion cannot do (an arbitrary measured pattern, an
  independent cross-check, and a response that wraps around the period). A
  fractional `shift_bins` is applied to the *response* under either backend,
  costing one transform independent of the rate count.
- **`dfa::vv_vh_convolved`**, the polarisation-resolved decay in the form a fit
  compares against data: both rate products are convolved before the VV/VH
  projection, because the response acts on the photons and not on the anisotropy.
- **Bayesian Blocks burst search** (`TTTR::burst_search_bayesian_blocks`, also
  `burst_search(..., mode="bayesian_blocks")`). Rather than asking whether the
  rate near each photon clears a threshold, it finds by dynamic programming the
  single most probable partition of the photon stream into constant-rate
  intervals. There is no binning, no window duration and no phase, so burst edges
  are placed optimally instead of snapped to a window boundary, and the only
  detection parameter is `p0`, a false-alarm probability. The method is
  Scargle's, developed for time-tagged photon events from BATSE and Fermi — the
  same data model as a TTTR file.

  The segmentation is O(N²), so it runs behind a two-stage trigger in the manner
  of Fermi GBM and Swift BAT: a cheap loose sliding-window pass proposes
  candidate regions, and the exact segmentation runs only inside them. Cost is
  roughly `f · n̄ · N` for candidate fraction `f` and mean region size `n̄`, with
  `n̄` bounded by `max_region_photons`. See
  `include/BurstSearchBayesianBlocks.h`.

  Measured against the simulated ground truth in
  `examples/single_molecule/plot_burst_search_comparison.py`, pooled over 2688
  transits: it reaches 50% completeness at 18.4 photons, against 13.9 for
  `maxtree`, 21.6 for `cusum_sprt` and 22.7 for `sliding_window`. At the base
  condition its F1 is 0.918, against 0.961 for `cusum_sprt` and 0.952 for
  `maxtree`. It is the slowest of the five at roughly 1.2 Mphoton/s, against
  ~16 for `maxtree` and ~1080 for `sliding_window`.

  So it is **not** the best method on this benchmark — `maxtree` has the better
  detection limit and `cusum_sprt` the better F1 and purity. Bayesian Blocks
  looks strongest on isolated, sharp-edged, high-contrast bursts against a
  uniform background, which is the regime optimal segmentation is built for and
  which flatters it relative to real diffusion transits with soft edges. Its
  distinguishing property here is burst *extent*: because it places edges by
  likelihood rather than snapping them to a window boundary, it does not inherit
  the `m`-photon truncation the window-based searches do. Choose it when
  boundaries matter; choose `maxtree` for the lowest detection limit and
  `sliding_window` when throughput dominates.
- **Exact low-count detection statistics** (`include/BurstSignificance.h`,
  exposed in Python as `poisson_significance`, `li_ma_significance`,
  `log_poisson_upper_tail`, `log_p_to_sigma`, `sigma_for_false_alarm_rate`).
  The Gaussian z-score `(k − μ)/√μ` assumes the Poisson distribution is already
  normal, which is false at the counts this library operates at — a 20-photon
  transit against 2 expected background photons — so a "4 sigma" threshold did
  not deliver the false-positive rate it appeared to promise. Adds the exact
  Poisson upper tail (via the regularized incomplete gamma, computed in log space
  so it does not underflow where `scipy.stats.poisson.sf` returns 0) and the
  Li & Ma (1983) on/off statistic, which additionally propagates the uncertainty
  of a background that was *measured* rather than known — always the case here,
  since the baseline comes from a rolling-ball estimate.

  Selected by `significance_mode` on both `burst_search_maxtree` (default `0`,
  Gaussian, so existing results reproduce bit-exactly) and
  `burst_search_bayesian_blocks` (default `2`, Li & Ma).

  On the simulated benchmark, switching `maxtree` from Gaussian to Li & Ma is a
  modest, one-directional trade: purity rises from 0.904 to 0.916 and F1 from
  0.952 to 0.957, while completeness falls from 0.719 to 0.701 and the 50%
  detection limit moves from 13.9 to 14.6 photons. That is the expected
  behaviour — accounting for the background's own error makes the test strictly
  more conservative — and it is the right default only when false positives cost
  more than missed dim bursts. The effect is much larger on isolated bursts
  against a uniform background than on real diffusion transits, so do not expect
  the dramatic version of this result on experimental data.
- **Calibrated false-alarm thresholds** (`max_false_alarm_rate` on
  `burst_search_maxtree` and `burst_search_bayesian_blocks`). Expresses the
  detection threshold as expected spurious bursts per second instead of a bare
  sigma, correcting for the trials factor. Unlike a sigma, one such setting means
  the same thing on a 10 s and a 1 h acquisition. The trials correction is
  approximate — the exact factor for a multi-level search is not analytically
  available — so verify it against a background-only measurement before relying
  on the absolute number.
- **Burst-search algorithm table** in `doc/burst-analysis.rst`, comparing all
  five searches — how each decides, when to use it, and the primary literature
  each derives from, with resolvable DOI/arXiv links.
- **Max-tree burst search** (`TTTR::burst_search_maxtree`, also
  `burst_search(..., mode="maxtree")`). A threshold-free burst search: it builds
  the component tree of the local log count rate — every connected component at
  every level — and keeps components that are maximally stable (the MSER
  criterion) and whose photon count, duration, contrast and Poisson significance
  are plausible. Because each burst is detected at its own level, dim and bright
  bursts in the same trace are both found, which no single rate threshold can do,
  and overlapping transits are deblended by the tree structure rather than by a
  separate splitting step. Includes a rolling-ball (morphological opening)
  baseline that tracks drift without chunking. See
  `include/BurstSearchMaxTree.h`.

  Measured against a simulated ground truth at a dilute single-molecule
  condition (`examples/single_molecule/plot_burst_search_comparison.py`): recall
  0.98 / precision 0.93 / F1 0.95, versus 0.93 / 1.00 / 0.96 for `cusum_sprt` and
  0.84 / 0.69 / 0.76 for `sliding_window`. Its recall is flat across a 16x
  bright-to-dim brightness spread, where the threshold searches lose 10-15 points,
  and its merge rate is roughly half that of `cusum_sprt` as concentration rises.
  It is slower: ~15 Mphotons/s against ~110 for `cusum_sprt` and ~1000 for
  `sliding_window`.

  Note the method assumes bursts are a *minority* of the trace: its contrast and
  significance filters are measured against an estimated baseline. Above roughly
  half of photons belonging to bursts that assumption fails and those two filters
  should be disabled (`min_contrast=0`, `min_significance=0`).
- **Bayesian Blocks stage-1 trigger is now O(n).** The Fries/Eggeling trigger — a
  pure macro-time-difference test — marked all `m` photons of every firing
  window, which costs O(n*m) and costs it precisely inside bursts, where windows
  fire at every offset. Consecutive firing windows overlap by construction, so
  the candidate runs are now accumulated in a single pass and the intermediate
  per-photon flag array is gone. The candidate set is unchanged; only the
  bookkeeping is cheaper, which makes permissive trigger settings more affordable.

  Measured against the simulated ground truth, the trigger/segmentation trade is
  monotonic but shallow: `trigger_contrast` 1.5 gives F1 0.952 in 51 ms,
  2.5 (the default) 0.943 in 11 ms, and 4.0 0.941 in 4.5 ms for 200k photons. A
  permissive stage 1 is genuinely the most accurate setting, at roughly 5x the
  time for +0.01 F1.

- **Coincident (multi-detector) burst search** — `TTTR.burst_search_coincident`,
  registered as `coincident`. Keeps only bursts found *independently* in several
  groups of detectors, which is what rejects singly-labelled and photobleached
  molecules in ALEX/PIE; the classical dual-channel burst search is the
  two-group case. Any number of groups is accepted and `min_groups` sets how many
  must agree, so "2 of 3" is expressible as well as "all of 2". It is a
  composition rather than a new algorithm — the search named by `algorithm` runs
  once per group — so it works with every other registered search, including ones
  added later. Coincidence is decided in time, not per photon.

  On a simulation with 40 molecules emitting into all three detector groups plus
  40 *brighter* ones emitting into a single group, a pooled max-tree search finds
  all 80 and cannot separate them; the coincident search finds the 40 complete
  events and none of the 40 partial ones, at every quorum tested.

  Composite entries like this carry a `parameters_of: {category, selector}` link
  on the property holding the inner search's parameters, so a consumer can render
  them as a nested form rather than as raw JSON. chisurf does; the link is
  declarative, so any future composite entry nests the same way with no new code.

- **API registry and generated API index.** Two complementary ways to ask what
  tttrlib can do, both as JSON so every language binding reads the same bytes
  instead of re-declaring lists that drift.

  `tttrlib.registry()` (C++ `tttrlib::registry_json()`, `include/Registry.h`) is
  the *curated* layer: `{category: {name: entry}}`, currently `burst_search` and
  `file_container`. Entries carry what a signature cannot express — what a thing
  is, which parameters are meaningful, their units, ranges and defaults — as
  standard **JSON Schema**, so a consumer that can already render a JSON Schema
  can offer a tttrlib feature with no tttrlib-specific code and picks up new
  entries on upgrade. `registry("burst_search")` supersedes
  `TTTR.burst_search_algorithms()`, which still works.

  `tttrlib.api_index()` is the *generated* layer: every class, method, function,
  property and attribute the module exports — 111 classes, ~1450 methods, 73
  functions — with signatures, defaults and docstrings, so a scripting language
  can discover and call anything without parsing C++ headers. It is derived from
  the built module rather than authored, which is the only way coverage that wide
  stays correct; a hand-written index would be wrong within a release.
  `tools/generate_api_index.py` dumps either layer to a file.

- **Burst-search registry** (`TTTR::burst_search_algorithms_json()`, and in Python
  `TTTR.burst_search_algorithms()`, `TTTR.burst_search_defaults()`,
  `TTTR.burst_search_by_name()`). Advertises every burst search with its label,
  summary and a **JSON Schema** of its parameters — types, defaults, ranges and
  units — the way `container_names` advertises the readable file containers.
  Because the parameter description is standard JSON Schema, a tool that can
  already render one can build a burst-search user interface with no
  tttrlib-specific code, and picks up new algorithms on upgrade.
  See `src/BurstSearchRegistry.cpp`.
- **Burst-search benchmark example**
  (`examples/single_molecule/plot_burst_search_comparison.py`). Simulates a
  single-molecule sample with `SimEngine`, recovers the true transits from the
  simulator's per-photon molecule labels, and scores every burst search on
  precision, recall, and the split and merge rates, sweeping concentration and
  brightness heterogeneity.

### Fixed
- **The Python extension compiled with OpenMP but never linked it.** The
  top-level OpenMP flags reach `CMAKE_CXX_FLAGS` and `CMAKE_EXE_LINKER_FLAGS`,
  but a Python extension is a `MODULE` library rather than an executable, so it
  received the compile flags and none of the link ones. The R and Java modules
  have always linked `OpenMP::OpenMP_CXX` explicitly; the Python module now does
  too.

  The failure mode was worse than a plain link error. On macOS the Python module
  is linked with `-Wl,-flat_namespace,-undefined,dynamic_lookup` so that Python
  symbols resolve at load time, and that flag let the unresolved OpenMP symbols
  through as well. The build therefore succeeded and the module failed at
  **import**, with `symbol not found in flat namespace '___kmpc_barrier'` —
  which forced every downstream build to inject `-lomp` and an rpath by hand.
  `pip install .` in an environment where CMake can find OpenMP now produces an
  importable module with no extra flags.
- **`CLSMImage::compute_ics` returned an array longer than it allocated,
  segfaulting any caller that used frame lags.** The function allocates one
  correlation map per correlated frame *pair* — `calloc(pairs * pixels)` — but
  reported the number of input *frames* as the first output dimension. The two
  agree only for the default auto-correlation, where every frame is paired with
  itself. Any other pairing has fewer pairs than frames: correlating a stack of
  50 frames at a lag of 2 writes 48 maps but declares 50, so the returned NumPy
  array over-declares its own length and reading the tail — `arr.mean(axis=0)`
  is enough — walks off the allocation and crashes the interpreter. This made
  the whole spatiotemporal side of image correlation (STICS/TICS/iMSD, anything
  with a non-zero frame lag) unusable from Python.

  The first dimension is now the number of pairs actually correlated. Frame
  pairs are also bounds-checked against the ROI and dropped if out of range;
  the correlation loop indexes `roi[frame * pixel_in_roi]` directly, so an
  out-of-range frame number was a second out-of-bounds read. A pair list with
  no valid entries returns an empty result instead of correlating garbage. See
  `test/python/clsm/test_clsm_ics.py`.
- **MLE decay fits (`Fit23`/`Fit24`/`Fit25`/`Fit26`) could segfault on a decay
  whose length did not match the IRF.** The fit derives its channel count from
  the experimental `data`, but `DecayFitData::set_data()` grows only `data` (and
  `model`) to a new decay's length while leaving `irf`/`background` at their
  original size. A decay longer than the IRF therefore made the objective read
  past the end of `irf`/`background` and crash the process — in every binding
  (Python/Java/R), since they all reach native code through `DecayFit*::fit`. The
  single-decay path now validates array consistency the way the batch
  `fit_matrix` already did, via `DecayFitData::has_consistent_fit_arrays()`, and
  reports an invalid fit (`x[0] = -1`) instead of segfaulting. `targetf` is
  guarded too, as it is exposed directly. See
  `test/python/decayfit/test_DecayFit23.py` (`..._decay_longer_than_irf...`,
  `..._empty_data...`).
- **BVA and HMM dropped the last photon of every burst.** Burst index ranges are
  inclusive `[start, stop]` everywhere they are produced — a 30-photon burst is
  reported as `[0, 29]`, and `BurstFilter` sizes it as `stop - start + 1` — but
  `BVA::compute` and `HMM::set_bursts_from_tttr` indexed them half-open. Both
  therefore silently discarded each burst's final photon: a 5% count error on a
  20-photon burst, and a biased one, since the discarded photon is the photon
  that ended the burst. This changes the numeric output of existing BVA and HMM
  analyses. The stale "half-open" wording in `BVA.h` and `HMM.h` is corrected,
  and `test/python/burstfilter/test_burst_range_convention.py` now pins the
  convention across producers and consumers together — which is what the previous
  per-component tests could not do, since each agreed only with itself.

### Changed
- **Bayesian Blocks is ~9.6x faster** (119 ms -> 12 ms on 127k photons), by
  applying **PELT pruning** (Killick, Fearnhead & Eckley 2012,
  doi:10.1080/01621459.2012.737745) to the dynamic program. A candidate block
  start that has fallen behind the current optimum by more than the change-point
  penalty can never recover, so it is dropped. This is an *exact* optimisation:
  verified byte-identical output across 72 parameter combinations and 19,647
  bursts. The practical consequence is that cost no longer scales with region
  size — raising `max_region_photons` from 512 to 8192 now changes runtime by
  under 10%, against more than 12x before — so boundary quality can be bought
  without a quadratic bill.
- **Max-tree is ~1.3x faster** (8.1 ms -> 6.4 ms). The rolling-ball baseline was
  53% of its runtime and is now threaded, each chunk re-deriving its monotonic
  deque from a time-based halo so the result is identical to the serial sweep;
  the per-photon rate signal is threaded too. The component-tree sweep is
  inherently sequential, which caps this at about 1.5x parallel speed-up.
  Verified byte-identical output across 48 parameter combinations.
- The Bayesian Blocks candidate set is now a struct-of-arrays, carrying each
  candidate's `block_length[i]` and `best[i-1]` beside it so the inner loop reads
  sequentially instead of gathering. Worth a further ~5%, output unchanged.
- **Bayesian Blocks defaults retuned against the simulated ground truth**:
  `p0` 0.05 -> **0.005** and `trigger_contrast` 1.5 -> **2.5**. Together these
  are worth a further ~2x (12.0 ms -> 6.8 ms on 127k photons), bringing the
  method level with `maxtree` on throughput.

  This is not a speed-for-accuracy trade. Chosen by sweeping 60 parameter
  combinations against per-transit ground truth and taking the Pareto frontier,
  then **validated on held-out conditions** (different seeds, plus 0.4x and 3x
  crowding) to check the choice was not fitted to one simulation. On the
  held-out set the new defaults are 3.5x faster with **purity 0.903 vs 0.869**,
  completeness 0.678 vs 0.680 and the 50% detection limit unchanged at 19.1 —
  i.e. the only metric that moved beyond noise moved in the right direction.

  Two things the sweep exposed. `p0 = 0.05` was simply the wrong default:
  0.005 measured better on purity, completeness *and* detection limit
  simultaneously, because splitting a burst into spurious extra blocks costs
  more here than missing a marginal change point does. And `trigger_contrast`
  = 1.5 defeated the two-stage architecture entirely — at that setting the
  trigger fires on roughly a quarter of background positions, and once each
  firing is padded and merged the segmentation covers essentially the whole
  stream, so the cheap stage filtered nothing.

  `max_region_photons` was also swept and left at 4096: dropping it to 512 is
  faster but collapses completeness to ~0.59, and 2048 is indistinguishable
  from 4096.
- **Max-tree `delta` retuned, 0.5 -> 0.15**, which turns out to matter far more
  than any of the performance work. Against the ground truth this raises
  completeness from 0.719 to 0.799, purity from 0.904 to 0.964 and drops the 50%
  detection limit from 13.9 to 11.4 photons — while running slightly *faster*.
  At the base condition it reaches recall 1.000 / precision 0.975 / F1 0.988,
  against 0.976 / 0.929 / 0.952 before, and its split rate falls to zero.
  Confirmed on the held-out conditions (0.714 -> 0.800 completeness,
  0.897 -> 0.973 purity, 15.3 -> 12.0 limit).

  The reason is worth stating because the parameter reads backwards. A *smaller*
  `delta` probes a smaller level drop, so components grow less over it, so more
  of them survive `max_variation` — lower is the more *permissive* setting. It
  therefore shifts the work from the stability heuristic onto the contrast and
  Poisson-significance filters, and since those two are the statistically
  principled tests and stability is a shape heuristic, that is the better
  division of labour.

  This also changes what `significance_mode` is worth: with the new `delta`,
  Li & Ma reaches precision 0.997 and purity 0.994 against the Gaussian form's
  0.975 / 0.964, a much clearer margin than before.

  `background_window` measured better still at 0.2-0.5 s but was **left at
  0.05 s**: the simulation has a constant background, so a longer window is free
  there in a way it will not be on real data, where tracking drift is the whole
  point of the rolling ball. Documented in the header rather than adopted.
- **Defaults are no longer written down more than once.** `TTTR::burst_search`
  had the max-tree and Bayesian Blocks defaults spelled out as literals; it now
  reads them from the settings structs. `burst_search_sliding_window` and
  `burst_search_cusum_sprt` had no C++ defaults at all while the registry
  advertised some, so the two entry points disagreed — they now carry the same
  defaults, and every burst search is callable with no arguments. A new test
  asserts the registry defaults equal the C++ signature defaults for every
  algorithm and parameter, so the remaining duplication cannot drift silently.
  `plot_burst_search_comparison.py` no longer pins tuning parameters either; it
  was benchmarking a combination nobody runs.
- `parallel_for` was duplicated in `BVA.cpp` and the Bayesian Blocks source and
  was about to be copied a third time; it now lives in `include/ParallelFor.h`.

### Not done
- **Approaching `sliding_window`'s throughput is not achievable** for the exact
  searches, and the gap is inherent rather than an implementation defect:
  `sliding_window` does one comparison per photon (~1100 Mphotons/s), while
  `bayesian_blocks` (~10) and `maxtree` (~20) do strictly more work to get a
  lower detection limit and better boundaries. The remaining Bayesian Blocks
  cost is concentrated in flat background stretches, where PELT has no change
  point to prune against; `pad_photons` pads every region with exactly such
  stretches, which is why it is the dominant cost knob.
- Decoupling the local background estimate from the segmented region (a wider
  *counted* span rather than a wider *segmented* one) was implemented and then
  removed: measurement showed it changed completeness by less than the binomial
  error. Padding helps by giving the segmentation edge-placement context, not by
  improving the background estimate.
- A hand-rolled polynomial logarithm (1 ulp, 1.75x faster than `std::log` in
  isolation) was implemented and then removed: in the actual inner loop it made
  the search **23% slower**. Measured in a loop resembling the real one,
  `std::log` costs 3.51 ns/iteration against 4.4-4.6 ns for three different
  hand-rolled variants. The isolated microbenchmark was misleading — there the
  logarithm is the only work, whereas in the real loop its latency is already
  hidden by neighbouring loads and multiply-adds, and a 10-term series is one
  long dependency chain with no instruction-level parallelism to hide. The DP
  now carries a comment saying so, to stop the idea being retried.
- **Functional pruning (FPOP, Maidstone et al. 2017) was implemented and
  removed: it pruned nothing.** The candidate set stayed at 114.1 live starts and
  the cell count came out bit-identical to PELT's (14,480,250), while the root
  solving it requires made the search ~52x slower.

  Two findings are worth recording so this is not retried blind. First, the
  algebra collapses: "candidate i is beaten outright by the newest candidate" is
  exactly `val + ncp_prior <= best_val`, i.e. PELT's rule is the single-rival
  special case of functional pruning. Any gain must therefore come from the
  *intersection* over several rivals. Second, that intersection is empty far less
  often than the literature suggests, because a dilute photon stream is mostly
  homogeneous background: every candidate start inside a long background stretch
  implies almost the same rate, so their viable rate ranges sit on top of one
  another and none is squeezed out. Functional pruning needs candidates that
  disagree about the parameter, and background does not supply that.

  Note also that a fully correct FPOP needs per-candidate interval *lists*, not a
  single interval: a candidate beats newer rivals on an interval but older ones
  on the complement of an interval. The version tried here tracked one interval
  and so pruned strictly less than true FPOP — but since it pruned nothing at
  all, the extra machinery would have had to overcome a 52x cost deficit to break
  even.


## [0.27.0] - 2026-07-17

A **performance and memory** release. Confocal (CLSM/FLIM) reconstruction is
faster and much lighter on memory, single-detector lifetime fitting and
dynamic-FRET analysis are new, and a cross-version benchmark now tracks speed and
peak memory across releases. Measured numbers: [`PERF.md`](PERF.md).

### Performance / memory
- **CLSM lazy fill.** `CLSMImage.fill()` now stores a one-bit-per-event
  acceptance stream-mask instead of eagerly materializing a per-pixel
  `std::vector<int>` of photon indices; per-pixel containers are built only when
  a pixel handle is actually requested. Intensity, lifetime, phasor and
  tttr-index queries run straight off the mask.
- **Virtual fill.** New `CLSMImage(..., build_pixels=False)` skips per-pixel
  allocation entirely; `get_intensity_masked()` does a single-pass scatter into
  the image. Byte-identical intensity to a full fill.
- **Cached moments/phasor.** Mean-lifetime, fast-lifetime and phasor images share
  cached per-pixel raw moments/phasor sums, so re-tuning the IRF/background/
  modulation frequency is an O(pixels) correction rather than an O(photons)
  rescan.
- Measured against 0.26.2 on the same machine (Apple M1 Pro, CPU only; task
  memory = peak RSS minus the post-import baseline): CLSM fill+structure
  −63% time / −12% memory; 2.6 M-pixel HT3 fill −81% time / −40% memory;
  correlation −36% time.
- Lower-level trims (identical results, unchanged API): int32 instead of int64
  for within-burst count / Viterbi back-pointer buffers (BVA, HMM); skip the
  unused macro-time buffer in BVA photon-count mode; reserve HMM CSR/Δt and
  `write_ps_file` dataset buffers up front. `FitNExp` buffer-based overloads pass
  NumPy arrays with a single copy instead of boxing through Python lists.
  Deliberately kept: the `fit_buffers` owning-vector copy (required by `fit()`'s
  signature) and the ARGOUTVIEWM malloc handoffs (required by NumPy ownership).

### Added
- **HMM and BVA** C++ modules for dynamic FRET: photon-by-photon hidden Markov
  modelling by the H2MM algorithm (Baum-Welch EM, SQUAREM acceleration,
  Viterbi) and burst variance analysis, with NumPy-array burst inputs/outputs.
- **FitNExp**: native single- and multi-exponential Poisson reconvolution fitter
  for one decay curve, with batched `fit_many` and per-pixel `fit_map` variants
  that thread across cores.
- **Photonscore `.photons` (D7)** reader/writer and **TIFF** 2D/3D array I/O.
- NumPy-native burst API: `TTTR.burst_search` and the BurstFilter/BVA/HMM
  accessors return NumPy arrays and accept NumPy inputs, no list conversion.
- Cross-version performance + peak-memory monitor (`benchmarks/bench_versions.py`,
  `make_version_plots.py`), a benchmark-backed performance guide, and
  [`PERF.md`](PERF.md) as the single place where benchmark results are recorded.
- **Photon-simulation subsystem** (`SimEngine`, `SimSystem`, `SimSpecies`,
  `SimIntegrator`, `SimGrid`): single-molecule diffusion + photon simulation with
  an OpenMM-style API, PSF fillers, per-molecule coasting for throughput, and
  faithful `to_tttr` export.
- Photon simulator: **ALEX (alternating laser excitation)**. The engine now takes one
  excitation grid per laser (`excitation` may be an array) and each species a per-laser
  brightness matrix `SimSpecies.q_alex`; `SimIntegrator.alex_period` alternates the active laser
  per macro-window (equal-duty, exact integer-window schedule). A doubly-labelled FRET molecule
  emits DD+DA under the green laser and AA under the red laser; the alternation is encoded in the
  macro-time and recovered with `TTTR.alex_to_microtime`. Optional laser-switch markers
  (`alex_markers`) give explicit ground truth. New `SimEngine.alex_period()` / `n_lasers()`,
  config `alex.json`, example `alex_smfret.py`, notebooks `alex_01_simulation_basics.ipynb` /
  `alex_02_smfret_es.ipynb`, and `test/python/simulation/test_alex.py`.

### Fixed
- `SimEngine.to_tttr` no longer segfaults when the stream contains marker events (CLSM scan or
  ALEX) and no longer mangles the micro-time: the SPC encoder now carries the simulated micro-time
  (FLIM) axis faithfully (a `to_tttr` round-trip preserves `micro_times` exactly) and skips marker
  events instead of indexing out of bounds. Micro-time filters can now be used after `to_tttr`.
- Photon-count stop condition (`n_ph_max`) now counts photons only; marker events no longer consume
  the photon budget.
- T2 decoding: HydraHarp/MultiHarp `special` records with channel 0 are the sync
  input and now decode as photons on channel 0 (validated bit-exact against the
  independent `ptufile` decoder), not as markers.
- CLSM marker/dimension header auto-configuration ported from the Python wrapper
  into C++ so the R/Java/native bindings reconstruct images correctly.
- Cross-platform SWIG correctness on LP64 Linux: burst-array parameters use the
  NumPy `IN_ARRAY1` convention (a `std::vector<int64_t>` argument silently
  rejects Python lists/arrays there), FitNExp buffer overloads use unique
  parameter names (a re-`%apply` left a stale argout typemap in the R wrapper),
  and burst structured-property helpers avoid NumPy-2 array truthiness.

### Changed
- Faster used-channel scan and by-const-ref tag lookup in `TTTR`; the GIL is
  released around the CPU-only decay fits.
- Portable SIMD: AVX/NEON kernels are compiled in and selected at runtime, so a
  single binary stays fast across CPUs.

## [0.26.0] - 2026-03-08

### Added
- Becker & Hickl SPCM support (PR #48 by @cqian89)
  - Pixel-marker binning for BH SPC-130/140/150 detectors
  - Automatic `.set` file parsing and dimension inference
  - Frame 1 adjustment for BH SPC data
  - Truncated recording recovery

### Fixed
- CLSM `get_fluorescence_decay` stack_frames bug (PR #49 by @cqian89)
  - Fixed bug where only the last frame was processed when stack_frames=True
- CLSM `get_phasor` precision loss (PR #49 by @cqian89)
  - Fixed precision loss by using float instead of int calculation

### Changed
- Updated test data to include BH SPCM FocalCheck sample data
- Added new integration tests for BH pixel marker binning

## [0.25.1] - 2025-02-20
### Fixed
- Various bug fixes and improvements

## [0.25.0] - 2024-12-15
### Added
- Support for Photon-HDF5
- Transparency in-memory compression
- Linearity correction for micro times

For older releases, please refer to the documentation.

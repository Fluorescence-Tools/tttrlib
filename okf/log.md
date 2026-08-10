# Bundle update log

## 2026-08-10 (18th entry)

* **PRD-010 Phase 5e: the AD advantage peaks and then decays, and `FitNExp` was
  never a candidate.** Two results from asking a question none of the earlier
  benchmarks had: what happens at 200 exponentials?

  **`FitNExp` has no gradient to convert.** It appears in PRD-010's problem
  statement as an `i_lbfgs` consumer. It is not one — `DecayFitNExp.cpp` never
  constructs a `bfgs`. Lifetimes are optimised coordinate-wise by Brent and
  amplitudes are profiled out by EM. Struck from the open list; the premise had
  survived unexamined since the PRD was written.

  **Every AD decision so far was taken at N <= 18, and that is the good part of
  the curve.** Both methods are O(N) — central differences pay 2N objective
  evaluations, a vectorized forward pass makes every scalar carry an N-vector —
  so the ratio is a race between two O(N) costs settled by constants and memory
  traffic. Measured on a 1024-channel decay: the advantage peaks at **13.3x**
  around 8–16 exponentials and falls to **3.3x** at 200 (N = 400). Central
  differences stay near-linear (895x the objective against a theoretical 800x);
  AD goes superlinear, 257x where pure O(N) predicts ~160x. The reason is size:
  a `Dual<double, GradVec<400>>` is 3.2 kB, so one 1024-channel intermediate is
  3.13 MB, far outside cache, while the finite-difference path re-walks a plain
  8 kB array. AD still wins at 200 exponentials — but at that size both are the
  wrong tool, because for a sum of exponentials the analytic gradient is closed
  form and costs about one objective evaluation.

  The generalisable point: **"AD is 10x faster" is a statement about a
  parameter count, not about AD.** Quoting it without the N is how a
  benchmark becomes folklore.

  Also tidied `DecayFit25` and `DecayFit26` onto `set_bounds`. 25's hand-rolled
  penalty was dead; 26's was correctly signed, unlike 23's, so that one was a
  tidy rather than a bug fix — but it shared 23's other defect of being
  invisible to an analytic gradient. `gamma`'s hard clamp in fit23 is kept
  deliberately: unlike `tau` it has no arithmetic failure outside its range.

## 2026-08-10 (17th entry)

* **`_mmfdb_operation.algorithm` finished: every writer that knows its
  estimator now records it.** The item was half-done — ChiSurf's writers
  validated the term but no caller supplied one, so a container said *a burst
  search happened* without saying which, and two selections of the same
  measurement were indistinguishable except by a settings hash nobody can
  read back into a method.

  Four terms were added to mmfdb rather than approximated with existing ones,
  which is rule 4 of `okf/specs/mmfdb-is-the-vocabulary.md`: the IRF is three
  genuinely different instrument responses (`gaussian_prompt_fit`,
  `skew_normal_prompt_fit`, `measured_prompt`) and burst fusion is a
  `recurrence_probability` test. Wiring is a map per writer — `irf_model` in
  the MLE exporter, `used_filter` in burst selection, fixed strings for fusion
  and the MLE itself.

  **Two things had to be checked rather than assumed, and both would have
  produced a confidently wrong container.** `TttrlibSearchSettings.algorithm`
  holds `"maxtree"` whether or not that search ran, so reading it
  unconditionally stamps every container with a method it did not use; it is
  read only when `used_filter` names that mode. And `COUNT_RATE` and `BURST`
  look like two searches: `count_rate_filter` thresholds photons per time
  window, `burst_filter` is the L/m/T form of the same test. Both are
  `sliding_window`. Reading the two implementations is what settled it —
  the names suggest otherwise.

  `burst_gs` and `burst_ebfret` still record nothing, deliberately. **Absent
  means unrecorded; a guessed term is worse than none, because a missing
  provenance prompts a question and a wrong one is believed.** Pinned by
  `test_every_writer_that_knows_its_estimator_records_it`, which checks each
  mapping table against the live mmfdb enumeration instead of by eye, and by
  six end-to-end cases carrying `used_filter` through
  `selection -> write_container -> put_table` and reading the tag back out —
  the term crosses three functions, any of which could drop it silently while
  the settings hash still differed.

## 2026-08-10 (16th entry)

* **PRD-010 Phase 5c/5d: bounds are priors, and acting on that found two bugs.**
  Minimising `-log L + p(x)` is MAP estimation with `p = -log prior`, so a bound
  and a prior are one object. Both codebases already said so —
  `DecayFitContext.h:65`, *"A bound is a uniform prior in this interface"*, and
  ChiSurf folding a `UniformPrior` back onto its port bounds. It follows that
  `i_lbfgs`'s soft bound, `k(x-hi)^2` outside the box, is already a proper prior
  that nobody had named. Unifying the decay fits onto it turned up two defects
  that had been sitting behind the clamps.

  **The Poisson likelihood paid the optimiser to zero out a model bin.** `Wcm`
  and `wcm_p2s` skipped any bin at or below `1e-12`, under a comment reading
  "this is only for stability reasons". The term a near-zero bin contributes to
  the minimised objective is `-C*log(m)`, large and *positive*, so dropping it
  is a discontinuous improvement: measured, the objective falls **828.9** across
  the threshold and is identical for every negative model value. Now continued
  by the tangent to `log` — C1 across the floor, finite below, monotone. Proven
  inert two ways, because that was the whole claim: bitwise identical above the
  floor, and a 143,360-bin sweep of the clamped DecayFit23 box that never gets
  below 1.86e-07.

  **`DecayFit23`'s hand-rolled tau penalty had its sign inverted.**
  `penalty = (x[0] < kMinTau) ? -x[0] : 0` is negative over the whole band
  `0 < tau < kMinTau`, so crossing *below* the bound improved the objective —
  measured 9e-4 better stepping from 1.1e-3 to 9e-4 — and it was discontinuous
  there. Replaced by `set_bounds`. Gamma's soft bound, meanwhile, was applied
  only inside the branch that frees gamma, so the pre-fit ran under different
  rules than the main fit; both are now set once, unconditionally.

  Three corrections to my own reasoning, each caught only by measuring:

  1. I claimed `gamma < 0` made the objective NaN. That was a transcription
     error in my probe, which guarded on `C > 0` where `Wcm` guards on
     `M > 1e-12`. `Wcm` never NaNs — it silently drops the bin, which is worse
     because it is invisible.
  2. I claimed clamps make L-BFGS stall here. Below `kMinTau` the gradient was
     `-1`, not zero — the hand-rolled penalty was supplying it. The flat region
     was real for *gamma*, not tau.
  3. "Delete the clamps" is not possible. Without the tau floor
     `exp(-dt/tau)` overflows for `tau` in `(-dt/709, 0)`: at `tau = -1e-6`
     every model bin is `inf` and the objective NaN, and no penalty rescues a
     NaN because the line search must be able to score what it proposes. The
     guards stay; only the constraint role moved.
  4. Making the floor *smooth* — `soft_floor`, identity above it bit-for-bit,
     `m0*exp((v-m0)/m0)` below — is right and is now in, but it does **not**
     buy back a gradient below `kMinTau`, which is what one hopes for. That
     flatness is physical: at `dt = 0.032`, `exp(-dt/tau)` is 1.3e-14 at
     `tau = 1e-3` and underflows below, and `d/dtau` is already exactly 0 at
     `tau = 1.1e-3`, *above* the floor. `tau_eff` keeps moving; the model stops
     caring. Tuning `kMinTau` cannot fix it — no parameter map manufactures
     information the likelihood does not contain.

  **ChiSurf's bound transforms are not the thing to copy.** `leastsqbound.py`
  uses MINUIT's `sin` (two-sided) and `sqrt(v^2+1)` (one-sided). Measured, both
  reintroduce the pathology they exist to avoid: the `sin` derivative is 3e-17
  at the bound and the map is periodic and non-monotonic; the `sqrt` form is
  *even* in `v`, with derivative exactly 0 at `v = 0`, which maps to the bound.
  The logistic/exponential pair used for the localization fit is monotonic and
  attains its bounds only asymptotically. What *is* worth taking from ChiSurf is
  `priors.py` — priors as extra residuals, imposing no bound at all.

  The AD payoff is the reason to care: `i_lbfgs` adds the bound penalty **and
  its gradient** to whatever a registered analytic callback returns
  (`i_lbfgs.h:313-320`), whereas a term added to the objective by hand is
  invisible to that callback. The old tau penalty would have made an AD gradient
  wrong by exactly `-1` below the bound.

## 2026-08-10 (15th entry)

* **PRD-034 registered: .pto as its own TTTR sink.** Scoping found the design
  pre-drawn and half-built: the spec's example object is already
  `PtoKind "photons"` + `PtoEncoding "dstore"`, and a loader exists that reads
  `macro_time`/`micro_time`/`routing_channel`/`event_type` columns into
  `append_events` (`io_pto.cpp:2583-2598`) — **but it applies no header at
  all**, so the TTTR comes back dimensionless; `build_cues` refuses native
  photons as "not a record stream" (`io_pto.cpp:2617`); and there is no writer
  (`can_write` is never true for pto). The PTU checklist is in the PRD: six tag
  groups from `io_pq.cpp`, of which clocks and `ImgHdr_*` (CLSM auto-config)
  are the ones a sink dies without, and `tyBinaryBlob` is the type the PTU
  reader currently drops with an error (`io_pq.cpp:114`) that `PtoType::Bytes`
  was designed to carry. Design: normative 4-column dstore table pinned to
  TTTR's in-memory dtypes, header as PtoTags on the object's UID (required
  closed set: both resolutions, bin count, source record type; everything else
  verbatim fidelity), ranges as dstore row slices so cues stay an
  embedded-stream concern. Acceptance includes a bit-identical PTU round trip
  with an imaging file whose CLSM reconstructs from the `.pto`.

## 2026-08-10 (14th entry)

* **PRD-010 Phase 5: Eigen left the project.** It had been a project-wide
  `FIND_PACKAGE(Eigen3 REQUIRED)` — an apt package on Linux CI, a Homebrew keg
  on macOS, a vcpkg port on Windows, a `dnf` package inside the manylinux wheel
  builder — for two things: the batched GEMMs in `NeuralNet`, already migrated
  to `Mat.h`, and **one struct member** in `ImageLocalization`, the fixed-size
  vector sitting in the derivative slot of the vectorized forward-mode dual
  number. That member is now `GradVec<N>` (`modules/math/include/GradVec.h`).

  The PRD's own decision line — *"Eigen is an acceptable dependency:
  header-only with no link step"* — is now marked reversed. Header-only
  understates the cost. A `REQUIRED` package is a package on every platform's
  installer whether or not it links anything, and the distinction that admitted
  Eigen while ruling out mlpack turned out not to be the one that mattered.

  Three findings worth keeping. **The cost of the swap is 0–16%** — parity at
  N=9, 13–16% slower at N=6 and N=12 — recorded as a number rather than rounded
  to "equivalent", and small against the 3.95–5.44× AD wins over central
  differences in the first place. **The gap closed by removing temporaries, not
  by alignment.** `alignas(32)` measured *slower* (it inflates every `Dual`, and
  169 are live in the inner loop) and padding N to the SIMD width did nothing;
  returning a proxy from `scalar * grad`, so the multiply fuses with the
  accumulate that always follows it in the product and quotient rules, is what
  worked — and made the vectorized gradient bitwise identical to autodiff's own
  scalar `dual`. **The guard test the source comment claimed had never been
  written.** `ImageLocalization.cpp` asserted the vectorized path was "guarded
  by a unit test that compares [it] against scalar `dual`"; there was no such
  test, and the `NumberTraits` specialization it depends on is undocumented
  upstream, so an autodiff bump would have compiled cleanly and produced wrong
  derivatives. `test/cpp/test_ad_gradient.cpp` now does what the comment said.

  A methodological note that cost real time and is recorded in
  `benchmarks/README.md` so the next person does not repeat it: the first
  harness used `steady_clock` and reported speedups from 0.22× to 4.77× for the
  same binary on consecutive runs. The machine was at load average 43 and wall
  clock keeps counting while the thread is descheduled.
  `CLOCK_THREAD_CPUTIME_ID` with interleaved implementations and min-of-nine
  made it repeatable to a few percent.

  Also closed `modules/MODULE-DEBT.md` item 3, and not by its stated exit — the
  plan was to extract `nn` so Eigen would be needed by two modules instead of
  all of them; what happened is that both consumers stopped needing it. The
  audit that came with it found `clsm` and `superres` declaring
  `EXTERNAL_DEPS tttrlib::eigen` while including neither, and `localization`
  declaring Eigen while using autodiff. Nothing caught that, because the
  top-level `INCLUDE_DIRECTORIES` for `thirdparty/` puts the vendored headers on
  every module's path regardless — so `EXTERNAL_DEPS` is documentation until a
  module compiles with only what it asked for. That is the item's new exit.

## 2026-08-10 (13th entry)

* **PRD-016 M5 closed: the JavaScript binding is packageable.** The milestone
  read as metadata work — "`prebuildify` binaries, `node-gyp-build`, npm
  release". Neither half of that survived contact.

  `prebuildify` drives `node-gyp`, and this addon is a CMake target linking 35
  module libraries; a second hand-maintained build description in
  `binding.gyp` is the drift this project avoids everywhere else. Only the
  *output* is a contract, so `scripts/prebuild.mjs` produces
  `prebuilds/<triple>/node.napi[.<libc>].node` from `cmake --install` and
  `node-gyp-build` reads it unchanged.

  **The actual work was that the addon had never been relocatable.** It found
  its 35 sibling libraries plus HDF5 and libomp through absolute build-tree
  RPATHs, and `ext/CMakeLists.txt` carried a comment asserting the modules "sit
  next to the addon in js-pkg/, the same layout the npm package ships" —
  describing a staging step that did not exist. Every test had run on the
  machine that built it, so nothing could have caught it. Three answers:
  `-DTTTRLIB_MODULE_TYPE=STATIC` for prebuilds (one 14 MB `.node`, two external
  dependencies), a `js` install component so CMake rewrites RPATHs to
  `@loader_path` / `$ORIGIN` — *install*, never a copy out of the build tree —
  and a verifier that refuses any prebuild whose dependencies reach outside its
  own directory.

  Two checks earn their keep. The STATIC smoke test counts **registry entries**
  rather than merely loading the binary, because a static archive can drop the
  translation units whose only purpose is a registration side effect and the
  result loads perfectly while advertising nothing. And `pack.mjs` refuses a
  package missing any of the five platforms: a partial one is
  indistinguishable from a complete one until somebody on the missing platform
  installs it.

* **PRD-032 broke the JavaScript binding, and its own verification could not
  have seen it.** `TTTR::burst_search_algorithms_json` parsed the migrated
  category into an `nlohmann::json` and dumped it again. That type is a
  `std::map`, so `params_schema.properties` came back alphabetically sorted —
  and that key order *is* the C++ argument order. JavaScript has no `**kwargs`,
  so `burstSearchByName` began passing `max_false_alarm_rate` where `p0`
  belongs. Python, R and Java all survived it.

  The before/after registry capture reported "0 entries changed" because it
  compared **parsed** JSON, and parsing is precisely the step that discards key
  order. That is the generalisable lesson: a structural diff of a
  consumer-visible blob is blind to ordering, so it cannot certify a
  serialisation change. The invariant that does catch it is now a test —
  `required` keeps declaration order, so it must be a subsequence of the
  property keys.

## 2026-08-10 (12th entry)

* **PRD-032: `kBurstSearchRegistry` migrated and deleted.** Seven searches
  described in a literal in one file and dispatched from a table in another —
  two lists with nothing keeping them in step, which is not a hypothetical:
  `bocpd` and `coincident` were advertised with a `method` the dispatcher had
  never heard of. One `register_burst_search(descriptor, fn)` call each now,
  descriptor registered first so "described but not runnable" is
  unrepresentable. `BurstSearchDispatch.cpp` no longer names a single search.

  **The check that matters is the before/after registry capture**: 0 removed,
  0 changed, 63 added. Additive-only is the whole contract for a
  consumer-visible blob, and it is cheap to verify and impossible to eyeball.

  Two failures only running it revealed. `algorithms_json("burst_search")`
  came back **empty** — it primes the algorithm module's built-ins, not the
  burst ones — so the category silently lost all seven entries instead of
  failing; watch for that in the remaining migrations. And
  `PluginHost::burst_searches_json()` returns a **brace-less fragment**, because
  it was written to be spliced into the middle of a literal; the splice it
  replaced found its insertion point with `find_last_of('}')`, a parser written
  in string search, which a description ending in a brace would have moved.

* **PRD-012 closed with a criterion waived, not met.** No FLIM LABS sample file
  is published anywhere and none is expected, so the `STT1` reader ships
  verified against the specification and synthetic fixtures only. The
  distinction is written into both the PRD and the index, because "green" here
  does not mean what it means on the other rows: the three facts the BrightEyes
  work had to *measure* (photons pair with the next laser word, the TDC counts
  backwards, only 27% of records carry their own laser word) are exactly the
  class of thing a specification does not state — and `STT1` has no equivalent
  measurement behind it. If a real file ever arrives, verify before trusting.

* **PRD-016: M6 was done and nobody had noticed.** The gap list said the
  conformance runner did not exist; `test/js/conformance.test.mjs` runs all 96
  cases across 19 areas with no `unsupported` declarations, which is this PRD's
  actual acceptance criterion. Two more of its six gaps were also already
  closed (HDF5 is wrapped in all four bindings). **A status note nobody reruns
  decays into fiction in about four days** — the note is now dated and its
  closed rows kept visible rather than deleted.

* **A test that described a property instead of exercising it.** The one test
  covering the zero-copy contract asserted that `arraysAreZeroCopy()` returns a
  *boolean*. That passes in a build that copies every array, in one that copies
  none, and in one where the function is a stub. This is the second time this
  binding has produced a vacuous test in the same way, so it is now written down
  as a rule rather than an anecdote. The replacement writes through an
  `ARGOUTVIEW` output (`Pda.get_amplitudes`, a view onto a live `std::vector`)
  and reads it back, and asserts **against the build mode** — so the zero-copy
  and copy builds must disagree. Both were built and run; they do. That also
  retires the "copy fallback has never executed" gap: the Electron/V8-sandbox
  path now has the whole suite behind it.

* **The leak that four bindings and a conformance suite did not see.**
  `get_routing_channel`, `get_event_type` and `get_used_routing_channels` are
  the three accessors on `(signed char** output, int* n_output)`. `signed char`
  was applied as `ARGOUTVIEW` — "C++ owns this" — while every sibling type on
  the identical signature used `ARGOUTVIEWM`. All allocate through
  `get_array<T>`, which `malloc`s. The promised owner did not exist, so nothing
  freed it: one byte per event per call, in Python, R, Java and JavaScript.
  39 MB over 200 calls, against 0.4 MB fixed.

  **The values were always correct**, which is the whole reason it survived:
  every test asks what the numbers are and none asks what the memory does. The
  regression test measures the footprint, and was confirmed to fail on a
  rebuilt-with-the-bug binary (36.8 MB against a predicted 36.7) before being
  kept — a leak test that has never seen the leak is the same vacuity as above.
  The JavaScript leak check read only `get_macro_times` and `get_micro_times`,
  both always `ARGOUTVIEWM`, so it could never have fired; it now reads the two
  affected accessors too.

* **ASAN is blocked on the platform, not on us.** PRD-016 asks for the "drop the
  owner, then read the view" case under a sanitiser. The instrumented addon
  builds, but preloading the ASAN runtime into Node on macOS arm64 hangs before
  any JavaScript runs — reproduced with **no addon loaded at all**. Run it from
  the Linux CI job; do not spend more time on it here.

## 2026-08-10 (11th entry)

* **PRD-026 closed.** The three items on its remaining-work list are done.

  **stdin (R2).** `tttr sm -`, `convert -` and `correlate -` read a pipe. It is
  spooled to a temp file, not streamed, and the note is the point: every
  container reader here seeks -- a PTU jumps to its record block, a PTO reads a
  directory at the end -- so streaming would mean a second decoder per format or
  a silent failure on the formats that jump. `InputPath` is a type so the
  temporary dies on every exit path. Two things only running it revealed: `-`
  must not reach the burst table's `First File` column or the container's object
  names (a piped input is `stdin`), and the source checksum must be taken over
  the bytes actually read, since `-` cannot be opened. Piped and direct output
  are now identical but for the source column.

  **Window column order.** `DetectorSetup::windows` was a `std::map` beside a
  `detectors` vector that already carried an "in file order" comment. A setup
  declaring prompt/delayed produced delayed/prompt: values right, positions
  wrong, invisible until something reads the table positionally -- which is what
  a `.bur` is for.

* **Two collisions with the other instance, both instructive.** It renamed the
  operation vocabulary (`bva` -> `burst_variance_analysis`, `kde_cde` ->
  `burst_2cde`, `mle_green`/`mle_red` -> `burst_lifetime_fitting`,
  `hmm_photon_by_photon` -> `photon_hmm`) across `OperationRegistry.cpp`,
  `BuiltinAlgorithms.cpp` and `mmfdb.dic` while I was working in the same files.
  The registry/dictionary conformance test written this morning reported the
  half-applied state precisely, in both directions -- which is what it is for --
  and everything agreed once rebuilt. I followed the rename rather than
  reverting it; descriptive mmfdb names are what PRD-027 asks for.
  Also: I "restored" a `data_format` enumeration that had been reduced to one
  row, and was wrong to. There is an upstream **mmfdb package**
  (`../mmfdb/src/mmfdb/data`) whose dictionaries are the authority, and
  `test_vocabulary_matches_mmfdb.py` fails on any term tttrlib invents. mmfdb's
  `data_format` is about *storage* -- bur, dstore, ptu, csv, hdf5 -- while the
  MFD companion suffixes (.bg4, .bv4, .2c4, .td4) name what a table *is*, which
  is `operation_type`'s job; and an object inside a container has no suffix at
  all, which PRD-026 already settled by writing every artifact as `dstore`. The
  reduction was right and my restore was not. Reverted to mmfdb's terms, with
  the reasoning written into the item so the next person does not re-add them.

* **The hand-copied `.so` got SIGKILLed by AMFI again** (rc=137) after a deploy
  that raced the other instance's build. `codesign -s - -f` on the extension and
  the module dylibs revives it. This is the failure the build note warns about;
  `pip install -e .` remains the supported path.

## 2026-08-10 (10th entry)

* **New rule, and it is a rule about API shape**: every algorithm added to this
  library must be usable on photons. Written up in
  [specs/photon-native-algorithms.md](specs/photon-native-algorithms.md) and
  registered in the index. Two entry points, never one -- a standard form on the
  binned array (numerically identical to whatever reference people will compare
  it against) and a `*_events` form that takes the detections with fractional
  coordinates and never builds the grid. Where an algorithm genuinely has no
  event-wise formulation, the fallback is `Jitter.h`, not binning.
* **`Jitter.h` / `Jitter.cpp`** (new, in `modules/math`): `jitter_coordinates`,
  `events_from_counts`, `counts_from_events`, with SWIG bindings and
  `test/python/misc/test_jitter.py`. The dither is uniform across the bin, drawn
  from the counter-based path of the central RNG per (photon, axis) so the
  result does not depend on thread count. The justification in the header is
  deliberately *not* "binning biases the mean" -- it does not; it is that
  binning creates ties, and 98.4% of nearest-neighbour distances collapse to
  exactly zero, which is what makes distance-based methods degenerate rather
  than merely noisy.
* **`richardson_lucy_events` gained `psf_oversampling`**, and it is not a tuning
  knob. Interpolating the kernel at a photon's fractional offset is itself a
  convolution of variance `t(1-t)` -- up to 0.25 px^2, *varying with sub-pixel
  position*, which is the exact quantity event mode exists to preserve. Flux,
  centroid and non-negativity were all exactly right while this was happening.
  Sampling `K` times finer divides it by `K^2`. It had also been flattering the
  benchmark, since an over-wide forward model over-sharpens.
* **Two smaller fixes in the same file.** The kernel is now normalised on the
  *comb* it is actually sampled at rather than on the fine array's Riemann sum
  (worth 1e-4 in flux and 8e-4 px in centroid), and `scan_blur_kernel`
  integrates its fine grid down by overlap rather than to the nearest output
  sample -- `refine` is even, so one fine sample per output sample sat exactly
  on a boundary and rounding sent every one of them the same way, leaving a
  symmetric kernel whose mean was 1/4096 px off centre.
* **Measured and recorded**: PSF *truncation*, not interpolation, sets how
  accurately a photon reconstructs to its own position -- 3.7 sigma of support
  gives 8e-4 px, 5 sigma gives 3e-6, 6.3 sigma gives 2e-9. Five sigma is the
  number to remember.

## 2026-08-10 (9th entry)

* **The .pto provenance vocabulary is now in mmfdb.dic, and a test keeps it
  there.** The ask was that the registry literals be documented in mmfdb and
  compatible with it; the gap turned out to be much wider than the four names I
  had flagged. `mmfdb.dic` had **no operation category at all** -- so every
  `_mmfdb_operation.*`, `_mmfdb_artifact.*` and `_mmfdb_edge.*` tag the .pto
  writer has been emitting all along, including the eight hand-authored pipeline
  operations, was an undefined name. A reader holding a .pto could not validate
  its `operation_type`, resolve a settings schema for it, or learn what one row
  of an artifact is.
  Added: three categories; `_mmfdb_operation.operation_type` with an enumeration
  that IS the controlled vocabulary (12 operations); controlled vocabularies for
  `row_grain` (burst / photon / curve_point / histogram_bin) and `data_format`
  (bur, bg4, br4, bv4, 2c4, fu4, td4, irf); definitions for `settings_json`,
  `parent_operation`, `relationship_type`, `source_node_id`; and a save block per
  operation with its label, grain, format and replayability.
  `test/python/test_registry_matches_mmfdb.py` enforces it **both ways** -- a
  registered operation absent from the dictionary fails, and a dictionary entry
  nobody registers fails too, so the dictionary cannot quietly describe an
  operation the library dropped. Both directions were verified to actually fire
  by perturbing the dictionary; a conformance check that cannot fail is
  decoration, and this file already had one category of names nobody was
  checking.
  Remaining for criterion 10: settings keys and output column names are not yet
  checked against the dictionary.

## 2026-08-10 (8th entry)

* **PRD-032, unblocking criterion 1**: the algorithm registry moved to its own
  module (`modules/algorithm`) depending on nothing but a JSON writer. It had
  to: `registry` already depends on `burst`, so an algorithm module that wanted
  to register itself could not depend on `registry` without closing a cycle --
  nothing could migrate while the mechanism sat *above* the algorithms.
  `AlgorithmDescriptor` also gained `dispatch_name` (emitted as `method`, and
  omitted when empty, since that absence is the signal consumers read) and
  `provider`, and every entry now carries `params_schema` beside
  `settings_schema`. So a category can migrate onto registrations without its
  entries changing shape -- verified against a pre-change capture of the whole
  registry: 16 differences, all additions, none removed or changed.

* **Two more registry-advertised searches that ran something else**: `bocpd` and
  `coincident` both carry a `method`, so a UI dispatching by name called them,
  and both returned sliding-window bursts through the unrecognised-name
  fallback. `bocpd` now dispatches; `coincident` cannot (its essential input is
  the channel grouping, and (L, m, T) has nowhere to put it) so it raises and
  names the call that works. This is the fourth instance today of the same
  shape: a plausible answer from the wrong code path. The test that should have
  caught it passed vacuously -- it asserted the result was well-formed, which
  the fallback always is. It now requires each name to produce what its own
  entry point produces.

## 2026-08-10 (7th entry)

* **PRD-032 criteria 3 and 4**: a burst search contributed by a plugin is now
  callable through `TTTR::burst_search(name, ...)`, the door every built-in
  uses. The interesting part is what the old code did instead: the `if/else`
  chain fell through to the sliding window for any name it did not recognise, so
  passing a plugin's name returned sliding-window bursts. The registry listed
  the plugin's search and the obvious call quietly ran something else -- a
  wrong answer that looks like a right one, which is the failure mode this
  library keeps finding.
  Also found: the whole plugin test suite had been skipping. `_plugin_binary()`
  globbed only scikit-build's `build/<tag>/` layout, so anyone configuring into
  `build_new/` got 24 skips and a message telling them to turn on the option
  they had already turned on. Widened; the fast lane went from 46 skipped to 23.
  Verified the registry blob is byte-identical to a capture taken before the
  work (0 differences) -- worth doing before touching anything consumer-visible.

  **Criterion 1 (delete the three literals) is blocked, not merely undone**, and
  the reasons are recorded in the PRD: (a) `burst_search` and `fit` have a
  consumer-visible entry shape (`method`, `params_schema`, `provider`) the
  generic descriptor does not produce, and PRD-027 requires the shape be kept --
  so the descriptor needs a dispatch-name field and aliases first; (b) retiring
  `kOperationRegistry` faithfully means each descriptor moving next to the code
  that performs it, and several of those operations have no C++ home yet
  because they are the PRD-026 pipeline steps. Moving eight entries into eight
  blocks in the same module would satisfy the wording and none of the purpose.

## 2026-08-10 (6th entry)

* **PRD-027 criterion 6**: `TTTR::burst_search` now dispatches on a name through
  a table (`BurstSearchDispatch.h`) rather than a chain of `if (mode == ...)`.
  The point is not tidiness: the chain sat inside the one function every burst
  search has to be reachable from, so adding a search meant editing that
  function, and a search contributed from anywhere else could not be reached by
  name at all. `register_burst_search(name, fn)` from the search's own
  translation unit is now the whole of it.
  Two behaviours were easy to lose in the move and are pinned by the new
  `test/python/burstfilter/test_burst_search_dispatch.py` (17 cases): the `T`
  reinterpretation the narrow `(L, m, T)` signature forces on `kalman`,
  `maxtree` and `bayesian_blocks` -- a property of the signature, not of the
  algorithms -- and the fallback that runs the sliding window for an
  unrecognised mode instead of raising, which callers rely on. Fast lane green:
  2325 passed.
  Found while scoping the rest of criterion 2: the `burst_search` and `fit`
  categories have a consumer-visible entry shape (`method`, `params_schema`)
  that the generic descriptor does not produce, and the PRD requires those
  categories keep their shape. So retiring those two literals needs a
  dispatch-name field and shape-compatible aliases first -- it is not a move.

## 2026-08-10 (5th entry)

* **PRD-026, the C++ burst-table half: done and verified against ChiSurf.**
  [PRD-026](prds/PRD-026-mfd-sim-to-ndx-pto-pipeline.md) updated with what
  landed. `tttr sm` now generates its columns from the `--setup` detector file
  rather than a hardcoded green/red split, and the result matches ChiSurf's
  `generate_burst_dataframe` **cell for cell** on simulated MFD data: 23/23
  columns on a two-detector setup, 37/37 on a four-detector one, 27/27 with PIE
  windows and micro-time gates. ndX opens the container, joins the companions
  and maps the provenance graph.

  Six things the port settled that were not in the design, all recorded in the
  PRD: `data_format` is `dstore` and not the legacy `bur`/`bg4`/`bv4`/`2c4`
  (which name a *file* layout); the `.bur` 2N+1 interleave and its blank column
  are deliberately **not** written, contradicting R5 as originally worded,
  because a container declares relations as keys and ChiSurf's own container
  writer deinterleaves first; a companion's run identity has to include its
  parent's `settings_hash` or the next search silently overwrites it; a burst
  search returns *inclusive* indices, so the old table was one photon short in
  every burst; `tttr sim` was encoding on a hardcoded micro-time axis, so a
  3.8 ns simulated decay read back as 1.9 ns; and the container was holding a
  **re-encoded, channel-filtered** copy of the measurement where the profile
  promises the instrument file verbatim — it now goes in unchanged with a
  SHA-256, round-trips byte for byte, and is smaller for it (1.9 MiB of `.spc`
  against 4.6 MiB of re-encoded `.sm`).

  Worth keeping from the verification: the two column tables agreed on 23/23,
  37/37 and 27/27 cells *including* `Confidence (sigma)`, which is the strongest
  evidence the port is a port and not a re-derivation. The one column that did
  differ on the first run was `First File` — ChiSurf writes `Path(f).name`, with
  the suffix, and `file_stem` drops it, so a merged table could not have told
  `run.spc` from `run.ptu`.

  The two placeholder MLE tables were **removed rather than kept** — they held
  the same five constants for every burst, which nothing downstream can
  distinguish from a measurement. BVA and FRET-2CDE are now real computations,
  and are omitted when their donor/acceptor streams cannot be named. Doing MLE
  for real is the remaining work, and is blocked on the simulator putting all
  its background photons in micro-time channel 0 (`BUGS.md`), which any
  lifetime fit on simulated data would read as a large scatter component.

* **The two chiSurf PRDs that touch the same files, handled with it.** `tttr sm`
  is a writer of `.mmfdb.pto` containers that lives outside chiSurf, so chiSurf
  PRD-84 (units) and PRD-88 (every provenance graph reconstructs) are its
  problems too, and neither was going to be caught from one side.

  - **PRD-88 reproduced its own root cause here.** Its finding was that a writer
    which *creates* a container never re-asks for the primary, so the defect only
    shows on **reopen**. The C++ writer had exactly that, in its own idiom: it
    looked the photon stream up by **file name**, so extending a container
    chiSurf had made added a *second* `tttr_photon_stream` whenever the name did
    not match — two roots, with the burst table on the one nothing else
    references. Now matched on `_mmfdb_artifact.checksum`. Verified by driving
    the real writers: chiSurf creates, the CLI extends, every artifact walks back
    to one primary — including when the CLI is pointed at a byte-identical copy
    under a different name.
  - **PRD-84's unit rule had three holes, and a second implementation is what
    found them.** `Number of Photons` was `photons` while
    `Number of Photons (green)` — same row, same table — was unitless, because
    the fallback table keys on the exact name. `S prompt green (kHz) | 0-2048`
    carries *both* in-band conventions at once and matched neither, since the bar
    branch gave up on `0-2048` and the suffix regex is anchored at the end. And
    `FRET 2CDE` had no entry at all. Fixed in chiSurf, ported to C++, and pinned
    by a test that asks both writers for the unit of every column and requires
    the same answer — 40/40 and 30/30. Worth repeating at the next boundary: two
    implementations turn "what should this column's unit be?" from taste into a
    question with one answer.
  - **PRD-88, widened: the container writer was emitting four words nothing can
    query.** The profile defines no vocabulary of its own, and ChiSurf's
    `put_table` checks every term against the dictionary before writing — so a
    ChiSurf writer *cannot* invent one, and every `operation_type` in that tree
    is declared. The C++ writer makes no such check (it does not link mmfdb and
    has no mmCIF parser) and had been emitting `bva`, `kde_cde`,
    `mle_<detector>` and `companion_of`, where the dictionary says
    `burst_variance_analysis`, `burst_2cde`, `burst_lifetime_fitting` and — for
    a companion — plain `derived_from`. Every container written so far carries
    them. The check now lives on the ChiSurf side, which is the only side that
    can read the dictionary.

    Two things fell out. The cross-writer test's own "the relation is a term"
    assertion had been *listing* `companion_of` among the accepted values, so it
    passed on precisely what it existed to catch — an allow-list copied from the
    code it is checking is not a check. And ndX's `_COMPANION_TYPES` was a set of
    informal spellings no writer produces; it kept working only because the
    parent edge is what actually links a companion, so it was a fallback that
    would have failed the first time it was needed.

    **Third instance today of one shape:** a term, a unit or a name that one side
    invents and the other never checks. Same fix each time — put the check where
    the vocabulary lives, and drive the other side through it.

  - **A build defect the OpenMP fix exposed, and the wrong fix for it first.**
    `BUILD_WITH_INSTALL_RPATH ON` gave the build-tree binary only the *install*
    rpath, and `@loader_path/../lib` does not resolve in a build tree — the
    library is at `<build>/libtttrlib.dylib`, not `<build>/lib/`. That was
    survivable as the single entry: `tttr` failed loudly and a
    `DYLD_LIBRARY_PATH` fixed it. Adding the OpenMP prefix as a second entry
    made it dangerous, because a conda prefix commonly carries a
    `libtttrlib.dylib` symlink into *another* build tree — so the tool started
    successfully on a months-old library, with the wrong subcommands and no
    error anywhere, and `tttr sm --output x.pto` overwrote an existing container
    with the old JSON burst list. **A loud failure is a bug; a quiet wrong
    answer is worse**, and the first attempt turned one into the other. Caught
    only because the cross-repo audit reopened a container the CLI had just
    written and found JSON where EBML should be — the tests all passed
    throughout, because each sets `DYLD_LIBRARY_PATH` for the subprocess and so
    never exercised the rpath at all.

* **PRD-026's MLE half, and four things that looked fine and were not.**
  `tttr sm --mle --irf <spec>` fits one lifetime per burst per detector through
  `fit23`. Ground truth recovered: 3.87 ns for the 3.8 ns species, 1.71 ns for
  the 1.6 ns one, as two resolved modes.

  What makes this worth writing down is that the *first* version recovered
  3.69 ns and looked like a success. It was luck. On a synthetic burst of the
  same size the same configuration returned **6.68 ns for a 3.8 ns truth**, and
  moved by a factor of two on a change of start value — with a 2I\* that still
  looked reasonable. Four corrections, in the order they were found:

  - **`DecayFitProblem(n_channels, n_bins, dt)`**, and it was being called
    `(n_bins, 1, dt)`. Every burst then failed `require_valid`, every exception
    was caught per burst, and the whole thing printed `0/3787 fitted` — which
    reads as "no burst had enough photons", not as an error. The catch now
    reports the first reason once.
  - **Only `tau` may be fitted.** A hundred photons do not determine an
    anisotropy. r0 = 0 with rho held is also the precondition for the kernel's
    own well-conditioned path.
  - **`gamma` is the background *fraction of the burst*** — the kernel
    renormalises the background pattern to the burst's own total, so the pattern
    gives a shape and gamma gives the magnitude. Measured from the photons no
    burst contains, times the burst's duration. Left at 0 it subtracts nothing
    and biases every lifetime up by ~50% on a 13% background. (Measured on a
    synthetic burst: 5.67 ns at gamma 0, 4.09 at the true fraction, for a
    3.8 ns truth.)
  - **Rebin before fitting** (`--mle-bins`, default 128): ~100 photons over 4096
    raw TAC channels is almost all zeros, and the convolution is 4096 long for
    no gain. 14 s → 0.26 s.

  `--irf` is **required** and has no default: a prompt is a claim about the
  instrument, and a lifetime fitted against the wrong one is wrong by roughly
  its width with nothing saying so. `--irf delta` exists and has to be typed.
  The synthetic prompts are `gauss:FWHM[,T0]` and `sgauss:FWHM[,T0[,SKEW]]` —
  the skew-normal `exp(-z²/2)·(1 + erf(αz/√2))`, default skew 1.5, positive
  tailing to later times as a real prompt does. `T0` is the skew-normal's
  *location* parameter, not its peak; that is how it is parameterised
  everywhere and what a fit to a measured IRF hands back, and it is the sort of
  thing that reads as a 0.1 ns calibration error if nobody writes it down.

  **The IRF is checked by physics, not by parsing.** A spec that parses and is
  then ignored passes every test about column names. So: the simulation
  convolves with nothing, therefore `delta` is the *correct* model and must
  recover the truth best (3.87 ns against 3.8); a wider prompt takes more out of
  the decay, so the recovered lifetime must fall monotonically with its width
  (3.87 → 3.67 → 3.03 ns for delta, 0.5 and 2.0 ns FWHM); and a skewed prompt of
  the same width must differ from the symmetric one. Three properties, each of
  which a disconnected IRF fails.

  The general lesson, which is the same one as the rpath: **an ill-conditioned
  fit does not announce itself.** It agreed with the truth on real data and was
  wrong by 75% on synthetic data of the same size. The only thing that
  distinguished them was a ground truth cheap enough to test against every time.

* **Two setup errors that change no total and every conclusion.** The shipped
  MFD setup had `green: [8, 0]` while the simulation emits green-*parallel* on
  routing 0 — `chs[::2]` is parallel, so every anisotropy from that file would
  have been inverted with every photon count correct. PRD-026's R3 contained the
  same contradiction in prose (it called 8/9 parallel while listing `green →
  [0, 8]`); the lists are what the code reads, so the prose was corrected.
  Separately, the setup's `g_factor`/`l1`/`l2` were parsed and thrown away — a
  g-factor left at 1 when the file says otherwise is a wrong number, not a
  missing one.

* **Naming, now normative:** `.pto` is *the container*; `.mmfdb.pto` is a `.pto`
  that also carries the PTO.MFDB profile. The tag goes on the stem, never the
  suffix — `.pto.mmfdb` would stop being a container to everything dispatching
  on the extension. Written into the profile spec (chisurf
  `okf/specs/pto-mfdb.md`, regenerated into `doc/formats/pto-mfdb.rst`), the
  container spec, both module READMEs and `tttr sm --help`.
  `modules/io/pto/README.md` had had it backwards as `.pto.mfdb`.

## 2026-08-10 (4th entry)

* **PRD-027 Parts 1/3**: `AlgorithmDescriptor` + `register_algorithm` — one
  registration path for built-ins and plugins — and `registry_json()` assembling
  `fcs`, `hmm` and `pda` from live registrations. This closes the PRD's *first*
  stated problem: those three families worked and were invisible, and the reason
  was not architectural but clerical — burst searches and decay fits had a
  hand-written JSON literal each, and these had none. Consequence of being
  invisible: no UI listing, no schema for `.pto` provenance to validate or
  replay against, and no name for a plugin to compete under.
  Each entry carries full prose `description` and a `references` array
  transcribed from the reference lists the sources already carried (`Correlator.h`,
  `HMM.h`); PDA carried none, so its citation comes from the literature and is
  marked as needing confirmation. Replayable registrations reach the `operation`
  category. Merging is additive by design — a live registration never displaces
  a hand-authored entry of the same name, because a name collision is a mistake
  to surface, not to settle by load order.
  30 new tests plus a conformance case (`registry.live_algorithm_categories`) so
  every binding has to serve the new categories; the conformance suite caught
  the category-list change itself, which is what it is for. Whole fast lane
  green: 2308 passed.
  Explicitly left, each its own piece: retiring the three literals (PRD-032),
  table dispatch on `TTTR::burst_search`, the `TTTRLIB_MODULAR_ALGORITHMS`
  split, `PtoFile.citations*`, and mmfdb dictionary entries for the four new
  operation types (criterion 10 cannot be claimed without them).

## 2026-08-10 (3rd entry)

* **Feature**: `StreamingCLSMImage` — scanned-image reconstruction from a live
  photon stream, with LIVE (the frame being scanned, filling in) and
  INTEGRATING (the sum over completed frames) and a switch between them that
  works mid-acquisition without discarding anything: the sum accumulates in
  either mode, so the mode is a view rather than a processing path. The design
  decision worth keeping: it buffers one frame and hands it to the ordinary
  `CLSMImage` constructor instead of reimplementing where a frame begins (which
  depends on the reading routine, a walk-back over markers sharing a macro-time
  tick, and a B&H first-frame correction). Memory is one frame rather than the
  acquisition -- the part that genuinely has to change for a stream -- and the
  pixel assignment is the batch's by construction. The partial frame is rebuilt
  per query, not per photon; a viewer redraws thousands of times slower than
  events arrive. Two traps found on the way: an *incomplete* frame must not be
  allowed to settle the image geometry (the first redraw pinned it at 26 lines),
  and `TTTR::append_events` left the used-routing-channel cache stale, so a
  CLSMImage built on an assembled TTTR returned the right shape with zero
  photons in it and no error anywhere. `cmc` has an equivalent decoder
  (`analysis/image/clsm_decoder.hpp`) that had to reimplement the marker
  handling because tttrlib offered nothing to stream against.

* **Environment**: the `build_new` tree links Homebrew HDF5 while the mambaforge
  interpreter loads conda's, so reading a Photon-HDF5 file from that build
  aborts with "HDF5 library version mismatched error". Not a code defect --
  `HDF5_DISABLE_VERSION_CHECK=1` runs the whole tttr suite green (633 passed).

## 2026-08-10 (2nd entry)

* **Correctness**: `StreamingCorrelator` fixed and
  [PRD-033](prds/PRD-033-streaming-correlator.md) closed. The class was
  documented as broken "at cascade >= 2, probably a normalization or
  border-handling issue"; it was neither. `get_correlation` read every level at
  a constant first coarse lag of `n_bins/2`, while the multi-tau axis it
  publishes needs level b to start at `x[b*n_bins] / 2^b` — 0, 8, 12, 14, 15,
  15, ... Only level 1 equals `n_bins/2`, which is why cascades 0 and 1 agreed
  with the batch correlator and nothing above did. Reporting a shorter lag than
  the label claims inflates a decaying G, and inflates it more the coarser the
  level: the recorded 1.19 / 1.25 / 1.12 / 2.39. Mean stream/batch ratio is now
  1.0000 on every cascade out to lag 65520. Also fixed: the coarse-bin width in
  the normalization was `2^(j/n_bins)` instead of the batch's
  `2^((j-1)/n_bins)`, so every block-boundary lag was off by two; and the
  two-channel entry point accumulated one channel's weight only, computing an
  autocorrelation and calling it a cross-correlation (replaced by
  `push_photon(mt, w, channel)` with a per-channel history). New
  `test/python/streaming/test_streaming_correlator.py` asserts agreement
  **per cascade**, because an aggregate tolerance is exactly what hid this.
  The lesson worth keeping: the ratio being non-constant (1.19, 1.25, 1.12,
  2.39) was read at the time as evidence of a subtle normalization problem. It
  was evidence of the opposite — a constant would have been a normalization
  problem; a drift means the lags themselves are wrong.

* **Performance**: the same correlator was also paying a full cascade step for
  every *empty* bin (~19 ns), and a real acquisition has hundreds to thousands
  of empty bins per photon. Empty runs are now skipped in closed form -- level b
  emits `n0 / 2^b` times, so a run covers a difference of two divisions and only
  the first emission can carry a non-zero accumulator. 80k photons over a 4.0M
  bin span: 0.136 -> 0.063 s; over a 200M bin span the old code would have spent
  about 4 s and it now takes 0.069 s. The cost no longer scales with the length
  of the acquisition. Numbers in PERF.md.

* **Correctness**: the three streaming consumers PRD-033 listed as "works"
  beside the broken correlator were checked against their batch equivalents
  rather than taken at their word. `StreamingDecayHistogram` and
  `StreamingPhasor` hold up (exact vs `np.bincount` and the batch microtime
  histogram; 1e-12 vs `DecayPhasor`, and on the universal semicircle).
  `StreamingBurstDetector` did not: every burst ended one photon late (the
  failing window ends the burst at the *preceding* window's last photon);
  a burst of coincident photons -- the highest rate the detector can see -- was
  reported as no burst, because the rate was `m/span` with a zero span guarded
  by `rate := 0`; it kept every photon's macro time in a consumer meant for an
  unbounded live acquisition (now a ring buffer, O(m)); and a non-positive
  macro-time resolution silently returned the whole stream as one burst, which
  is reachable by accident since an unread header reports -1.0. Boundaries are
  now compared index for index against the batch search.

* **Build**: the macOS OpenMP detection that landed alongside this work made
  every library target fail to link — the flags reach the compiler but nothing
  linked the runtime. `LINK_LIBRARIES(OpenMP::OpenMP_CXX)` after detection.

## 2026-08-10

* **Correctness**: The non-symmetric eigensolver (`QREigen.h`, used by
  `BurstML`, and duplicated inside `GopichSzabo`) returned eigenvectors with a
  relative residual `||Av - lambda v|| / ||A||` of order 1 — no information —
  on every non-trivial matrix. Three independent causes: the null vector was
  un-permuted by the elimination's row pivots (row pivots permute equations,
  not unknowns), the null direction was read off a pivot that is rounding-level
  noise when the shift is an eigenvalue, and the balancing back-transform
  scaled columns instead of rows and was applied to vectors that had never been
  balanced. Eigenvalues were always right, which is why nothing downstream
  noticed. Residuals are now 1e-15. Also fixed: the QR iteration stalled
  silently on matrices with a symmetric spectrum (no exceptional shift) and
  reported success with zeros; `mat_solve`/`mat_inverse_inplace` used an
  absolute 1e-300 singularity floor, which is not a rank test; `mat_lstsq_minnorm`
  used an absolute rather than relative `rcond`, zeroing the solution of any
  uniformly small system; and the MaxEnt TCSPC active set handed its
  least-squares fallback the matrix `mat_solve` had already destroyed. New
  `test/cpp/test_mat_linalg.cpp` and `test/cpp/test_qreigen.cpp` check these as
  properties, not stored numbers.

* **Performance**: Eigendecomposition 5.5x faster serial, 10x on eight threads
  (n=100: 45.3 -> 8.3 -> 4.5 ms) by moving inverse iteration onto the
  Hessenberg form (O(n^4) -> O(n^3)), dropping the unused Schur-vector
  accumulation, and parallelising the per-eigenvector loop.
  `mat_lstsq_minnorm` 2.5-7.3x faster (column-major working copy, carried
  column norms). `GopichSzabo` now shares the solver instead of its own O(n^4)
  path. New tracked benchmark `benchmarks/bench_linalg.cpp` with a recorded
  baseline and a `--check` mode that fails on a >1.30x regression; table and
  method in PERF.md. Module READMEs (`math`, `kinetics`) and CHANGELOG updated.
  The FLIM/competitor plots are unchanged — this change touches no FLIM path.

## 2026-08-08 (2nd entry)

* **Performance**: FLIM per-pixel reconvolution MLE (`fit_map`) improved from
  456 ms to 161 ms (2.8x) on a 256x256 image. Three changes: (1) allocation-free
  inner optimization loop via `FitWorkspace` scratch buffers in `DecayFitNExp.cpp`
  (the grid scan + Brent + EM loop previously allocated ~6 vectors per
  evaluation); (2) `fit_batch_flat_buffers` SWIG binding with `IN_ARRAY2`
  typemaps eliminating the Python->vector element copy that cost ~400 ms on the
  image path; (3) `FitNExp` Python wrapper (`FitNExpWrapper.py`) providing the
  `__call__`/`fit_many`/`fit_map` interface the benchmarks reference. CPU now
  beats FLIMKit GPU by 5.3x (was 1.25x). Documented in
  [testing/benchmarking.md](testing/benchmarking.md) with the full steps and
  the documentation obligations for future perf work. PERF.md, CHANGELOG.md,
  and the decay module README updated.

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

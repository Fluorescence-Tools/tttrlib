# Handover — the Seidel / redRobin harvest

**Status 2026-07-29.** The tttrlib foundation is finished and green. None of the
Seidel/redRobin *harvest* has started. This file is the entry point for picking
it up.

Approved plan: `~/.claude/plans/check-seidel-and-robin-sleepy-frog.md`.
Nothing here is committed — both repos have large uncommitted working trees.

---

## Where to start next (the entry point)

**Read `~/.claude/plans/check-seidel-and-robin-sleepy-frog.md` first**, then start
at **the DFA model wrapper** — item 2 of the plan, and the last piece of
foundation before any harvest work.

Everything it needs now exists:

| It needs | Where it is |
|---|---|
| A model interface to subclass | `include/DecayFitModel.h` (all-`const` virtuals) |
| Variable-length rate spectra | registry `count_from` — `fit_nexp` is the worked exemplar |
| The VV/VH physics | `include/DecayFitDFA.h`, `dfa::vv_vh_convolved` |
| Convolution with a choice of backend | `dfa::convolve` (this session) |
| Parameters shared across measurements | `fit_linked` — the piece CELFIS needed |
| Bounds / priors | `DecayFitPrior.h`; now actually enforced on both paths |

Concretely, `src/DecayFitModelDFA.cpp` + a registry entry with:

- three rate spectra (donor / FRET / depolarisation), each `count_from` a setup
  slot, weights and rates flattened by the normative declaration-order rule;
- setup slots: `dt`, `period`, `g_factor`, `l1`, `l2`, `r0`, timeshift,
  `convolution_method`, D0-vs-DA mode;
- scatter, background, autofluorescence and `xaf` as `problem.patterns` entries
  (count-fraction ↔ amplitude-fraction conversion is the fiddly part);
- objectives from the registry category: Poisson deviance and Gehrels-weighted
  LSQ for low counts;
- **then the fit23 benchmark** the plan asks for: same simulated VV/VH data in
  fit23's own regime (one lifetime, one rotational correlation time), reporting
  bias and spread of τ, r0, ρ *beside* wall time, into `PERF.md`. It ships as an
  option; fit23 stays the default for burst MLE and `img_pixel_mle` whatever the
  outcome.

Use `ConvolutionMethod::Recursive` in the model. The spectral path is there for
measured patterns, a wrapping response, and cross-checks — not for speed (see
below).

After that, the harvest proper, in plan order: localization selection layer +
Mortensen CRLB → FRET nanoscopy extending `sm_image_mle` → drop `bocpd` →
imaging pipeline ending in species images → cPBSA → superposition consolidation.

---

## What is done

**tttrlib — the unified decay-fit interface, all 7 planned tasks.** One entry
point (`DecayFit2` by registry name + `DecayFitProblem` + `DecayFitConstraints`),
flat vectors with the registry supplying names, links as global slot ids, bounds
as priors, batched and linked fits, docs + example + notebook + tests, and the
old per-estimator surface deleted (Python keeps a deprecated shim until 0.29;
R and Java got a clean break).

**tttrlib — the DFA kernel and both convolution backends** (this session).

**Green at handover:** tttrlib **1006 passed / 21 skipped**; R all four suites;
Java all five tests. The four cross-language reference values reproduce exactly.

---

## This session, in detail

### Both convolution backends, benchmarked

`dfa_convolve(rates, weights, irf, n_bins, shift_bins, method)` — `0` recursive
(default), `1` spectral — plus `dfa_vv_vh_convolved` for the polarisation form.

**The FFT is not the fast path.** The recursion is **1.7×–6.2× faster**, and the
gap *widens* with the rate count — which is the regime a donor⊗FRET⊗anisotropy
outer product lives in. Both are `O(n·n_rates)`; the closed form costs a complex
division per rate and frequency against a multiply-add per rate and bin.
`benchmarks/bench_convolution.py`, recorded in `PERF.md`.

> **Correction to earlier notes.** A pre-implementation probe reported a flat
> ~7.2×. The measured figure through the real `convolve()` is 1.7×–6.2×, rising
> with rate count. Trust the benchmark, not the old number.

**The backends agree to machine precision (≤1.4e-14)** — and that took a
derivation rather than a tolerance. The recursion's trapezoid rule leaves the
kernel `exp(-kL)` at every lag except `L = 0`, where it leaves **one half**.
Halving one sample is subtracting half a delta; a delta has a flat spectrum; so
the entire difference is a constant `½` subtracted from the periodic spectrum.

> **Correction to earlier notes.** The "the two conventions differ by exactly
> half a bin, best match at a 0.5-bin shift" finding is superseded. A `[0.5,0.5]`
> IRF pre-filter *looks* like the fix and is wrong: it leaves a **rate-dependent**
> factor `(1+e^{-k})/2` (0.5% at k=0.01, 5% at k=0.1) that reweights a rate
> spectrum instead of scaling it — the same sin the circulating DFA formula
> commits. `D(w) − ½` is exact.

**Known limitation, pinned by a test:** the recursion starts at bin 0 as though
nothing preceded it, so it cannot see an instrument response whose tail wraps
around the period. Compact response → machine precision; broad one → only the
spectral path is right.

### Two defects found and fixed at the root

1. **`fconv_per_cs` shifted its wrap-around tail one bin early.** The tail loop
   applied a decay step *before* writing bin 0, but the value it held was already
   the continuation at bin `period_n` — which *is* bin 0 of the next period. All
   three `_cs` kernels (scalar, NEON, 2-channel NEON) carried it; `fconv_per` did
   not, because its main loop ends one bin earlier and compensates. Invisible
   when the decay completes within the period; **5.8e-5 of peak when it does
   not**, and worse for long lifetimes or high repetition rates. Fixed; the
   recursion now matches an exact circular convolution to 1.3e-15.

2. **Priors were stored, serialised — and ignored** by the single-row `fit()` for
   fit23/24/25/26. `fit_linked` honoured them; the primary path did not, because
   those models build their optimiser internally and never saw the constraints.
   So the documented "bounds are priors" contract silently did nothing while the
   caller believed the bound held. Bounds now travel on `DecayFitContext`
   (`lower`/`upper`, borrowed) and are applied by every fit2x kernel *including*
   fit23's 1-D Brent specialisation — an unbounded fast path would make a fit's
   answer depend on which internal route it took.

### The fit23 reference test moved its start value — read this before touching it

The 58-photon cross-language reference decay has an objective that **falls
monotonically as τ grows**: 23.80 at τ=0.74, 12.3 at 5, 5.1 at 20, 2.49 at
τ→∞. The historical answer is a **local** minimum with a basin of roughly
**0.5–1.2**. The old start of 2.1 sat near the edge, and the 1e-4 model change
from fix (1) tipped it out — the fit then *converges*, reports success, and
returns τ ≈ 57000.

**The reference values are unchanged** (2I\*=23.802337, τ=0.74219,
r_exp=0.25974). Only the start moved to 1.0, in Python, R and Java, each with a
comment. If you ever see these tests "fail" by an order of magnitude, this is
what happened — do not re-pin the numbers.

The runaway is now pinned by
`test_a_sparse_decay_runs_the_lifetime_away_silently` and documented as a failure
mode in `doc/fit-guide.rst`. **Bounding τ does not rescue it** — the objective at
any bound above ~1 is already better than at the true minimum.

### Files touched this session

```
include/DecayFitDFA.h          ConvolutionMethod, convolve, vv_vh_convolved
src/DecayFitDFA.cpp            both backends; the D(w)-1/2 reconciliation
src/DecayConvolution.cpp       wrap off-by-one, 3 kernels
include/DecayFitContext.h      lower/upper + apply_context_bounds()
src/DecayFitModelFit2x.cpp     bounds_from_priors / bind_bounds
src/DecayFit23/24/25/26.cpp    apply_context_bounds; fit23 Brent range
ext/python/DecayFit.i          dfa_convolve, dfa_vv_vh_convolved
benchmarks/bench_convolution.py            new
examples/fluorescence_decay/plot_convolution_methods.py   new
test/python/decayfit/test_convolution_methods_example.py  new
test/python/decayfit/test_dfa_kernel.py    +11 backend tests
test/python/decayfit/test_decay_fit_interface.py  +2, start moved
test/python/decayfit/test_fit2x_compat.py  start moved
test/r/test_decayfit.R, test/java/DecayFitTest.java  start moved
doc/fit-guide.rst, doc/modules/decay.ipynb, doc/api-coverage.yml
PERF.md, CHANGELOG.md
```

---

## Open / unverified at handover

- **chisurf's non-GUI suite is UNVERIFIED — it hangs.** Resolve this before
  committing. See "The chisurf suite hangs" below; it is very likely
  pre-existing and unrelated to this work, but that is an assumption until
  someone proves it.
- **Nothing is committed.** ~105 modified paths in tttrlib. chisurf's tree is
  shared with other agent instances: commit only your own files, never
  `git add -A`, never a destructive git command.
- The `bench_tttrlib.py` no-regression gate (single-curve 0.25 ms, batched
  0.07 ms/fit) has **not** been re-run since the convolution fix.
- OKF concepts and `okf/log.md` in chisurf are **not** yet updated for this work.

## The chisurf suite hangs — what is known

Three attempts, none reached a result. Each time the process **blocks** rather
than works: ~13 s of CPU over 7–8 minutes, 0.3–0.5% CPU, state `S`.

A `sample(1)` of the stuck process shows the main thread inside a **PyQt slot
chain ending in `poll`** — a nested Qt event loop, i.e. something is waiting on a
modal dialog that will never be answered headless. ZMQ IO/reaper threads sit in
`kevent` alongside it, so a chisurf server/client may be involved.

What has been ruled out:

- **Not my invocation.** It hangs with the project's own filter,
  `-k 'not gui and not widget and not window'`, and with
  `QT_QPA_PLATFORM=offscreen` set.
- **Not the tttrlib changes**, on the evidence available: chisurf needed no
  source change for the interface, and both fixes are ≪1e-4 in chisurf's regime.
  Unproven, though — nobody has run this suite to completion recently.

What is still unknown: **which test**. All three runs produced *zero* output,
even with `-v` writing straight to a file, which suggests it may hang during
**collection/import** rather than inside a test body.

How to chase it next (in order):

1. `pip install pytest-timeout` (not installed) then
   `--timeout=60 --timeout-method=thread` — the traceback names the culprit
   directly. This is by far the fastest route.
2. Failing that, bisect by directory: run `test/` alone, then each
   `chisurf/plugins/**/test` directory, with `-p no:cacheprovider -x` and output
   going to a terminal, not a pipe.
3. `--collect-only` first — if *that* hangs, it is an import-time modal and the
   offending module is the one after the last one listed.
4. Cross-check against the known trap in this codebase: raw `QMessageBox` /
   `QProgressDialog` are banned by a guard test in favour of `ChiSurfMessageBox`
   / `ChiSurfProgress` precisely because they hang headless. A new one may have
   slipped in, or a third-party dialog is being raised.

**Do not pipe a long run through `tail`** — pytest buffers, and you get nothing
at all until it exits. That cost ~50 minutes on the first attempt, during which
the run looked healthy and was in fact parked.

## Environment notes that cost time to rediscover

- Python: the `arm64` conda env (`/Users/tpeulen/mambaforge/envs/arm64`), py3.12.
  New `src/*.cpp` need a CMake re-configure; `.i` edits need
  `touch ext/python/tttrlib.i`; copy the `.so` **and** `tttrlib.py` to
  site-packages, then `/usr/bin/codesign --force --sign -`.
- **R lives in the `rtest` env**, not arm64:
  `/Users/tpeulen/mambaforge/envs/rtest/bin/Rscript`. `which R` finds nothing.
- **`build-r` and `build-java` need libomp explicitly** or they fail to link on
  `_omp_set_num_threads`:
  `cmake build-r -DCMAKE_SHARED_LINKER_FLAGS="-L$ARM/lib -lomp -Wl,-rpath,$ARM/lib"`.
  Then `install_name_tool -add_rpath $ARM/lib` and codesign.
  `DYLD_FALLBACK_LIBRARY_PATH` does **not** satisfy an `@rpath` reference.
- Java: `/usr/bin/javac` and `/usr/bin/java` (no JDK in the conda envs).
- **Run chisurf tests with the project's own filter**, not bare `pytest test/`:
  `-k 'not gui and not widget and not window'`. Without it a GUI test parks the
  whole run in a modal dialog — the process sits at 0.3% CPU with the main thread
  in a PyQt slot spinning `poll`, and looks identical to "still working". Never
  pipe a long run through `tail`; you lose all progress visibility.

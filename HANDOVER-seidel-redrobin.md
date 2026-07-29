# Handover — the Seidel / redRobin harvest

**Status 2026-07-29.** The tttrlib foundation is finished and green. None of the
Seidel/redRobin *harvest* has started. This file is the entry point for picking
it up.

Approved plan: `~/.claude/plans/check-seidel-and-robin-sleepy-frog.md`.

**Committed** (local only, never pushed):
- tttrlib `77662d29` — the interface, the DFA kernel, both convolution backends,
  the two defect fixes, docs/examples/tests. 76 files.
- chisurf `ffb742849` — the mle facade + burst-MLE wizard reaching tttrlib
  through the interface. 6 files.

Both were committed through a **temporary `GIT_INDEX_FILE`**, because both trees
are shared with other agent instances that had their own work staged — in
tttrlib a peer's Bayesian-Blocks CHANGELOG hunk, in chisurf 53 staged files. A
bare `git commit` would have swept those in. Afterwards the shared index was
realigned with `git reset -- <only my paths>` so a peer's next bare commit does
not revert this work. **Do the same next time; do not `git add -A`.**

Still uncommitted from this work: the `PERF.md` section (that file is untracked
and carries another agent's benchmark content, so committing it would have taken
their work too).

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

## Defect found late: `DecayFit2::model_curve` corrupts memory

`model_curve` segfaults when several short-lived `DecayFit2` + `DecayFitProblem`
pairs are created, used and destroyed in one process with `fit()` interleaved.
It is **not** reproducible with one long-lived fitter, and not with
`model_curve` alone — 8 create/curve/destroy cycles pass. It needs the mix:

```python
# segfaults on iteration 1, inside tttrlib's model_curve
for i in range(6):
    f = Fit2x(settings(), model=Fit2xModel.FIT23)   # one fitter for the curve
    c = f.model_curve([2.0, 0.0, 0.38, 1.2])
    d = poisson_from(c)
    g = Fit2x(settings(), model=Fit2xModel.FIT23)   # a second for the fit
    g.fit(d, [2.0, 0.0, 0.38, 1.2], [0, -1, -1, -1], include_model=True)
    del f, g; gc.collect()
```

The crash surfaces one cycle *after* the corrupting call, which is the signature
of a heap overwrite rather than a dangling pointer — and it is loud in a test
suite: it took down unrelated tests in `test_state_split.py` at 91%, four runs
out of four, alternating SIGSEGV and SIGABRT.

Sizes check out on inspection (`model_curve` allocates `problem.total_size()` =
`n_channels * n_bins`, and `DecayFit23::modelf` writes `2 * Nchannels` where
`Nchannels` is bins per polarisation), so the overflow is somewhere below
`modelf` — `fconv_per_cs_2ch` and the shared `fit_signals`/`fit_corrections`
statics are the places to look. Worth running the repro under ASan.

**chisurf does not use it.** A `Fit2x.model_curve()` pass-through was written and
then **removed** rather than shipped, because a facade method that can segfault
is worse than an absent one. Nothing else calls it, so the exposure today is
limited to direct `DecayFit2.model_curve` users.

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

A `sample(1)` of the stuck process settles what it is — and it is **not** a modal
dialog, which is what the PyQt frames in the stack first suggested. Searching
206 KB of stack for `QDialog`, `QMessageBox`, `QEventLoop`, `exec_` and
`processEvents` returns **zero** hits.

What it actually is: the main thread sits inside a **Python slot invoked from Qt**
(`QObject::event` -> `PyQtSlotProxy::unislot` -> `PyQtSlot::call`), and inside
that slot a C-level call blocks in `qt_safe_poll` -> `poll`. Underneath, a
sub-branch shows `zmq_msg_recv` -> `socket_base_t::recv` -> `mailbox_t::recv` ->
`signaler_t::wait` -> `poll`.

**So it is a blocking ZMQ receive made from a Qt slot** — chisurf's ZMQ/JSON-RPC
layer waiting on a server that never answers. Hunt for a test that stands up a
chisurf server or client and blocks on an RPC, *not* for a stray `QMessageBox`.

That also explains two dead ends: `QT_QPA_PLATFORM=offscreen` cannot help,
because offscreen rendering does not unblock a socket; and
`-k 'not gui and not widget and not window'` cannot help, because the blocking
code is not in a test named for the GUI.

What has been ruled out:

- **Not my invocation.** It hangs with the project's own filter and with
  `QT_QPA_PLATFORM=offscreen` set.
- **Not the tttrlib changes**, on the evidence available: chisurf needed no
  source change for the interface, and both fixes are <<1e-4 in chisurf's
  regime. A ZMQ RPC has nothing to do with a convolution kernel. Still unproven
  in the strict sense — nobody has run this suite to completion recently.
- **Not a test body.** `--collect-only` hangs too, so it happens at
  import/collection time.

How to chase it next (in order):

1. A scan that imports each of the 858 test modules in its own subprocess with a
   timeout was running at handover — that names the module outright. The script
   is `find_hang.py` (recreate: walk `test/` and every `chisurf/plugins/**/test`,
   `subprocess.run([python, "-c", "import <mod>"], timeout=45)`, print on
   `TimeoutExpired`). **Write its output straight to a file** — see below.
2. `pip install pytest-timeout` (not installed) then
   `--timeout=60 --timeout-method=thread`, which gives a traceback naming the
   line inside the module.
3. Grep the suspects directly: tests touching `chisurf/server`, `ChisurfClient`,
   `ChiSurfAPI` in `server`/`hybrid` mode, or anything constructing a plugin
   whose `manifest.json` declares `rpc_methods`.
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

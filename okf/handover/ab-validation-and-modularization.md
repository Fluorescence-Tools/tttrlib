# Handover — upstream A/B validation sweep + modularization close-out

**Date:** 2026-08-18 · **Branch:** `dev` (64 commits ahead of `origin/dev`,
**nothing pushed**) · **Author:** Claude (Fable 5) session, `okf/log.md`
entries 30–47

Two threads ran in this job, one after the other. Both are at a natural stop:
what is left needs either CI (a push) or a design decision by the owner.

## 1. What the job was

1. **A/B-validate every tttrlib algorithm against an independent *upstream*
   implementation** (not a ChiSurf port when real code exists), mark each
   kernel in source (`// Validation:` header block) and in the registers
   (`okf/testing/algorithm-validation.md`, `okf/testing/math-kernel-validation.md`),
   add a benchmark pair proving identical-and-faster, and — standing rule —
   "document the code, create py and ipynb example that illustrate the use
   based on sim data".
2. **Finish the modularization plan** (asks 1–3 of
   `~/.claude/plans/tttrlib-must-be-more-structured-kurzweil.md`; board tickets
   `T-20260818-01..07` in `okf/agent-board.md`).

## 2. State — thread 1 (validation)

Verdict vocabulary: PASS / PASS(bounded) / KNOWN-ANSWER / NO-REF, defined at
the top of `okf/testing/algorithm-validation.md`. Every row there is current.

References that now exist (all recorded as `.npz` fixtures under
`test/data/reference/` with a `gen_ab_*.py` generator next to the test, or run
live in a `benchmarks/.venvs/<name>` venv):

| kernel | upstream reference | test |
|---|---|---|
| `fit_vb` (HmmVB) | hmmlearn `VariationalCategoricalHMM` (dense dt=1 streams) | `test/python/hmm/test_ab_hmm_reference.py` |
| `HmmPosterior::ess`, split-R-hat | ArviZ `ess(method="mean")`, Vehtari 2021 | same |
| `max_tree_1d` | skimage `morphology.max_tree` (2 M-sample signal) | `test/python/burstfilter/test_ab_burst_reference.py` |
| PCH / ICS / STICS | pysimfcs, Kolin/Wiseman `stics.m` (Octave) | `test_ab_pch_reference.py`, `test_clsm_ics.py` |
| vectorial PSF | PyFocus (BrightEyes-ISM) | `test_ab_localization_superres_reference.py` |
| every reader | ptufile / phconvert / libttp / photonsfile / vendor STT1 / tifffile / pyarrow / h5py | `test_ab_core_reference.py`, `test_tiff.py`, `test_photonscore.py`, `test_flimlabs.py` |
| `build_kde` (BurstFeature) | FRETBursts `kde_laplace`/`kde_gaussian`, via TwoCDE | `test/python/bva/test_ab_bva_2cde_recurrence_reference.py` |
| neyman/gehrels objectives | NumPy/scipy | `test_ab_decay_reference.py::TestFit2xLeastSquaresObjectives` |
| MT19937 engine | NumPy | `test_math_ab_numerics.py` |

Bugs the A/Bs found and fixed (all committed, all in CHANGELOG):
PicoHarp T3 decoder/writer (channel 15 special), SPC-600 header ignored, PTU
from a bare TTTR lacked `Measurement_Mode`, BH `.set` TAC gain ignored
(6.1 ps read as 3.05 ps), `statistics::pearson` not squared, `neyman_lsq` /
`gehrels_lsq` advertised but Poisson ran, `ELBO` was not Beal's bound
(`elbo` vs `elbo_normalised` now both returned).

**Remaining unreferenced, deliberately** (each row says why): max-tree
*selection* stage, Bayesian-blocks two-stage search, `reassign_photons`,
SimEngine physics (analytic known answers), MaxEnt (ChiSurf's is not a valid
reference), CZ raw, Kristine TW correlator (a *feature gap*, not a validation
gap: tttrlib has no per-time-window-normalised estimator).

Reference-side defects worth knowing (documented in the tests, not ours to
fix): phconvert's BH reader is wrong on the QC channel packing and the femto
flag; phconvert has no SF-compressed HT3 support and silently reports an
18× compressed time axis; hmmlearn's VB priors default to 1/K, so set the
`_`-suffixed prior attributes explicitly when generating a fixture.

Benchmarks: new pairs `hmm_vb`, `max_tree` in the sciref set; reader pairs
SPC-630 / SPC-QC / `.sm` / PicoHarp T3 in the reading set. `PERF.md` table
updated for those. arviz, libttp, photonsfile installed into their venvs by
`benchmarks/build_envs.sh`.

Examples added: `examples/single_molecule/plot_hmm_variational_bayes.py/.ipynb`;
objective section in the decay-fit interface example; PicoHarp T3 in the
round-trip example.

## 3. State — thread 2 (modularization)

All three original asks are done. Board `T-20260818-*`:

| ticket | status | where |
|---|---|---|
| T-02 registries → objectives | ✅ | `DecayStatistics.h`, `DecayFitContext.objective` |
| T-05 last dispatch chains | ✅ | `Correlator::correlation_methods()`, `DecayFitPrior::kinds_table()`, `reassign_photons` refuses unknown/`sofi`; both are now **plugin capabilities** (`tttrlib_correlation_method_v1`, `tttrlib_decay_prior_v1`) |
| T-03 thin aggregates | ✅ | per-module OBJECT libraries; `libtttrlib.so` / `libtttrlib_static.a` link `$<TARGET_OBJECTS>` (`cmake/TTTRLibModule.cmake`, `CMakeLists.txt`) |
| T-07 optional modules | ✅ | `WITH_<NAME>` on all 35 modules, `-DTTTRLIB_WITHOUT_<NAME>` + `#ifndef` guards in all four `tttrlib.i` and `ext/python/split/mod_*.i`, presets `dev-sim` / `dev-clsm` / `dev-hmm` |
| T-06 friend cycle / burst TUs | ✅ | the friend pair was dead and is gone; `TTTR::burst_*` stay members *by decision* (see head of `modules/spectroscopy/burst/src/TTTRBurstSearch.cpp`) |
| Python split (`TTTRLIB_PYTHON_SPLIT`, six extensions) | ✅ opt-in | `ext/python/split/README.md` has every SWIG trap |
| **T-01** flip the split to default | 🆕 | needs a CI wheel first |
| **T-04** export macros | 🆕 | needs Windows CI; shape recorded on the ticket |

Verified today: full suite green on the final build (chunk A 1471 + chunk B
1688 passed, 0 failed); `tools/check_swig_multilang.sh` OK (7 declared parity
gaps, all deliberate Java ARGOUTVIEWM cases); JS addon and Java
(`TTTRLIB_MODULE_TYPE=STATIC`, as CI) both build on the new layout;
`HmmLattice.i` now in R/Java/JS and `hmm_forward_log` gives the Python value
bit-for-bit from Node; a Release+LTO `libtttrlib_static.a` links into a
consumer on macOS (ld64 reads bitcode archives; GCC gets
`-ffat-lto-objects`; any other LTO toolchain falls back to its own compile —
`TTTRLIB_STATIC_FROM_OBJECTS`, reported at configure).

Docs: `modules/README.md` (subset builds), `modules/plugin/README.md` (six
tables → consuming layer), `doc/plugins.rst` (user guide, in the user-guide
toctree), `okf/MODULE-DEBT.md` §2/§6/§6b/§7 closed with the reasoning kept.

## 4. Open items, in the order I would take them

1. **Push and watch CI.** 64 commits, four bindings, a build-system rewrite.
   The things that can only fail there: Windows (`WINDOWS_EXPORT_ALL_SYMBOLS`
   with the OBJECT-library layout — the `.def` generation now reads the
   objects' `/GL-`; MinGW LTO), the conda R recipe (`ninja tttrlibStatic`
   now produces the archive from module objects — `recipes/r/build.sh`
   discovers `modules/*/include`, unchanged), bioconda. Memory note
   `tttrlib-ci-dev-monitoring.md` has the crafted-commit technique if the
   worktree is dirty when fixing.
2. **T-01**: once a wheel ships green with `TTTRLIB_PYTHON_SPLIT=ON` in CI,
   flip the default (`ext/CMakeLists.txt`), keep the monolith as opt-out for
   one release.
3. **T-04**: `generate_export_header(tttrlib_<name>_objects BASE_NAME
   TTTRLIB_<UPPER>)` on the OBJECT libraries (one compile per source, so the
   `_EXPORTS` define lands once), a *per-module* macro (one shared macro is
   wrong — a dllexport class merely *used* from another DLL links with
   LNK2019), `extern template` for `read_tiff<T>`. Only verifiable on
   Windows CI.
4. ~~PRD-032~~ **done 2026-08-18 on the owner's ruling "only one registry"**
   (508c1135a, cd9b85b1e): `kFitRegistry` and `kOperationRegistry` deleted;
   fits/setups/objectives/priors/operations register next to their code into
   the `algorithm` module's `register_algorithm` table; the plugin host
   registers every plugin capability into the same table at load (rolled back
   on a failed init); `Registry.cpp` only primes and assembles; content
   unchanged (0 removed / 0 changed), `params_schema` order pinned by test;
   new categories `prior`, `correlation_method`. Left inside it: the
   `burst_selection` entry's column list is still the two-detector default
   (kept verbatim; a note sits next to the entry) -- changing that contract is
   consumer-visible (ChiSurf/ndx) and the owner's call.
5. Feature gap, if wanted: a per-time-window-normalised correlation estimator
   (Kristine TW), which would turn the one NO-REF correlator row into an A/B.

## 5. Things that bite (read before touching)

- **Shared checkout.** Another session (crush) has an uncommitted hunk in
  `okf/agent-board.md` (~line 880). Never `commit -a`; stage own board hunks
  by filtering `git diff okf/agent-board.md` into a patch and
  `git apply --cached` (the technique used all day). Never `git stash` in
  this tree.
- **No AI trailers** in commits (`no-claude-in-git` memory).
- **Editable install shadows everything.** `pip install -e .` puts a finder
  on `sys.meta_path` that beats `PYTHONPATH`; to test a slim preset build,
  strip it (`scratchpad/run_slim.py` did:
  `sys.meta_path = [m for m in sys.meta_path if 'editable' not in ...]`).
  Never `cp` a `.so` into site-packages (AMFI kills it); always reinstall.
- **Two test chunks**, not one background run (a 1 h task limit killed a
  full run twice): A = `clsm tttr streaming simulation hmm decayfit`
  (~17 min), B = everything else (~6 min). `--deselect
  test/python/hmm/test_surrogate.py::test_matches_chisurf_reference` (slow).
- **SWIG guards are `#ifndef TTTRLIB_WITHOUT_<NAME>`**, deliberately, so a
  bare swig run (`tools/check_binding_parity.py`, docs) sees the whole API.
- **Module dependency check lives in `tttrlib_finalize_modules`** because
  declaration order ≠ dependency order (decay is declared before registry);
  a `TARGET` check inside `tttrlib_add_module` fails spuriously.
- `%attributestring` hides `set_correlation_method`; in Python use
  `c.method = "..."`. `Correlator.correlation_method_names()` is static.
- `TTTRLIB_LTO` is only applied in `Release`; `build/`'s cache had an empty
  `CMAKE_BUILD_TYPE` — do not benchmark there. The `pip -e` build dir is
  `build/cp310-*/`, Release + LTO.
- Java/JS/R compile checks: `scratchpad/b_js`, `scratchpad/b_java` configure
  lines are in `okf/log.md` entry 47's addendum context (JS:
  `-DBUILD_JAVASCRIPT_INTERFACE=ON -DBUILD_LIBRARY=OFF`; Java as in
  `.github/workflows/ci.yml:819`).

## 6. Where the record is

- Board: `okf/agent-board.md` (T-20260818-01..07, T-20260811-04 closed on
  evidence today).
- Log: `okf/log.md` entries 30–47 (47 = today's modularization close-out).
- Registers: `okf/testing/algorithm-validation.md`,
  `okf/testing/math-kernel-validation.md`.
- Debt: `okf/MODULE-DEBT.md`.
- Memory: `~/.claude/projects/-Users-tpeulen-dev-tttrlib/memory/tttrlib-modularization.md`
  (+ Mnemosyne `be1a6cd3b511d5f6`).

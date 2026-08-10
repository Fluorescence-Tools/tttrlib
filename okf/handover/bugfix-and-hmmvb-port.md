# Handover — bug-fixing pass + HMM Variational Bayes port

**Date:** 2026-08-09 · **Branch:** `dev` · **Author:** Crush (assisted)

This documents a bug-fixing pass over the tttrlib test suite and a partial
port of the HMM Variational Bayes prototype to C++. Everything below is
verified to build; the VB port is built into `build/ext` but **not yet
exercised end-to-end** because of an environment shadowing issue (see §4).

---

## 1. What was fixed (all verified)

### Test infrastructure
- **`pyproject.toml`** — added `addopts = ["-p", "no:pytest-qt"]`. The
  pytest-qt plugin aborted collection with RC 4 when no Qt binding was
  importable, gating the entire suite.
- **`test/python/conftest.py`** — prepend `build/ext` to `sys.path` so the
  SWIG-built module wins over a stale site-packages install.
- **`test/python/test_pto.py`** (`_in_another_process`) and
  **`test/python/plugin/test_plugins.py`** (`run_in_subprocess`) — set
  `PYTHONPATH` to `build/ext` for subprocesses that don't inherit the
  conftest's `sys.path`.
- **`test/python/test_pto_prd25.py`** — `PTO_BIN` path moved from
  `build/tools/pto` to `build/modules/io/pto/pto` (module restructuring).
- **`test/python/misc/test_cli.py`** — rewritten for the native C++ `tttr`
  binary (the old Python/click `bin/tttrlib` was deleted). Prefers the build
  binary over a stale installed one.

### C++ bugs
- **`modules/spectroscopy/decay/src/DecayConvolution.cpp`** — unbounded
  `while(lamp[lamp_start++]==0)` could overrun the buffer if the whole IRF
  was zero; added `lamp_start < stop` bound (scalar + AVX).
- **`modules/spectroscopy/fcs/src/Correlator.cpp`** — four `malloc` calls
  were `memcpy`'d without null checks; added null check + cleanup.
- **`modules/imaging/clsm/src/CLSMImage.cpp`** — per-frame mean divided by
  `nframes_roi * pixel_in_roi` instead of `pixel_in_roi` (wrong average).
- **`modules/io/cz/src/io_cz.cpp`** — `hex_measure_id` `new[]` never freed
  (leak); `1./frequency_float` div-by-zero on corrupt file.
- **`modules/core/src/TTTR.cpp`** — `copy_from` leaked the old `header`
  pointer; added `delete header;` before reassignment.
- **`modules/cli/src/cmd_sm.cpp`** — `macro_ptr`/`micro_ptr`/`rout_ptr`
  malloc'd copies never freed; added `free()` after use.
- **`modules/spectroscopy/burst/src/RecurrenceAnalysis.cpp`** — early
  `return {counts, expected}` was invalid brace-init of a `vector<double>`;
  now returns a zero-filled flat `[counts|expected]` result.

### CLI bugs
- **`modules/cli/src/cmd_image.cpp`** — `tttr image --help` returned an
  error instead of help.
- **`modules/cli/src/cmd_pto.cpp`** — `tttr pto --help` returned an error
  instead of help.
- **`modules/cli/src/cli_main.cpp`** — `sim`/`simulate` subcommand missing
  from the usage text.

### Build / conformance
- **`modules/spectroscopy/decay/src/DecayFitNExp.cpp`** — `comp_ptrs` was
  `vector<const double*>` but pointed into mutable `comp_flat`; changed to
  `vector<double*>` (fixed a build break on the Java target).
- **`test/conformance/cases/registry.json`** — `registry.categories` now
  expects 8 categories (the `operation` category was added).
- **`ext/js/jsarrays.i`** — added `js_get` overloads for `long`/`unsigned
  long` (LP64 distinct types); fixed the JS build.

### sklearn
- Already fully ported per `okf/prds/PRD-010`: C++ `NeuralNet` in
  `modules/math/` replaces `MLPRegressor`/`StandardScaler`. sklearn is not a
  dependency. Made the last example import (`GaussianMixture`) optional in
  `examples/single_molecule/plot_burst_selection_advanced.py`.

### Test results
- **Python:** 2237 passed, 46 skipped, 0 failed (full `test/python/`).
- **JavaScript:** 41 passed, 0 failed.
- **R / Java:** build clean.

---

## 2. HMM Variational Bayes port (IN PROGRESS)

Ported `prototype/hmm/vb.py` + `prototype/hmm/special.py` to C++.

### New files
- **`modules/spectroscopy/hmm/include/HMMVB.h`** — `digamma(double)`,
  `dirichlet_kl(const double*, const double*, int)`, `struct HmmVB`
  (posterior concentrations, `elbo`, `loglik`, `n_iter`, `converged`,
  `history`, `mean()`, `std()`), and `fit_vb(const HMM&, const HmmModel&,
  const HmmRestraints*, int max_iter, double tol)`.
- **`modules/spectroscopy/hmm/src/HMMVB.cpp`** — implementation. Reuses
  `HMM::evaluate` (the existing E-step) verbatim with geometric-mean
  weights `exp(ψ(α) − ψ(α₀))` substituted for point estimates, exactly as
  the prototype does. ELBO = `log Z̃ − Σ KL(q‖p)`.
- **`modules/spectroscopy/hmm/CMakeLists.txt`** — added `HMMVB.cpp` to
  SOURCES.
- **`ext/python/HMM.i`** — added `#include "HMMVB.h"`, `%include
  "HMMVB.h"`, and `TTTRLIB_NOGIL(tttrlib::fit_vb)`.

### Status
- **Builds clean** into `build/ext` (176 VB symbol matches in the generated
  wrapper). `digamma`, `fit_vb`, `HmmVB` are all in the wrapper.
- **NOT yet exercised end-to-end** — see §4.

### Design notes (from the prototype docstring, preserved in HMMVB.h)
- The VB E-step is the existing forward-backward with `Ã = exp(E_q[log A])`
  substituted for `A`. This is exact, not an approximation: the latent
  variable is the full tick-level path, so `E_q[log p(z|A)] = Σ n_ij
  E_q[log A_ij]` with `n_ij` the one-tick counts, and marginalising
  intermediate ticks of a chain weighted by `Ã` gives exactly `Ã^Δt`.
- Fixed entries are deliberately unsupported (a pinned parameter is a point
  mass, not a Dirichlet).

---

## 3. What still needs doing

1. **Exercise the VB port end-to-end.** Write a Python test
   (`test/python/hmm/test_vb.py`) that:
   - builds a small HMM, runs `fit_vb`, checks `elbo` is finite and
     non-decreasing, `mean()` rows sum to 1, `std()` is non-negative;
   - cross-checks `digamma`/`dirichlet_kl` against the prototype
     (`prototype/hmm/special.py`) and/or `scipy.special.digamma`.
2. **Resolve the module shadowing** (see §4) so the test can import the
   build module.
3. **Update `modules/spectroscopy/hmm/README.md`** to mention VB (per
   AGENTS.md, every module folder must keep its README aligned).
4. **Update `okf/prds/PRD-010`** status if VB is considered part of the
   surrogate-model work (it is the intended default Bayesian engine).

---

## 4. Environment blocker: installed tttrlib shadows build/ext

`/Users/tpeulen/mambaforge/lib/python3.10/site-packages/tttrlib/` now has a
real `__init__.py` + `_tttrlib.cpython-310-darwin.so` (a proper installed
package, not the earlier namespace stub). It **shadows** `build/ext` even
when `build/ext` is first on `sys.path`:

```
PYTHONPATH=build/ext python -c "import tttrlib; print(tttrlib.__file__)"
# -> /Users/tpeulen/mambaforge/lib/python3.10/site-packages/tttrlib/__init__.py
```

The conftest `sys.path.insert(0, build/ext)` fix works for pytest runs that
start fresh, but a direct `import tttrlib` still resolves to the installed
package. To test the VB port reliably:

- **Option A (recommended):** `pip install -e .` (or reinstall the wheel)
  so the installed package points at the build. This is the normal dev loop.
- **Option B:** temporarily move/rename the installed
  `site-packages/tttrlib/` dir, or uninstall it, so `build/ext` is the only
  source.
- **Option C:** in the test, load the build module explicitly via
  `importlib.util.spec_from_file_location` before importing `tttrlib`.

The earlier "namespace package" state (no `__init__.py`) is what the conftest
fix was written for; the environment has since changed to a real package, so
the conftest fix alone is no longer sufficient for direct imports.

---

## 5. Notes / gotchas

- The repo is mid-restructuring: `modules/decay/`, `modules/hmm/`,
  `modules/imaging/` files are being moved to
  `modules/spectroscopy/{decay,hmm}/` and `modules/imaging/{clsm,superres,
  localization}/`. Many `git status` deletions are the *old* locations, not
  lost work. Do not `git add -A` blindly.
- The `build/` Makefile disappears intermittently (parallel builds /
  reconfigure races). If `cmake --build build` says "No rule to make target
  'Makefile'", re-run `cmake -S . -B build` first.
- `test/python/test_pto.py` and `test/python/plugin/test_plugins.py` spawn
  subprocesses; they need `PYTHONPATH=build/ext` (already added).
- The `tttr` CLI binary is at `build/bin/tttr`; the standalone `pto` tool is
  at `build/modules/io/pto/pto`.

# Module debt

Compromises the module split accepts, why, and what would let each one go. A
compromise recorded here is a decision; one that is not is a surprise for whoever
hits it next.

## 1. ~~`legacy`~~ -- done

There is no residual module. `src/` and `include/` are empty and every source
belongs to exactly one of sixteen subsystems, checked at configure time.

The extraction order was **not** the one planned, and the reason is worth
keeping. Taking `burst` first -- as the plan said -- is a link cycle, which CMake
rejects outright between shared libraries:

- `burst` -> `legacy`, because `BurstFilter.h` includes `TTTR.h`, `TTTRMask.h`
  and `Channel.h`;
- `legacy` -> `burst`, because `HMM.cpp` includes `BurstFilter.h`, and
  `BurstConfidence.h`, `BurstSearchBayesianBlocks.h` and `BurstSearchMaxTree.h`
  all include `BurstSignificance.h`.

`nn` has the same shape. The general rule: **nothing above `core` can come out
while `core` is still inside the residual module**, because the residual sits on
both ends of every edge. Order actually used: `io_image`, `opt`, `hist`, `sim`,
`pda`, `superres`, `localization`, `util`, `core`, `imaging`, `registry` (since 2026-08-18 folded into `core`),
`decay`, then `burst`, `nn`, `hmm`, `graph`.

Two edges only the linker or the compiler found, not a reading of the headers:
`Registry.cpp` calls `fit_models_json()` and friends, so the fit description
tables belong with `registry` and not `decay`; and `LayerNode.cpp` reaches into
the CLSM hierarchy that `LayerNode.h` gives no hint of.

## 1b. ~~One SWIG module for Python~~ -- split available (`TTTRLIB_PYTHON_SPLIT`, 2026-08-17)

`ext/python/split/mod_{core,formats,kernels,spectroscopy,imaging,sim}.i` build six extensions
that %import each other over one shared type table; `__init__.py.in` re-exports
flat, so `tttrlib.TTTR` is unchanged. Measured on this machine (LTO on): a
change to `Sim.i` rebuilds `sim` alone in ~45 s where the monolith took the
whole 3 min; a change to a core fragment still rebuilds all four in parallel,
bounded by `core` (110k of 334k wrapper lines after `formats` and `kernels` were split
off it the same day; T-20260818-02 done). Default OFF until CI
has run the wheel with it; the parity guard keeps both fragment lists identical.
Design and the traps that cost time: `ext/python/split/README.md`.

## 2. ~~`tttrlibShared` and `tttrlibStatic` still compile every source themselves~~ -- thin aggregates since 2026-08-18

**Closed 2026-08-18.** Every module compiles its sources exactly once into an
OBJECT library (`tttrlib_<name>_objects`); the module `.so`/`.a` and both
whole-library targets are links over `$<TARGET_OBJECTS:...>`. Names unchanged.
One wrinkle: `libtttrlib_static.a` must hold real code for an `ar`/linker
without the LTO plugin. GCC compiles the objects with `-ffat-lto-objects`
(bitcode + code in one object, one compile); Apple clang cannot but ld64 reads
bitcode archives natively (verified: a consumer links a Release+LTO archive);
any other toolchain with LTO on falls back to the archive's own `-fno-lto`
compile (`TTTRLIB_STATIC_FROM_OBJECTS`, reported at configure). A module
switched off with `WITH_<NAME>=OFF` is now absent from the aggregates too, as it
is from the bindings -- before, they compiled it anyway. Original text kept
below for the reasoning.

The three SWIG targets now link the modules instead of recompiling the glob, so
the sources are compiled three times per configure rather than five. The two
library targets still have their own compile because they are what a C++
consumer installs (`libtttrlib.so`, with its soname) and what the conda R package
links (`libtttrlib_static.a`), and changing their artefact names would break both.

They build from `TTTRLIB_CLAIMED_SOURCES` -- the same list
`tttrlib_finalize_modules()` validates -- and **not** from a glob of `src/`. That
is not a stylistic choice. Extracting `io_image` moved `TiffArrayIO.cpp` out of
`src/`, and while the glob still configured, built, linked and passed every test,
`libtttrlib_static.a` had quietly lost the TIFF symbols: nothing in the test
suite links that archive, and the R package that does link it was only being
*compiled*, not run. An extraction must not be able to silently subtract from an
artefact nobody exercises.

**Exit:** make them thin aggregates over the module objects once the modules
exist, keeping the installed names.

## 3. ~~`legacy` still carries every third-party dependency~~ -- Eigen and autodiff are gone

Eigen was the case that made this debt visible: a project-wide `REQUIRED` for
the sake of two files. It is now removed entirely, and the exit was not the one
planned. The plan was to extract `nn` so that Eigen would be "needed by exactly
two modules" instead of by everything. What actually happened is that both
consumers stopped needing it:

- `NeuralNet`'s batched GEMMs moved to `Mat.h` (`modules/math`), which has its
  own SIMD kernel and its own benchmark against Eigen;
- `ImageLocalization`'s AD gradient carried its derivatives in
  `Eigen::Array<double, N, 1>`, and now carries them in `GradVec<N>`
  (`modules/math`), measured against Eigen in `benchmarks/bench_gradvec.cpp`.

So `FIND_PACKAGE(Eigen3 REQUIRED)` is gone from the top-level `CMakeLists.txt`,
`tttrlib::eigen` is gone from `cmake/TTTRLibThirdParty.cmake`, and the four CI
platforms, the vcpkg port and the two wheel-builder images no longer install it.

`autodiff` then went the same way, and it was the cheaper case to argue: it was
*vendored*, so nothing had to be installed anywhere — but 20 headers and ~10k
lines of it sat in the tree so that one file could use one class template, and
the class only did what that file needed through an undocumented trait hook.
`Dual.h` in `modules/math` (~200 lines) replaced it, `tttrlib::autodiff` is gone
from `cmake/TTTRLibThirdParty.cmake`, and `localization` now declares no
external dependency at all. The conversion was measured against autodiff before
autodiff was deleted; see the 24th log entry.

Worth keeping in view: `superres` and `clsm` were *declaring*
`EXTERNAL_DEPS tttrlib::eigen` (and `superres` also `tttrlib::autodiff`) while
including neither, and `localization` was declaring Eigen while actually using
autodiff. Both target names have since been deleted, which retires those
particular wrong declarations without fixing the mechanism that hid them. Nothing caught it, because the top-level `INCLUDE_DIRECTORIES` for
`thirdparty/` puts the vendored headers on every module's include path anyway.
A declared dependency is documentation until a module compiles with only what
it asked for.

**Remaining exit:** give modules their include paths from `EXTERNAL_DEPS` alone,
so a wrong declaration fails the build instead of being absorbed by the
directory scope.

## 4. Module libraries carry no soname

Shipped inside the Python package next to the extension rather than installed as
system libraries, so a versioned soname would only mean two extra symlinks that a
wheel stores as full copies. The versioned library for system consumers is the
separate `tttrlibShared` target.

**Exit:** add `VERSION`/`SOVERSION` if and when modules are installed to a
system library directory as a supported thing to link against.

## 5. Windows exports everything via `WINDOWS_EXPORT_ALL_SYMBOLS`

There is no `__declspec` anywhere in `include/`, and annotating properly is ~97
sites (43 classes with out-of-line members, ~54 free functions). Until then CMake
generates the `.def` file.

**Exit:** `TTTRLIB_<MOD>_EXPORT` macros at *class* granularity -- needed anyway
for typeinfo and vtables across `.so` boundaries, for the accepted `friend`
relationships, and for the `read_tiff<T>`/`write_tiff<T>` instantiations (which
also need `extern template`). Annotate `io_image` and `pda` first, `core` last.

## 6. Known cross-module reach-ins, to resolve as the modules land

- ~~`CLSMImage` <-> `Correlator` <-> `DecayPhasor` are mutual friends~~ --
  gone 2026-08-18. `fcs` and `clsm` had long been separate libraries (clsm
  depends on fcs, fcs only forward-declared CLSMImage); the two remaining
  `friend` lines were dead -- CLSMImage reaches only public Correlator members
  (`curve`, `get_corr*`, `set_tttr`) -- and are removed. No accessor was needed.
- `TTTR` publishes the whole burst-search API as its own methods; the ten
  translation units that define those `TTTR::` members now live in `burst`
  (core's `libtttrlib_core.so` does not reference them), and the bindings
  `%ignore` them under `TTTRLIB_WITHOUT_BURST` (2026-08-18). **Exit:** free functions taking `const TTTR&`, with the methods kept as
  forwarders -- and SWIG `%extend` re-attaches them to the Python proxy, so the
  API does not change.
- `HMMEmission.h -> SimDecay.h`, `HMMRestraints.h -> DecayFitPrior.h`,
  `NeuralNet.cpp -> SimPcgRandom.h` make `hmm` depend on nearly everything.

## 6b. ~~Optional modules~~ -- `WITH_<NAME>` for every module (2026-08-18)

Every module has a `WITH_<NAME>` switch; OFF drops it from the objects, the
aggregates and all four bindings (`-DTTTRLIB_WITHOUT_<NAME>` on the swig line,
`#ifndef` guards around each fragment). `dev-sim`, `dev-clsm`, `dev-hmm`
presets build and import. See `modules/README.md`.

## 7. ~~R and Java `%include` lists lag Python by 12 fragments~~ -- enforced by `tools/check_binding_parity.py`

**Status 2026-08-18.** The four lists are compared on every
`tools/check_swig_multilang.sh` run; a gap must be declared with a reason in
`tools/binding_parity_exceptions.txt`, and a stale declaration fails the check.
R and JS are at full parity (`DecayFitMLEWrapper.i` is commented out
everywhere); Java's seven remaining gaps are all deliberate ARGOUTVIEWM cases
with `_into` helpers in `ext/java/helpers.i` (documentation.i is a measured
no-op). `HmmLattice.i` joined R/Java/JS today. The generated-list exit below
was not needed. Original text kept.

`ext/python/tttrlib.i` lists 30 fragments; `ext/r/tttrlib.i` and
`ext/java/tttrlib.i` list 18 each while claiming in a comment to be identical, so
R and Java silently lack Registry, HMM, Sim, BVA, TwoCDE and NeuralNet.

**Exit:** generate one `%include` list from each module's `SWIG_INTERFACES`, so
an exclusion has to be declared rather than merely happening.

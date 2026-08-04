# Module debt

Compromises the module split accepts, why, and what would let each one go. A
compromise recorded here is a decision; one that is not is a surprise for whoever
hits it next.

## 1. `legacy` still holds everything

`modules/CMakeLists.txt` declares one module containing all of `src/`. That is
the starting point, not the goal: switching the build to shared module libraries
and splitting the sources are independent risks, and doing both at once makes
every failure ambiguous.

**Exit:** extract leaves first (`opt`, `hist`, `imageio`, `sim`, `pda`,
`superres`, `localization`), then `burst`, `core`, `decay`, `imaging`, `nn`,
`hmm`. `tttrlib_finalize_modules()` refuses to configure if a source ends up
claimed twice or not at all, so each extraction is a small, checkable change.

## 2. `tttrlibShared` and `tttrlibStatic` still compile `src/` themselves

The three SWIG targets now link the module instead of recompiling the glob, so
the sources are compiled three times per configure rather than five. The two
library targets still have their own compile because they are what a C++
consumer installs (`libtttrlib.so`, with its soname) and what the conda R package
links (`libtttrlib_static.a`), and changing their artefact names would break both.

**Exit:** make them thin aggregates over the module objects once the modules
exist, keeping the installed names.

## 3. Modules carry every third-party dependency

`legacy` declares all of `tttrlib::{json,pocketfft,autodiff,eigen,highfive}`
because it contains every source. Eigen is a project-wide `REQUIRED` today for
the sake of two files.

**Exit:** the declaration becomes per-module. Eigen stops being mandatory when
`localization` and `nn` are the only modules that ask for it.

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
also need `extern template`). Annotate `imageio` and `pda` first, `core` last.

## 6. Known cross-module reach-ins, to resolve as the modules land

- `CLSMImage` <-> `Correlator` <-> `DecayPhasor` are mutual friends, so they must
  be co-located in one `imaging` module: CMake will not accept a link cycle
  between shared libraries. **Exit:** replace the three `friend` declarations
  with a narrow accessor.
- `TTTR` publishes the whole burst-search API as its own methods, and five
  translation units define `TTTR::` members outside `TTTR.cpp`, so those stay in
  `core`. **Exit:** free functions taking `const TTTR&`, with the methods kept as
  forwarders -- and SWIG `%extend` re-attaches them to the Python proxy, so the
  API does not change.
- `HMMEmission.h -> SimDecay.h`, `HMMRestraints.h -> DecayFitPrior.h`,
  `NeuralNet.cpp -> SimPcgRandom.h` make `hmm` depend on nearly everything.
- `SimSimd.h` (508 lines) is dead in-tree -- nothing includes it.

## 7. R and Java `%include` lists lag Python by 12 fragments

`ext/python/tttrlib.i` lists 30 fragments; `ext/r/tttrlib.i` and
`ext/java/tttrlib.i` list 18 each while claiming in a comment to be identical, so
R and Java silently lack Registry, HMM, Sim, BVA, TwoCDE and NeuralNet.

**Exit:** generate one `%include` list from each module's `SWIG_INTERFACES`, so
an exclusion has to be declared rather than merely happening.

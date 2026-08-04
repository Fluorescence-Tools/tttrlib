# Module debt

Compromises the module split accepts, why, and what would let each one go. A
compromise recorded here is a decision; one that is not is a surprise for whoever
hits it next.

## 1. `legacy` still holds the core cluster

Out so far: `util`, `opt`, `hist`, `imageio`, `sim`, `pda`, `superres`,
`localization`. `legacy` is down from 68 sources to 53 and now holds the part
that is genuinely tangled -- core, burst, decay, imaging, nn and hmm.

Two of the extracted modules are leaves of the *include* graph without being
independent of core, and say so in `DEPENDS`: nothing includes `Pda.h` or
`CLSMSuperRes.h`, but both implementations read the photon-stream data model.
`DEPENDS legacy` becomes `DEPENDS core` and `DEPENDS core imaging` once those
exist. `sim`'s edge runs the other way from what its name suggests: the
simulator reaches into nothing but `Random.h`, and it is `hmm` and `nn` --
still inside `legacy` -- that include `SimDecay.h` and `SimPcgRandom.h`, so
`legacy` declares `DEPENDS sim`.

**Exit:** `core`, then `imaging`, `decay`, `burst`, `nn`, `hmm`.
`tttrlib_finalize_modules()` refuses to configure if a source ends up claimed
twice or not at all, so each extraction is a small, checkable change.

**The order matters, and it is not the one originally planned.** Taking `burst`
first produces a link cycle, which CMake rejects outright between shared
libraries:

- `burst` -> `legacy`, because `BurstFilter.h` includes `TTTR.h`, `TTTRMask.h`
  and `Channel.h`;
- `legacy` -> `burst`, because `HMM.cpp` includes `BurstFilter.h`, and
  `BurstConfidence.h`, `BurstSearchBayesianBlocks.h` and `BurstSearchMaxTree.h`
  all include `BurstSignificance.h`.

The same applies to `nn`: `HMMSurrogate.h` includes `NeuralNet.h` while
`NeuralNet.cpp` reaches into imaging and sim. Nothing above `core` can come out
while `core` is still inside `legacy` -- the residual module is on both ends of
every edge. `core` first, and `BurstSignificance.h` goes with it, since core's
own burst-search headers include it.

## 2. `tttrlibShared` and `tttrlibStatic` still compile every source themselves

The three SWIG targets now link the modules instead of recompiling the glob, so
the sources are compiled three times per configure rather than five. The two
library targets still have their own compile because they are what a C++
consumer installs (`libtttrlib.so`, with its soname) and what the conda R package
links (`libtttrlib_static.a`), and changing their artefact names would break both.

They build from `TTTRLIB_CLAIMED_SOURCES` -- the same list
`tttrlib_finalize_modules()` validates -- and **not** from a glob of `src/`. That
is not a stylistic choice. Extracting `imageio` moved `TiffArrayIO.cpp` out of
`src/`, and while the glob still configured, built, linked and passed every test,
`libtttrlib_static.a` had quietly lost the TIFF symbols: nothing in the test
suite links that archive, and the R package that does link it was only being
*compiled*, not run. An extraction must not be able to silently subtract from an
artefact nobody exercises.

**Exit:** make them thin aggregates over the module objects once the modules
exist, keeping the installed names.

## 3. `legacy` still carries every third-party dependency

The extracted modules declare only what they use -- `localization` asks for
Eigen and autodiff, `pda` and `superres` for pocketfft, `sim` for HighFive --
but `legacy` still declares all of them, because it still contains a consumer of
each.

Eigen in particular is a project-wide `REQUIRED` for the sake of two files, and
`localization` is only one of them.

**Exit:** extracting `nn`, the other Eigen consumer. At that point Eigen is
needed by exactly two modules and can stop being mandatory for the whole
project.

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

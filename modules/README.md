# `modules/` — tttrlib Modular Subsystems

`tttrlib` is organized into modular subsystems under `modules/`. Each subsystem has its own headers in `include/`, source files in `src/`, and a `CMakeLists.txt` declaring its target and dependencies using `tttrlib_add_module()`.

## Module Layering & Hierarchy

```
cli
 │
 ├── imaging (clsm, superres, localization)
 └── spectroscopy (fcs, burst, decay, hmm, pda)
      │
      ├── core (TTTR, TTTRHeader, DataStore, Histogram, the registry)
      │    │
      │    ├── io (base, format modules: pq, bh, cz, sm, ps, be, fl, hdf5, image, csv, store, pto, table)
      │    │
      │    └── simulation (SimEngine, SimGrid, SimSystem, SimVectorGrid)
      │
      └── util (Verbose, ProgressMonitor, ProgressTicker, NeuralNet, L-BFGS, RNGs, SIMD)
```

## Subdirectories

- **`util/`**: Low-level utilities (SIMD, RNGs, logging, neural nets, optimization).
- **`io/`**: File input/output readers and writers for TTTR file formats and table representations.
- **`plugin/`**: Native plugin host interface and dynamic library loader.
- **`simulation/`**: Diffusion and photon emission simulator.
- **`core/`**: Core TTTR photon stream data structures, histograms, selection masks, DataStore containers -- and **the registry** (`Registry.h`): the one table every algorithm, fit model, objective, prior, correlation method and pipeline operation registers itself in, next to its code, when its library loads; `registry()` is assembled from it.
- **`spectroscopy/`**: Spectroscopy analysis algorithms (`fcs`, `burst`, `decay`, `hmm`, `pda`).
- **`imaging/`**: Imaging and microscopy modules (`clsm`, `superres`, `localization`).
- **`cli/`**: Standalone command-line interface executable (`tttr`).

## Rules for Modules

1. **Every module folder must contain a `README.md`** describing its purpose, components, and dependencies.
2. **Strict Layering**: Modules can only depend on declared `DEPENDS` to prevent cyclic dependencies.
3. **Source Claiming**: `tttrlib_finalize_modules()` verifies that every source file under `modules/` is claimed by exactly one module.
4. **One compile per source**: a module's sources are compiled once into
   `tttrlib_<name>_objects` (an OBJECT library); the module library and the two
   whole-library aggregates (`libtttrlib.so`, `libtttrlib_static.a`) link those
   objects.

## Building a subset (`WITH_<NAME>` switches)

Every module has a `WITH_<NAME>` cache option, default ON. Switching one off
removes it from the build, from `libtttrlib.so` / `libtttrlib_static.a` and
from all four bindings: the SWIG fragments of an OFF module are guarded with
`#ifndef TTTRLIB_WITHOUT_<NAME>` and CMake passes that define for every OFF
module (`tttrlib_module_swig_flags()`). A module whose declared `DEPENDS` is off
fails the configure with a message naming both switches.

Ready-made subsets are presets: `cmake --preset dev-sim` (core + simulation),
`dev-clsm` (core + fcs + clsm + TIFF), `dev-hmm` (the photon-HMM stack:
math, algorithm, burst, registry, decay, simulation, hmm). Each builds ~15
modules instead of 35 and a Python extension that imports; tests that touch an
OFF module fail with `AttributeError`, which is the honest answer.

Known seam: the burst searches are `TTTR::` members declared in `core` and
defined in `burst`; with `WITH_BURST=OFF` the bindings `%ignore` them
(`ext/python/TTTR.i`) and the C++ symbols are absent (see board ticket
T-20260818-06 for the free-function exit).

## The registry

There is one registry and it is in `core` (`Registry.h`): `register_algorithm`
/ `register_algorithm_json` put a described capability into it, `registry_json()`
assembles every category from what registered (plus the file-format, table-format
and plugin-status catalogs). Every module declares its own entries **next to
its code, from a static initialiser** -- burst searches and burst pipeline
operations in `spectroscopy/burst`, fits / setups / objectives / priors / MLE
operations in `spectroscopy/decay`, correlation methods and `burst_fcs` in
`spectroscopy/fcs`, `photon_hmm` in `spectroscopy/hmm`, PDA in
`spectroscopy/pda`. A plugin's capabilities are recorded by the plugin host
(`plugin`, beneath core) and pulled into the same table. No registry literal
exists anywhere.

Because registration happens at load, a consumer that links a **static**
build must link it whole: in-tree static-module builds (`TTTRLIB_MODULE_TYPE=STATIC`,
the JNI native) link the module objects (`tttrlib_link_all_modules`), and the R
package links `libtttrlib_static.a` with `--whole-archive` / `-force_load`
(`recipes/r/build.sh`). Without that an archive member nothing references is
dropped, and its registry entries with it.

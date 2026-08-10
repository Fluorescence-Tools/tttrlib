# `modules/` — tttrlib Modular Subsystems

`tttrlib` is organized into modular subsystems under `modules/`. Each subsystem has its own headers in `include/`, source files in `src/`, and a `CMakeLists.txt` declaring its target and dependencies using `tttrlib_add_module()`.

## Module Layering & Hierarchy

```
cli
 │
 ├── registry
 ├── imaging (clsm, superres, localization)
 └── spectroscopy (fcs, burst, decay, hmm, pda)
      │
      ├── core (TTTR, TTTRHeader, DataStore, Histogram)
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
- **`core/`**: Core TTTR photon stream data structures, histograms, selection masks, and DataStore containers.
- **`spectroscopy/`**: Spectroscopy analysis algorithms (`fcs`, `burst`, `decay`, `hmm`, `pda`).
- **`imaging/`**: Imaging and microscopy modules (`clsm`, `superres`, `localization`).
- **`registry/`**: Subsystem capabilities registry.
- **`cli/`**: Standalone command-line interface executable (`tttr`).

## Rules for Modules

1. **Every module folder must contain a `README.md`** describing its purpose, components, and dependencies.
2. **Strict Layering**: Modules can only depend on declared `DEPENDS` to prevent cyclic dependencies.
3. **Source Claiming**: `tttrlib_finalize_modules()` verifies that every source file under `modules/` is claimed by exactly one module.

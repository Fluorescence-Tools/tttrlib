# Split Python extensions (`TTTRLIB_PYTHON_SPLIT`)

The Python bindings can be built as **six extensions** instead of one 290k-line
wrapper: `core`, `formats`, `kernels`, `spectroscopy`, `imaging`, `sim` (`mod_<name>.i`
here; `core` holds TTTR/headers/selections/histograms/DataStore, `formats` the file
formats, `kernels` the NumPy-only kernels (not `io` / `math`: the proxies' own
`import io` / `import math` would be re-exported over the submodules)). Each
compiles in parallel; touching one fragment regenerates one module. The Python
API stays flat -- `__init__.py.in` re-exports every public name of the four,
and `tttrlib.TTTR` is `tttrlib.core.TTTR`. The six `%module`s share one SWIG
runtime type table (`SWIG_TYPE_TABLE=tttrlib`), which is what lets `sim` accept
a `core.TTTR`. `ext/python/tttrlib.i` (the monolith) stays the reference list;
`tools/check_binding_parity.py` refuses a fragment that is in one and not the
other, or in two split modules.

Import graph: `core` ← `formats`, `kernels`; `spectroscopy` ← core, kernels;
`imaging` ← core, kernels, spectroscopy; `sim` ← core. `__init__.py` imports them in that
order.

## How a non-core module is built

    %module(...) spectroscopy
    #define TTTRLIB_TEMPLATES_IMPORTED
    %include "split/common.i"      // preamble: features, GIL macro, docstrings
    %include "misc_types.i"        // SWIG library + numpy.i + typemaps: INCLUDED,
                                   // so their runtime fragments land in THIS wrapper;
                                   // the %templates inside become nameless
                                   // (`%template() std::vector<double>`) -- traits
                                   // and typemaps without a second proxy class
    %{ #include "TTTR.h" ... %}    // headers of imported %shared_ptr classes: the
                                   // type table's up-casts are emitted here too
    %import "split/mod_core.i"     // core's TYPES, no wrappers
    %pythoncode %{ _tttrlib = _spectroscopy; from tttrlib.core import * %}
    %include "BurstFilter.i" ...   // the fragments this module wraps

## Traps met while making it work (do not re-introduce)

* **A split file must not share a fragment's name.** `%include "Sim.i"` from a
  file called `sim.i` resolves relative to the including file first, and macOS
  is case-insensitive: it included itself and produced an empty module. Hence
  `mod_*.i`.
* **`%import` brings declarations, not fragments.** A module that only imported
  `misc_types.i` compiled without `SWIGPY_SLICEOBJECT`, `PyArray_*` and the
  `swig::traits<T>` for its `std::vector` returns. Include the library pieces
  in every module; import only the project types.
* **`%import` skips `%{ #include %}` but not the type table.** The up-cast
  functions for imported `%shared_ptr` classes (`DecayFitPrior` & co.) are
  emitted into every module that knows the type; without the header the
  wrapper does not compile.
* **Helpers that name the C module.** `TTTR.py`, `CLSMImage.py`, `Correlator.py`,
  `TTTRHeader.py` call `_tttrlib.new_TTTR(...)`; each split module aliases its
  own C module to `_tttrlib`, so a helper must live in the module that wraps
  the class it drives.
* **`import *` is not the re-export.** `ImageLocalizer.py` sets `__all__` for
  its own two names and, pasted into `imaging.py`, would have hidden every
  other imaging class from `from .imaging import *`. `__init__.py` copies the
  public names explicitly.
* **Dependency tracking.** With CMake >= 3.20 the wrapper depends on what swig
  reports (`-MD`); the old glob of every `.i` as an extra dependency of every
  module would rebuild all four on any change.
* **Global `%exception` directives are positional and do not survive `%import`.**
  In the monolith a fragment inherits whichever `%exception { ... }` the
  previous fragment left active (MicrotimeLinearization.i's maps
  `std::invalid_argument` to ValueError; TTTRMask.i's and HMM.i's catch only
  `std::exception`). A split module must restate, before each fragment, the
  handler that fragment inherited in `ext/python/tttrlib.i`'s order -- otherwise
  a bad argument surfaces as RuntimeError. `mod_*.i` carry those blocks with a
  note; recompute them if the monolith order changes.
* **A template one module relies on must be instantiated where the type is
  used.** `HmmStateSidecar.states` is `std::vector<uint8_t>`; the monolith got
  its `VectorUint8` proxy from Sim.i, which the split's `spectroscopy` never
  sees, so `states_np` came back as a 0-d array of an opaque pointer. The
  byte-vector templates now live in misc_types.i (named in core, nameless
  elsewhere) instead of Sim.i / Pto.i.
* **Submodule names must not collide with stdlib modules the proxies import.**
  `tttrlib.io` / `tttrlib.math` were clobbered by the proxies' own `import io`
  / `import math` during the flat re-export -- hence `formats` and `kernels`.

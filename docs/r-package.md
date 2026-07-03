# tttrlib for R (`r-tttrlib`)

R bindings for the tttrlib C++ engine, generated with SWIG from the same
language-neutral interface as the Python and Java bindings. You get native R
vectors in and out, and the full TTTR/correlation/decay/imaging API.

> **Maturity note.** The R (and Java) bindings are **newer and less battle-tested
> than the Python package.** The C++ core is shared and heavily tested through
> Python, but the R-specific marshalling has only light coverage so far. Please
> [report issues](https://github.com/fluorescence-tools/tttrlib/issues) — see
> [Testing status](#testing-status) below.

## Installation

### Conda / Mamba (recommended)

The `r-tttrlib` package is built by `recipes/r/` (rattler-build) and published to
the project conda channel:

```bash
mamba install -c conda-forge -c tpeulen r-tttrlib
```

Linux and macOS are supported; Windows is not built for R yet.

### From source

Requires R (**4.4.x** — SWIG's R runtime is not yet compatible with R ≥ 4.5),
a C++17 compiler, CMake, SWIG ≥ 4.1, and HDF5.

```bash
git clone --recursive https://github.com/fluorescence-tools/tttrlib.git
cd tttrlib

# 1. Generate the SWIG R wrapper + build the static C++ core
cmake -S . -B build-r -DBUILD_PYTHON_INTERFACE=OFF -DBUILD_R_INTERFACE=ON -DBUILD_LIBRARY=ON
cmake --build build-r --target tttrlibR tttrlibStatic

# 2. Install the R package (the recipe script recipes/r/build.sh automates this,
#    including staging the generated wrapper and setting the include/link flags)
R CMD INSTALL ext/r/pkg
```

## Usage

SWIG-R exposes C++ classes as **S4 objects**; methods are **generics called
function-style** (`Class_method(obj, ...)`), not `obj$method()`.

```r
library(tttrlib)

# Read a photon stream
tttr <- TTTR("photon_stream.ptu")
n    <- TTTR_size(tttr)

# Native R vectors
macro   <- TTTR_get_macro_times(tttr)      # numeric()
micro   <- TTTR_get_micro_times(tttr)
routing <- TTTR_get_routing_channels(tttr)

# Correlation
corr <- Correlator(tttr = tttr)
# ... configure channels, then read Correlator_get_x_axis(corr) / _get_corr(corr)
```

Notes and known limitations:

- **64-bit macro times** are carried through R's `double`, so exact integer
  values above 2^53 lose precision (a limitation of R's numeric type). Java does
  not have this issue.
- **Directors** (subclassing `PdaCallback` in R) are **not** available; only the
  built-in callbacks are exposed.
- The object model is S4 external-pointers; use `TTTR_*`-style generics.

## Testing status

The bindings share the C++ core with Python, but the language-specific
marshalling needs more coverage. Current cross-language tests assert **identical
reference values across Python, R and Java** for a known file
(`test/python/tttr/test_cross_language_reference.py`,
`test/r/test_tttr.R`, `test/java/*.java`) — e.g. photon count, micro-/macro-time
sums, and CLSM image sums.

**Goal:** every check done in the Python test suite should also run in R and Java
(not necessarily the full suite, but the same core operations) to surface
binding-specific issues early. The concrete plan — coverage matrix, priority
order, per-language how-to, and milestones — is in
**[PRD-001](../PRDs/PRD-001-cross-language-test-parity.md)**. Contributions
that port a Python test to R/Java are very welcome.

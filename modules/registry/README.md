# `registry` — Global Algorithm and Model Registry

Assembles `registry()` from the one algorithm registry plus the format and plugin catalogs; holds no table of its own.

## Contents

- **`Registry.h` / `Registry.cpp`**: `registry_json()` / `registry_category_json()` and the category views (`fit_models_json`, ...); primes every module's registrations.

## Dependencies

- Depends on `io`, `plugin`, `util`, `core`, `burst`, `decay`, `fcs`, `algorithm` -- everything that declares registry entries.

## One registry

There is exactly one registry: the `register_algorithm` table in the
`algorithm` module (`AlgorithmRegistry.h`). Every built-in algorithm, fit
model, fit-setup block, objective and pipeline operation registers itself
there **next to its code** (`register_algorithm`, or `register_algorithm_json`
with a complete JSON entry), and so does every capability a plugin brings
(the plugin host registers it as the plugin loads, and unregisters it if the
plugin's init fails). This module does not hold a table of its own and no
hand-authored registry literal exists anywhere any more: `registry()` is
assembled from `algorithms_json(<capability>)` for each capability that
registered, plus the three catalogs that are not algorithms (`file_container`
from the I/O format table, `table_format`, and `plugin` status).

Who registers what, and where:

| category | declared in |
|---|---|
| `burst_search` (7) | `spectroscopy/burst/src/BurstSearchRegistry.cpp` (`register_burst_search(descriptor, fn)`: description and dispatch in one call) |
| `fit` (5), `fit_setup` (2) | next to the models: `spectroscopy/decay/src/DecayFitModelFit2x.cpp`, `DecayFitModelNExp.cpp` |
| `objective` (4) | `spectroscopy/decay/src/DecayStatistics.cpp` |
| `operation` (8 built-in) | next to the code performing each: `BurstSearchRegistry.cpp` (burst_selection), `BVA.cpp`, `TwoCDE.cpp`, `RecurrenceAnalysis.cpp` (burst_fusion), `fcs/src/Correlator.cpp` (burst_fcs), `decay/src/DecayFitDescriptors.cpp` (tcspc_calibration, mle_green, mle_red) |
| `fcs`, `hmm`, `pda` | `algorithm/src/BuiltinAlgorithms.cpp` |
| plugin `burst_search` / `fit` / `operation` / `correlation_method` / `prior` | `plugin/src/PluginHost.cpp`, at load |

`Registry.cpp::prime_registrations()` asks each of those modules to register
before anything is enumerated — explicitly, because a static initialiser in an
archive member nothing references is dropped by the linker and its category
would silently be empty. `fit_models_json()`, `fit_setup_json()`,
`fit_objectives_json()` and `operation_registry_json()` are category views over
that one table, kept because the bindings and the decay module call them.

The `operation` category is the union of the entries declared as operations
and every `can_replay` registration of any capability, so a consumer reads one
category whichever way an operation was declared. A registry key is unique
across the whole table (`AlgorithmDescriptor::name`, defaulting to
`operation_type`); a duplicate is refused, never resolved by load order.

### Adding an algorithm

Fill in an `AlgorithmDescriptor` and call `register_algorithm` from
`register_builtin_algorithms()` (or, for a plugin, through
`tttrlib_plugin_init_v1`). Two fields are not optional in practice:

- `description` — prose a user reads *instead of* the source when deciding
  whether the algorithm suits their data. A restatement of the name is checked
  for, and fails the test suite.
- `references_json` — at least one citation, so a `.pto` artifact can be traced
  to the literature. Transcribe from the algorithm's own source where it
  carries a reference list; where it does not, say so in the entry rather than
  presenting an unverified citation as if it came from the code.

Registration is explicit, never a static initialiser: a static initialiser in a
translation unit nothing references is dropped when the library is linked as a
static archive, and the algorithm then silently does not exist. That lesson is
already recorded in `DecayFitModelRegistration.h`.

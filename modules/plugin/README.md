# `plugin` — Dynamic Plugin Host System

Loads drop-in C-ABI plugins (`tttrlib_plugin.h`) found on `TTTRLIB_PLUGIN_PATH`
and hands each one a host table to register its capabilities with. Nothing is
loaded at import; the first lookup that could be answered by a plugin loads
them, once per process.

## Contents

- **`tttrlib_plugin.h`**: the versioned C ABI. Six capability tables, each a
  struct with a `struct_size` for forward compatibility:
  | table | what it contributes | reached through |
  |---|---|---|
  | `tttrlib_container_v1` | a file format | `TTTR(path, "NAME")`, `registry("file_container")` |
  | `tttrlib_decay_fit_v1` | a decay fit model | `registry("fit")`, the fit setups |
  | `tttrlib_burst_search_v1` | a burst search | `TTTR.burst_search_by_name`, `TTTR.burst_search(mode)` |
  | `tttrlib_operation_v1` | a self-describing pipeline operation | `registry("operation")`, the burst pipeline |
  | `tttrlib_correlation_method_v1` | a correlation kernel | `Correlator.set_correlation_method`, `correlation_method_names()` |
  | `tttrlib_decay_prior_v1` | a prior kind for the decay fits | `DecayFitPrior.from_json_string`, `DecayFitPrior.kinds()` |
- **`PluginHost.h` / `PluginHost.cpp`**: loader, host table, per-plugin
  journal (a plugin whose init fails has everything it registered rolled
  back), and the lookups the layers above call (`burst_search(name)`,
  `correlation_method(name)`, `decay_prior(kind)`, …).

Every registered capability is also an entry in the **one registry**
(`register_algorithm_json`, module `algorithm`) the moment it registers --
`registry("fit")["exp1_plugin"]` sits beside `fit23` with `provider: plugin`
-- and is unregistered again if the plugin's init later fails. Nothing is
spliced into the registry from here. The last two tables are looked up by
`fcs` and `decay` when their own built-in table misses -- per call, never
cached -- so a rolled-back plugin simply stops being found.

`examples/plugin/tttrlib_example.c` registers one of each and is what
`test/python/plugin/test_plugins.py` exercises
(`-DTTTRLIB_BUILD_EXAMPLE_PLUGIN=ON`).

## Dependencies

- Depends on `util`, `io`, `algorithm`.

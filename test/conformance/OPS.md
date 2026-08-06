# The conformance op vocabulary

Every op below must exist in all four dispatch tables (`test/conformance/py/interpreter.py`,
`test/r/conformance.R`, `ext/java/pkg/src/test/…/ConformanceRunner.java`,
`test/js/conformance.test.mjs`) or be declared `unsupported` by the cases that use it.

**Adding an op is a reviewed change.** The suite stays maintainable only while the
vocabulary is construct / call / reduce / compare and nothing else. If an area
needs control flow, a loop, or arithmetic between two bindings, that case belongs
in the Python suite instead — see `README.md`.

## Step shape

```json
{"op": "<name>", "on": "<binding>", "args": [...], "as": "<binding>", "throws": false}
```

| field | meaning |
|---|---|
| `op` | the operation, from the tables below |
| `on` | binding the op reads (the receiver); omitted by constructors |
| `args` | literal JSON values, or `$name` to substitute a binding, `$data0`…`$dataN` for the case's data files, `$tmp0`…`$tmpN` for scratch file paths |
| `as` | name to bind the result to; omitted when the result is not used |
| `throws` | when true the step is expected to raise, and `as` is bound to a **boolean**: did it raise. The step's normal result is discarded. |

A binding is written once. Re-binding a name is a malformed case and the schema
rejects it.

## Values

A binding holds one of: a **scalar** (integer, double, boolean, string), a
**numeric array**, a **string list**, or an **object handle** (opaque; only ops
accept it). `expect` may only name bindings holding scalars, arrays or string
lists — comparing an object handle is not defined across languages.

## Generic ops

These read `on` and are the only ops that reduce an array to something
comparable.

| op | on | args | result |
|---|---|---|---|
| `len` | array, string list, string | — | integer |
| `sum` | numeric array | — | integer or double, matching the element type |
| `mean` | numeric array | — | double |
| `min` / `max` | numeric array | — | element |
| `argmin` / `argmax` | numeric array | — | integer index of the **first** extreme |
| `first` / `last` | array, string list | — | element |
| `nth` | array, string list | `[i]` | element |
| `slice` | array, string list | `[start, stop]` | array or string list |
| `to_list` | array, string list | — | list (use only for short arrays) |
| `unique_sorted` | numeric array | — | ascending list of distinct values |
| `shape` | array | — | list of integers |
| `dtype` | array | — | canonical dtype string (below) |
| `contains` | string | `[substring]` | boolean |
| `round` | double | `[ndigits]` | double |
| `count_gt` | numeric array | `[threshold]` | integer — how many exceed it |
| `identity` | any comparable | — | itself (renames a binding) |

`argmax` ties resolve to the lowest index in every language — NumPy, R's
`which.max`, a hand-written Java loop with `>` and the JS reduce all agree, and a
case whose peak is not unique is a bad case regardless.

### Canonical dtype strings

`float64`, `float32`, `int64`, `int32`, `int16`, `int8`, `uint64`, `uint32`,
`uint16`, `uint8`, `bool`, `string`. A runner whose language cannot distinguish
two of these (R has one numeric type; JavaScript has no int16) reports the dtype
the **binding** produced, not the one the language would prefer — that is exactly
the marshalling fact the case is pinning.

## `tttr.*`

| op | on | args | result |
|---|---|---|---|
| `tttr.open` | — | `[path, container_type]` | TTTR handle |
| `tttr.size` | tttr | — | integer |
| `tttr.n_valid_events` | tttr | — | integer |
| `tttr.n_micro_channels` | tttr | — | integer |
| `tttr.macro_times` | tttr | — | array |
| `tttr.micro_times` | tttr | — | array |
| `tttr.routing_channels` | tttr | — | array |
| `tttr.macro_time_at` | tttr | `[i]` | integer |
| `tttr.micro_time_at` | tttr | `[i]` | integer |
| `tttr.routing_channel_at` | tttr | `[i]` | integer |
| `tttr.used_routing_channels` | tttr | — | array |
| `tttr.by_channel` | tttr | `[[ch, …]]` | TTTR handle |
| `tttr.burst_search` | tttr | `[L, m, T, mode]` | array (flat start/stop pairs) |
| `tttr.microtime_histogram` | tttr | `[coarsening]` | array |
| `tttr.header_json` | tttr | — | string |
| `tttr.micro_time_resolution` | tttr | — | double |
| `tttr.macro_time_resolution` | tttr | — | double |

## `correlator.*`

| op | on | args | result |
|---|---|---|---|
| `correlator.curve_size` | — | `[n_bins, n_casc]` | integer |

## `ds.*` — DataStore

| op | on | args | result |
|---|---|---|---|
| `ds.new` | — | — | store handle |
| `ds.add_f64` / `ds.add_f32` | store | `[name, [values]]` | — |
| `ds.add_i64` / `ds.add_i32` / `ds.add_i16` / `ds.add_i8` | store | `[name, [values]]` | — |
| `ds.add_u64` / `ds.add_u32` / `ds.add_u16` / `ds.add_u8` | store | `[name, [values]]` | — |
| `ds.add_string` | store | `[name, [strings]]` | — |
| `ds.n_rows` / `ds.n_columns` / `ds.n_groups` | store | — | integer |
| `ds.column_names` | store | — | string list |
| `ds.add_group` | store | `[name]` | store handle |
| `ds.ensure_group` | store | `[path]` | store handle |
| `ds.group` | store | `[path]` | store handle |
| `ds.has_group` | store | `[path]` | boolean |
| `ds.remove_group` | store | `[path]` | boolean |
| `ds.group_names` | store | — | string list |
| `ds.group_paths` | store | — | string list |
| `ds.set_label` | store | `[label]` | — |
| `ds.label` | store | — | string |
| `ds.nbytes` | store | — | integer |
| `ds.column_f64` / `ds.column_f32` | store | `[name]` | array of that type |
| `ds.column_i64` / `ds.column_i32` / `ds.column_i16` / `ds.column_i8` | store | `[name]` | array of that type |
| `ds.column_u64` / `ds.column_u32` / `ds.column_u16` / `ds.column_u8` | store | `[name]` | array of that type |
| `ds.column_strings` | store | `[name]` | string list |
| `ds.column_dtype` | store | `[name]` | dtype string |
| `ds.select_range` | store | `[column, lo, hi]` | — |
| `ds.n_selected` | store | — | integer |

`ds.column_<type>` names the type the caller expects; a column of a different
type is an error, which is what makes a dtype round trip testable rather than
silently coerced.

## `tiff.*`

| op | on | args | result |
|---|---|---|---|
| `tiff.write_f64` | — | `[path, n_frames, height, width, [values]]` | — |
| `tiff.read_f64` | — | `[path]` | array, flat row-major |

The suite's `IN_ARRAY3` case. Values arrive **flat** with the dimensions as
separate arguments and each runner shapes them, the same rule as `hist.update`.

## `burst.*`

| op | on | args | result |
|---|---|---|---|
| `burst.new` | — | `[$tttr]` | BurstFilter handle |
| `burst.find` | filter | — | array, (n_bursts, 2) start/stop, row-major |
| `burst.properties` | filter | — | array, (n_bursts, 5), row-major |

Both are 2-D outputs and each binding reaches them differently — Java has no 2-D
output typemap, so `find_bursts` needs the `%ARRAY_INTO_2D` accessor in
`helpers.i` and `get_all_burst_properties` comes back as a nested vector proxy
read row by row.

## `pda.*`

| op | on | args | result |
|---|---|---|---|
| `pda.new` | — | `[nmax, n_channels, bg1, bg2, [pF]]` | Pda handle |
| `pda.append` | pda | `[amplitude, probability]` | — |
| `pda.evaluate` | pda | — | — |
| `pda.s1s2` | pda | — | array, (nmax+1)² row-major |
| `pda.histogram_y` | pda | — | array of bin values |

`pda.s1s2` is the suite's `ARGOUTVIEW_ARRAY2` case. Row is channel 1 and column
is channel 2 everywhere; a case must give its two species **different**
probabilities, or a transpose would pass.

## `fit.*` — decay fitting

| op | on | args | result |
|---|---|---|---|
| `fit.names` | — | — | string list of fit models |
| `fit.setup_names` | — | `[model]` | string list |
| `fit.result_names` | — | `[model]` | string list |
| `fit.setup_vector` | — | `[model, json]` | array |
| `fit.problem` | — | `[n_curves, n_channels, dt, [irf], [background], [data]]` | problem handle |
| `fit.new` | — | `[model, [setup], [irf]]` | fit handle |
| `fit.run` | fit | `[[start], [link], $problem]` | outcome handle |
| `fit.objective` | outcome | — | double |
| `fit.parameters` | outcome | — | array |
| `fit.results` | outcome | — | array |

Deliberately the **raw** interface. `decay_fit_setup_vector`, `DecayFit2` and
`DecayFitProblem` exist under those names in all four bindings; Python's
`setup_vector()` and `results_as_dict()` are `%pythoncode` and exist in one.

These are the suite's only iterative numerical results, so their cases carry a
tolerance. Everything else is exact.

## `clsm.*`

| op | on | args | result |
|---|---|---|---|
| `clsm.open` | — | `[$tttr, [channels]]` | CLSMImage handle |
| `clsm.n_frames` / `clsm.n_lines` / `clsm.n_pixel` | image | — | integer |
| `clsm.intensity` | image | — | array, (frame, line, pixel) row-major |
| `clsm.mean_micro_time` | image | `[$tttr]` | array, same layout |
| `clsm.fluorescence_decay` | image | `[$tttr, coarsening, stack_frames]` | array, (frame, line, pixel, tac) |

The suite's 3-D output case. Only Python and JavaScript carry the shape with
the array — R flattens and Java fills a 1-D buffer — so a case pins the three
dimensions as separate scalars and never asks a CLSM array for its `shape`.

## `hist.*`

| op | on | args | result |
|---|---|---|---|
| `hist.new` | — | — | histogram handle |
| `hist.set_axis` | histogram | `[dim, name, begin, end, n_bins, kind]` | — |
| `hist.update` | histogram | `[[samples]]` | — |
| `hist.counts` | histogram | — | array of bin counts |

`hist.update` is the suite's `IN_ARRAY2` case. The samples arrive as a **flat**
list and each runner shapes them `(n, 1)` — one column, n rows. Doing that in
the runner rather than in the case file is deliberate: a shape in the case
format would be the first step down the road this vocabulary is capped to avoid.

## `bitmask.*`

| op | on | args | result |
|---|---|---|---|
| `bitmask.new` | — | `[n]` | BitMask handle (every bit set) |
| `bitmask.set` | mask | `[i, value]` | — |
| `bitmask.size` | mask | — | integer |
| `bitmask.count` | mask | — | integer |
| `bitmask.to_bytes` | mask | — | array, one byte per bit |

`bitmask.to_bytes` is the suite's `INPLACE_ARRAY1` case, and the one op whose
*calling convention* genuinely differs per language: Python and JavaScript
mutate the array they are handed, Java copies back when the JNI array is
released, and R cannot mutate a caller's vector at all so `rarrays.i` returns
the filled one. Each runner hides that; the bytes must agree.

## `registry.*`

| op | on | args | result |
|---|---|---|---|
| `registry.json` | — | — | string (the whole registry) |
| `registry.category_json` | — | `[category]` | string |
| `registry.categories` | — | — | string list |

The two JSON ops return raw material by design: a case reduces them with `len`
and `contains` rather than pinning a serialiser's exact whitespace into the
expectations.

## `file.*`

| op | on | args | result |
|---|---|---|---|
| `file.write_text` | — | `[path, text]` | — |

The one non-library op, and it exists for one reason: criterion 14 says the HDF5
probes answer politely on a file that is not HDF5, and something has to make that
file.

## `hdf5.*`

| op | on | args | result |
|---|---|---|---|
| `hdf5.write` | — | `[path, $store, group]` | boolean |
| `hdf5.read` | — | `[path, group]` | store handle |
| `hdf5.groups` | — | `[path]` | string list |
| `hdf5.has` | — | `[path, group]` | boolean |

`hdf5.write` writes with the library default mode (update) and no compression;
a case that needs truncate says so with `hdf5.write_mode`, which does not exist —
add it only when a case genuinely needs it.

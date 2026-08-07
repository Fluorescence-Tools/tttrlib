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
| `args` | literal JSON values, or `$name` to substitute a binding, `$data0`…`$dataN` for the case's data files, `$tmp0`…`$tmpN` for scratch file paths, optionally with a suffix (`$tmp0.dstore`) for a writer that picks its format from the extension |
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
| `contains` | string | `[substring]` | boolean |
| `round` | double | `[ndigits]` | double |
| `count_gt` | numeric array | `[threshold]` | integer — how many exceed it |
| `identity` | any comparable | — | itself (renames a binding) |

`argmax` ties resolve to the lowest index in every language — NumPy, R's
`which.max`, a hand-written Java loop with `>` and the JS reduce all agree, and a
case whose peak is not unique is a bad case regardless.

### Canonical dtype strings

`ds.column_dtype` answers with one of: `float64`, `float32`, `int64`, `int32`,
`int16`, `int8`, `uint64`, `uint32`, `uint16`, `uint8`, `bool`, `string`. That
reads the column's **C++** `ColumnType`, so every binding can answer it.

There is deliberately no generic `dtype` op for arrays. R has one numeric type,
so an array's element width is not observable from R at all, and a case using it
could never run in all four — which is the bar every case here has to clear.

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
| `correlator.new` | — | `[n_bins, n_casc]` | Correlator handle |
| `correlator.set_tttr` | correlator | `[$tttr_a, $tttr_b]` | — |
| `correlator.x_axis` | correlator | — | array of lag times |
| `correlator.correlation` | correlator | — | array, normalised |

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

## `mask.*` — photon selection

| op | on | args | result |
|---|---|---|---|
| `mask.new` | — | `[$tttr]` | TTTRMask handle |
| `mask.select_channels` | mask | `[$tttr, [channels], mask]` | — |
| `mask.select_count_rate` | mask | `[$tttr, time_window, n_ph_max, invert]` | — |
| `mask.size` | mask | — | integer |
| `mask.mask_array` | mask | — | array, one byte per event (1 = marked) |

## `nn.*` — neural net

| op | on | args | result |
|---|---|---|---|
| `nn.from_json` | — | `[spec]` | NeuralNet handle |
| `nn.predict` | net | `[[inputs]]` | array |
| `nn.n_layers` / `nn.n_inputs` / `nn.n_outputs` | net | — | integer |

## `csvfile.*`

| op | on | args | result |
|---|---|---|---|
| `csvfile.write` | — | `[path, $store]` | — |
| `csvfile.read` | — | `[path]` | store handle |

`csvfile.write` calls the **native** writer, not Python's `write_csv`: the
latter is a `%pythoncode` convenience taking keyword arguments and exists in
one language.

## `feature.*` — burst features

| op | on | args | result |
|---|---|---|---|
| `feature.new` | — | `[kind, $tttr, [donor], [acceptor]]` | BVA or TwoCDE handle; kind is `"bva"` or `"twocde"` |
| `feature.compute` | feature | `[$bursts, …]` | — |
| `feature.values` | feature | — | array, one value per burst |

`feature.compute` takes the burst bounds as the **flat** `tttr.burst_search`
binding and each runner shapes them `(n, 2)` — the same rule as `hist.update`.
It calls `compute_bursts`, the non-overloaded entry point, because SWIG's R
overload dispatcher cannot accept a matrix at all (`class(matrix)` is two
values since R 4.0).

`feature.compute`'s trailing arguments differ by kind: BVA takes
`[photons_per_slice, minimum_window]`, 2CDE takes `[tau]`.

## `phasor.*`

| op | on | args | result |
|---|---|---|---|
| `phasor.g` / `phasor.s` | — | `[g_irf, s_irf, g_exp, s_exp]` | double |
| `phasor.from_bincounts` | — | `[[counts], frequency, min_photons, g_irf, s_irf]` | array of `[g, s]` |

`phasor.from_bincounts` calls `phasor_of_bincounts`, the `IN_ARRAY1`
overload added for it. The native `compute_phasor_bincounts` takes a
`std::vector<int>&`, which R cannot pass — SWIG's R dispatcher wants a typed
S4 proxy and `VectorInt32()` returns a bare externalptr it will not match.

Static methods, and the only area that reads no file at all — so these cases
run wherever the binding exists.

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
| `clsm.decay_of_pixels` | image | `[$tttr, [f0, l0, l1, p0, p1], coarsening, stack_frames]` | array |

`clsm.decay_of_pixels` is the suite's `INPLACE_ARRAY3` case — the only live user
of that typemap category in the library. The mask is described as a rectangle
and each runner builds the array, because the vocabulary has no way to
construct one and a literal 2.6-million-element mask in a case file would be
absurd.

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

## `pto.*`

The container, and its targeted reads.

| op | on | args | result |
|---|---|---|---|
| `pto.create` | — | `[path, title]` | file handle, open for writing |
| `pto.open` | — | `[path]` | file handle, read-only |
| `pto.close` | file | — | — |
| `pto.commit` | file | — | boolean |
| `pto.add_file` | file | `[kind, encoding, name, path]` | integer uid |
| `pto.add_store` | file | `[kind, name, $store]` | integer uid |
| `pto.n_objects` | file | — | integer |
| `pto.names` / `pto.kinds` | file | — | string list, in object order |
| `pto.size_of` | file | `[$uid]` | integer — payload bytes |
| `pto.read_text` | file | `[$uid, at, n]` | string |
| `pto.store_columns` / `pto.store_groups` | file | `[$uid]` | string list |
| `pto.read_store` | file | `[$uid, [columns], first_row, n_rows]` | store handle |
| `pto.build_cues` | file | `[$uid, spacing]` | integer — cues built |
| `pto.cue_events` | file | `[$uid]` | list of integers |
| `pto.events` | — | `[path, selector, first, n]` | TTTR handle |

**A uid is random**, so it is raw material everywhere it appears: bound, passed
on, never expected. What a case compares is a name, a count, a column, or text.

It is 53 random bits rather than 64 *because* of this suite. R and JavaScript
represent every integer as a double, so a wider uid comes back from those two
runners as a different number and cannot be handed back to the call that
produced it. See `random_uid` in `modules/io/pto/src/io_pto.cpp`.

`pto.read_text` decodes the payload bytes as **latin-1** — the one text encoding
that round-trips an arbitrary octet in all four languages, so a case can pin a
byte range without four different bytes-to-string conventions getting in the
way. An empty column list in `pto.read_store` means every column, and `n_rows`
of 0 means to the end; that is the library's own convention, not the runner's.

`pto.build_cues` raises when it builds none, so a case that expects an object to
be indexable says so by not passing `throws`.

## `stream.*`

Decoding a buffer of undecoded records, and reading a container in pieces


| op | on | args | result |
|---|---|---|---|
| `tttr.new` | — | — | an empty TTTR handle, to decode into |
| `stream.n_records` | — | `[path, container_type]` | integer — records, without decoding one |
| `stream.record_type` | — | `[path, container_type]` | integer — the record encoding |
| `stream.ranged` | — | `[path, container_type]` | boolean — readable in pieces? |
| `stream.record_name` | — | `[record_type]` | string, e.g. `"SPC-130"` |
| `stream.record_bytes` | — | `[record_type]` | integer — 0 when there is no fixed width |
| `stream.can_decode` | — | `[record_type]` | boolean |
| `stream.read_records` | — | `[path, container_type, first, n]` | record buffer (bytes) |
| `stream.state` | — | — | a decode state handle |
| `stream.overflows` | state | — | integer — macro time overflows counted so far |
| `stream.decode` | tttr | `[$buffer, record_type, $state]` | integer — events appended |
| `stream.events` | — | `[path, container_type, first, n]` | TTTR handle, decoded |
| `stream.apply_channels` | tttr | `[container_type]` | — |

**The container is an integer here, not a name** — unlike `tttr.open`, which
takes `"SPC-130"`. These ops are about a container's *record stream* rather than
about opening one, and the record-type constants the same cases carry are
integers too, so one convention beats two. The registry publishes the id.

`stream.read_records` yields a **byte buffer**, and the element ops (`len`,
`nth`) work on it. It is deliberately not widened to a numeric array: a whole
container is millions of bytes and `stream.decode` takes bytes anyway.

A chunked decode is written out as steps — two `stream.read_records` and two
`stream.decode` sharing one `stream.state` — rather than hidden behind an op
that loops. The claim being tested is that *the library does not have to own the
loop*, so an op that owned it would be testing the runner.

## `bhset.*`

The whole Becker & Hickl `.set` sidecar, as opposed to the five
imaging tags the photon reader folds into a header.

| op | on | args | result |
|---|---|---|---|
| `bhset.n` | — | `[path]` | integer — parameters parsed |
| `bhset.sections` | — | `[path]` | string list, ascending |
| `bhset.value` | — | `[path, section, name]` | string |

**Values are strings**, including the numeric-looking ones. A `.set` declares
its own type per parameter and a parser that guesses is wrong about one field in
a hundred and silent about it, so the interpretation is the caller's — which
means the expectation in a case is `"6.554e-08"` and not `6.554e-08`.

## `table.*`

One vocabulary for a table in a file, whatever the file is. These add no
capability — every one calls a reader that already exists — and what the cases
pin is that the choosing happens once and gives the *same* answer in every
format.

| op | on | args | result |
|---|---|---|---|
| `table.read` | — | `[spec, group?, columns?, first_row?, n_rows?]` | store handle |
| `table.write` | — | `[spec, $store, group?]` | boolean |
| `table.groups` | — | `[spec]` | string list |
| `table.columns` | — | `[spec, group?]` | string list |
| `table.has` | — | `[spec, group?]` | boolean |

**`spec` is `path` or `path|object`.** A PTO holds many objects, each of which
is a tree, and the pipe is where that extra addressing axis goes. Everything
after it is identical across the three formats:
`table.read` on `run.dstore`, on `run.h5` and on `run.pto|bursts` with the same
`group` and `columns` gives the same table.

**The reader takes its format from the content and the writer from the
extension.** That asymmetry is inherent — the file a write targets need not
exist yet, so there is nothing to sniff — and it is why the cases here name
scratch files as `$tmp0.dstore` rather than `$tmp0`.

**Group paths are normalised to the bare form.** HDF5's own listing gives
`/results` and includes the root; the native format's gives `results` and does
not. One of them had to win or a path taken from one listing could not be handed
to the other, and the bare form wins because it is the one a caller writes.

**The three queries are silent on any input** and `table.read` is not: a caller
probes with the queries to decide whether a file is worth opening, often in a
loop, while a read that returned an empty store to mean "could not read" could
not be told from one that read an empty table.

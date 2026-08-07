# PRD-023 — One vocabulary for a table in a file, whatever the file is

> **PRD #:** 023 · **Status:** 🟢 Done · **Created:** 2026-08-07 · **Updated:** 2026-08-07 · **Owner:** tpeulen

> **Implemented**, in five stages. Four notes for the reader, each a place the
> implementation learned something the proposal did not know:
>
> - **Criterion 18 became moot.** It asked that a format unable to carry NA
>   ranges materialise the mask instead. Stage 2 fixed HDF5's dropped metadata,
>   so both tree formats carry the ranges — and HDF5 writes the mask *as well*,
>   because that format exists to hand a table to a reader that cannot be
>   assumed to know what an `na` range is. CSV materialises, as the criterion
>   intended.
> - **`nbytes()` deliberately does not count the description.** Adding it was
>   tried and reverted: it broke "a float32 column is half a float64 one" for a
>   reason that has nothing to do with dtypes, and a description is not a buffer.
> - **`take`/`compact` drop the NA ranges and keep the validity.** Not in the
>   PRD, and it has to be true: a gather reorders rows, so a range naming the
>   source's rows says nothing about the result's.
> - **Running the R runner found what no Python test could.** SWIG-R generates
>   an unsatisfiable dispatcher for any function taking a
>   `std::vector<std::string>`, so six of eight new conformance cases failed
>   there while Python was green. Noted in `ext/r/tttrlib.i`, since it is a
>   property of the parameter type and recurs.
>
> Java and JavaScript conformance runners have the ops but were **not executed**
> locally — no built addon or JNI in this tree. Python: 101 cases. R: 95.

## Summary

A `DataStore` can be written to three files — `.dstore`, HDF5, and a `dstore`
object inside a `.pto` — and read back from all three. The three round-trip the
same tree. They do not share a single call, a single parameter name, or a single
path convention, and each can do things the others cannot.

So the formats are **not** interchangeable, and a caller that wants to swap one
for another rewrites its I/O layer rather than changing an extension. That is
the whole gap: the file formats already agree about the data, and the API does
not.

This asks for one vocabulary over all three, and for the capability gaps to be
**closed natively** rather than papered over, so that swapping a format changes
what the bytes look like on disk and nothing else — including the cost.

It rests on one rule, and the rule is what makes it possible at all:
**everything happens in memory, and a file changes only when it is written.**
Every difference left between the three formats is on the mutation side, so a
vocabulary that does not mutate has nothing left to disagree about.

It also asks for the two in-memory operations without which the reading is not
worth doing — `concat` and `take` — because a caller that can read N files into
N stores and cannot make them one table will keep building data frames instead,
which is the position the downstream migration is stuck in today.

And it asks for the layout to be **written down**. How several columns sit in a
`.dstore`, and what `columns=` means when the same name appears in three tables
of one tree, are not documented anywhere and are not guessable.

## Problem / motivation

### Three names for every verb

| | `.dstore` | HDF5 | PTO |
|---|---|---|---|
| read the tree | `load_store(f)` | `read_hdf5(f)` | `pto_store(file, uid)` |
| read one group | — | `read_hdf5(f, group)` | — |
| column subset | `load_store(f, columns=)` | — | `pto_store(f, uid, columns=)` |
| row range | `load_store_region` only | — | `pto_store(f, uid, first_row=, n_rows=)` |
| list groups | `store_groups(f)` | `hdf5_table_groups(f)` | `pto_store_groups(f, uid)` |
| list columns | `store_columns(f, group)` | `read_hdf5_table_columns(f, group)` | `pto_store_columns(f, uid, group)` |
| does it hold one | — | `hdf5_table_has(f, group)` | — |
| remove one | — | `hdf5_table_remove(f, group)` | `PtoFile` methods |
| write the tree | `save_store(f, store)` | `write_hdf5(f, store, group, compression, mode)` | `pto_add_store(...)` |

Every row of that table is the same operation. The gaps run **both ways** —
HDF5 is the only one that can select a group, `.dstore` and PTO are the only
ones that can select columns — so neither format is a superset and a caller
cannot even pick the capable one.

### The path convention disagrees on the one thing that has to line up

`DataStore.group_paths()` returns `["results", "a/b"]`. `hdf5_table_groups()`
returns `["/results", "/a/b"]`, and `"/"` for the root. `store_groups()` returns
the first form. So the string that identifies a group depends on which file it
came out of, which is exactly the value a caller passes straight back in.

### It is costing a real migration

`BUGS.md` records the downstream position: HDF5 is kept because the burst and
imaging files are *interchange* formats other programs read, and `.dstore` is
wanted because it is dramatically faster than compressed HDF5 and is a wash
against uncompressed. Both are true at once, so the package needs both — and
today "both" means two code paths, not one call with a different extension.

### The performance claim is in the header, unmet

`io_store.h` states it outright: *"Reading one column of four takes 0.0001 s
rather than 0.009 s: the directory says where each column is, so the others are
never touched. There is no equivalent through the HDF5 path here."* HDF5 stores
one 1-D dataset per column, so there is no reason for that to be true — the
subset read is a different set of `H5Dread` calls, not a different design.

## Proposal

### Part 0 — the model, which is what makes the rest possible

> **Every operation happens in memory. A file changes when, and only when, it is
> written.**

Read a file into a store; add, remove, concat, take, gate and rename in the
store; write it back. There is no call in this vocabulary that mutates a file
except `write_table`, and there is no way to change part of a file without
having the part in memory first.

This is not a restriction bolted on — **it is what makes the two formats look
alike**, and it is worth seeing why. Every asymmetry left in the table above is
on the mutation side:

| | reading | mutating |
|---|---|---|
| `.dstore` | group / columns / rows | whole file only — directory last, no seeking |
| HDF5 | group only | `Update` mode replaces one group in place |
| PTO | group / columns / rows | replaces one object in place, by design |

Three formats with three different *mutation* models cannot present one
interface without either lying about cost or exposing the differences. Three
formats with the same *read* model and a single "write this store" verb can, and
the differences become what they should be — an implementation detail of the
write, chosen by the format:

* `.dstore` rewrites. That is the price of a directory at the tail and a single
  forward pass, which is what makes it fast to read.
* HDF5 can replace one group and leave the others, which matters because other
  programs write into the same file.
* PTO replaces one object's payload in place, because recomputing a burst table
  beside an 8 GiB photon stream must not rewrite the photon stream.

A caller does not choose between those and does not need to know which happened.
It hands over a store and the file afterwards holds it.

**Two things this rule does not touch.** `PtoFile` stays as it is — it manages
objects, blobs and tags in a container, which is a different job from reading a
table out of one. And `hdf5_table_remove` stays as a format-specific escape
hatch for a caller who genuinely wants to reach into an HDF5 file; it is simply
not part of this vocabulary.

#### The write should be smart, and the rule is what allows it

Because the caller hands over a whole store and does not say *how*, a format is
free to write only what changed. For PTO — which already updates one object in
place — that means going a level finer: a `dstore` payload is blobs plus a
directory, so a burst table whose `Tau` was recomputed and whose other ninety
columns were not should rewrite one blob, not ninety-one.

**A dirty flag will not do**, and this is the trap to write down before someone
tries it. `f64_data()` deliberately hands out a writable pointer for the hot
loops, and a numpy view writes straight into the buffer, so a column can change
without any setter being called. A flag would be right almost always, and the
"almost" is silent data loss.

**A per-blob checksum in the directory** is what does work: hash each column on
write and skip the ones whose hash and length are unchanged. `.dstore` already
checksums its directory and deliberately does not checksum payload —

> *The directory is small, so verifying it is free. Checksumming four gigabytes
> of payload on every open would cost more than the format saves.*

— and that reasoning is about **open**, not write. A write is already touching
the data, and hashing at several GB/s to avoid writing at disk speed is the
trade the right way round. It also gives PTO's free-extent machinery something
exact to decide against.

Scoped as a **follow-up, not part of this PRD**: it is a PTO/`.dstore` writer
optimisation with its own correctness burden (a missed change is a corrupt
file), and it needs Part 5's format version to land first so the checksum has
somewhere to live. Recorded here because Part 0 is what makes it possible —
under an API where the caller patched files itself, there would be nothing for
the writer to be smart about.

### Part 1 — one vocabulary, keyed by a spec

Five free functions, the format inferred from the file exactly as
`TTTR(filename)` already infers a container:

```
read_table(spec, group="", columns=None, first_row=0, n_rows=0) -> DataStore
write_table(spec, store, group="", mode="update")               -> bool
table_groups(spec)                                              -> [str]
table_columns(spec, group="")                                   -> [str]
table_has(spec, group="")                                       -> bool
```

Five, not six: **there is no `table_remove`.** Removing a group is
`read_table` → `store.remove_group(path)` → `write_table`, which is Part 0's
rule applied. A remove verb would be the only call that changed a file without
the caller holding what changed, and it is the one that cannot be given the
same cost in all three formats — `.dstore` would rewrite, HDF5 would unlink,
PTO would free an extent. The three queries and the two transfers are the whole
surface.

Free functions rather than a handle object, because that is what every other
entry point here is and it needs no new wrapped class in four bindings.

**`spec` is `path` or `path|object`**, reusing `kSubfileSeparator` and
`subfile_path`/`subfile_selector` — the mechanism the TTTR readers already use
for `run.pto|m001.ptu`. A PTO has one more addressing axis than the other two
(it holds many objects, each of which is a tree), and the pipe is where that
axis goes:

```python
read_table("run.dstore",       group="results", columns=["Tau"])
read_table("run.h5",           group="results", columns=["Tau"])
read_table("run.pto|bursts",   group="results", columns=["Tau"])
```

One object, one group, one column subset, one row range — in all three.

### Part 2 — close the gaps natively

Emulation was considered and rejected: a layer that reads a whole file and
slices gives the right answer at the wrong cost, and a caller who swapped an
extension to get a subset read would get the opposite. Each gap is filled where
the format actually is.

**HDF5** — `io_hdf5_table`:
- `columns=`: open only the named datasets. One dataset per column is already
  the layout, so this is a shorter loop, not a new design.
- `first_row` / `n_rows`: a hyperslab on each column's dataset. HDF5 does this
  natively; nothing has to be read and discarded.

**`.dstore`** — `io_store`:
- `group=`: the directory names every node and every column's offset, so
  reading one group is the same skip the column subset already performs. The
  tree above it still comes back, being the directory.
- `table_has`, which is a directory read and touches no payload.

Note what is *not* on that list, because of Part 0: no partial write and no
remove. Those were the expensive half of the gap and the rule removes the need
for them rather than the ability.

**PTO** — `io_pto`: already has columns and rows; needs `group=` passed through
to the embedded store, and the spec form.

**And a byte counter for HDF5**, matching `store_bytes_read()`. Criterion 3 is
the whole point of doing this natively, and it cannot be asserted on the format
it is new in without one.

### Part 3 — the invariant, stated so it can be tested

> **The same tree, written to `.dstore` and to HDF5, must answer every call in
> this vocabulary identically — same values, same order, same spelling — except
> for a short, enumerated list of what a format genuinely cannot carry.**

That list is the whole of the permitted difference, and it exists already:

| | `.dstore` | HDF5 |
|---|---|---|
| bool column | survives | comes back `uint8` — HDF5 has no boolean type |
| store label | survives | nowhere to put it |
| row selection | saved as data | **applied**: only selected rows are written |
| a column and a group of the same name | legal | refused before the file is touched — one link namespace per group |
| column metadata (`units`, PRD-022) | survives | **lost today** — see Part 5; this PRD fixes it rather than listing it |

Anything not on that list is a defect, not a format difference. The test is the
substitution: write one tree both ways, run every call against both, compare.

### Part 4 — the two in-memory operations the access layer is useless without

`BUGS.md` counts them in the shipped downstream package: **`concat` at 24 call
sites in 13 files**, `take`/`compact` at 9 plus every filtered export. They are
in this PRD rather than a separate one because they are what makes the reading
worth doing:

* **`concat`** — a burst folder is read per measurement and combined. Without
  row-append, `read_table` over N files gives N stores that cannot become one
  table, so the caller builds frames instead and the store never reaches memory.
  That is why the measured 109.5 MB → 60.2 MB saving on a 1M-row burst table is
  still not being collected even though both file boundaries have already moved.
* **`take` / `compact`** — a selection is expressible and cannot be
  *materialised*. `read_table(..., group=g)` then a gate gives a store you
  cannot write back out as what you selected. Twenty-nine `select_*` methods
  exist and not one of them yields a store.

```
concat(stores, axis="rows",    join="outer") -> DataStore
concat(stores, axis="columns", join="outer") -> DataStore
store.take(indices)                          -> DataStore
store.compact()                              -> DataStore
```

Pure `DataStore` operations with no format in them, so they are wrapped once and
every format gets them.

**Both axes, because the downstream uses both** — and calls them by these names
already, in `read_mfd_hdf5(..., merge_mode=)`: `rows` stacks separate
measurements, `columns` puts files describing the *same* bursts side by side.
`axis=0` / `axis=1` are accepted too, so the call reads the way `pd.concat`
does. There is no `ignore_index`: a store has no index, which is why `BUGS.md`
records that its ten `reset_index` sites simply vanish.

**`axis="rows"`** — same columns, stacked; `n_rows` is the sum.

* `join="outer"` (default): the union of column names. A column missing from one
  store has those rows marked **not measured**.
  This is where a store beats a frame outright, and the PRD says so: pandas has
  to widen an `int64` column to `float64` to hold `NaN`, so the dtype is lost and
  cannot be recovered afterwards. "Not measured" is expressed without touching
  the dtype, which is exactly what it is for.

  **And it is recorded as metadata, not as a bit mask.** A concat-introduced gap
  is always a *contiguous run* — one whole store's contribution — so a per-row
  bit mask stores a million copies of one fact. `Column::metadata()` is already
  a free-form extensible description (PRD-022), so the *shape* needs no format
  change:

  ```
  {"name": "Tau", "units": "ns",
   "na": [{"rows": [1000000, 2000000], "why": "absent in m002.hdf5"}]}
  ```

  The saving is real but should not be oversold — measured on 20 stores of 1M
  rows, a column present in one:

  | | data | bit mask | as ranges |
  |---|---|---|---|
  | float64 | 160 MB | 2.5 MB (1.6%) | ~40 B |
  | int8 | 20 MB | 2.5 MB (12.5%) | ~40 B |

  So it matters most exactly where a frame hurts most — narrow columns, where
  the mask approaches an eighth of the payload — and it is close to free
  everywhere else.

  **The better reason is that a range can say *why* and a bit cannot.** "These
  rows are not measured because that file did not have this column" is
  information; `NaN` is the absence of it. A caller merging twenty files can
  then report which ones contributed what, which is a question the downstream
  currently answers by keeping the file list beside the frame.

  `Column::valid(i)` and `has_mask()` stay the interface — the ranges are how
  the answer is stored, not a second thing to ask. A bit mask is materialised
  lazily and only when a pattern is genuinely scattered, which is what
  `mask_non_finite()` produces and a merge does not.

  **And it must be materialised on the way into HDF5**, because — measured, not
  assumed — HDF5 carries the validity mask and **drops column metadata
  entirely**:

  ```python
  c.set_units("ns"); c.set_attribute("na", "[[2,4]]")
  # via .dstore : {"na":"[[2,4]]","name":"Tau","units":"ns"}   has_mask True
  # via HDF5    : ''                                            has_mask True
  ```

  So writing NA-as-ranges to HDF5 unchanged would lose them silently, which is
  precisely the class of bug Part 3's invariant exists to catch — and it was
  caught by running that test rather than by reasoning. The rule: a format that
  cannot carry the ranges gets the mask instead. `valid(i)` is preserved
  everywhere; the *reason* and the compactness survive only where the metadata
  does.

  That is a difference between the two formats, so by Part 3 it goes on the
  enumerated list — **or it gets fixed**, and it should be. HDF5 has attributes;
  a dataset attribute is where a column's metadata belongs, and PRD-022 added
  `units` precisely so a consumer could know that `Tau` is nanoseconds. Today
  writing that table to HDF5 throws it away. Folding the fix in here is one
  attribute write and one read, and without it PRD-022's whole point stops at
  the `.dstore` boundary.

  **What this does not fix, and the PRD must not imply it does:** the *data*
  buffer. Those million rows are still allocated for a column that has no values
  there — 16 GB for a 100-column union over 20M rows, against 250 MB of masks.
  Making that cheaper means a column that stores only the extent it covers, and
  that breaks `f64_ptr()`'s contiguity, which every hot loop and the
  mmap-a-column path in `io_store.h` depend on. Out of scope here, and the
  reason `join="inner"` is worth having next to `outer`.
* `join="inner"`: the intersection — what the downstream hand-rolls today,
  `sorted(set(a.columns) & set(b.columns))` then stack. Worth having so that
  loop can go.

**`axis="columns"`** — same rows, columns side by side.

* Row counts must match, and a mismatch **refuses and names both counts**. The
  downstream currently warns and silently skips the file, which is how a merge
  quietly loses a measurement.
* A duplicate column name refuses by default. Today the second copy is dropped
  and the caller cannot tell which survived; `on_duplicate="keep-first"` keeps
  that behaviour for anyone who wants it.

**Groups.** `concat` operates on the store it is given, not on the tree.
Concatenating two roots that each hold a `results` group is ambiguous — are the
`results` tables being stacked, or the roots? — and row counts differ per group
anyway. Concatenating groups is `concat([a.group("results"),
b.group("results")])`, said out loud. Same rule as `histogram`: PRD-019
introduced no cross-group operation and this introduces none either.

**`take` / `compact` copy.** `DataStore` has no borrowing column, and inventing
one to make `take` a view is a far larger change than this PRD. `compact()` is
`take(the selected rows)`, named separately because it is what a caller means.

### Part 5 — column metadata is stored as msgpack, not as JSON text

PRD-022 shipped column metadata encoded as **JSON text**. That was the right
first move and is the wrong long-term storage, and Part 4 is what forces the
issue: the moment metadata carries structure — row ranges, counts, a reason —
rather than three short strings, the encoding starts to matter.

**This is a consistency fix, not a new idea.** `TTTRMask` already offers both
and its header already makes the argument:

> *One JSON integer **per event**: a 10 M-photon mask is ~20 MB of decimal text.
> Use `to_msgpack` for anything at photon scale.*
>
> *The mask is already stored 64 events to a word; msgpack's `bin` type lets
> those bytes travel as bytes, so the payload is `size/8` bytes plus a small
> header rather than the ~2 bytes per event `to_json` costs.*

The same reasoning applies to a column description, for four reasons in order of
weight:

1. **Types survive.** JSON has one number type. An integer row index, a count,
   a flag and a float all come back as doubles and have to be re-inferred —
   msgpack keeps `int` an `int`. For `na` ranges that is the difference between
   a row boundary that is exact and one that is exact *up to 2^53*.
2. **Binary values need no base64.** A `.set` sidecar is base64-encoded into a
   header tag today purely because the tag is text; `bin` carries bytes as
   bytes. That is the same 33% tax `TTTRMask` cites, and it removes a whole
   class of "why is this string 4 MB".
3. **The directory is read on every open.** A `.dstore`'s metadata is parsed
   whether or not a caller ever looks at it, so its parse cost is on the open
   path, not on an accessor.
4. **It is smaller.** Measured on realistic column metadata (name, units, an
   MMFDB item and two `na` ranges): **117 bytes as JSON text, 88 as msgpack** —
   25%, and the gap widens with structure rather than narrowing.

**No new dependency.** `nlohmann::json`, already vendored, has
`to_msgpack` / `from_msgpack` built in. The *value model* stays what it is —
objects, arrays, integers, floats, strings, binary — and only the encoding
changes. That is what keeps this a storage change rather than an API change.

**The API keeps a text face.** `metadata()` and `set_metadata()` continue to
take and return **JSON text**, because a string crosses four bindings with no
typemap, and because it is what a human reads in a debugger and a diff. msgpack
is how it is *stored*, not how it is *asked for*. One thing does want fixing
while here: `set_attribute(key, value)` takes a string value, so structured data
has to be double-encoded — today `set_attribute("na", "[[2,4]]")` really stores
the *string* `"[[2,4]]"`, verified. A typed setter alongside it removes that.

**Format versions.** `.dstore` goes v2 → v3, and a v2 reader path stays; PRD-022
already built the machinery to test an older version against the current writer
(`test_store_file.py::_downgrade_to_v1`), so the same walk covers v2. For HDF5 —
where Part 4 found metadata is dropped entirely — an opaque byte attribute takes
msgpack directly and needs no text escaping at all.

### Part 6 — write down how a table is actually laid out

The multi-column model is not documented anywhere a caller can find it, and it
is not obvious. `doc/saving-tables.rst` gains it, for both formats side by side:

**`.dstore` is one blob per column, addressed by a directory at the tail.**

```
[48-byte header] [blob] [blob] ... [directory]

node:    label, n_rows, row_mask{n_bits, blob}, n_columns, [column],
         n_groups, [name, node]
column:  name, type, n, flags, data blob, mask blob, dictionary blob
blob:    u64 offset, u64 bytes          -- 8-byte aligned, always
```

Nothing is interleaved and nothing is row-major: a column's values are one
contiguous run in its own dtype. A column is up to **three** blobs — its data,
its validity mask, and for text its dictionary. So reading a subset costs one
seek to the tail for the directory plus one read per wanted blob, and the rest
of the file is never touched.

Measured on a 1M-row burst table of four columns, 28.0 MB, counting bytes and
not seconds: the whole table moves 28 000 000 bytes, `columns=["Tau", "E"]`
moves 16 000 000, `columns=["Tau"]` moves 8 000 000. Exactly what was asked for
and nothing else. **HDF5 has no equivalent today**, and closing that is Part 2.

**The two things that surprise people, both verified:**

1. **`columns=` matches by name in EVERY node of the tree, not just the root.**
   A tree whose root, `g1` and `a/b` each hold a `Tau` gives all three back —
   with their own row counts (4, 9, 2), because they are different tables that
   happen to share a column name.
2. **The tree comes back whole either way.** The structure *is* the directory,
   so filtering columns costs nothing structural: a node with no matching
   column comes back empty but keeps its `n_rows`, and is distinguishable from
   a table that really has no rows.

**HDF5 is the same shape** — an HDF5 group holding one 1-D dataset per column,
a sub-group per child — which is why a table written there loads with no
conversion and no transpose. The subset read is missing from the *API*, not
from the format, and Part 2 is what closes that.

**Why not row-interleaved**, since a table suggests rows. Measured on the same
1M-row, 6-column table (40 MB, 40-byte row):

| | columnar | interleaved |
|---|---|---|
| one column, every row | 8 MB | 40 MB — strides over every row |
| 50 rows, every column | 2 kB, 6 reads | 2 kB, **1** read |

The page costs the same *bytes* either way — the directory gives an offset and
a length per column — so interleaving would save five `pread`s and nothing
else, against 5× on the column scan. And a column scan is what this library
does: a histogram fill reads one or two columns over every row, a gate reads
one and writes a bitmask, every burst feature is the same shape. On top of
that a `DataStore` is already one typed vector per column (so interleaving buys
a transpose in both directions), columns have different widths (so a row is a
padded or unaligned struct, and the mmap-a-column path the layout reserves stops
being possible), and bit-packed masks and dictionary-encoded text are defined
*along* a column.

Where columnar genuinely costs is **appending rows** — which is Part 4's
`concat`. If append ever has to happen in the file rather than in memory, the
answer is **row groups**, columnar within a chunk of rows as Parquet and ORC
do, because that keeps the column scan and adds the append. Not interleaving.
Recorded here so it is not re-argued; the rationale is also in `io_store.h`
beside the other four format trades.

### Part 7 — one path convention

Group paths have **no leading separator**, anywhere, matching
`DataStore.group_paths()` and `DataStore.__getitem__`. `""` is the root. A
leading or trailing separator is accepted on input, as the C++ walker already
does, and never produced. `hdf5_table_groups` keeps its current spelling for
compatibility; `table_groups` is the one that is consistent.

## Acceptance criteria

1. `read_table` reads a whole tree identically from `.dstore`, HDF5 and PTO,
   for a tree with nested groups, every dtype, a dictionary-encoded text column,
   a validity mask and a label — allowing for what a format genuinely cannot
   carry (HDF5 has no bool and no label; both are documented today).
2. `read_table(spec, group=g)` returns the same store as
   `read_table(spec).group(g)` for all three formats.
3. `read_table(spec, columns=[...])` returns those columns, in that order, for
   all three, and **reads only them**. Asserted with a byte counter, not a
   timer: a wall clock on a warm page cache measures the cache. `.dstore` and
   PTO already have one — `store_bytes_read()`, and PTO's payload goes through
   the same reader — so **HDF5 needs the matching counter**, which is a
   deliverable of this PRD and not an afterthought. Without it the central
   claim is unprovable for exactly the format it is new in.
4. `read_table(spec, first_row=, n_rows=)` returns that row window for all
   three, each table clamped to its own length, and reads only it — same
   evidence as criterion 3.
5. `table_groups` returns the same list for the same tree written to all three
   formats — same order, same spelling, no leading separator.
6. `table_columns` and `table_has` agree across the three.
7. `write_table(spec, store)` followed by `read_table(spec)` is the identity for
   all three, for the tree in criterion 1.
8. `write_table(spec, store, group=g)` leaves the file holding `store` at `g`
   and its siblings unchanged — **in all three**, whatever each does underneath
   (`.dstore` rewrites, HDF5 replaces the group, PTO replaces the object). The
   caller is not told which, and the result is the same file contents either
   way. This is Part 0's rule as a test.
9. **No call but `write_table` changes a file.** Asserted rather than assumed:
   take a hash of the file, run every read and query in the vocabulary against
   it, and the hash is unchanged.
10. An unsupported combination is refused **by name** and never silently
   approximated: the message says which format and which capability.
11. The registry publishes the per-format capabilities, so a caller can ask
    rather than try — same shape as `ranged_reads` and `params_schema` on
    `file_container`. It includes **whether a write rewrites the file**, which
    is the one cost Part 0 makes invisible and which a caller with a 4 GB file
    is entitled to know before it writes rather than after.
12. **The substitution test.** One tree written to `.dstore` and to HDF5 answers
    every call in the vocabulary identically, and the only differences are the
    four enumerated in Part 3. Written as one parametrised test over the format,
    so a new format joins by being added to the parameter list.
13. `concat(axis="rows")` of N stores read from N files gives one table: the
    column order of the first, `n_rows` the sum, dtypes preserved, and a
    dictionary column's labels merged rather than renumbered wrongly.
    `join="inner"` gives the intersection.
14. **A column missing from one store keeps its dtype.** An `int64` column
    present in two of three stores comes back `int64`, with the third store's
    rows marked not-measured — not `float64` carrying `NaN`. That is the one
    thing a store can do here that a data frame cannot, and it is the reason to
    have written `concat` rather than kept using pandas.
15. **A concat-introduced gap costs metadata, not a bit per row.** After
    concatenating 20 stores of 1M rows where a column appears in one,
    `column.has_mask()` is false, `column.metadata()` carries the range, and
    `valid(i)` answers correctly on both sides of the boundary. `nbytes()` shows
    the mask was never allocated. The range names the store it came from, so a
    caller can ask *why* a row is missing and not only *whether*.
16. A genuinely scattered pattern — `mask_non_finite()` on a column with NaNs
    dotted through it — still materialises a bit mask, and `valid(i)` gives the
    same answers either way. The representation is an implementation detail
    behind one interface, not two kinds of column.
17. **Column metadata survives HDF5.** `units`, and every other key, round-trips
    through `write_table` / `read_table` for all three formats. It does not
    today — HDF5 returns `''` — which stops PRD-022 at the `.dstore` boundary
    and would silently drop the `na` ranges above.
18. Writing NA-as-ranges to a format that cannot carry them materialises the
    mask instead, so `valid(i)` is preserved even where the reason is not.
19. **Column metadata is stored as msgpack and its types survive.** An integer
    written as an integer reads back as one, not as a double; a binary value
    round-trips without base64. `metadata()` still returns JSON text, so no
    caller changes.
20. **A v2 `.dstore` still reads.** Asserted against a v2 file built from the
    current writer by the same directory walk PRD-022 used for v1, so the
    fixture cannot rot.
21. `concat(axis="columns")` puts N stores side by side; a row-count mismatch
    and a duplicate column name each **refuse and name what clashed**, rather
    than skipping the file or dropping a column silently, which is what the
    downstream does today.
22. `store.take(indices)` and `store.compact()` give a store whose rows are the
    ones asked for, with dtypes, validity masks and dictionary encodings intact,
    and which writes back out through `write_table` unchanged.
23. Everything above is reachable from Python, R, Java and JavaScript and
    appears in the conformance case list, per PRD-015. One case list, three
    formats, parametrised over the format: that is the test that the formats are
    interchangeable.
24. The multi-column layout of both formats is documented in
    `doc/saving-tables.rst`, including the two behaviours verified in Part 6 —
    `columns=` matching per node, and the tree coming back whole.
25. Nothing regresses: `load_store`, `save_store`, `read_hdf5`, `write_hdf5`,
    `pto_store` and their listings behave exactly as today.

## Risks and non-goals

* **`.dstore` cannot write one group in place, and under Part 0 it does not have
  to.** The format is blobs streamed forward with the directory last and no
  seeking, which is what makes it fast to read; replacing a group means
  rewriting the file. That is a *cost*, not a missing capability — the result is
  the same file contents HDF5 and PTO would produce, and no caller has to branch
  on it. Where it matters is size: rewriting a 4 GB `.dstore` to change a
  one-row `meta` group is the case to reach for **PTO** in, which replaces one
  object's payload in place and exists for exactly that.

  What this PRD must not do is hide the difference *silently*. The registry says
  which formats rewrite (criterion 11), so a caller that cares can ask before it
  writes rather than discovering it from a disk-full.
* **Not a common file handle.** Six one-shot calls, each opening and closing.
  A viewer paging a large file wants a handle held open, and that is a separate
  change with a separate cost — mentioned here so it is not mistaken for an
  omission.
* **Not a lowest common denominator.** The point is to raise every format to the
  same capability, not to expose only what all three already do. Where a format
  genuinely cannot (the bullet above), it declines by name and the registry says
  so in advance.
* **`concat` is not a join, and `take` is not an index.** Stacking rows of the
  same columns, and materialising a row subset. No keys, no alignment, no
  ordering guarantee beyond the one asked for. The rest of the frame API that
  `BUGS.md` lists — `to_numeric`, group-by over a dictionary column, `argsort`,
  column insert and rename — is deliberately **not** here: each is small and
  local, none of them blocks the file layer, and bundling them would make this
  PRD about replacing pandas rather than about making two formats look alike.
* **Not a new file format, and no format change.** Every byte written stays what
  it is today; this is only the way in.
* **`hdf5_table_groups`'s leading slash is not changed.** It is published
  behaviour with tests; the new vocabulary is where the convention is
  consistent, and the two are documented against each other.

# PRD-019 — Data groups: a store is a tree, in memory and in the file

> **PRD #:** 019 · **Status:** ✅ Implemented · **Created:** 2026-08-06 · **Updated:** 2026-08-06 · **Owner:** tpeulen
>
> All 21 criteria are met. Criterion 21 was the last one open: it needed the
> PRD-015 conformance suite to exist and needed R and Java to wrap `DataStore`
> at all. Both landed; the eleven `datastore.*` cases in
> `test/conformance/cases/datastore.json` now run green from Python, R, Java
> and JavaScript.

## Summary

A `DataStore` is one flat table, and the HDF5 writer can put one table in a
file — it takes a `group` argument and then truncates the whole file on every
call, so writing a second group destroys the first. Both halves of that are the
same missing idea.

Give `DataStore` **named child groups**, each a full store with its own columns,
row count and selection. The file format then stops being a separate concept: an
HDF5 group *is* a `DataStore` group, and writing a file is writing the tree. The
single-table case is unchanged — a store with no children serialises exactly as
it does today.

## Problem / motivation

### One table is not the shape of the data

The things this library is asked to store are almost never a single table:

| What | Shape |
|---|---|
| per-pixel imaging results | a `results` table (one row per pixel) **plus** a `meta` back-reference to the photon file |
| a burst export | a per-photon table **plus** a per-burst summary — different row counts, same measurement |
| a fit result | the chain, the per-dataset residuals, the parameter summary |
| a multi-channel measurement | one table per detection channel, sharing a header |

Each is several tables that belong together and are meaningless apart. Today the
caller either invents a file-naming convention and loses the association, or
keeps a data-frame HDF5 layer around for its multi-key files — which is what
still ties several downstream writers to a native dependency this library could
replace outright.

Row counts differ between those tables, which is exactly why they cannot be one
store: `results` has one row per pixel, `meta` has one row.

### The writer destroys what it does not write

`write_hdf5_table` accepts a group and then creates the file with
`H5F_ACC_TRUNC` (`modules/io/hdf5_table/src/io_hdf5_table.cpp:403`):

```python
write_hdf5_table(p, table, "/", 0)      # -> True
write_hdf5_table(p, meta,  "/meta", 0)  # -> True
read_hdf5_table_columns(p, "/")         # -> ()     the table is gone
read_hdf5_table_columns(p, "/meta")     # -> ('source_tttr',)
```

Both calls report success. Nothing says the second destroyed the first, and
nothing lets a caller ask what a file contains.

### The reader is already there

Measured by building the target layout with a third-party HDF5 writer, since this
library cannot yet produce it — a root table with a `/meta` sub-group beside it
reads back correctly, the sub-group ignored rather than confusing the root:

```python
read_hdf5_table_columns(p, "/")      -> ('Tau', 'X pixel')
read_hdf5_table_columns(p, "/meta")  -> ('source_tttr',)
read_hdf5_table(p, "/")              -> DataStore(6 rows, 2 columns)   int32 kept
```

So the file work is write-side plus one strictness change. The larger half of
this PRD is the in-memory model.

**Adjacent, deliberately not in scope:** a text column serialises with its labels
materialised rather than dictionary-encoded (the file comes out *larger* than the
data-frame writer's), and there is no reader for the legacy data-frame layout.
Both are real, both are tracked separately, neither is needed here.

## Goals

**In memory**

- A store may hold named child groups; each child is a full store — its own
  columns, row count, validity masks, selection and label.
- Groups nest, and are addressable by path.
- A handle to a group stays valid when siblings are added.
- Memory accounting, iteration and the registry understand the tree.

**In the file**

- One call writes a whole tree; one call reads it back, identical.
- Writing one group of an existing file leaves the others intact.
- Writing a group that exists replaces it entirely — no columns survive from its
  previous contents.
- A caller can enumerate what a file holds without reading it.
- A read of a group that holds no table is distinguishable from a read of a table
  with no rows.
- A failed write never replaces a good group with a partial one.

**Everywhere**

- The API stays plain enough for SWIG to generate for Python, R, Java and
  JavaScript without hand-written typemaps.
- A store with no children behaves exactly as today, in memory and on disk.

## Non-goals

- **A relational model.** Groups are named tables in a tree. No foreign keys, no
  joins, no cross-group query.
- **Cross-group row alignment.** Two groups with the same row count are not
  thereby related; nothing checks or maintains that.
- **Appending rows** to an existing on-disk table. Several tables in one file is
  this PRD; growing one needs chunked extendible datasets and is its own change.
- **Concurrent writers.** HDF5 without SWMR is single-writer. Document it, do not
  defend it.
- **HDF5 attributes as a metadata channel.** A one-row table in its own group
  covers the need and round-trips through the same code path as everything else.
- **Reading foreign layouts** (data-frame HDF5, Photon-HDF5). Separate.

## Part 1 — a store is a tree

### Model

A `DataStore` gains an ordered, named collection of child stores. A store is
therefore both *a table* (its own columns) and *a container* (its groups); either
may be empty. A store holding only groups and no columns is the normal shape for
a root that just associates several tables.

Each group keeps everything a store keeps, **independently**:

| Per group | Why it cannot be shared |
|---|---|
| columns | the point |
| `n_rows` | `results` has one row per pixel, `meta` has one |
| row mask / selection | follows from differing row counts |
| per-column validity masks | belong to the column |
| label | a group is a thing a user names |

### API

```cpp
// -- containment
int          n_groups() const;
bool         has_group(const std::string& path) const;
DataStore&   group(const std::string& path);              // throws if absent
const DataStore& group(const std::string& path) const;
DataStore&   add_group(const std::string& name);          // empty; throws if it exists
DataStore&   ensure_group(const std::string& path);       // get or create, nesting as needed
bool         remove_group(const std::string& path);       // false when absent
std::vector<std::string> group_names() const;             // direct children, in order
std::vector<std::string> group_paths() const;             // every descendant, depth-first
void         clear_groups();
```

`add_group` takes a *name* (no separator). `group`, `has_group`, `ensure_group`
and `remove_group` take a *path*, and so reach descendants.

### Reference stability is a requirement, not an implementation detail

**A handle to a group must survive the addition of another group.** This is
stated as a requirement because the equivalent bug has already shipped once in
this library: columns lived in a `std::vector<Column>` and `add()` handed out a
reference into it, so the next `add()` reallocated and every previously handed
out `Column` read freed memory — reporting an empty name and an empty array
rather than raising. The symptom was a column that silently went blank, which no
assertion catches. It was fixed by moving to a container whose references survive
an append; the same must be true of groups from the start.

```python
g = store.add_group("results")
for i in range(50):
    store.add_group(f"other{i}")
g.n_rows()          # must still be the results table, not garbage
```

Removing a group may invalidate handles into the removed subtree — unavoidable
and expected — but must not invalidate handles to unrelated siblings.

### Paths

| Given | Means |
|---|---|
| `""`, `"/"` | this store |
| `"results"`, `"/results"` | the child `results`; a leading separator is optional |
| `"a/b"` | nested |
| `"a/"` | same as `"a"` |

Rejected by throwing rather than by silent reinterpretation: an empty component
(`"a//b"`), `.` or `..` as a component, a NUL, and a name containing the
separator passed to `add_group`.

### Rules

- **Sibling names are unique.** Adding an existing name throws rather than
  replacing; replacing is `remove_group` then `add_group`, said out loud.
- **Order is insertion order**, preserved by iteration, by serialisation and by a
  round trip. Alphabetical is not order — see the trap in *Notes*.
- **No cycles.** A store cannot be made a descendant of itself; the operation
  that would do it throws.
- **Copy is deep**, move moves. A group is owned by its parent, not shared.
- **Selection does not propagate.** Groups have different row counts, so a mask
  cannot be inherited. Provide a recursive *clear* (`select_none_recursive()`, or
  an explicit flag) because that is the one tree-wide operation callers actually
  want — and nothing else.
- **`nbytes()` and `memory_report()` recurse**, the report keyed by group path so
  a caller can see which table is the expensive one.
- **The registry holds roots.** A group is not registered independently; the
  root's entry reports the tree's total bytes, and `data_store_report` gains the
  per-group breakdown rather than a second kind of entry.
- **Column and group namespaces are separate.** A store may have a column and a
  group with the same name; nothing needs to disambiguate, because the accessors
  differ.

### Python surface, and one collision to avoid

`store[...]` already means **column** — `store["Tau"]` is a `Column` and
`"Tau" in store` tests columns. That must not change. A `str` key silently
switching between a column and a group depending on what happens to exist is
exactly the ambiguity that produces a bug report about the wrong thing.

Groups get their own accessors:

```python
store.groups                      # {name: DataStore}, insertion-ordered
store.group("results")            # a DataStore
store.group("a/b")                # nested
store.add_group("meta")
store.group_names()               # ['results', 'meta']
store.group_paths()               # ['results', 'meta', 'a/b', ...]
"results" in store.groups         # containment for groups
```

The same lifetime rule as `__getitem__` applies, for the same reason: a group
handed to Python is a borrowed reference into its parent, so the proxy must keep
the **root** alive (the existing `_store` attachment), or a zero-copy view reached
through a group points at freed memory once the root is collected.

`__repr__` should say so: `DataStore(0 rows, 0 columns, 2 groups, 41.2 MB)`.

### Histogram, profile, selection

These operate on **one** store — the one they are called on. `store.histogram("x")`
never reaches into groups; `store.group("results").histogram("x")` is how a group
is histogrammed. This PRD introduces no cross-group operation.

## Part 2 — the file is the tree, serialised

An HDF5 group is a `DataStore` group. The format needs nothing new: the layout
this produces is the one the reader already handles.

```cpp
enum class Hdf5WriteMode {
    Update,     // create the file if absent; replace what is written; keep the rest
    Truncate,   // replace the whole file
};

// Whole tree in, whole tree out.
bool       write_hdf5(const std::string& filename, const data::DataStore& store,
                      int compression = 4, Hdf5WriteMode mode = Hdf5WriteMode::Update);
data::DataStore read_hdf5(const std::string& filename);

// One group at a time, for adding to an existing file.
bool write_hdf5_table(const std::string& filename, const data::DataStore& store,
                      const std::string& group_name = "/", int compression = 4,
                      Hdf5WriteMode mode = Hdf5WriteMode::Update);
data::DataStore read_hdf5_table(const std::string& filename,
                                const std::string& group_name = "/");

// What does this file hold? Groups with a readable table, in file order, full
// paths ("/" for a table at the root). A non-HDF5 file, or one with no table
// anywhere, gives an empty vector rather than throwing.
std::vector<std::string> hdf5_table_groups(const std::string& filename);
bool hdf5_table_has(const std::string& filename, const std::string& group_name = "/");
bool hdf5_table_remove(const std::string& filename, const std::string& group_name);
```

`write_hdf5(file, store)` writes the store's own columns at the root and each
group into an HDF5 group of the same name, recursively; `read_hdf5(file)` rebuilds
the tree. **Round-tripping a tree gives an equal tree** — group names, order,
nesting, column order, dtypes, validity masks and dictionary columns all
preserved. That single property is what makes the feature usable, and it is the
criterion everything else supports.

`Hdf5WriteMode` should reach the bindings as a plain enum. If that proves awkward
across four backends, `bool truncate = false` is an acceptable fallback — the
semantics matter, the spelling does not.

### Writing

1. **`Update`, file absent** — create it, write. Identical to today.
2. **`Update`, file present and HDF5** — open read-write, replace what is being
   written, leave every other group untouched.
3. **`Update`, file present but not HDF5** — return `false`. Do not truncate
   someone else's file by inference; a caller that means "replace whatever is
   there" asks for `Truncate`.
4. **`Truncate`** — the file is recreated and holds only what is written now.

**Replacement is whole-group.** Writing `/results` with `{a, b}` over a
`/results` that held `{a, b, c}` leaves `c` gone. Merging column-wise is a
caller's decision, never the writer's: a table that silently keeps a stale column
from a previous run is worse than one that lost it, because it looks current.

**Replacement must not half-happen.** Write into a sibling temporary group, then
delete the target and `H5Lmove` the temporary into place, then flush. On any
failure, unlink the temporary and return `false` with the original intact. HDF5
has no transactions; this is as close as the format allows.

> HDF5 does not reclaim freed space in place, so repeatedly replacing a group
> grows the file. That is inherent to the format — `h5repack` is the answer, not
> a cleverer implementation here. Say so in the docstring.

### Reading

- `read_hdf5_table(file, group)` on a group that **does not exist**, or that
  exists and holds no table, **throws**. It currently returns an empty
  `DataStore`, indistinguishable from a table that legitimately has zero rows —
  so a caller that trusts it opens a foreign file as a blank table and reports
  success.
- `read_hdf5_table_columns` keeps returning an empty vector for those cases: it
  is the cheap predicate, and callers already treat empty as "not ours".
- A table with zero rows reads back with its columns and `n_rows() == 0`, and
  must not be confused with the above.

### What counts as a table

A group holds a table when it contains at least one 1-D dataset and all of its
1-D datasets share one length. Sub-groups are not datasets and are ignored, so a
root table with a `/meta` group beside it reads as a table at `/` — that is the
imaging layout and it must work. A group whose 1-D datasets disagree in length is
not a table: `hdf5_table_has` false, read throws.

## Compatibility

- **A store with no groups is unchanged**, in memory and on disk. Every existing
  file is read by the new code, and every file the new code writes for a
  childless store is readable by the old.
- **The write default changes from truncate to update.** For every file this
  library has ever written the two are indistinguishable — a single-group file is
  the only thing it could produce, and rewriting that group replaces its contents
  either way. The observable differences are exactly the broken cases: a second
  group survives, and a non-HDF5 file is refused rather than silently replaced.
- **`read_hdf5_table` throwing is the riskier half.** Known callers already cope:
  the companion viewer's reader wraps it in `try`/`except` and then checks
  `len(columns) < 1`, taking the same branch either way. Land it with the rest and
  note it in the changelog; if it proves disruptive, a `read_hdf5_table_or_empty`
  is a cheaper retreat than reverting the PRD.
- **ABI.** Adding members to `DataStore` breaks binary compatibility with existing
  builds; sequence this with PRD-018 rather than against it.

## Acceptance criteria

**In memory**

1. A store holds groups; each keeps its own columns, row count, selection and
   label, and a group whose row count differs from its parent's is normal.
2. A handle to a group survives 50 subsequent `add_group` calls — name, row count
   and data intact. (The columns-vector bug, not repeated.)
3. Nested paths, an optional leading separator and a trailing separator address
   what the *Paths* table says; the rejected forms throw.
4. A duplicate sibling name throws; `ensure_group` is idempotent and creates
   intermediates.
5. `group_names()` is insertion order, not alphabetical; `group_paths()` is
   depth-first and complete.
6. Copy is deep: mutating the copy's group does not touch the original's.
7. Making a store its own descendant throws.
8. `nbytes()`/`memory_report()` include descendants, keyed by path; the registry
   lists the root once, with the tree's total.
9. `store["name"]` still returns a **column**, and a store carrying a column and a
   group of the same name is unambiguous.
10. A group proxy handed to Python keeps the root alive; a zero-copy view taken
    through a group outlives the last reference to the root.

**In the file**

11. `write_hdf5` then `read_hdf5` on a tree gives an **equal tree**: names, order,
    nesting, column order, dtypes, validity masks, dictionary columns.
12. Writing `/results` then `/meta` leaves both readable, with columns in the
    order they were added. This is the case that fails today.
13. Writing `/results` again replaces it — a column present only in the first
    write is gone — and `/meta` is untouched.
14. `hdf5_table_groups` returns both, in file order; on a data-frame-written or
    non-HDF5 file it returns empty without throwing.

    **Met, with "file order" pinned down.** A tree written in ONE call keeps its
    insertion order, because the writer records it in a `groups` attribute
    (`datastore.tree_roundtrip`). Siblings written by SEPARATE `write_hdf5`
    calls come back **alphabetically**: neither call touches the root, so no
    attribute records their order and the listing falls back to what HDF5
    reports by name. `datastore.table_groups_listing_order` writes `/zulu` then
    `/alpha` and pins `["/alpha", "/zulu"]`, so the behaviour is stated rather
    than left to the reader of "file order". Making the incremental case
    preserve write order would mean the writer updating a root attribute on
    every call — a change to the on-disk convention, and out of scope here.
15. `hdf5_table_has` agrees with `hdf5_table_groups` for every group.
16. `read_hdf5_table` throws on an absent group and on a group with no datasets,
    and returns a zero-row store for a table with columns but no rows.
17. `Truncate` on a multi-group file leaves only what was just written.
18. `Update` on a non-HDF5 file returns `false` and leaves it **byte-identical**.
19. A write that fails partway (a read-only file, or a forced column-write
    failure) leaves the previous group readable and unchanged.
20. Row selection still applies: a gated store writes only its selected rows, per
    group.

**Everywhere**

21. The same assertions run from Python, R, Java and JavaScript in the conformance
    suite (PRD-015) — this is new public API on all four.

    **Met.** `test/conformance/cases/datastore.json` holds eleven cases covering
    criteria 2, 3, 4, 5, 9, 11, 12, 13, 14, 16 and 20, and all four runners pass
    them. Getting there needed three things beyond the cases themselves:

    * `DataStore.i` and `Hdf5Table.i` added to the R and Java modules (and
      `Hdf5Table.i` to the JavaScript one, which had a note saying to add it once
      the reader landed);
    * `%VIEW_INTO` accessors in `ext/java/helpers.i`, because `jarrays.i` defines
      no ARGOUTVIEW typemaps and a column was otherwise unreadable from Java;
    * a keep-alive for group proxies in R and Java, matching the Python one.
      Criterion 10 called this Python-only "by nature"; it is not. Both languages
      reproduced the use-after-free — R's `n_rows()` answered from freed memory
      after one `gc()` — and both now hold the root from the proxy.

## Test plan

- C++ unit tests for 1–8 and 11–20, beside the existing `hdf5_table` tests.
- Python tests for all of it; they are the reference implementation of the
  assertions, and 9–10 are Python-only by nature.
- Conformance entries for 2, 11, 14 and 16 — enough to prove the marshalling of
  the new container, the tree round trip, the string vector and the throw.
- One test that pins *why this exists*, readable as documentation: build the
  imaging shape (a `results` table of one row per pixel plus a one-row `meta`
  back-reference) as one store, write it in one call, reopen it and assert both
  halves — then rewrite `results` alone and assert `meta` survived.

## Notes for whoever implements it

- **Column order is link-creation order, and it is easy to lose.** A table written
  by this library reads back in the order its columns were added, while the same
  datasets written without creation-order tracking read back *alphabetically* —
  measured: a file built by a third-party writer with `X pixel` before `Tau` reads
  as `('Tau', 'X pixel')`. Whatever property list turns that on must be set on
  **every** group the new paths create, including nested intermediates and the
  temporary group that is moved into place. `H5Lmove` is where this will silently
  regress. The same applies to *group* order, which criteria 5 and 11 require.
- **Pick the group container for reference stability first** (criterion 2), not
  for lookup speed. Group counts are small; a node that can be reallocated out
  from under a handle is the failure this library has already had once.
- `H5Fis_hdf5` decides write case 2 vs 3. Do not infer it from `H5Fopen` failing —
  that also fails on a permission error, and truncating then would destroy a file
  the caller cannot even read.
- `H5Ldelete` the target group before the `H5Lmove`: deleting datasets one by one
  leaves the group behind, and `H5Gcreate2` on an existing group fails.
- `H5Pset_create_intermediate_group` is needed for nested paths, or `/a/b` fails
  when `/a` does not exist.
- The `QuietHdf5` guard already in the file should wrap the new paths: the queries
  must not print to stderr when handed a foreign file, because probing is a normal
  thing for a caller to do.
- The compression default is worth revisiting while this file is open — level 4
  costs 2.58 s against 0.08 s on a 1M-row numeric table to save 8% of the file,
  and these files are written once and read repeatedly — but changing it is not
  part of this PRD.

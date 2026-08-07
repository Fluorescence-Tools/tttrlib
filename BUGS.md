# Known bugs

Found from outside the library, with a reproduction each. Anything fixed moves
to the changelog and leaves here.

*Nothing open.*

---

## FIXED — `Column.numpy()` hands out a view that does not keep the `DataStore` alive

> **Fixed 2026-08-07.** Both halves of the suggested fix, because the first
> alone does not close it:
>
> * The owner is now an object that is **not** an ndarray
>   (`_DsBuffer` in `ext/python/datastore_support.py`), so numpy's chain
>   collapse stops at something that owns the buffer. The report is right that
>   the collapse then works in the library's favour: the root of every derived
>   view is the owning object.
> * **Every** way of getting a column carries the store, not just the two that
>   went through `DataStore.py`. `store.column(0)` and
>   `store.column_by_name("x")` are wrapped C++ with no link back at all, and
>   still dangled with the first half in place — they now get the same
>   `TTTRLIB_DS_KEEP_ROOT` append as `group`/`add_group`/`ensure_group`.
>
> The owner is the `Column` proxy rather than the `DataStore` the report
> suggests, and reaches the store through `Column._store`; with the second half
> above that link now always exists, so the chain
> `array → _DsBuffer → Column → DataStore` holds for every accessor.
>
> The reproduction below is a test —
> `test/python/test_datastore_paths.py::test_a_csv_column_survives_the_store_it
> _was_read_from`, run eight times as filed — beside one per accessor and one
> asserting the root of a collapsed chain still owns the buffer.
>
> Zero-copy is unchanged: writing through `np.asarray(col)` still reaches the
> C++ buffer.
>
> Kept here rather than deleted because the analysis is the useful part, and
> the same trap is one `ARGOUTVIEW` away in any other binding.

**Found:** 2026-08-07 · **Severity:** silent wrong data · **Affects:** `DataStore`
(any store, `.dstore` and CSV-read alike) · **tttrlib 0.27.0, macOS arm64,
Python 3.12, numpy 2.x**

`Column.numpy()` returns a zero-copy view into the column's buffer — which is
the point of a store, and is documented as such. What is missing is the
ownership link: **no object in the returned array's base chain owns the memory
or references the store**, so the array is a dangling pointer the moment the
`DataStore` is collected.

It does not raise. It returns plausible numbers with occasional wrong ones.

### Reproduction

```python
import gc, tempfile, pathlib, numpy as np, tttrlib

d = pathlib.Path(tempfile.mkdtemp()); p = d / "t.csv"
expected = np.arange(1000, dtype=float) * 3.0
p.write_text("a\tb\n" + "".join(f"{v}\t{v * 2}\n" for v in expected))

def read():
    store = tttrlib.read_csv(str(p), delimiter="\t")
    # np.asarray, not np.array: with a matching dtype this does NOT copy.
    return {store[i].name(): np.asarray(store[i].numpy(), dtype=float)
            for i in range(store.n_columns())}          # <- store dies here

got = read()
gc.collect()
print((~np.isclose(got["a"], expected)).sum(), "values wrong")
```

Eight runs of exactly that: `1, 1, 0, 0, 1, 1, 1, 1` values wrong. Always at
**row 2**, which the file gives as `6.0`, read back as either `0.0` or
`6.001000000000001e-05` — a partially overwritten double, i.e. reused memory.

Downstream, the same defect read **84 of 154 rows** of a burst table's
`First Photon` column as `3.3e-319` instead of `2755`.

### What the base chain shows

```python
x = store[0].numpy()
y = np.asarray(x, dtype=float)      # matching dtype

x.flags["OWNDATA"]        # False
x.base.flags["OWNDATA"]   # False   <- nothing in the chain owns the buffer
x.base.base               # None
np.shares_memory(x, y)    # True
y.base is x               # False
y.base is x.base          # True    <- numpy COLLAPSES the chain
```

The last line is why the bug is intermittent rather than constant. numpy
shortcuts a view-of-a-view to the root, so a derived array does not even keep
the array it was derived from alive — and since the root does not own the
memory either, there is nothing anywhere holding the store. Whether a given
expression corrupts is then down to allocator timing, which is the worst
possible failure mode: `store[i].numpy()` alone looked correct in every trial,
and `np.asarray(store[i].numpy(), dtype=float)` — the same memory, one extra
temporary — corrupted in six trials of eight.

### Ruled out

* **Not a parser race.** With `threads=1`, and with the array copied
  immediately, 25 threaded reads and 10 single-threaded reads of the same file
  gave zero wrong values. The data written into the store is correct.
* **Not specific to the CSV reader.** It is a property of `Column.numpy()`; the
  reader only makes it easy to hit, because the store is usually a temporary.
* **Not the documented `Column`-proxy invalidation.** That is about a *proxy*
  going stale across a structural change, and is worked around by re-fetching.
  This is the *array*, after the store is gone, with no structural change at
  all.

### Suggested fix

Give the returned array an owner: set its `base` to the Python object that keeps
the store alive (the SWIG proxy for the `DataStore`, not for the `Column` —
the column is itself borrowed), so the buffer cannot outlive its owner. numpy's
chain-collapsing then works in the library's favour rather than against it,
because the root of every derived view is the owning object.

Until then the contract is "copy or keep the store", and it has to be *said* —
the current docstring advertises the zero-copy view without the lifetime that
makes it safe.

### Workaround in use downstream

`np.array(..., copy=True)` for anything that outlives the store, plus a test
asserting the arrays survive their store. Note that `np.asarray(x, dtype=...)`
is **not** a copy when the dtype already matches, which is exactly how this got
into shipped code.

**No longer needed.** `chisurf/core/datastore.py:207` carries the warning and
the copy rule; both can go once the downstream pins a tttrlib with the fix.
The copy is not free — it is the one on the largest array in the process.

---

# Enhancements

Not defects — things a downstream migration needs and cannot express today.
**Rewritten 2026-08-07 from evidence rather than prediction**: the first version
of this list was written before migrating any consumers, and the migration
disagreed with it. What follows is what actually cost time.

## Landed since this list was first written

`concat` / `append_rows` / `append_columns`, `take` / `compact`, the column
lifetime fix, `container_read_records` / `container_read_events` /
`decode_records`, na-ranges and column descriptions.

**The prediction was half right.** `concat` *was* the item that changed the
shape of the migration — but only for the **file layer**. With it, one downstream
plugin (burst fusion: core, driver and view-model) went from frames to stores
end to end, and the burst reader now returns a store. That half is done and it
worked as argued.

**It was wrong about the consumer layer**, which is where the remaining cost
actually is, and the blocker there was not on the list at all.

## The blocker that matters now: a `Column` is not array-like

`np.asarray(column)` and `len(column)` work. Nothing else does:

```python
column[0]          # TypeError: 'Column' object is not subscriptable
column[1:3]        # TypeError
column > 1         # TypeError: '>' not supported
list(column)       # TypeError: not iterable
```

So every consumer that touched `frame[name]` as a value has to be rewritten to
take `np.asarray` first — not because the arithmetic changes, but because the
*handle* does not behave like the thing it replaced. Counted across the files
still holding frames in the downstream package:

| Idiom that needs a column to behave like an array | calls | files |
|---|---|---|
| `col.to_numpy(...)` | 31 | 10 |
| `col[i]` / `col[a:b]` | 17 | 4 |
| `col > x`, `col == x` (building a mask) | 5 | 2 |
| `col.map(fn)` | 3 | 2 |

That is ~56 mechanical edits whose only purpose is to insert a conversion. Every
one of them is a place a reader will later ask "why is this wrapped?".

**Element access and comparison would remove almost all of it.** A column that
supports `__getitem__`, `__len__`, `__iter__` and rich comparison returning a
bool array is the difference between "swap the reader" and "rewrite every
consumer". `map` is not needed — `np.asarray(col)` plus a comprehension is
honest — but indexing and comparison are used everywhere and have no readable
substitute.

## After that, in the order they were hit

| Operation | calls | files | Note |
|---|---|---|---|
| **`DataStore.copy()`** | 28 | 13 | `frame.copy()` before mutating. `take(range(n))` is the workaround and says nothing about intent. |
| **row selection returning a store** (`loc`/`iloc` shaped) | 36 | 8 | `take`/`compact` cover it; what is missing is a *readable* spelling at the call site. |
| **group-by over a dictionary column** | 6 | 5 | Unchanged from the first list. |
| **`argsort` / `sort_by`** | 4 | 2 | |
| **`rename_column`, `insert_column(position)`** | — | — | `insert(0, "source", …)` prepends a provenance column before writing; `add` appends only. |
| **`to_numeric(column)`** setting the mask rather than raising | — | — | Mostly evaporated: the CSV reader already types columns, so what is left is coercing text that arrived from elsewhere. |

## What the migration confirmed about the file layer

Worth recording because it was the argument for all of this, and it held:

* an `int32` column survives an **outer join** where a frame must widen to
  `float64` to hold the `NaN` and cannot recover the dtype;
* columns line up **by name**, which is what a burst folder needs — two runs
  need not have written them in the same order;
* a dtype conflict is **refused and named** rather than promoted silently;
* ranged reads compose: `container_read_records` + `decode_records` from record
  0 with a carried state gave macro times **identical** to a whole-file read on
  a 174 438-event SPC-130 file.

## `.dstore` specifically

Nothing missing for the migration: `save_store` / `load_store` already keep
column order, dtypes, dictionary-encoded text, validity masks, labels, the group
tree and the row selection. Two notes from using it:

* **It is the right default for anything only this ecosystem reads** — measured
  downstream at a wash against uncompressed HDF5 on bulk I/O and dramatically
  faster than compressed. What keeps HDF5 in the picture downstream is that the
  burst and imaging files are *interchange* formats read by other programs.
* **The column-lifetime defect above is fixed**, and `.dstore` was where it
  would have bitten hardest: `load_store` is exactly the call whose result a
  caller lets go of after pulling arrays out of it.

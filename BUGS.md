# Known bugs

Found from outside the library, with a reproduction each. Anything fixed moves
to the changelog and leaves here.

---

## `Column.numpy()` hands out a view that does not keep the `DataStore` alive

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

---

# Enhancements

Not defects — things a downstream migration needs and cannot express today.
Counted from the 27 files of one downstream package still holding tables as
DataFrames, which is the population being migrated onto `DataStore`.

## What a `DataStore` needs before a table layer can stop being a frame

The migration has moved every *file* boundary onto the store (HDF5 and CSV, read
and write) and has stopped there, because the operations below have no
equivalent. Each line is the number of call sites blocked, in the shipped
package, not a guess:

| Operation | Blocked call sites | Note |
|---|---|---|
| **`concat` / append rows** | **24 in 13 files** | The biggest single blocker after construction. Same columns, stacked — a burst folder is read per measurement and combined. Today the only way is to build one frame per file and concatenate. |
| **coerce a column to numeric, invalid → missing** | **43 in 10 files** | `to_numeric(errors="coerce")`. Half of these disappear on their own — the CSV reader already types a column, so what is left is coercing a *text* column that arrived from elsewhere. A `to_numeric(column)` that sets the validity mask rather than raising would cover the rest. |
| **`take` / `compact`** — realise a selection into a new store | 9 (`dropna`) + every filtered export | A selection is expressible as a mask and cannot be *materialised*. Listed in the downstream PRD as T4 and still the one that makes filtering a table impossible without a frame. |
| **group-by over a dictionary column** | 7 in 6 files | Mostly `codes` + `bincount`, which is exactly why it belongs here: every consumer hand-rolling that loop is how the codes get copied around. |
| **`argsort` / sort by column** | 4 in 2 files | Also the table widget, which sorts through a per-column numpy array today — fine for one column, not for a stable multi-column sort. |
| **insert a column at a position** | 4 in 3 files | `insert(0, "source", …)` — a provenance column prepended before writing. `add` appends only. |
| **rename a column** | 2 in 1 file | |
| **iterate rows** | 9 (`itertuples`/`iterrows`) | Low priority: most of these are better rewritten as column arithmetic anyway, and the ones that are not are small. |

`reset_index` (10 sites) needs nothing — a store has no index, which is the
point; those calls simply vanish.

**The one that would change the shape of the migration is `concat`.** Everything
else has a workaround that is ugly but local; without row-append, a package that
reads N measurements has to hold N stores and cannot combine them, so it builds
frames instead and the store never reaches memory. That is why the downstream
memory measurement — 109.5 MB as frames against 60.2 MB as a store on a 1M-row
burst table — is still not being collected even though both file boundaries have
moved.

## `.dstore` specifically

Nothing missing for the migration: `save_store` / `load_store` already keep
column order, dtypes, dictionary-encoded text, validity masks, labels, the group
tree and the row selection. Two notes from using it:

* **It is the right default for anything only this ecosystem reads** — measured
  downstream at a wash against uncompressed HDF5 on bulk I/O and dramatically
  faster than compressed. What keeps HDF5 in the picture downstream is that the
  burst and imaging files are *interchange* formats read by other programs.
* **The column-lifetime defect above applies to a store loaded from `.dstore`
  exactly as it does to any other**, and is more likely to bite there, because
  `load_store` is the call whose result a caller naturally lets go of after
  pulling arrays out of it.

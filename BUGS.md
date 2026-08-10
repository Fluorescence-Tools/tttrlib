# Known bugs

Found from outside the library, with a reproduction each. Anything fixed moves
to the changelog and leaves here.

## TCSPC MaxEnt is half-landed: the lifetime axis is here, the FRET distance axis is not

**2026-08-10, found from ChiSurf.** `solve_tcspc_mem_lifetime` covers the
**lifetime-axis** MEM inversion and covers it well — priors, the `_esm` error
estimates, `chisq`/`Q`/`S`/`niter`/`success` on `MemTcspcResult`. So ChiSurf's
`solve_lifetime_mem` now delegates to it.

Its sibling has nowhere to go. The **distance-axis** inversion — the one that
returns *p(R<sub>DA</sub>)* instead of a lifetime distribution, which is the
reason a FRET experiment runs MEM at all — has no counterpart here:

```python
>>> import tttrlib
>>> [n for n in dir(tttrlib) if "mem" in n.lower() or "maxent" in n.lower()]
['MemTcspcResult', 'maxent_invert', 'solve_tcspc_mem_lifetime', 'tcspc_run_mem']
>>> [n for n in dir(tttrlib) if "fret" in n.lower() and "mem" in n.lower()]
[]
```

So `chisurf/plugins/fluorescence_decay/maxent_decay/core/solver.py` still holds
`solve_fret_mem` and everything under it, and that "everything" is the part
worth having here rather than in a plugin:

* `_build_Fi_distances` — the design matrix over a distance grid. Each column is
  a donor decay quenched at the FRET rate for one R, built from `tau0`, `R0`,
  a donor-only reference and its fraction. This is the piece that makes the
  inversion a *distance* distribution rather than a lifetime one, and it is
  where the physics lives.
* `_build_Fi_lifetimes` — the lifetime-axis equivalent, presumably duplicating
  what `solve_tcspc_mem_lifetime` already does internally.
* `_run_mem` — the generic MEM engine both axes share.
* `_quadpr_bound` — the bounded quadratic-programming step.

**Why this is a bug and not a wish.** A library that ships half of a pair
invites exactly what happened: the half that exists gets delegated, the half
that does not stays behind, and the two drift — the shared `_run_mem` in the
plugin is now a *second* MEM engine maintained against this one, with no test
holding them together. The lifetime axis is also the less interesting half.

**What would close it:** a `solve_tcspc_mem_fret` (or a distance-grid option on
the existing entry point) taking the R grid, `tau0`, `R0` and the donor-only
reference with its fraction, returning the same `MemTcspcResult`. Sharing the
engine with `solve_tcspc_mem_lifetime` is the point — the two differ only in
how the design matrix is built.

Related, and the same shape of problem: **FCS MaxEnt** (`chisurf/core/models/fcs/maxent.py`)
is a third MEM implementation, over a diffusion-time axis, also with no home
here. If the engine were exposed with a pluggable design matrix, all three
would be one solver and two matrix builders.

## `GopichSzabo::set_scheme` rejects any disconnected kinetic scheme

**2026-08-10, found from ChiSurf.** `set_scheme` returns `false` whenever the
rate matrix has a repeated zero eigenvalue — an all-zero matrix, or any scheme
with a state that does not exchange with the rest — and `log_likelihood` then
reports `-inf`. The all-zero case is the **no-exchange limit** — the static mixture a dynamic photon-by-photon fit is
compared against — so an optimiser exploring towards slow exchange hits a wall
where the likelihood is in fact perfectly well defined, and a likelihood-ratio
test against the static model cannot be computed at all.

**It is not only the all-zero case.** Any scheme whose exchange graph is
*disconnected* is rejected — including an ordinary three-state model in which
one state simply does not exchange with the other two, which is a scheme a user
would reasonably fit:

| scheme | `set_scheme` |
| --- | --- |
| 2 states, all zeros | **False** |
| 3 states, all zeros | **False** |
| 3 states, two exchanging + one isolated | **False** |
| 2 states, one-way only (0 → 1, rate 1e3) | True |
| 2 states, off-diagonals 1e-12 | True |
| 2 states, off-diagonals 1e-6 / 1e-3 / 1e3 | True |

So the trigger is a repeated eigenvalue at zero — one per disconnected
component — not the literal zero matrix. Note the last rows: a *one-way*
scheme is accepted, and a perturbation as small as 1e-12 is enough to make the
all-zero case pass, so the boundary is exact degeneracy rather than
ill-conditioning.

The zero generator is the *best*-conditioned input there is — its eigenvector
basis is the identity. NumPy returns eigenvalues `[0, 0]` with `cond(V) = 1.0`.

### Reproduction

```python
import numpy as np, tttrlib

emission = np.array([[0.8, 0.2], [0.2, 0.8]])      # two states, two colours
rates = np.zeros((2, 2))

g = tttrlib.GopichSzabo()
print(g.set_scheme(rates.flatten().tolist(),
                   emission.flatten().tolist(), 2, 2))   # -> False, expected True
```

The correct answer for four photons `d, a, d, a` at 0, 1e-5, 2e-5, 3e-5 s is
`log(0.5*(0.8*0.2*0.8*0.2) + 0.5*(0.2*0.8*0.2*0.8))` = `-3.66516292749662`,
which ChiSurf's own implementation reproduces to 16 digits.

### Where it goes wrong

`set_scheme` has exactly one `return false`, from `eigendecompose`
(`modules/spectroscopy/kinetics/src/GopichSzabo.cpp:44`). Two of the three
stages under it already guard the zero case, so neither is the cause:

* `qreigen_detail::balance` skips a row/column whose off-diagonal sum is zero,
  so `scale` stays `1.0` and the later `/= s` cannot divide by zero;
* `qreigen_detail::compute_eigenvectors` maps a zero `anorm` to `1.0`, so
  `pivot_floor` stays finite.

That leaves `francis_qr` or, more likely, `zinv`. Inverse iteration for a
repeated eigenvalue solves the *same* exactly-singular system `(H - 0*I)x = b`
for every eigenvector, so all `n` of them come back parallel; the eigenvector
matrix is then rank deficient and `zinv` fails — even though the true
eigenspace is the whole space and the identity would serve. Worth checking
whether `cond` comes out `NaN` there too, since `NaN <= MAX_COND` is `false`
and would swallow the failure the same way.

The three-state "two exchanging + one isolated" row above is the confirmation:
that generator has a *simple* zero eigenvalue for the connected pair and
another for the isolated state, giving the repeated zero. So a fix that
special-cases the all-zero matrix would not be enough — it has to handle a
repeated eigenvalue with a full eigenspace generally, which is what LAPACK's
`dtrevc`/`dhsein` do by orthogonalising successive inverse-iteration vectors
against the ones already found.

### Note for whoever fixes it

ChiSurf no longer turns a rejected scheme into `-inf` — a *setup* failure is
not an impossible model, and conflating the two is what made this silent for so
long. It falls through to its own implementation instead, so the symptom is now
"quietly slower at the static limit" rather than "wall in the likelihood".
Fixing this here removes the need for that fall-through to ever fire.

## `disassemble` does not create the directories an object's name implies

**2026-08-07.** An object name is written out as a *relative path* — which is
useful, and is what ChiSurf now relies on to address a container like a folder
(`m000.pto/countrate_All 0.2000#60/bursts`). But the writer does not create the
directories the name implies, so the first name containing a separator fails:

```
PtoMfdbError: could not disassemble into /tmp/unpack:
    cannot create /tmp/unpack/countrate_All 0.2000#30/bursts
```

Worked around by walking `objects()` and `mkdir(parents=True)`-ing each name's
parent before the call. Either the writer should do that, or it should say that
a name is a flat identifier and reject a separator — the present behaviour
accepts the name and then fails on it, which is the one option that teaches
nothing.

## A container's objects have no identity beyond `(kind, name)`, so a reader cannot tell two runs apart

Found driving ChiSurf's burst pipeline end to end over a `.pto` built from ten
`.spc` files: search, change one setting, search again. Each run writes an
object of kind `burst_table` named `bursts` — correctly, because the older
result is meant to stay reachable. But `PtoFile.objects()` gives a reader
nothing to *choose* between them with except tags it has to know to look up
(`_mmfdb_operation.settings_hash`), and `find(name)` resolves a name that is
not unique.

The consequence in a reader that does the obvious thing: ndX read every
`bursts` object and concatenated them side by side, lining up three unrelated
analyses of 4621, 2318 and 1099 rows against each other and padding the short
ones — no error, a plot of a mixture of three searches.

```python
import tttrlib
f = tttrlib.PtoFile(); f.open("m000.pto")
names = [(o.uid, o.kind, o.name) for o in f.objects()]
# [(…, 'burst_table', 'bursts'), (…, 'burst_table', 'bursts'), (…, 'burst_table', 'bursts')]
f.find("bursts")   # one uid, and nothing says which
```

Worked around on the reader side (take the last object of the right
`operation_type`, then only tables whose parent is that one). Two things would
make that unnecessary, and the second matters more:

* **`objects()` should promise write order.** The workaround leans on it and
  the header does not say it holds.
* **A container should be able to say which object is current for a given
  `(kind, name)`** — a `superseded_by` edge, or a `current` flag the writer
  moves. Every reader otherwise re-implements "newest wins" and they will not
  agree; a reader that guesses wrong shows old numbers with no sign of it.

Related and smaller: `Measurement.metadata()` returns `""` for a container
written by `Measurement.create()`, so nothing at the file level says what the
measurement *is* while every object below it is richly tagged.

## Tags are appended, never replaced, and nothing dedupes an edge

Same session. Re-running an analysis updates its table in place (good — the
container does not grow an object per re-run) but every tag written during that
update is *added* to the object's tag list. The parent edge is written each
time, so a container analysed three times claimed the same source four times:

```python
m.parents(uid)   # [6005969235780289] * 4 — one source, recorded four times
```

Fixed on the ChiSurf side by reading `parents()` first and skipping what is
already there, which is a workaround for something the writer should not allow:
`add_tag` on a `PtoType_UID` item that already holds that exact value should
replace, or the API should expose `set_tag`/`clear_tags(uid, item)` so a caller
can express "these are the parents" rather than "add a parent".

Worth checking whether the same doubling affects the scalar tags — an object
re-described three times may hold three `_mmfdb_artifact.row_grain` values with
readers silently taking the first.

## Not a bug, recorded so it is not re-derived: `.pto` round-trips CLSM markers exactly

`Leica_SP5.ptu` packed into a `.pto` and read back through the container gives
byte-identical event types, marker routing counts and `CLSMImage` geometry:

```
event_types  {0: 6596261, 1: 118288}   markers {1: 59133, 2: 58924, 4: 21, 6: 210}
CLSMImage    n_frames=1 n_lines=7921 n_pixel=256 counts=443139     # both
```

The `no complete frames; salvaging 1 frame(s) with 7921 line(s)` warning that
comes with it is **not** a container problem — the raw `.ptu` produces it too.
It is a marker-configuration question in `CLSMImage` (this file's frame marker
appears 21 times and is not being used), and it makes the intensity image of a
standard fixture a 256×7921 stripe instead of 31 frames of 256×256.

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

# Coverage gaps

Not defects. Places where something works and is verified in **one** language,
recorded because "it compiles" is not "it passes" — and the R runner proved
that distinction on 2026-08-07, failing six of eight new conformance cases that
Python had green.

## CSV options are not in the conformance suite, in any language

`test/conformance/cases/csvfile.json` has three cases and all three go through
default options:

```python
tttrlib._write_csv_native(path, store, tttrlib.CsvWriteOptions())
tttrlib.read_csv_into(store, path, tttrlib.CsvOptions())
```

So the round trip, the digits and the column order are pinned in four
languages, and **every knob is pinned in Python only**:

| Not covered cross-language | Added |
|---|---|
| `nan_rep` — what a `NaN` is written as | 2026-08-07 |
| `metadata` / `comment` — the JSON Lines block | 2026-08-07 |
| `na_rep`, `true_string`, `false_string` quoting | earlier |
| `quoting`, `float_precision`, `float_decimals` | earlier |
| `na_values`, `text_columns`, `use_float32` | earlier |

Why it matters here specifically: the metadata block is the one CSV feature
whose *point* is that another program reads the file. A binding that built the
options struct wrongly would write a file this library reads back perfectly and
nothing else does — which is exactly the failure that has no local symptom.

**What closing it looks like.** The op signatures are the work, not the cases:
`csvfile.write` and `csvfile.read` take no options today, so they need an
options argument that four runners each build. Once they do, one case per knob
is cheap. Worth doing when the next CSV option lands rather than as its own
task — the ops only need generalising once.

Nothing is known to be wrong. This records that nothing is known to be right
either, outside Python.

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

**And, since this list was rewritten: the blocker below is closed.** A `Column`
now supports `col[i]`, `col[a:b]`, `col[mask]`, `list(col)`, `col[i] = x` and
all six comparisons; `DataStore.copy()` exists in C++, so all four bindings have
it. Two things came out of implementing it that the proposal did not have:

* **`column == value` was not a missing feature, it was a wrong answer.** It
  returned SWIG's identity `False` rather than raising, so a selection built
  from it matched nothing and said nothing. `>` and the other three orderings
  raised a `TypeError` and were never dangerous. That reordered the work.
* **The proposed `self.numpy()[key]` cannot be implemented literally.** For a
  text or bool column `numpy()` is a *copy*, so an integer index would decode
  the whole column to read one row — measured at 38 s per thousand accesses on
  200 000 rows against 0.6 ms for the routed form. An integer key goes through
  `string_at` / `value_at`; only a slice or an index array goes through
  `numpy()`.

What remains from the list below: a readable spelling for row selection,
group-by over a dictionary column, `argsort` / `sort_by`, and
`rename_column` / `insert_column(position)`.

**The prediction was half right.** `concat` *was* the item that changed the
shape of the migration — but only for the **file layer**. With it, one downstream
plugin (burst fusion: core, driver and view-model) went from frames to stores
end to end, and the burst reader now returns a store. That half is done and it
worked as argued.

**It was wrong about the consumer layer**, which is where the remaining cost
actually is, and the blocker there was not on the list at all.

## ~~The blocker that matters now: a `Column` is not array-like~~ — CLOSED

*Kept for the record; this is what the migration hit.* At the time,
`np.asarray(column)` and `len(column)` worked and nothing else did:

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
| ~~**`DataStore.copy()`**~~ | 28 | 13 | **DONE.** The copy constructor did this already and nobody could find it; it is now a named method in C++, so all four bindings have it. |
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

---

# Proposal — give `Column` the array protocol

A concrete form of the blocker above, because "make it array-like" is not
actionable on its own and the interesting part is what it should do about
masks and text.

## The change

Four dunders and rich comparison, all delegating to the buffer the column
already exposes:

```python
class Column:
    def __getitem__(self, key):        # scalar for an int, ndarray otherwise
        return self.numpy()[key]

    def __setitem__(self, key, value): # numeric only -- see below
        ...

    def __iter__(self):
        return iter(self.numpy())

    # __len__ already exists; __array__ already works.
    # __eq__ __ne__ __lt__ __le__ __gt__ __ge__ -> np.asarray(self) OP other
```

Nothing new is computed: `numpy()` is a zero-copy view for a numeric column and
already materialises a text one. This is a *handle* change, not a data change.

## Why it is worth more than it looks

It is the difference between "swap the reader" and "rewrite every consumer".
Measured on the package migrating onto `DataStore`: **~56 call sites** exist
purely to insert a conversion — 31 `to_numpy`, 17 element accesses, 5 mask
comparisons, 3 `map`s — and every one is a place a later reader asks why the
wrapping is there. The arithmetic around them does not change at all.

## Three decisions worth making deliberately

**1. Should element access honour the validity mask?** Today `numpy()` ignores
it: a masked row still returns its stored value. Measured — a column with
`set_mask([1,0,1])` returns `[1., 2., 3.]`, and the `2.` is not a measurement.

The consistent answer is that the *array protocol* returns what is stored and
says nothing about validity, exactly as `numpy()` does, and that "value or
missing" stays an explicit question (`valid(i)` / `mask_numpy()`). The
alternative — `col[i]` returning `NaN` where masked — cannot work for an
integer or text column without changing its dtype, which is the whole reason
the mask exists. **Recommend: no masking, and say so in the docstring**, since
the silent-wrong-answer risk is real and one sentence removes it.

**2. Should `col[i] = x` write through?** The numeric view is writable, so
delegation works for numeric columns and *silently loses the write* for boolean
and text ones, which decode through a copy. A write that vanishes is worse than
one that refuses. **Recommend: implement `__setitem__` for numeric dtypes and
raise `TypeError` naming the dtype for boolean and text**, pointing at the
dictionary/`set_bool` route.

**3. What does comparison return for a text column?** `np.asarray` on a
dictionary column gives an object array of Python strings, so `col == "m000.spc"`
gives an elementwise bool array — which is what a caller wants and what the
frame did. It also decodes the whole column, so it is O(n) in Python. That is
acceptable for a comparison, and worth a note: a caller filtering a large text
column repeatedly should compare `codes()` against a dictionary index instead.

## What is deliberately *not* asked for

`map`, `isin`, `groupby` on the column. `np.asarray(col)` plus a comprehension
or `np.isin` is honest, reads fine, and does not grow a second table API inside
the column. The gap being closed here is the *protocol* a numpy user already
expects, not a dataframe surface.

---

# ~~Proposal — `write_csv` should say what a `NaN` is written as~~ — DONE

**Shipped as `nan_rep`.** Two defects were found while adding it and fixed in
the same change: the null/true/false texts were written unquoted, so an
`na_rep` containing a delimiter produced a file that did not read back; and
`quoting="never"` raised a bare `KeyError`. One thing the proposal got wrong:
it assumed `nan` round-trips as a NaN value. It does not — `nan` is one of the
reader's default `na_values`, so both spellings come back masked, and the
option is about what *other* programs read.

A second concrete request, from the same migration. Smaller than the array
protocol and it removes a whole class of workaround.

## The gap

`na_rep` controls what an **invalid** (masked) value is written as. It says
nothing about a float `NaN`, which is a *value*, so it goes out as the text
`nan`:

```python
store_from_arrays({"x": np.array([1.0, np.nan, 3.0])})
write_csv(None, s, na_rep="")      # -> "1\nnan\n3\n"
```

A frame's writer produces the empty field for both, and these files are read by
programs that were written against that. So a caller wanting the old text has to
**mask every non-finite float before writing**.

## Why that workaround is worse than it looks

It is not the cost — masking 12 columns of 500 000 rows is 19.5 ms against a
732 ms write, 3%. It is that **the mask is part of the table**, so doing it in
place means *writing a table changes it*:

```
before write: has_mask = False,  valid(1) = True
after  write: has_mask = True,   valid(1) = False
```

That shipped in the downstream package and was found only by measuring this. It
is fixed there by copying the store before masking — which is a whole-table copy
on every CSV write, to express one formatting choice.

## The change

```python
write_csv(..., nan_rep=None)   # None: as now, the shortest text ("nan")
                               # "":   the empty field, what a frame writes
                               # any:  that text
```

Independent of `na_rep`, because the two are genuinely different questions: a
masked cell says *not measured*, a `NaN` says *the number is not a number* —
a fit that diverged, a ratio with no denominator. The store keeps them apart on
purpose, and CSV has one blank field for both, so the writer is exactly the
place the caller has to be able to choose.

Suggested default `None` (unchanged), so no existing file changes.

## Why not solve it downstream

It is solved downstream, and the fix is a full copy of the table per write. The
information needed — "this float is NaN" — is already in the writer's hands as
it formats each value.

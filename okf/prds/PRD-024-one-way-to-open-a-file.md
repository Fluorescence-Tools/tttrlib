# PRD-024 — What is this file, and open it as that

> **PRD #:** 024 · **Status:** 🔵 Proposed · **Created:** 2026-08-07 · **Updated:** 2026-08-07 · **Owner:** tpeulen

## Summary

Two things, and the second is built on the first.

**Auto-type.** `identify(path)` says what a file *is* — a photon stream, a
table, a parameter set, a container — from its content rather than its name, and
without reading its payload.

**Auto-load.** `load(path)` opens it as that and returns the object the file is
a serialisation of: a `TTTR` for `.ptu`, a `DataStore` for `.dstore`, a `TTTR`
for a Photon-HDF5 `.h5` and a `DataStore` for a columnar one.

Three things follow from taking that seriously rather than as sugar:

* **A sidecar is not a second file.** `m001.set` beside `m001.spc` is part of
  the measurement, so `load` attaches it — and **every** parameter in it lands
  in the header, not the four the imaging path happens to interpret.
* **A directory is loadable.** `load("bursts/")` reads the folder, which is what
  a caller with twenty measurements actually has.
* **Loading the wrong thing must stop being silent.** It is not today.

## Problem / motivation

### Eight doors, no sign on any of them

| The file | What opens it | Returns |
|---|---|---|
| `.ptu`, `.ht3`, `.spc`, `.sm`, `.photons`, `.ttr`, `.raw` | `TTTR(path)` | `TTTR` |
| Photon-HDF5 `.h5` | `TTTR(path)` | `TTTR` |
| columnar `.h5` | `read_hdf5(path)` | `DataStore` |
| `.dstore` | `load_store(path)` | `DataStore` |
| `.csv` | `read_csv(path)` | `DataStore` |
| `.pto` | `PtoFile(path)` then `pto_store` / `pto_tttr` | either |
| `.set` | `bh_set(path)` | parameters |

A caller with a folder of measurements must pick per file, and the knowledge is
not in the name: `.spc` is two formats, `.bin` is two, and `.h5` is **two
different kinds of thing**.

### Picking wrong is silent, which is why this is worth doing

Verified on this build:

```python
tttrlib.write_hdf5("burst.h5", store, "/t")
t = tttrlib.TTTR("burst.h5")
t.get_n_events()                 # 0
t.get_tttr_container_type()      # 'PHOTON-HDF5'
# and on stderr:
#   H5G__traverse_real(): component not found
#   Warning: /photon_data/timestamps not found.
```

The sniffer registered for `PHOTON-HDF5` is `isHDF5File`, which reads the
eight-byte HDF5 signature. **Every** HDF5 file matches it, including every table
this library writes. The container is then "identified", the Photon-HDF5 reader
runs, finds no `/photon_data`, and returns an empty stream.

`.dstore` and `.csv` are quieter: `File ... not supported.` on stderr, zero
events, no exception. A caller who checks `n_events == 0` sees an empty
measurement; one who does not sees nothing at all.

**So this is the fix for a family of silent wrong answers that already ships**,
and the fix has to be in the library: telling callers to dispatch better leaves
`TTTR("burst.h5")` returning zero events forever.

### Why the caller cannot do this themselves

1. **Extensions are ambiguous and content is not.** The registry already sniffs
   content for the TTTR formats — the answer exists and is not reachable for the
   rest.
2. **Every binding needs it.** A Python-side `if` fixes one of four.
3. **The decision needs to be inspectable.** A dispatcher nobody can ask "why?"
   is one that gets debugged by bisection.

## Proposal

### Part 1 — the kinds, which are the type system this rests on

A closed set. Each names exactly one C++ type, which is what makes the typed
family in Part 7 possible at all.

| `kind` | C++ type | Written by | `load` opens it |
|---|---|---|---|
| `photons` | `TTTR` | PTU, HT3, SPC-130/600/QC, CZ-RAW, SM, PHOTONS, BrightEyes-TTR, FLIM LABS, Photon-HDF5 | yes |
| `table` | `data::DataStore` | `.dstore`, columnar HDF5, CSV | yes |
| `parameters` | the `.set` parameter set | Becker & Hickl `.set` | yes |
| `container` | `PtoFile` | PTO | yes |
| `image` | — | TIFF | **no** — see below |
| `unknown` | — | anything not identified | no, raises |

**`image` is identified and not loaded.** TIFF has unambiguous magic bytes, so
saying "this is an image" is free and makes a folder listing complete. Opening
it is `imread`, which is already unambiguous and returns an array rather than an
object of this library's — so `load` on a TIFF raises and names `imread`. This
is a deliberate asymmetry, not an oversight.

`unknown` is a real answer, not a failure mode: a folder listing reports it,
and `load` is where it becomes a raise, because that is where somebody asked
for the contents.

### Part 2 — `identify`, the primitive

```python
tttrlib.identify("bursts.h5")
# {'kind': 'table', 'format': 'HDF5-TABLE', 'reader': 'read_hdf5',
#  'why': 'HDF5 with a tttrlib columns attribute at /t'}

tttrlib.identify("m001.spc")
# {'kind': 'photons', 'format': 'SPC-130', 'reader': 'TTTR',
#  'why': 'BH SPC-130 record layout', 'sidecars': ['m001.set']}
```

* `kind` — from the table above.
* `format` — the registry's format name, so a caller can act on the specific
  format and not only the kind.
* `reader` — what `load` would call. How a caller graduates from `load` to the
  specific reader when they want its knobs.
* `why` — what the decision rested on. Load-bearing for the one branch that is
  inference rather than a marker; see Part 4.
* `sidecars` — files that belong to this one and are not separate measurements.
  Part 5.

**`identify` reads no payload.** At most the first few kilobytes, and for HDF5 an
open plus a look at the group structure. Asserted with the byte counters, not
asserted in prose.

### Part 3 — `load`, one consumer of it

The rule, stated so it can be argued with: **`load` returns the object the file
is a serialisation of.**

```python
tttrlib.load("m001.ptu")          # TTTR
tttrlib.load("photons.h5")        # TTTR       -- Photon-HDF5
tttrlib.load("bursts.h5")         # DataStore  -- a columnar table
tttrlib.load("bursts.dstore")     # DataStore
tttrlib.load("m001.set")          # the parameters
tttrlib.load("run.pto|m001.ptu")  # TTTR, out of the container
```

`kind=` asserts rather than converts:

```python
tttrlib.load("bursts.h5", kind="table")     # fine
tttrlib.load("m001.ptu", kind="table")      # raises -- it is not a table
```

`load(p)` is `identify(p)` and a call; `load(p, kind=k)` is the same with a
check that the answer was `k`.

### Part 4 — the HDF5 decision, which is the hard one

Structurally distinguishable, and the check is cheap:

| | Photon-HDF5 | tttrlib table |
|---|---|---|
| `/photon_data/timestamps` | yes | no |
| a `columns` attribute on a group | no | yes |
| equal-length 1-D datasets in a group | maybe | yes |

**The order matters and is argued, not assumed:**

1. `/photon_data/timestamps` exists → **photons**. The specification's required
   dataset; nothing else legitimately has it.
2. else a group carries a `columns` attribute → **table**. This library's own
   marker, so this is not a guess.
3. else `hdf5_table_has` finds a group of equal-length 1-D datasets → **table**.
   The foreign-file case — a pandas `to_hdf` file — and the only branch that is
   inference. `why` says so.
4. else → **unknown**, and `load` raises naming what it looked for. It does
   **not** fall back to the photon reader, because that fallback is the bug.

A file that is both — a Photon-HDF5 with a burst table written into it, which
this library can produce — resolves to **photons** by rule 1, and
`load(path, kind="table")` reaches the other half. That is a real case and it is
why `kind=` exists rather than being a purity violation.

### Part 5 — the `.set` sidecar, and every parameter of it

A Becker & Hickl measurement is `m001.spc` **and** `m001.set`. The second is not
a separate measurement; it is the settings the first was taken with.

**What happens today**, verified: `TTTR("m001.spc")` already finds the sibling
`.set` and calls `read_bh_set_file`, which stores

* the whole file, base64-encoded, as one `BH_SPC_SetFile` tag — 44 916 bytes on
  a real file, kept so a `.spc`+`.set` → `.ptu` → `.spc`+`.set` round trip is
  byte-exact;
* a handful of interpreted tags the imaging path needs — TAC range, ADC
  resolution, the scan markers.

**What is missing**: the parameters themselves. That same file carries **207**
of them across four sections, and reaching any one means calling `bh_set(path)`
separately — on a file that may no longer be beside the data, or may have been
folded into a PTU where only the base64 survives.

```
IDENTIFICATION    9      SYS_PARA   170
TRACE_PARA       16      WIND_PARA   12
```

**Proposal: every parameter lands in the header**, namespaced by section, when
the sidecar is read:

```
BH_SET.IDENTIFICATION.Title      "sample_c10"
BH_SET.SYS_PARA.SP_ADC_RE        4096
BH_SET.SYS_PARA.PR_PDEV          18
...
```

Typed as the `.set` declares them, so an integer stays an integer — which is
what makes this worth doing rather than storing one big string.

**The size objection, answered with the measurement rather than a guess.** 207
tags cost roughly **9.9 kB**. The base64 blob already in that same header is
**44.9 kB**. So this adds 22% to something already being written, and it removes
the need for the caller to have the sidecar file beside the data.

Two consequences:

* **`load("m001.spc")` needs no second call** to know the settings. The
  parameters are on the object.
* **`load("m001.set")` alone works**, `kind: "parameters"`, for the case where
  the sidecar is what you have.

#### These tags are Becker & Hickl's, and they stay there

**A BH measurement stays BH and a PicoQuant one stays PQ.** The parsed
`BH_SET.*` tags exist on a header read from a BH file and are **not written into
another vendor's container**. Writing a hundred and seventy `SYS_PARA` keys into
a PTU would put one instrument's settings namespace inside another's file
format, where nothing can interpret them and every consumer of that PTU has to
step over them.

The `BH_SPC_SetFile` base64 tag is a separate, pre-existing mechanism with a
different job: it makes `.spc`+`.set` → `.ptu` → `.spc`+`.set` reconstruct the
original sidecar byte for byte. It is an opaque payload for a round trip, not
settings anything reads. **This PRD does not extend it and does not add to what
a foreign container carries.**

So the parsed tags are a view of the sidecar, built on read, scoped to the
format that has one. A `.set` this parser does not fully understand still round
trips losslessly through the blob, because the blob is what the round trip was
always built on.

### Part 6 — a directory is loadable

```python
tttrlib.load("bursts/")            # every file in it
tttrlib.identify("bursts/")        # what is in it, without reading any of it
```

Four rules, each of which is a decision:

**1. Sidecars do not appear as entries.** A folder of twenty `.spc`+`.set` pairs
is twenty measurements, not forty files. This falls straight out of Part 5 and
is the reason the two parts belong in one PRD.

**2. Sorted by name, always.** A folder listing is filesystem order, which is
neither stable across machines nor meaningful. `m001, m002, ...` is what the
caller means and what makes a concatenation reproducible.

**3. Not recursive by default.** `recursive=True` for the other case. A burst
folder with a `raw/` subdirectory should not silently double.

**4. Unloadable files are reported, not fatal.** A `.tif` or a stray `.txt` in
the folder appears in `identify` with its kind and is **skipped** by `load`,
which returns what it could open. One unreadable file must not lose the other
nineteen. What was skipped is reachable — see the return shape below.

**Combining.** The reason to load a folder is usually to get one thing out of
it, and for tables that is already built:

```python
tttrlib.load("bursts/", combine="rows")    # ONE DataStore
```

This is `concat` over the files, so it inherits what `concat` already does: a
column missing from one file keeps its dtype, columns line up by name and not by
position, a dtype conflict is refused and named — and the gap is recorded as a
range whose reason is `absent in 'm002.dstore'`. **A folder read is exactly the
case that machinery was built for.**

`combine="rows"` on a folder of **photon streams raises and says why**: merging
streams means deciding what happens to macro times across a file boundary, and
that is a decision for the caller, not a default. `TTTR.append` with an explicit
offset is the honest spelling.

### Part 7 — one dispatcher, two shapes of API

The decision is C++ in `modules/io/base`, so nothing re-implements it. What
differs is only how the result is spelled, and it differs **by whether the
language has to write the return type down**:

| | dynamic — Python, JavaScript | static — Java, R |
|---|---|---|
| identify | `identify(path)` → dict | `identify(path)` → `FileInfo` |
| identify a folder | `identify(dir)` → list of dict | `identify(dir)` → `FileInfo[]` |
| open one | `load(path)` → the object | `loadTTTR` / `loadTable` / `loadParameters` |
| open a folder | `load(dir)` → list | `loadTTTRs` / `loadTables` |
| combine a folder | `load(dir, combine="rows")` | `loadTableCombined(dir)` |
| assert a kind | `load(path, kind="table")` | the typed call *is* the assertion |

```java
FileInfo[] found = Tttrlib.identify(dir);
DataStore all = Tttrlib.loadTableCombined(dir);   // raises if they are not tables
```

```r
info <- identify(dir)
all  <- load_table_combined(dir)
```

**Why not one `load` returning a variant everywhere.** A `LoadResult` carrying a
kind and a typed accessor was the obvious symmetric design and it is worse: it
makes every Java and R call site unpack a wrapper to reach an object whose type
they have already tested for — the boilerplate this PRD exists to delete. The
typed family costs a few entry points per static language and gives each of them
a signature that says what it returns.

**The typed calls verify.** `loadTTTR` on a table raises naming the kind it
found. It is `load(path, kind="photons")` under a different name, not a cast.

**What is shared, and it is the part that matters:** `identify` has the same
name, fields and answers in all four, so the conformance cases are written once
against it. Only the opening verb differs.

### Part 8 — the ambiguous extensions, which are already solved

`.spc`, `.bin`, `.ptu` and the rest go to `IORegistry::infer_container_type`,
which sniffs content and already resolves SPC-130 against SPC-QC and STT1
against ITT1. **No new inference is written for these.** The only change is that
a file the registry cannot identify becomes `unknown` and a raise, instead of an
empty `TTTR`.

`.csv` has no magic bytes and never will. It is identified by extension plus a
first line that parses as a delimited header — and `why` says so, so a caller
can see that this one was a guess where the others were not.

### Part 9 — a container is not a table

`.pto` holds objects: photon streams, stores, anything. `load("run.pto")` with
no selector returns the **container**, because that is what the file is a
serialisation of. Returning the first object would be arbitrary; returning all
of them would read the whole file.

```python
c = tttrlib.load("run.pto")           # PtoFile
c.objects()                           # what is in it
tttrlib.load("run.pto|m001.ptu")      # the object
```

### Part 10 — what this does *not* do

* **No conversion.** `load` never turns a table into a `TTTR` or the reverse.
  `kind=` asserts; it does not build.
* **No caching, no laziness.** `load` reads. `identify` does not.
* **It does not replace the specific readers.** `read_hdf5(f, columns=[...])` is
  what a caller uses when they know what they have and want part of it, and
  `identify`'s `reader` field is the bridge from one to the other.
* **No `save`.** The counterpart is obvious and is a separate question: a write
  has a target format the caller must choose, so the symmetry is only apparent.

## Acceptance criteria

1. `load` returns a `TTTR` for every container in
   `TTTR.get_supported_container_names()` that has test data, **equal to what
   the specific reader returns** — same event count, same first and last macro
   time.
2. `load` returns a `DataStore` for `.dstore`, a columnar `.h5` and `.csv`, each
   equal to what `load_store` / `read_hdf5` / `read_csv` returns.
3. **A columnar HDF5 table is never opened as photons.** This is the shipped
   bug: today it returns zero events and prints to stderr.
4. **A Photon-HDF5 file is never opened as a table**, including one that also
   carries a written table — rule 1 wins, and `kind="table"` reaches the other.
5. A file that cannot be identified is `kind: "unknown"` from `identify` and a
   **raise** from `load`, naming the path and what was looked for. Neither
   returns an empty anything.
6. `identify` reads no payload, asserted with `store_bytes_read()` /
   `hdf5_bytes_read()`: identifying a folder of gigabyte files costs kilobytes.
7. `identify(p)["kind"]` and the type `load(p)` returns agree for every format,
   parametrised over the format list rather than written out per format.
8. The ambiguous extensions resolve by content: two `.spc` files (SPC-130 and
   SPC-QC) and two `.bin` files (STT1 and ITT1) each identify correctly.
9. **Every `.set` parameter is a header tag** after `load("m001.spc")` — all 207
   of the reference file, typed as the `.set` declares them.
10. **A converted PTU carries no `BH_SET.*` tag.** A BH measurement's settings
    do not travel into another vendor's container; the parsed tags exist on a
    header read from a BH file and nowhere else.
11. The base64 `BH_SPC_SetFile` tag is unchanged in what it holds and where it
    goes, and a `.spc`+`.set` → `.ptu` → `.spc`+`.set` round trip is still
    byte-exact. Nothing here adds to what a foreign container carries.
12. `identify("m001.spc")` lists `m001.set` as a sidecar; `load("m001.set")`
    alone gives the parameters.
13. `load(dir)` gives one entry per measurement, **not** one per file: a folder
    of N `.spc`+`.set` pairs gives N.
14. `load(dir)` is sorted by name, is not recursive unless asked, and **skips
    rather than throws** on a file it cannot open — nineteen good files are not
    lost to one bad one, and what was skipped is reachable.
15. `load(dir, combine="rows")` on tables equals `concat` of the individual
    loads, gaps included: a column missing from one file keeps its dtype and its
    range says `absent in '<that file>'`.
16. `combine="rows"` on photon streams raises and names the reason.
17. `load(path, kind=...)` raises when the file is not of that kind rather than
    converting, and the message names the kind that was found.
18. **`identify` gives the same answer in all four languages**, in the
    conformance case list, parametrised over the format. The typed openers are
    covered per language against that language's own readers.
19. A typed opener raises on the wrong kind — `loadTable` on a `.ptu` — with the
    same message the dynamic `kind=` form gives.
20. `load` on a TIFF raises and names `imread`; `identify` reports it as
    `image`.
21. Nothing regresses: every existing reader behaves exactly as today, including
    `TTTR(path)` on a real photon file, and the four imaging tags the CLSM path
    reads out of a `.set` still arrive.

## Risks and non-goals

* **Rule 3 is inference and will be wrong sometimes.** A foreign HDF5 file of
  equal-length 1-D datasets that is not a table will be called one. Accepted:
  refusing every file without our own marker makes `load` useless for the
  interoperability case HDF5 exists to serve. `why` says the inference branch
  was taken, which is the mitigation.
* **207 tags in a header is a real change to written files.** It is 22% on top
  of a blob already there, and every tag is namespaced under `BH_SET.` so it
  cannot collide with a PTU tag. But a consumer that enumerates header tags will
  see more of them, and that is worth saying out loud rather than discovering.
* **A directory load can be enormous.** Twenty gigabyte files is twenty
  gigabytes. `identify(dir)` is the cheap half and exists partly so a caller can
  decide before committing.
* **`load` invites use where a specific reader belongs.** Someone who knows they
  have a `.dstore` and wants two columns should call `load_store(columns=)`.
  Documented, not enforced.
* **Not a plugin surface.** A format added by a C-ABI plugin gets `load` free
  through the registry if it is a TTTR container. Non-TTTR plugin kinds are out
  of scope.

## Open questions

22. **What is the return shape of a directory load in a dynamic language?** A
   list is simplest and orders naturally; a dict keyed by filename is what a
   caller usually wants next. Leaning **list of `(name, object)` pairs**, since
   it preserves order, converts to a dict in one call, and matches the static
   languages' array.
23. **Should `combine="columns"` exist too?** `concat(axis="columns")` is there,
   and a folder of per-detector tables side by side is a real shape. It needs
   every file to have the same row count, which a folder cannot promise —
   leaning no for v1, and the error message from `combine="rows"` should not
   imply the other exists.
24. **Do sidecars generalise beyond `.set`?** PTU has no sidecar convention;
   Photon-HDF5 has none. If `.set` is the only member, `sidecars` is a list with
   at most one entry and might be better named. Leaning: keep it plural and
   general, because the concept is right even if the set is currently one.

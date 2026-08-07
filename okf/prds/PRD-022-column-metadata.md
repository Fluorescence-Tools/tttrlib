# PRD-022 — A column is described, not just named

> **PRD #:** 022 · **Status:** 🟢 Done · **Created:** 2026-08-07 · **Updated:** 2026-08-07 · **Owner:** tpeulen

> **Implemented.** All ten criteria are met. Two notes for the reader:
>
> - The version 1 fixture for criterion 7 is **built, not committed**. It is
>   produced from whatever the current writer emits by walking the directory and
>   removing the one field version 2 added, so it cannot rot the way a binary
>   checked in beside the code does. The walk is in
>   `test/python/test_store_file.py::_downgrade_to_v1`.
> - The directory is positional in three places, not two: `read_node` reads a
>   column record, `walk_paths` steps over one without touching a blob, and both
>   have to consume the new field. Missing the second would have shifted every
>   column and group after the first text column — silently, and only in
>   `store_columns`.

## Summary

A `Column` carries a name and a type and nothing else. That is enough to read a
table and not enough to *understand* one: `Duration` is milliseconds, `Tau` is
nanoseconds, `Mean Macro Time` is milliseconds in one writer and seconds in
another, and none of that is written down anywhere. It lives in the column name
when somebody remembered — `Duration (ms)`, `Mean Macro Time (ms)` — and in a
docstring when they did not.

Give a column **one extensible description instead of a growing list of
members**. The name becomes an attribute of that description rather than a field
beside it, so adding units, or a dictionary item name, or a longer label, is not
a change to the class or to the file format — it is another key.

```
name:  "Duration"
type:  Float64
metadata: {"name": "Duration", "units": "milliseconds",
           "item": "_mmfdb_burst.duration"}
```

## Problem / motivation

### The unit is in the name, or nowhere

Every consumer of a burst table already has to know what the numbers mean, and
the only thing that tells it is a naming convention nobody can enforce:

| Column | Unit | How you know |
|---|---|---|
| `Duration (ms)` | milliseconds | it is in the name |
| `Mean Macro Time (ms)` | milliseconds | it is in the name |
| `Count Rate (KHz)` | kilohertz | it is in the name, with unconventional capitalisation |
| `Number of Photons` | a count | it is not a physical quantity |
| `Tau` | nanoseconds | you have to know |
| `First Photon` | an index | you have to know |

Parsing the unit back out of a parenthesised suffix is not a solution; it is the
same convention with a regular expression on top, and it fails on the columns
that need it most — the ones where somebody left the suffix off.

### A growing list of members is the wrong shape

The obvious fix is a `units` field beside `name`. The next request is a
description, then the mmCIF item the column corresponds to, then a display
precision, and each one is a change to the class, to the `.dstore` directory
record, to every binding and to the format version. That is four places per
attribute, forever.

One JSON object is one place. The format learns nothing new when an attribute is
added, because the attribute is inside a string the format already carries.

### The name is not special enough to be a field

It is a *lookup key*, which is a property of how the column is used rather than
of what it is. Keeping it as an attribute of the description keeps the model
honest: a column has a description, and one of the things the description says
is what to call it.

That is a data-model decision with a performance consequence, and the
consequence is handled rather than accepted — see below.

## Goals

1. A column carries arbitrary named attributes, and units in particular.
2. Attributes survive a `.dstore` round trip and a PTO container round trip.
3. `column.name()` stays an O(1) string reference. Lookup is the hot path and
   parsing JSON per lookup would not be acceptable.
4. Nothing existing changes shape: `add_column(name, type)` and `name()` keep
   working, so the 22 call sites in this repo are untouched by the model change.
5. Reachable from Python, R, Java and JavaScript.

## Non-goals

- A schema for the attributes. `units` is conventional here and PTO.MFDB
  constrains it; the store neither validates nor interprets it.
- A unit *system*. Nothing converts, checks dimensions, or arithmetically
  composes units. A column says what it is in; deciding what to do about that is
  the application's business.
- Per-*cell* metadata.

## Design

### The model

```cpp
class Column {
    /// Everything known about the column, as a JSON object. `name` is an
    /// attribute of it, not a field beside it.
    const std::string& metadata() const;
    void set_metadata(const std::string& json);

    /// metadata["name"], cached. Lookup is the hot path.
    const std::string& name() const;
    void set_name(std::string s);

    /// One attribute. Empty when absent, so a caller never has to parse.
    std::string attribute(const std::string& key) const;
    void set_attribute(const std::string& key, const std::string& value);

    /// Sugar for the attribute this exists for.
    const std::string& units() const;
    void set_units(std::string s);
};
```

`metadata_` is the authoritative store; `name_` and `units_` are caches kept in
step by the setters. JSON is parsed when metadata is *set*, not when a column is
*looked up*, so the hot path is a string compare exactly as it is today.

Parsing lives in `DataStore.cpp`, so `DataStore.h` does not grow a
`nlohmann/json.hpp` include — it is included by most of the tree.

### The file

`.dstore` bumps to **version 2**. Each column's directory record gains one
string at the end:

```
name : str          <- unchanged, still the lookup key, still first
type : u8
size : u64
flags: u8
...
metadata : str      <- new in version 2, "" when the column has none
```

The name stays a field of its own in the file even though it is an attribute of
the model. Two reasons, and both are about the reader rather than the model: a
column-subset read decides whether to skip a column *before* it has any reason
to parse anything, and `store_columns(filename)` promises the names without
reading data. Duplicating six bytes to keep both cheap is the right trade; the
metadata is the authority and the field is a cached copy, exactly as in memory.

A version 1 file reads under version 2 with empty metadata. A version 2 file is
refused by a version 1 reader, which already refuses anything above its own
`kVersion`.

## Acceptance criteria

1. `set_attribute` / `attribute` round-trip an arbitrary key through memory.
2. `set_units` is visible as `metadata()["units"]`, and setting metadata with a
   `units` key is visible from `units()`.
3. `set_name` updates `metadata()["name"]`; setting metadata with a `name` key
   updates `name()`.
4. A column with no metadata reports `""` for any attribute and does not
   fabricate a JSON object.
5. Metadata survives a `.dstore` write/read round trip, including through a
   column-subset read and a row-window read.
6. Metadata survives a PTO container round trip.
7. A version 1 `.dstore` file reads under the new library, with empty metadata.
8. `name()` remains a reference to a stored string — no allocation, no parse.
9. Malformed metadata JSON is rejected at `set_metadata` rather than stored and
   discovered on read.
10. Reachable from all four bindings and covered by the PRD-015 conformance
    cases.

## Test plan

`test/python/test_datastore.py` for the model, `test/python/test_store_file.py`
for the round trip, and a version 1 fixture committed under `tttr-data` so
criterion 7 is tested against a real old file rather than a reconstructed one.

The performance claim in criterion 8 is asserted structurally — `name()` returns
`const std::string&` — rather than by timing, because a microbenchmark of a
string compare measures the benchmark.

## Notes for whoever implements it

The trap is keeping the caches in step. There are two ways in — `set_name` and
`set_metadata` — and both have to update both, or a column will report one name
and serialise another. That is a silent divergence of exactly the kind the
FileUID width bug was, so it is worth a test that sets metadata *after* the name
and asserts both agree.

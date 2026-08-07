# PRD-020 — PTO: streaming and targeted reads

> **PRD #:** 020 · **Status:** ✅ Implemented · **Created:** 2026-08-06 · **Updated:** 2026-08-06 · **Owner:** tpeulen

> **Implementation notes.** All five parts are in, plus four decisions that
> are not in the text above:
>
> - **The range on a `TTTR` is a reader parameter, not a constructor argument.**
>   `TTTR("run.pto|m001.ptu", first, n)` cannot be added: it is ambiguous with
>   the existing `TTTR(const char*, int container_type, bool read_input)`. The
>   range goes through `set_container_parameters` instead — the mechanism the
>   registry already declares `params_schema` for, and which PTO now has one for
>   — plus a named `pto_read_events(spec, first, n, out)`.
> - **A uid is 53 random bits stored in a 64-bit element.** Nothing is narrowed
>   on disk: `FileUID` is a `uint64` written in eight octets whatever the value,
>   which is what any conformant reader gets and what in-place rewriting
>   requires. What is bounded is the number *this writer chooses*. It was
>   load-bearing when introduced — a JavaScript `Number` and an R `numeric` are
>   both IEEE doubles, so a wider uid came back naming no object — and both
>   bindings have since been fixed where the fix belonged: JavaScript routes
>   64-bit scalars through `BigInt` (the arrays always did), R carries a uid as a
>   character string. A container using the whole range now reads correctly
>   everywhere, and the bound stays only because it costs nothing and keeps a uid
>   inside a double. Uniqueness is a guarantee rather than a probability
>   (`unused_uid` retries against the objects already there). Nothing in EBML or
>   Matroska asks for the bound — libebml's `EbmlUInteger` is a whole `uint64`
>   and mkvmerge mints full 64-bit UIDs — so it is recorded as a writer's choice,
>   in the code and in the spec.
> - **Every payload is 8-byte aligned.** The specification already made this a
>   SHOULD and the writer ignored it. It now pads with `Void` on every path that
>   lays an object down, including relocation. A `.dstore`'s own blob offsets are
>   8-aligned *relative to the store*, so they were only aligned in the file by
>   luck.
> - **`compact` gained two knobs, and stopped holding payloads.** `tight=True`
>   drops the alignment padding as well as the holes, so an archive copy carries
>   no reclaimable `Void` at all; `reserve=f` does the opposite and leaves every
>   object room, so a container still being edited absorbs the next few updates
>   without relocating anything. The default is neither. It also **streams** each
>   payload instead of reading it whole — compacting an eight-gigabyte container
>   used to need eight gigabytes of memory, for the operation whose purpose is to
>   make the file smaller.
> - **A `.pto` is validated by libebml.** `test/tools/pto_ebml_check.cpp` walks a
>   container with the reference EBML implementation and checks DocType, that
>   every size is known and in range, and that a master's children exactly fill
>   it. Payload alignment is reported and only enforced under `--aligned`, since
>   the spec makes it a SHOULD and `compact(tight=True)` drops it on purpose.
>   Opt-in, and no dependency is added.
>
> `PtoFile::read_at` and `PtoFile::stream` stay C++-only — a `void*` and a
> `std::function` are not things a binding holds. `read(uid, at, n)` is their
> binding-facing form and is what the conformance cases exercise.
>
> **One corruption found while testing, and fixed.** `FileUID` was written with
> `uint_elem`, which packs to the smallest width that holds the value. That is
> right for a value written once and wrong for the only element that is
> *rewritten in place*: when an object outgrows its room it is written afresh
> with a new uid and the old uid is then written back over it, so a narrower
> replacement left the tail of the old element behind and every sibling after
> it — `PtoKind` and `PtoEncoding` among them — was read from the wrong offset.
> The symptom was an object that came back with an empty encoding, and nothing
> failed at the time it was written.
>
> With a full 64-bit uid it bites about one relocation in 256 -- the odds of the
> top octet being zero. It surfaced once in a full-file run and passed on every
> rerun, which is the shape of a defect a suite stays green on. (The 53-bit uid
> briefly in place while this was found made it far likelier, around one in
> thirty-two, which is how it was caught at all.)
> `FileUID` is now written at a fixed eight octets by `uint_elem_fixed`, and
> `test_every_file_uid_is_written_in_eight_octets` walks the EBML structure and
> asserts the width where the element actually is — scanning for the id finds it
> everywhere, because an eighteen-megabyte payload contains every two-byte
> sequence.

## Summary

PTO was built to hold an eight-gigabyte photon stream next to a burst table that
gets recomputed. It does that for **writing** — an object's payload is streamed
in after its header, and recomputing the table rewrites the table and not the
file. Reading has no equivalent. `PtoFile::read` returns the whole payload as a
`std::vector<unsigned char>`, and `pto_read_store` decodes the whole store, so
the only way to look at a container is to materialise the part of it you are
looking at, whole, in memory.

That is the weakness. A container whose stated purpose is to make large payloads
cheap to keep together is, on the read side, all-or-nothing.

Everything needed to fix it is already here and is not connected. `io_store`
reads a named subset of columns without touching the others — "two columns out
of a four-gigabyte store costs two seeks" — and separately reads a store that
begins `base` bytes into a file, which is exactly how a store sits inside a
container. There is no overload that does **both**, so the one case that matters
for PTO is the one case that cannot be expressed. Adding it is a fifth function
next to three that already differ only in which two arguments they set.

The rest of this PRD is the same idea carried to the other three payload shapes:
a byte range of any object, a row range of a table, and a position in a photon
stream.

## Problem / motivation

### The read side never got the write side's design

Writing an object is deliberately streamed. From `io_pto.cpp`, in `PtoFile::add`:

> Everything but the payload, so the header can be written and the payload
> streamed after it — a gigabyte never goes through a buffer.

The reader has no such sentence, because it has no such path:

| Call | What it costs |
|---|---|
| `PtoFile::read(uid)` | the entire payload, in memory, as a vector |
| `pto_read_store(f, uid, out)` | the entire store, every column, decoded |
| `PtoFile::extract(uid, path)` | streamed — but only to a **file**, never to a caller |
| `PtoFile::disassemble(dir)` | the same, for everything |

`extract` proves the machinery exists: it copies in blocks and an eight-gigabyte
payload costs a few kilobytes of memory. It just cannot deliver bytes to a
caller, only to the filesystem.

### The composable pieces exist and do not compose

`modules/io/store/include/io_store.h` declares three read overloads:

```cpp
void read_store_into(DataStore& out, const std::string& filename);
void read_store_into(DataStore& out, const std::string& filename,
                     const std::vector<std::string>& columns);   // subset
void read_store_into(DataStore& out, const std::string& filename,
                     std::uint64_t base, std::uint64_t bytes);   // a region
```

In `io_store.cpp` the three bodies are the same six lines, differing only in
whether `ColumnFilter want` is narrowed and whether `OpenStore` is given a
region:

```cpp
ColumnFilter want;                       // or: want.everything = false; want.wanted = &columns;
OpenStore file(filename);                // or: OpenStore file(filename, base, bytes);
Reader dir{file.directory.data(), file.directory.size(), 0};
Blobs  blobs{file.f.get(), file.file_bytes, file.base};
out.release();
read_node(dir, blobs, out, want);
```

Both knobs are independent and both already work. **A store embedded in a
container is always the `base/bytes` case, so a caller reading one can never ask
for a subset of its columns** — the single combination the container makes
routine is the single combination the API omits. `store_columns` and
`store_groups` take a filename with no region either, so a caller cannot even
*list* what is in an embedded store without decoding it.

### Nothing indexes a photon payload

An embedded PTU is bytes. Finding photon 10⁹ means decoding from the first
record. The format anticipated this and reserved the element for it —
`doc/formats/pto.rst`, *Reserved for later*:

> **Cues.** Matroska's `Cues` (0x1C53BB6B), repurposed as an index *into* a
> payload — "event 10⁹ starts at byte X" — for seeking within a photon stream
> without decoding it.

Until that exists, "targeted read" stops at the object boundary.

### Writing a large object has the mirror-image hole

`add` takes `const unsigned char* data, std::size_t n`, so embedding a file
means holding it whole first. In Python that is a `bytes` object the size of the
instrument file. This is already recorded downstream as a known issue; it
belongs here because it is the same missing verb on the other side.

## What libebml does, and what is worth taking

Read from `junk/libebml` (Matroska-Org). Cited because PTO deliberately reuses
Matroska's element IDs and framing, so the library that has carried
multi-gigabyte EBML documents for twenty years is the right prior art — **not**
because tttrlib should depend on it. tttrlib writes its own EBML and should keep
doing so; what is worth taking is three ideas it already proved.

**1. A read is parameterised by how much of it you want.** `ebml/EbmlTypes.h:17`:

```cpp
enum ScopeMode {
  SCOPE_PARTIAL_DATA = 0,
  SCOPE_ALL_DATA,
  SCOPE_NO_DATA
};
```

carried through `EbmlElement::ReadData(IOCallback&, ScopeMode ReadFully = SCOPE_ALL_DATA)`
and `EbmlElement::Read(..., ScopeMode)` (`ebml/EbmlElement.h:632-633`). Walking a
document and loading a payload are separate decisions, made per element, by the
caller. PTO has no such parameter anywhere: `read` means all of it.

**2. The source of bytes is an interface, not a filename.** `ebml/IOCallback.h`:

```cpp
virtual std::size_t read(void* Buffer, std::size_t Size) = 0;
virtual void setFilePointer(std::int64_t Offset, seek_mode Mode = seek_beginning) = 0;
virtual std::uint64_t getFilePointer() = 0;
virtual std::size_t write(const void* Buffer, std::size_t Size) = 0;
```

with `MemIOCallback`, `MemReadIOCallback`, `SafeReadIOCallback` and
`StdIOCallback` as implementations. A payload region can therefore be handed to
a decoder as a stream, with no copy and no temporary file, and the decoder never
learns whether it is reading a file, a memory block or a region of something
larger. That is precisely the shape `read_store_into(..., base, bytes)` is
faking with two integers.

**3. Skipping is a first-class operation.** `EbmlStream::FindNextElement(...,
MaxDataSize, ...)`, `FindNextID(ClassInfos, MaxDataSize)` and
`EbmlElement::SkipData(...)` (`ebml/EbmlStream.h:28-36`, `ebml/EbmlElement.h:584`)
walk by seeking. PTO's own index already makes this possible; nothing exposes it.

**Not taken:** libebml's class hierarchy, its semantic contexts, its
`EbmlCrc32`, and any dependency on it. PTO's CRC-32 and framing already exist
and match RFC 8794; adding a library to gain three verbs would be a poor trade,
and the deliberate design of this format is that a reader needs an EBML parser
plus twenty element IDs, not a framework.

## Goals

1. Reading any part of a container costs the part, not the whole.
2. The combination a container makes routine — a column subset of an embedded
   store — is expressible.
3. A caller can obtain bytes from an object without a temporary file and without
   materialising the payload.
4. A photon payload can be positioned into without decoding what precedes it.
5. Writing a large object from a file costs its own bytes, as reading one
   already does.
6. No change to the on-disk format that a PTO 1.0 reader would fail on. Cues are
   a new element ID, and unknown IDs are skipped by size.

## Non-goals

- Depending on libebml, or on any EBML library.
- A general query language. This is positioning and projection, not filtering.
- Compression. Unchanged from the format's stated position.
- Concurrent writers. Still one at a time.
- Changing what `read`, `pto_read_store` or `extract` already do. Every addition
  here is a new entry point; the existing ones stay, because the whole-payload
  case is common and correct when the payload is small.

## Part 1 — the composable store read

The smallest change with the largest effect, and the one that unblocks the
container's actual users.

```cpp
// io_store.h
void read_store_into(data::DataStore& out, const std::string& filename,
                     std::uint64_t base, std::uint64_t bytes,
                     const std::vector<std::string>& columns);

std::vector<std::string> store_columns(const std::string& filename,
                                       std::uint64_t base, std::uint64_t bytes,
                                       const std::string& group = "");
std::vector<std::string> store_groups(const std::string& filename,
                                      std::uint64_t base, std::uint64_t bytes);
```

and, in `io_pto.h`, the reason it exists:

```cpp
void pto_read_store(const PtoFile& file, std::uint64_t uid, data::DataStore& out,
                    const std::vector<std::string>& columns);

std::vector<std::string> pto_store_columns(const PtoFile& file, std::uint64_t uid);
```

The existing three overloads collapse into one implementation taking both knobs;
they remain as declarations, forwarding. Nothing else changes.

## Part 2 — bytes without the whole payload

```cpp
/// Read `n` bytes of an object's payload starting `at` bytes into it.
/// Reads short at the end of the payload, like a file read.
std::size_t PtoFile::read_at(std::uint64_t uid, std::uint64_t at,
                             void* into, std::size_t n) const;

/// Copy an object's payload to a sink in blocks, never holding it whole.
/// The way to stream an object somewhere that is not a file.
bool PtoFile::stream(std::uint64_t uid,
                     const std::function<bool(const void*, std::size_t)>& sink) const;
```

`extract` becomes `stream` with a file-writing sink, which is what it already is
internally.

For the bindings, `read_at` is the one that matters: it gives Python a
`memoryview`-shaped read and lets NumPy own the buffer, so a caller can page
through a payload at whatever granularity suits.

## Part 3 — row ranges

`DataStore` reads are whole-column today. A row range is a projection along the
other axis and belongs beside the column subset:

```cpp
void read_store_into(data::DataStore& out, const std::string& filename,
                     std::uint64_t base, std::uint64_t bytes,
                     const std::vector<std::string>& columns,
                     std::uint64_t first_row, std::uint64_t n_rows);
```

The directory already records where every column's blob begins and its element
width, so a row range is an offset and a length per column. Fixed-width columns
are exact; a dictionary-encoded text column reads its codes for the range and
the whole dictionary, which is small by construction.

**This is what a table viewer needs.** Paging a million-row burst table
currently decodes a million rows to show fifty.

## Part 4 — Cues, and positioning in a photon stream

Implement the reserved element rather than inventing one.

```
Cues            0x1C53BB6B     master, top level, Matroska's ID
  CuePoint      0xBB
    PtoCueUID       0x1E54F030   uint   which object this indexes
    PtoCueEvent     0x1E54F031   uint   event ordinal
    PtoCueOffset    0x1E54F032   uint   byte offset into the payload
    PtoCueTime      0x1E54F033   uint   macro time at that event, optional
```

Written on demand, not on every write: a cue table is built by a pass over a
payload, and a container without one is fully valid — a reader that finds no
cues decodes from the start, exactly as today.

```cpp
std::uint64_t PtoFile::build_cues(std::uint64_t uid, std::uint64_t every_n_events);
struct PtoCue { std::uint64_t event, offset, time; };
std::vector<PtoCue> PtoFile::cues(std::uint64_t uid) const;
```

`TTTR` then gains the ability to open a range of an embedded stream:
`TTTR("run.pto|m001.ptu", first_event, n_events)` seeks to the nearest cue at or
before `first_event` and decodes forward from there.

Spacing is the caller's: one cue per 10⁶ events on a 10⁹-event stream is a
thousand cues, a few tens of kilobytes, and bounds the decode to 10⁶ records.

## Part 5 — streaming in

```cpp
std::uint64_t PtoFile::add_file(const std::string& kind, const std::string& encoding,
                                const std::string& name, const std::string& path,
                                std::uint64_t reserve = 0);
```

`add` already writes the header first and then makes a single `m.f.write(data, n)`.
`add_file` sizes the payload with `std::filesystem::file_size`, writes the same
header, and replaces that one call with a block loop. Slot bookkeeping, reserve,
and the resulting bytes on disk are identical — this is not a second code path,
it is the same one with a different source of bytes.

No SWIG typemap work: the argument is a `std::string`.

## Compatibility

- Every addition is a new function. No signature changes, no behaviour changes.
- Cues are a new element ID inside a `Cues` master. A PTO 1.0 reader skips
  unknown IDs by size, so a file with cues stays readable by an implementation
  that has never heard of them. `DocTypeVersion` → 2, `DocTypeReadVersion`
  stays 1.
- A container written before this reads unchanged, and gains cues the first time
  something asks for them.

## Acceptance criteria

**Composable store reads**

1. `read_store_into(out, file, base, bytes, columns)` returns exactly the named
   columns of a store embedded at `base`, with dtypes, validity masks and the
   group tree intact.
2. Reading two columns of a ≥1 GB embedded store reads less than 1% of the
   payload's bytes, measured by instrumented `fread` volume, not by wall clock.
3. `pto_store_columns(file, uid)` lists an embedded store's columns without
   reading any column data.
4. The three existing `read_store_into` overloads produce byte-identical results
   to before, and are implemented by forwarding to the general one.

**Byte-level reads**

5. `read_at` returns the same bytes as the corresponding slice of `read(uid)`,
   for offsets at the start, in the middle, spanning a block boundary, and past
   the end (short read, not an error).
6. `stream` copies an 8 GiB payload with resident memory bounded by a small
   multiple of the block size.
7. `extract` is implemented in terms of `stream` and its behaviour is unchanged,
   including the failure paths.

**Row ranges**

8. A row range of a fixed-width column equals the corresponding slice of the
   whole column.
9. A row range of a dictionary-encoded text column returns the right labels.
10. Reading rows 500 000–500 050 of a 10⁷-row table reads fewer bytes than
    reading the table, by the same instrumented measure as criterion 2.

**Cues**

11. `build_cues(uid, n)` on a photon payload yields cues whose `offset` is the
    first byte of the event at `event`, verified by decoding from that offset and
    comparing the first event against a full decode.
12. `TTTR("run.pto|m001.ptu", first, n)` returns macro times, micro times and
    channels identical to the corresponding slice of a full open.
13. A container with cues is read correctly by a build that ignores them.
14. A generic EBML parser still walks a container with cues, finding `Cues` as a
    top-level element with a known size.
15. Cues survive `compact`, and are dropped when their object is removed.

**Streaming in**

16. `add_file` produces a container byte-identical to `add` given the same
    bytes, same name, same reserve.
17. Embedding a 4 GiB file with `add_file` holds resident memory to a small
    multiple of the block size.
18. `add_file` on a missing or unreadable path returns 0 and sets `error()`.

**Everywhere**

19. Every new entry point is reachable from Python, R, Java and JavaScript, and
    appears in the PRD-015 conformance case list.
20. `doc/formats/pto.rst` moves Cues out of *Reserved for later* and specifies
    the element; the PTO.MFDB profile is unaffected and needs no version bump.

## Test plan

`test/python/test_pto.py` gains a section per part, written against a real
container rather than a synthesised one, as the existing tests are.

Byte-volume assertions (criteria 2 and 10) need a counter, not a timer — a wall
clock on a warm page cache measures the cache. Add a build-time hook that
accumulates `fread` volume for the store reader, or compare against a
deliberately cold file; the point is that the reader *did not touch* the bytes,
which timing can only suggest.

For criterion 11, the honest check is a decode from the cue offset compared
against a full decode, not a comparison of two numbers this library computed.

## Notes for whoever implements it

Part 1 is a couple of hours and unblocks the container's users; do it first and
ship it on its own. Parts 2 and 5 are mechanical and independent. Part 3 needs
care in the dictionary-encoded case. Part 4 is the only one that touches the
format, and it should not start until the others are in, because it is the only
one that can be got wrong in a way that is written to disk.

The trap in Part 4 is that a cue is *advisory* and must be treated as such on
read: a cue that is wrong should produce a slower decode, never a wrong answer.
Seek to the nearest cue **at or before** the target and decode forward, and
never trust a cue to be exact.

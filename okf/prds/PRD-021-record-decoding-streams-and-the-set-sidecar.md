# PRD-021 — Decoding a buffer, reading a stream, and the whole `.set` sidecar

> **PRD #:** 021 · **Status:** ✅ Implemented · **Created:** 2026-08-07 · **Updated:** 2026-08-07 · **Owner:** tpeulen

> **Implementation notes.** All three parts are in, with four departures from
> the text below — three forced, one an addition the acceptance criteria
> demanded.
>
> - **`decode_records` is a `TTTR` method, not a free function.** The proposal
>   says it "fills a `TTTR` rather than returning arrays", and a method is what
>   that is: `t.decode_records(buffer, record_type, state)`. `TTTRDecodeState`
>   is a plain struct with the overflow counter, a record count and an event
>   count. Python additionally gets `tttrlib.decode_records(buffer, rt, state,
>   tttr)` as sugar, which accepts a `uint32` array as well as bytes.
> - **The buffer is bytes in every binding, not `uint32[]`.** One shape rather
>   than two, because a `uint32` array cannot express a six-byte SPC-600 record
>   and an overload per width would have to be resolved in four binding
>   generators. Python's wrapper views a `uint32` array as bytes for free.
> - **A ranged read reports macro times relative to `first_record`.** It has to:
>   the overflow count at that record is not in the records, and finding it
>   means reading every record before it, which is the cost the ranged read
>   exists to avoid. That is not a limitation of the composition —
>   `container_read_records` + `decode_records` from record 0 with a carried
>   state gives times identical to a whole-file read, and criterion 4 is
>   asserted that way. It is a property of `container_read_events` and of the
>   `first_record` / `n_records` reader parameters, documented on both.
> - **`TTTR::apply_container_channels` had to be added.** Criterion 4 says the
>   composition reproduces `TTTR(spec)` for *every* ranged container, and for
>   two of the seven it did not: a Carl Zeiss ConfoCor3 record has no channel
>   field at all (the header names the one channel), and an SPC-QC record
>   splits the detector across a router signal and a module input whose width
>   is in the header. Macro times, micro times and event types were already
>   identical for both; only the channels were the raw ones. The two private
>   fix-ups `read_file` already ran are now reachable through one public method,
>   called once after the last chunk.
>
> **A parser bug the conformance suite caught before the real data did.** A
> `.set` value may contain a comma — `#SP [SP_SCF_FN,S,C:\BHdata\a,b.cfg]` is a
> legal line — and splitting on the *last* comma, which `io_bh.cpp`'s
> header-tag parser does, turns that value into `b.cfg` and its type into
> `S,C:\BHdata\a`. It cannot reach the old parser, which reads five numeric
> keys and nothing else. It reaches this one, which reads every line. The
> synthetic sidecar case found it on the first run; neither reference file has
> such a value.
>
> **Measured against the two reference sidecars:** 222 parameters for
> `FocalCheck_A1_20x_8xzoom_750nm_m1.set` and 207 for `bh_spcqc004.set`,
> against the ≥115 / ≥121 the criteria ask for. The `#SP` + `#PR` counts alone
> are exactly the 115 and 121 the table below states; the surplus is the `#DI`
> display block, the indexed `#TR` / `#WI` rows and the identification header.
>
> **`read_bh_set_file` is unchanged**, and a test asserts the five tags it
> extracts agree with what the new parser reports for the same file.
>
> **All four runners pass**, the eight new cases included: Python 86/86,
> JavaScript 15/15 areas, Java 80/80, R 80/80.
>
> **Three PRD-020 bugs in the R runner, found by running it for the first
> time.** R had not been installed when PRD-020 landed, so its conformance ops
> were written by analogy and never executed. All three are in `test/r/`, none
> is in the library:
>
> - `file.write_text` used `writeLines`, which appends a newline. The other
>   three runners write the string's bytes and nothing else, so
>   `pto.a_byte_range_of_a_payload` read one byte too many *in R only*.
> - **`pto_read_store`'s generated R dispatcher cannot be satisfied at all.** It
>   tests `extends(argtypes[4], '_p_std__vectorT_std__string_t')`, so it wants a
>   wrapped `VectorString`; the typemap behind the wrapper it then calls is
>   `std_vector.i`'s, which does `Rf_coerceVector(..., STRSXP)` and dies with
>   "cannot coerce type 'externalptr' to vector of type 'character'". A plain
>   character vector satisfies the typemap and not the dispatcher; the proxy
>   satisfies the dispatcher and not the typemap. Calling the numbered overload
>   directly (`pto_read_store__SWIG_1`) is the only way through. Same class of
>   SWIG-R codegen defect as the scoped-enum one in `ext/r/tttrlib.i`.
> - The comment above that op asserted the opposite ("a plain character vector
>   matches no overload at all"), which is what an untested guess looks like.
>
> `pto_store_columns` / `pto_store_groups`, by contrast, *do* come back as
> native R character vectors — SWIG-R converts a returned `std::vector<T>` and
> proxies a parameter one, and the two directions do not match.
>
> **The downstream `_process_bh_spc_records_numba` is not in this repository**,
> so deleting it is a change to ChiSurf and is not part of this diff. What
> makes it deletable — `decode_records` with a carried state, reachable from
> Python — is.

## Summary

Three gaps in the same seam, found by asking why a downstream package still has
Becker & Hickl code of its own. The answer in every case was *the library can do
this and does not expose it*, so the caller re-implemented the half it needed.

1. **There is no way to decode records that are not in a file.** Every decoder
   sits behind `TTTR(filename)`. A caller holding a buffer — from a card, a
   socket, a container it unpacked itself — has to write the decoder again.
2. **There is no way to read a container incrementally.** [PRD-020](PRD-020-pto-streaming-and-targeted-reads.md)
   gave PTO targeted reads and streaming; the other **thirteen** container types
   are all-or-nothing. A file is opened whole or not at all.
3. **The `.set` sidecar is read for five tags out of ~120**, and the function
   that reads them is not in any binding.

None of the three is a performance problem, and that is worth saying at the top
because it is the natural assumption. `RecordProcessor<BH_RECORD_TYPE_SPC130>`
is *fast*: reading and decoding 299 999 records from a file costs 1.50 ms
(200 M records/s), against 0.66 ms for a hand-written numba copy decoding the
same records already in memory (457 M records/s). The copy exists because there
is no entry point, not because the library was slow.

## Motivation

### 1. Decoding a buffer

ChiSurf's acquisition path receives SPC-130 records from the card in memory and
decodes them with `_process_bh_spc_records_numba`, whose docstring says outright
that it "mirrors tttrlib's `RecordProcessor<BH_RECORD_TYPE_SPC130>`". It is a
hand-maintained copy of library code, and it has to be, because the library's
Python surface offers only:

* `TTTR(filename)` — decodes a *file*;
* `append_events(...)` — takes events that are **already** decoded.

Neither takes a buffer of undecoded records.

**Measured today it agrees exactly** — same event count, and macro times, micro
times and routing channels identical over all 174 438 events of `m000.spc`. That
is the good case and it is not stable. A copy of a decoder with nothing holding
it to the original is how two implementations come to disagree about an overflow
run, a gap flag or a marker on somebody's data months later, with no error
anywhere. The downstream package has pinned it with a test against this library
for now; the fix is to delete the copy.

The decoder state is the part that makes this a real interface rather than one
function: SPC-130 carries an **overflow counter across chunk boundaries**, so a
caller decoding a stream in pieces must be able to hand the state back in. The
numba copy takes `initial_overflow` and returns `final_overflow` for exactly
this reason — that is the shape the library should own.

### 2. Reading a stream

PRD-020 established the argument for PTO: a container whose purpose is to hold
large payloads is, on the read side, all-or-nothing. The same is true of every
other container this library reads, and for the same reason.

Fourteen container types are registered — PTU, HT3, SPC-130, SPC-600_256,
SPC-600_4096, PHOTON-HDF5, CZ-RAW, SM, PHOTONS, SPC-QC, BRIGHTEYES-TTR,
FLIMLABS-STT1, FLIMLABS-ITT1, PTO — and exactly one of them can be read in
pieces. Everything downstream that wants a progress bar, a first look at a large
file, or a live view of a file still being written has to either wait for the
whole read or write its own reader.

This is also the half that makes 1 useful for files rather than only for cards:
"give me records `[first, first+n)` of this container, undecoded" plus "decode
this buffer, carrying state" is a chunked reader anyone can write in five lines,
in any of the four bindings.

### 3. The `.set` sidecar

`read_bh_set_file` extracts `SP_IMG_X`, `SP_IMG_Y`, `SP_PIX_CLK`, and for
SPC-QC `SP_TAC_R` and `SP_ADC_RE`. That is the right scope for a *photon
reader* — they are what the 4-byte SPC header cannot carry.

It is a small fraction of the file. Measured on two real sidecars:

| file | sections | parameters | of which the five the library reads |
|---|---|---|---|
| `FocalCheck_A1_20x_8xzoom_750nm_m1.set` | 1 | **115** | 5 |
| `bh_spcqc004.set` | 1 | **121** | 5 |

The rest is the hardware configuration the measurement was taken with — CFD
levels, TAC range and gain, sync divider, collection time, dead-time
compensation, the `#PR` printer/plot block, the `#SP` setup block. A downstream
package that drives an SPC card re-implements the parser to reach it
(`chisurf/plugins/core/acq/tcspc_devices/bh_spc/reader.py`), and *that* parser
does not read the imaging tags, so the same file is parsed twice by two
implementations that each ignore what the other wants.

And `read_bh_set_file` is C++-only: it is `%include`d in no binding, so no
Python, R, Java or JavaScript caller can reach even the five tags.

## Proposal

### Part 1 — `decode_records`: a buffer in, events out, state carried

```
DecodeState                     # opaque, cheap to copy, one per stream
decode_records(buffer, record_type, state) -> (n_events, state)
```

* fills a `TTTR` (or an events struct) rather than returning arrays, so the
  existing accessors apply and nothing is copied twice;
* `record_type` is the same enum the file readers dispatch on, so **every**
  record type is covered by construction rather than one at a time;
* `state` carries the overflow counter and anything else a format accumulates.
  Passing a default-constructed state decodes a buffer standalone; passing the
  returned one continues a stream. This is the whole interface, and it is what
  makes chunked decoding correct rather than approximately correct;
* the buffer is a typed array in the bindings (`uint32[]` for the 32-bit record
  formats, `uint8[]` where a format's record is not word-aligned).

**Delete `_process_bh_spc_records_numba` downstream** when this lands, together
with the test pinning it.

### Part 2 — ranged and streaming reads for every container

```
container_n_records(spec)                    # how many, without decoding
read_records(spec, first, n)                 # undecoded, into a buffer
TTTR(spec, first, n)                         # decoded, the common case
```

* `read_records` + `decode_records` compose into a chunked reader in any
  binding, which is the point: the library does not have to own the loop;
* `TTTR(spec, first, n)` covers the common case without one. **Note the trap
  PRD-020 hit**: a range cannot be added as a constructor argument where it is
  ambiguous with `TTTR(const char*, int container_type, bool read_input)`, so
  this goes through `set_container_parameters` and a named function, exactly as
  PTO's did;
* formats whose records are not fixed-width, or which cannot say how many
  records they hold without scanning, **say so** rather than guessing — a named
  decline, not a silent full read.

### Part 3 — the whole `.set` sidecar, in every binding

```
read_set_file(filename)   -> {section: {name: value}}
parse_set(content)        -> {section: {name: value}}
```

* every `#PR` and `#SP` parameter, not five; sections preserved;
* values stay **as text**, with the numeric interpretation left to the caller.
  A `.set` is a device configuration file and its types are per-parameter; a
  parser that guesses is a parser that is wrong about one of a hundred fields
  and silent about it;
* `read_bh_set_file`'s existing behaviour is kept as the photon-reader path —
  it feeds the imaging tags into the header and must not start returning
  everything;
* exposed in all four bindings, which is what makes the downstream parser
  deletable.

## Acceptance criteria

1. A buffer of SPC-130 records decoded through `decode_records` gives macro
   times, micro times and routing channels **identical** to `TTTR(file)` for the
   same records — asserted on a real `.spc`, over the whole file, not a prefix.
2. The same file decoded in **chunks** through the carried state gives the same
   answer as decoding it in one call. An overflow run split across a chunk
   boundary is a case in the suite, not an afterthought: that is the only thing
   the state exists for.
3. Every registered record type is reachable through `decode_records`, and a
   type that cannot be decoded from a buffer alone declines by name.
4. `read_records(spec, first, n)` + `decode_records` reproduces `TTTR(spec)`
   for every container that supports ranged reads, and the ones that do not say
   which they are.
5. `read_set_file` returns **≥ 115 parameters** for
   `FocalCheck_A1_20x_8xzoom_750nm_m1.set` and ≥ 121 for `bh_spcqc004.set`,
   and the five tags `read_bh_set_file` extracts are among them with the same
   values.
6. All three are reachable from Python, R, Java and JavaScript, and appear in
   the conformance case list — the standing rule from
   [PRD-015](PRD-015-conformance-suite-language-parity.md).
7. Nothing regresses: `TTTR(filename)` for every container type is byte-for-byte
   the same table it is today.

## Risks and non-goals

* **The state is the interface.** Get it wrong and chunked decoding is subtly
  wrong rather than broken — an overflow miscounted at one boundary shifts every
  macro time after it, and the file still reads. Criterion 2 is the guard, and a
  split *inside an overflow run* is the case that matters.
* **Not a live-acquisition API.** This gives a caller the pieces to decode what
  it has received; owning the socket, the card, the buffering or the threading
  is out of scope, and should stay out.
* **Not a `.set` writer beyond what exists.** `write_bh_set_file` keeps writing
  the imaging tags. Round-tripping a whole device configuration is a different
  contract, and nobody has asked for it.
* **Ranged reads are not seek-free for every format.** A container that has to
  scan to find record `first` is still doing that work; what changes is that it
  does not also have to decode and hold everything after it.
* **A copy that agrees today is the dangerous kind.** The downstream numba
  decoder is correct right now, which is precisely why the duplication has
  survived. Deleting it is part of this PRD's done, not a follow-up.

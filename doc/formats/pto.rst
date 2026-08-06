.. _pto_format:

PTO — the PhoTon cOntainer
==========================

:Name: **Pho**\ ton c\ **O**\ ntainer — PTO
:Version: 1.0
:Status: Specification. Normative.
:Extension: ``.pto``
:Media type: ``application/x-pto``
:Built on: EBML (:rfc:`8794`), DocType ``pto``

PTO is a **container and nothing else**. It binds a set of opaque payloads
together in one file, gives each one an identity, and lets typed metadata be
attached to any of them. It does not define what a photon stream is, what a
spectrum is, or what it means for one table to be derived from another. Those
are the application's business; PTO's business is that they can be *said*, found
again, and updated without rewriting the file.

**PTO invents no framing.** It is an EBML document, exactly as Matroska is, with
``DocType = "pto"``. Element headers, the variable-size integers, the value
types, ``Void``, ``CRC-32`` and the rules for skipping what you do not recognise
are all :rfc:`8794` and are not restated here except where PTO constrains them.
Where Matroska already has an element that means what PTO needs, **PTO uses
Matroska's element, with Matroska's ID**. Only what is genuinely new gets a new
ID.

That is deliberate. An existing EBML parser walks a ``.pto`` file today; a
generic dumper prints most of it with the right names; ``libebml`` reads it
without modification. The parts an implementer has to write are the parts that
are actually about photons.

.. contents:: On this page
   :local:
   :depth: 2

What PTO is for
---------------

One measurement produces a photon stream, then a burst search over it, then a
selection of those bursts, then a spectrum from the same sample, and a note
about why half of it was discarded. Today those live in as many files, related
by filename convention, and the relationship dies the moment somebody renames
one.

PTO puts them in one file, each with a stable UID, so an application can record
how they relate. **PTO does not record the relationship itself** — it provides
UIDs and typed tags that can reference them, and stops. See
:ref:`pto_provenance`.

Non-goals
~~~~~~~~~

- **A data model.** PTO does not know what a column is. Tabular payloads are
  normally :ref:`dstore <pto_encodings>` files; PTO neither parses nor validates
  them.
- **Compression.** Payloads may be compressed by their own encoding; the
  container does not compress. Photon data compresses poorly and the cost is
  paid on every read.
- **Concurrent writers.** One writer at a time. Readers may read a file being
  written, because a commit is atomic; see :ref:`pto_commit`.

Relationship to EBML and Matroska
---------------------------------

A PTO file is::

    EBML          (0x1A45DFA3)   the standard EBML Header, DocType "pto"
    Segment       (0x18538067)   everything else

Read :rfc:`8794` for the framing. The parts that matter most here:

- An element is a **VINT Element ID**, a **VINT Element Data Size**, and the
  data. Unknown IDs are skipped by their size — that is the whole
  forward-compatibility story, and it needs no version negotiation.
- Element Data Size **may be written in a longer VINT than strictly necessary**,
  precisely so it can be overwritten later with a bigger number. PTO leans on
  this; see :ref:`pto_inplace`.
- ``Void`` (``0xEC``) reserves or reclaims space, anywhere.
- ``CRC-32`` (``0xBF``) covers its siblings and must be first in its parent.
- Value types are Unsigned Integer, Signed Integer, Float, String (ASCII),
  UTF-8, Date, Master and Binary. **Date is nanoseconds from 2001-01-01T00:00:00
  UTC**, not the Unix epoch.

Elements borrowed from Matroska
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Used with Matroska's IDs and Matroska's meaning:

.. list-table::
   :header-rows: 1
   :widths: 26 16 12 46

   * - Element
     - ID
     - Type
     - Used in PTO for
   * - ``Segment``
     - 0x18538067
     - master
     - The root. Everything is inside it.
   * - ``SeekHead``
     - 0x114D9B74
     - master
     - The index. Two of them; see :ref:`pto_commit`.
   * - ``Seek``
     - 0x4DBB
     - master
     - One index entry.
   * - ``SeekID``
     - 0x53AB
     - binary
     - The ID of the element pointed at.
   * - ``SeekPosition``
     - 0x53AC
     - uint
     - Its position, relative to the start of ``Segment``'s data.
   * - ``Info``
     - 0x1549A966
     - master
     - Facts about the file as a whole.
   * - ``SegmentUUID``
     - 0x73A4
     - binary
     - 16 random bytes identifying this file across copies and renames.
       (Formerly ``SegmentUID``.)
   * - ``Title``
     - 0x7BA9
     - utf-8
     - A human title.
   * - ``MuxingApp``
     - 0x4D80
     - utf-8
     - The library that wrote the container: ``"tttrlib 0.27.0"``.
   * - ``WritingApp``
     - 0x5741
     - utf-8
     - The application: ``"chisurf 26.x"``.
   * - ``DateUTC``
     - 0x4461
     - date
     - When the file was created.
   * - ``Attachments``
     - 0x1941A469
     - master
     - **The objects.** See below.
   * - ``AttachedFile``
     - 0x61A7
     - master
     - **One object.**
   * - ``FileUID``
     - 0x46AE
     - uint
     - Its identity. Required, non-zero.
   * - ``FileName``
     - 0x466E
     - utf-8
     - A label. Not unique, not a path.
   * - ``FileDescription``
     - 0x467E
     - utf-8
     - Prose about it.
   * - ``FileMediaType``
     - 0x4660
     - string
     - RFC 6838 media type. (Formerly ``FileMimeType``.)
   * - ``FileData``
     - 0x465C
     - binary
     - The payload. Possibly gigabytes.
   * - ``Tags``
     - 0x1254C367
     - master
     - Typed metadata.
   * - ``Tag``
     - 0x7373
     - master
     - One tag with its targets.
   * - ``Targets``
     - 0x63C0
     - master
     - What the tag is about.
   * - ``TagAttachmentUID``
     - 0x63C6
     - uint
     - **The object targeted.** Absent ⇒ the file as a whole.
   * - ``TargetTypeValue``
     - 0x68CA
     - uint
     - Informational scope level.
   * - ``SimpleTag``
     - 0x67C8
     - master
     - One name/value pair. Nestable.
   * - ``TagName``
     - 0x45A3
     - utf-8
     - The name.
   * - ``TagString``
     - 0x4487
     - utf-8
     - A text value.
   * - ``TagBinary``
     - 0x4485
     - binary
     - An opaque value.
   * - ``TagLanguage``
     - 0x447A
     - string
     - As Matroska. Default ``"und"``.
   * - ``Void``
     - 0xEC
     - binary
     - Free space and padding.
   * - ``CRC-32``
     - 0xBF
     - binary
     - Integrity of a master's children.

**An object is a Matroska ``AttachedFile``.** That is the single biggest reuse
here and it is not a stretch: an attachment is already a blob with a UID, a
name, a media type and a description, which is exactly what a photon stream
needs to be to a container that does not understand photons. Reusing it means
provenance targeting reuses ``TagAttachmentUID`` too, so the whole
tag-targets-object mechanism is Matroska's, unmodified.

Elements PTO adds
~~~~~~~~~~~~~~~~~

PTO-specific elements take four-octet IDs in the block ``0x1E54xxxx``. Matroska
assigns nothing beginning ``0x1E``, so there is no collision, and the ``0x1E``
prefix makes a PTO extension obvious in a hex dump.

.. list-table::
   :header-rows: 1
   :widths: 30 14 10 46

   * - Element
     - ID
     - Type
     - Meaning
   * - ``PtoKind``
     - 0x1E54F001
     - utf-8
     - What the object is for. :ref:`pto_kinds`. Required in ``AttachedFile``.
   * - ``PtoEncoding``
     - 0x1E54F002
     - string
     - How ``FileData`` is encoded. :ref:`pto_encodings`. Required.
   * - ``PtoRowCount``
     - 0x1E54F003
     - uint
     - Rows or events in the payload, so a listing need not decode it.
       Advisory.
   * - ``PtoGeneration``
     - 0x1E54F010
     - uint
     - In ``SeekHead``: which of the two is live. :ref:`pto_commit`.
   * - ``PtoSeekUID``
     - 0x1E54F011
     - uint
     - In ``Seek``: the ``FileUID`` this entry points at, when it points at an
       ``AttachedFile`` rather than a top-level element.
   * - ``PtoTagIndex``
     - 0x1E54F020
     - int
     - In ``SimpleTag``: position within an array; ``-1`` (default) for a
       scalar. Exists so a PicoQuant header, which repeats a name once per
       element, survives verbatim.
   * - ``PtoTagSourceType``
     - 0x1E54F021
     - uint
     - The type code this tag had in the format it came from, so a PTU header
       can be written back bit-exact. Informational.
   * - ``PtoTagUInt``
     - 0x1E54F022
     - uint
     - A tag value.
   * - ``PtoTagInt``
     - 0x1E54F023
     - int
     - A tag value.
   * - ``PtoTagFloat``
     - 0x1E54F024
     - float
     - A tag value.
   * - ``PtoTagDate``
     - 0x1E54F025
     - date
     - A tag value.
   * - ``PtoTagUID``
     - 0x1E54F026
     - uint
     - **A tag value that references an object.** The primitive provenance is
       built from.
   * - ``PtoTagUIDs``
     - 0x1E54F027
     - binary
     - Several, as big-endian ``u64``.
   * - ``PtoTagFloats``
     - 0x1E54F028
     - binary
     - An array, as IEEE-754 binary64, big-endian.
   * - ``PtoTagInts``
     - 0x1E54F029
     - binary
     - An array, as two's-complement ``i64``, big-endian.
   * - ``PtoAnnotations``
     - 0x1E54F100
     - master
     - Top-level. Notes written by people.
   * - ``PtoAnnotation``
     - 0x1E54F101
     - master
     - One note.
   * - ``PtoAnnotationTarget``
     - 0x1E54F102
     - uint
     - A ``FileUID``. May repeat. Absent ⇒ the file.
   * - ``PtoAnnotationFirstRow``
     - 0x1E54F103
     - uint
     - First row the note concerns.
   * - ``PtoAnnotationLastRow``
     - 0x1E54F104
     - uint
     - One past the last. Both absent ⇒ the whole object.
   * - ``PtoAnnotationText``
     - 0x1E54F105
     - utf-8
     - The note.
   * - ``PtoAnnotationAuthor``
     - 0x1E54F106
     - utf-8
     - Who or what wrote it.
   * - ``PtoAnnotationDate``
     - 0x1E54F107
     - date
     - When.

Annotations are separate from tags because they are prose for people, not values
for programs, and a reader listing metadata usually wants one and not the other.

The EBML Header
---------------

Standard, with::

    DocType            = "pto"
    DocTypeVersion     = 1
    DocTypeReadVersion = 1
    EBMLMaxIDLength    = 4
    EBMLMaxSizeLength  = 8

A reader must refuse a file whose ``DocTypeReadVersion`` exceeds what it
implements, and must accept a higher ``DocTypeVersion``, skipping what it does
not know.

File shape
----------

::

    EBML                      DocType "pto"
    Segment
      SeekHead                index A   [CRC-32, PtoGeneration, Seek...]
      Void                    slack, so A can be rewritten in place
      SeekHead                index B
      Void                    slack, so B can be rewritten in place
      Info
      Attachments
        AttachedFile          photon stream, FileData 8 GiB
        Void                  slack, so FileData can grow
        AttachedFile          burst table
        ...
      Tags
      Void
      PtoAnnotations
      Void                    reclaimed space from an earlier rewrite

Order is not fixed and a reader must not depend on it; the two ``SeekHead``\ s
come first only so that opening a file is two short reads.

.. _pto_objects:

Objects
-------

An ``AttachedFile`` with ``PtoKind`` and ``PtoEncoding`` added::

    AttachedFile
      FileUID        0x9F2C7A0155E3C104      required, non-zero, unique
      PtoKind        "photons"               required
      PtoEncoding    "dstore"                required
      FileName       "channel 0-3"
      FileMediaType  "application/x-dstore"
      PtoRowCount    1013994218
      FileData       <8.1 GiB>

An object with no ``FileData`` is legal: a placeholder that tags and annotations
can still reference.

``FileData`` **should be the last child written**, so a multi-gigabyte payload
can be streamed straight into the file. Write the ID, then an eight-octet Data
Size VINT holding a placeholder, then stream; when the length is known, seek
back eight bytes and write it. :rfc:`8794` permits the over-wide VINT expressly
for this. No temporary file, no size known in advance, no second pass.

.. _pto_kinds:

Kinds
~~~~~

``PtoKind`` says what an object is *for*. The list is open.

``photons``
    A photon stream: per-event macro time, micro time, channel, event type.
``table``
    Any other tabular data — bursts, localisations, fit results.
``spectrum``
    Intensity against wavelength or energy.
``image``
    A raster.
``attachment``
    Something carried along that PTO says nothing about: the original vendor
    file, a protocol PDF, a screenshot.

Kind is a hint about meaning; **encoding is the instruction**. A reader that
does not recognise a kind can still read the object.

.. _pto_encodings:

Encodings
~~~~~~~~~

``PtoEncoding`` says how to decode ``FileData``. The list is open; an
unrecognised encoding is an object the reader skips, not a file it rejects.

``dstore``
    A tttrlib native store file. **The default for anything tabular**, and the
    reason PTO needs no table format of its own: dstore already carries columns
    with their dtypes, dictionary-encoded text, validity masks, a row selection
    and a group tree. See :doc:`../saving-tables`.
``ptu``, ``ht3``, ``spc``, ``sm``, ``photons``, ``ttr``, ``bin``
    A vendor container, byte for byte. Embedding the original beside the decoded
    stream is the point: the decode can be redone, checked or corrected later.
``hdf5``
    An entire HDF5 file as a blob. **This is the only way HDF5 appears in a PTO
    file** — as cargo, never as the container.
``photon-hdf5``
    The same, where the HDF5 file is known to be Photon-HDF5.
``tiff``, ``png``, ``csv``, ``json``, ``npy``
    What they say.
``raw``
    Undifferentiated bytes; tags say what they are.

Nothing stops an object's payload being another ``.pto``. It is a blob like any
other.

.. _pto_tags:

Tags
----

Matroska's ``Tags`` structure, with typed values added. A ``SimpleTag`` carries
``TagName`` and **exactly one** value element, which may be Matroska's
``TagString`` or ``TagBinary``, or one of PTO's typed ones. A ``SimpleTag`` with
no value element is a flag: its presence is the information.

::

    Tag
      Targets
        TagAttachmentUID   0x3A71...        the burst table
      SimpleTag
        TagName            "MeasDesc_Resolution"
        PtoTagFloat        8.0e-11
        PtoTagSourceType   0x20000008       PTU tyFloat8, so it round-trips

The typed values cover PicoQuant's twelve header types, so a PTU header converts
without loss and, with ``PtoTagSourceType``, converts back:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - PTU type
     - PTO value element
   * - ``tyEmpty8``
     - none — the ``SimpleTag`` carries only a name
   * - ``tyBool8``
     - ``PtoTagUInt`` (0 or 1)
   * - ``tyInt8``, ``tyBitSet64``
     - ``PtoTagInt`` / ``PtoTagUInt``
   * - ``tyColor8``
     - ``PtoTagUInt``
   * - ``tyFloat8``
     - ``PtoTagFloat``
   * - ``tyTDateTime``
     - ``PtoTagDate`` (converted; the raw OLE double is recoverable from
       ``PtoTagSourceType`` plus the date)
   * - ``tyFloat8Array``
     - ``PtoTagFloats``
   * - ``tyAnsiString``, ``tyWideString``
     - ``TagString``
   * - ``tyBinaryBlob``
     - ``TagBinary``

``TagName`` is opaque to the container. Dotted names are conventional and mean
nothing to PTO. **The prefix ``pto.`` is reserved by this specification**;
applications should use their own.

.. _pto_provenance:

Provenance
----------

**PTO provides the mechanism and defines none of the meaning.** This is the part
an implementer is most likely to get wrong by trying to be helpful.

What the container guarantees:

1. Every object has a ``FileUID`` that is stable for the life of the file —
   across in-place updates, appends and compaction.
2. A tag can name one or more objects as its target, via ``TagAttachmentUID``.
3. A tag's *value* can be an object reference, ``PtoTagUID``, or a list of them.

That is enough to express any relation as a labelled edge: the target is the
subject, ``TagName`` is the predicate, ``PtoTagUID`` is the object. PTO stops
there. It does not define ``derived_from``. It does not check that a referenced
UID exists. It does not detect cycles, and it will not stop an application
recording that a spectrum was derived from a burst selection.

The reason is that applications disagree, and a container that picks a winner
becomes wrong for everyone else. chisurf owns its provenance model and writes it
in its own tag namespace; another tool writes another; both files stay readable
by both.

A conventional shape, offered and not required::

    Tag
      Targets
        TagAttachmentUID   0x3A71...              the bursts
      SimpleTag
        TagName            "chisurf.derived_from"
        PtoTagUID          0x9F2C...              the photons
      SimpleTag
        TagName            "chisurf.step"
        TagString          "burst search"
      SimpleTag
        TagName            "chisurf.parameters"
        TagString          "{\"m\": 10, \"T\": 500e-6}"

"Everything that mentions this object" is one pass over the ``Tags`` element,
looking for the UID as a target or as a value. That is a few kilobytes.

.. _pto_inplace:

Updating in place
-----------------

The requirement is a multi-gigabyte photon stream in the same file as a burst
table that gets recomputed, without rewriting gigabytes. Two EBML features do
it, and neither is a PTO invention.

**An over-wide Data Size VINT.** :rfc:`8794` allows an Element Data Size to be
written in more octets than the value needs, so it can later be overwritten with
a larger value without moving anything. A writer that expects a payload to grow
writes its size in eight octets from the start.

**``Void``.** Space is reserved by putting a ``Void`` after the element, and
reclaimed by turning a dead element into one. Adjacent ``Void``\ s may be
coalesced; a ``Void`` may be split, as long as each part is at least two octets
— one for the ID, one for a zero size. **Never leave exactly one spare octet**,
because no element fits in it.

So, to rewrite an object's payload:

*If the new payload fits in ``FileData``'s data size plus the following
``Void``:* write the bytes, write the new size into ``FileData``'s size VINT,
resize the ``Void`` to what is left, rewrite the index, commit. Nothing moves,
and no other object is touched.

*If it does not:* write a new ``AttachedFile`` — same ``FileUID`` — into a large
enough ``Void`` or at the end of the ``Segment``, turn the old one into
``Void``, rewrite the index, commit. **The UID survives; the offset does not**,
which is why nothing except the index may hold offsets.

Deleting is turning the ``AttachedFile`` into ``Void`` and dropping its index
entry. Tags targeting it become dangling, which is allowed — an application may
want to remember that something was there. A writer may remove them; it must not
be assumed to have done so.

Compacting is copying every live element to a new file, dropping the ``Void``\ s,
and renaming over the original. UIDs are preserved, offsets are not. It is the
only way space comes back, and it is the same bargain HDF5 makes with
``h5repack``.

.. _pto_commit:

Committing, and what a crash leaves behind
------------------------------------------

A ``Segment`` contains **two** ``SeekHead`` elements, each carrying a
``CRC-32`` and a ``PtoGeneration``.

  **The live index is the ``SeekHead`` with the greater ``PtoGeneration`` whose
  ``CRC-32`` verifies.**

To commit:

1. Write payloads and elements. Flush.
2. Rewrite the ``SeekHead`` that is **not** live — in place, into its reserved
   ``Void`` — with the new entries, a correct ``CRC-32``, and
   ``PtoGeneration`` one greater than the live one. Flush.

A crash before step 2 leaves the old index live and the file exactly as it was;
anything half-written is unreferenced and will be reclaimed as ``Void``. A crash
*during* step 2 leaves a ``SeekHead`` whose ``CRC-32`` fails, and the reader
falls back to the other, which is intact. There is no window in which a reader
sees a file that never existed.

This is shadow paging, and in EBML it costs one extra ``SeekHead``.

Both ``SeekHead``\ s must be given enough reserved ``Void`` at creation to hold
the largest index the file will need, since growing one in place is what the
scheme depends on. A writer that runs out must fall back to compaction.

Two consequences worth stating:

- **Step 2 must not touch the live ``SeekHead``.** A writer that rewrites the
  live one has removed its own fallback.
- Neither ``SeekHead`` is the truth. Every element carries its own size, so a
  reader can rebuild the index by walking the ``Segment`` from its first child.
  A reader **should** do that when neither ``CRC-32`` verifies.

The index
~~~~~~~~~

Matroska's ``SeekHead``, used as Matroska uses it — one ``Seek`` per top-level
element (``Info``, ``Attachments``, ``Tags``, ``PtoAnnotations``) — plus, for
PTO, optionally one ``Seek`` per ``AttachedFile``, distinguished by
``PtoSeekUID``::

    SeekHead
      CRC-32
      PtoGeneration   7
      Seek  { SeekID 0x1549A966,  SeekPosition 4096 }              Info
      Seek  { SeekID 0x1941A469,  SeekPosition 4600 }              Attachments
      Seek  { SeekID 0x61A7, PtoSeekUID 0x9F2C..., SeekPosition 4700 }
      Seek  { SeekID 0x61A7, PtoSeekUID 0x3A71..., SeekPosition 8700000000 }
      Seek  { SeekID 0x1254C367, SeekPosition 8712000000 }         Tags

``SeekPosition`` is relative to the first byte of ``Segment``'s *data*, as in
Matroska.

Without the per-object entries a reader can still find every object by walking
``Attachments`` and skipping each ``FileData`` by its size — seeks, not reads.
With a dozen objects that is a dozen seeks, which is why the per-object entries
are optional.

Alignment, and reading a payload in place
-----------------------------------------

EBML guarantees no alignment: an element header is a variable number of octets,
so ``FileData`` lands wherever it lands. For a payload a caller wants to
``mmap`` and use without copying — a column of ``double`` — a writer **should**
insert a ``Void`` immediately before the ``AttachedFile`` sized so that
``FileData``'s data begins on an 8-byte boundary, and **should** record nothing
about having done so.

A reader **must not assume it**. Check the offset; map it if it is aligned, copy
it if it is not. An alignment guarantee that a conformant writer may omit is not
a guarantee, and pretending otherwise is how a reader acquires a rare crash on
somebody else's file.

Implementing PTO
----------------

A conformant **reader** must:

- parse EBML per :rfc:`8794`, refusing an unknown ``DocTypeReadVersion``;
- pick the live ``SeekHead`` by ``PtoGeneration`` and ``CRC-32``, and fall back
  to walking the ``Segment`` if neither verifies;
- skip unknown element IDs by their Data Size, at every level;
- treat a ``SimpleTag`` with an unrecognised value element as a tag whose value
  it cannot represent, not as a broken file.

It does **not** need to understand ``dstore``, or any encoding at all, to list a
file's objects and read its tags. That is deliberate: everything the container
says about itself is EBML, so a listing tool is an EBML parser plus a table of
twenty-odd IDs.

A conformant **writer** must:

- give every object a non-zero ``FileUID``, unique in the file;
- write ``PtoKind`` and ``PtoEncoding`` on every object;
- follow the two-``SeekHead`` commit, including the flushes;
- never rewrite the live ``SeekHead``;
- leave ``Void`` of zero or at least two octets, never one.

It **should** write ``FileData`` last with an over-wide size VINT, reserve
``Void`` after payloads it expects to grow, and write ``MuxingApp`` and
``DateUTC`` so a file can be traced to what made it.

Why not tar, zip, or HDF5
-------------------------

**tar** was the first thing considered, and it is genuinely attractive:
``tar xf`` works everywhere, so "others can implement it" becomes "others
already have". It fails on one requirement. A tar member's size is written
before its payload and members are laid end to end with no slack, so growing one
by a byte means rewriting every byte after it. With a multi-gigabyte photon
stream in the file, changing a burst selection would rewrite gigabytes. The same
objection retires **zip**, which additionally does not align payloads and needs
zip64 past 4 GiB.

**HDF5** solves all of this and is a container in its own right. It is not used
as one because it is a large dependency to require of anything that wants to
read the file, because its own free-space handling has the same
``h5repack``-shaped hole, and because the point of PTO is to bind together
payloads it does not understand — including HDF5 files. HDF5 appears in a PTO
file as an object with ``PtoEncoding = "hdf5"``, which is the relationship that
was wanted.

**EBML**, meanwhile, was designed for exactly this and has been carrying
multi-gigabyte files with in-place metadata edits for twenty years.

Reserved for later
------------------

Anticipated and deliberately unspecified in 1.0, so that adding them is a
``DocTypeVersion`` bump rather than a format break:

- **Per-object checksums.** ``CRC-32`` already applies to a master's children,
  but a CRC over eight gigabytes of ``FileData`` costs more on every open than
  the container saves. It belongs behind an explicit verify, with its own
  element.
- **Cues.** Matroska's ``Cues`` (0x1C53BB6B), repurposed as an index *into* a
  payload — "event 10\ :sup:`9` starts at byte X" — for seeking within a photon
  stream without decoding it.
- **External payloads.** An object whose ``FileData`` is replaced by a URI, for
  data too large to embed.
- **Signing.** A signature over the live ``SeekHead``, which covers the file
  transitively once per-object checksums exist.

An implementation of 1.0 will read a 1.x file, because all of these are new
element IDs and unknown IDs are skipped by size.

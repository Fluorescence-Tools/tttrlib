.. _pto_format:

PTO — the PhoTon cOntainer
==========================

:Name: **Pho**\ ton c\ **O**\ ntainer — PTO
:Version: 1.0
:Status: Specification. Normative.
:Extension: ``.pto`` (a profile tags the stem: ``.mmfdb.pto``, see below)
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

.. _pto_naming:

Naming: ``.pto`` and ``.mmfdb.pto``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A ``.pto`` is **a container and no more** — an EBML document with
``DocType "pto"``, holding payloads whose meaning is the application's business.
The extension makes exactly that claim, which is why it is safe to put on a file
holding anything.

A **profile** constrains what goes inside one, and says so by tagging the *stem*:
:ref:`PTO.MFDB <pto_mfdb_format>`, the profile that carries a
single-molecule measurement and its analyses, uses

.. code-block:: text

   measurement.mmfdb.pto

``.mmfdb.pto``, not ``.pto.mmfdb``: the final suffix stays ``.pto`` so every
reader, file dialog and MIME table that dispatches on it still recognises the
file, and everything in this specification continues to apply. Read it the way
``.tar.gz`` is read — one format, an inner tag on the name.

The suffix is a convenience for people and directory listings. A reader decides
what a file conforms to by reading the container-level tags inside it, never by
its name; a conforming file that was renamed is still conforming, and a
``.mmfdb.pto`` written without those tags is not.

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
  **All** of them: the ``Void`` that pads an element out is a sibling and is
  covered. Checksumming only the part that carries information is
  self-consistent and unverifiable by anything else.
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
   * - ``Cues``
     - 0x1C53BB6B
     - master
     - Top-level. An index *into* payloads. :ref:`pto_cues`.
   * - ``CuePoint``
     - 0xBB
     - master
     - One entry of it.
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
   * - ``PtoCueUID``
     - 0x1E54F030
     - uint
     - In ``CuePoint``: the ``FileUID`` of the object this entry indexes.
   * - ``PtoCueEvent``
     - 0x1E54F031
     - uint
     - The event ordinal, counted from 0 within that payload.
   * - ``PtoCueOffset``
     - 0x1E54F032
     - uint
     - Where that event starts, in bytes from the start of ``FileData``.
   * - ``PtoCueTime``
     - 0x1E54F033
     - uint
     - The macro time at that event. Optional, and absent means 0.
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
    DocTypeVersion     = 2
    DocTypeReadVersion = 1
    EBMLMaxIDLength    = 4
    EBMLMaxSizeLength  = 8

A reader must refuse a file whose ``DocTypeReadVersion`` exceeds what it
implements, and must accept a higher ``DocTypeVersion``, skipping what it does
not know.

``DocTypeVersion`` is 2 because :ref:`pto_cues` exist. The read version stays 1,
which is the whole point of the distinction: cues are a new element ID inside a
new master, a 1.0 reader skips both by size, and the file it gets is the file it
would have got anyway -- one it has to decode from the start.

File shape
----------

::

    EBML                      DocType "pto"
    Segment                   Data Size in eight octets, rewritten as it grows
      SeekHead                index A   [CRC-32, PtoGeneration, Seek..., Void]
      SeekHead                index B
      Info
      Attachments
        AttachedFile          photon stream, FileData 8 GiB
      Void                    slack, so that FileData can grow
      Attachments
        AttachedFile          burst table
      Tags
      PtoAnnotations
      Void                    reclaimed space from an earlier rewrite

Order is not fixed and a reader must not depend on it; the two ``SeekHead``\ s
come first only so that opening a file is two short reads.

Three consequences of the layout, each of which an implementer will otherwise
have to rediscover:

- **A ``Segment`` has a known size, written in eight octets and rewritten as the
  file grows.** Not an unknown-size Master: a known size is what lets a reader
  tell a complete file from a truncated one, and eight octets is what lets the
  number be raised without moving anything after it.
- **``Attachments`` may appear many times, once per object.** Matroska permits
  one; PTO permits any number, and a reader concatenates them. Adding an object
  is then appending an element rather than growing one, and an object that
  outgrows its space can be moved on its own.
- **Each ``SeekHead`` is padded to a fixed reserve with a trailing ``Void``**
  (8 KiB in the reference implementation), because the commit protocol depends
  on being able to rewrite one where it lies. A writer whose index outgrows the
  reserve must compact rather than move it.

Bytes after the end of the ``Segment`` are not part of the file: they are an
abandoned write, from a session that extended the file and died before the
commit that would have claimed them. A writer opening such a file **should**
turn them into a ``Void`` and take them into the ``Segment``, which is the only
thing that stops a repeatedly-interrupted file growing forever.

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

.. _pto_bundling:

Bundling files
~~~~~~~~~~~~~~

A measurement is rarely one file. The instrument file, the settings sidecar it
cannot be read without, a table computed from it and the protocol somebody wrote
travel as a folder — and arrive with the sidecar missing. Putting that folder in
one container is what the format is for, and tttrlib's writer decides each
object's ``PtoKind``, ``PtoEncoding`` and ``FileMediaType`` from the file rather
than asking the caller to:

.. code-block:: bash

    tttr pto pack -o run.pto measurement/     # a folder becomes one file
    tttr pto add run.pto late-note.md         # bundle more into an existing one
    tttr pto extract-all run.pto out/         # the folder back again

.. code-block:: python

    f = tttrlib.PtoFile()
    f.create("run.pto", "DNA ruler, run 4")
    tttrlib.pto_bundle(f, "measurement/")
    f.commit()

Three rules make the folder survive the trip, none of them normative — a
conforming writer may do otherwise, and a reader is told everything it needs by
the elements themselves:

- **The name proposes and the bytes dispose.** A file some photon format claims
  by extension is offered to the content sniffers, and the ``PtoEncoding`` it
  gets is that format's own name — ``spc-130`` rather than ``spc``, which four
  formats claim. A file no photon format claims is never sniffed: several
  formats recognise a container by little more than its record size dividing
  evenly, and would take a small ``.png`` for a photon stream. Everything else
  is named by extension, and anything unrecognised is an ``attachment`` encoded
  ``raw`` — carried, named, and left alone.
- **A directory means everything under it**, each object named by its path
  relative to that directory (``raw/m001.ptu``), so two files of the same name
  in different folders stay two files and the directory comes back as it was.
- **A ``.set`` is tied to the ``.spc`` beside it** with ``pto.sidecar_of``
  (see :ref:`pto_provenance`), which is what makes the pair readable
  afterwards: a Becker & Hickl reader handed the ``.spc`` alone silently reads
  half a header.

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

Note what does **not** happen: nothing is reshuffled. The object being rewritten
is the only one that can move, and it moves only when it no longer fits. Every
other object stays exactly where it is, whatever its size — which is the whole
point, because the object that does not fit is the burst table and the object
beside it is eight gigabytes of photons. An object that is never updated is
never moved, so a photon stream added once keeps its offset for the life of the
file, and the :ref:`cues <pto_cues>` over it stay valid for the life of the file
too. A relocated object's cues do not: they index bytes that are no longer
there, so a writer **must** drop them, and this one does.

Space left behind is reused by the next object that fits in it, so a file does
not grow on every update — only when nothing already free is big enough.
Repeated growth still fragments, and ``compact`` is the answer.

Deleting is turning the ``AttachedFile`` into ``Void`` and dropping its index
entry. Tags targeting it become dangling, which is allowed — an application may
want to remember that something was there. A writer may remove them; it must not
be assumed to have done so.

Compacting is copying every live element to a new file, dropping the ``Void``\ s,
and renaming over the original. UIDs are preserved, offsets are not. It is the
only way space comes back, and it is the same bargain HDF5 makes with
``h5repack``.

A compacted file is not necessarily free of ``Void``: the padding that puts each
payload on a boundary is structural, not a hole, and it stays. A writer that
wants none at all — for an archive, or a copy about to be sent somewhere — may
drop the padding too, at the cost of :ref:`unaligned payloads <pto_align>`, and
a writer that expects the file to keep being edited may do the opposite and
reserve room after every object so the next few updates rewrite in place.
tttrlib spells these ``compact(to, tight=True)`` and
``compact(to, reserve=0.25)``.

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

.. _pto_align:

Alignment, and reading a payload in place
-----------------------------------------

EBML guarantees no alignment: an element header is a variable number of octets,
so ``FileData`` lands wherever the object's name and encoding strings happen to
leave it. That is fine for bytes and wrong for what a payload actually is here —
a PTU record stream is ``uint32``, a ``.dstore`` column is ``double`` — and a
caller that wants to ``mmap`` the file and point at one without copying needs
the first byte on a boundary.

So a writer **should** insert a ``Void`` immediately before the ``Attachments``
element, sized so that ``FileData``'s data begins on an 8-byte boundary, and
**should** record nothing about having done so. Eight rather than four costs
nothing to say and covers the 32-bit case as well; it is also the alignment
``.dstore`` uses for its own blobs, and those offsets are relative to the store,
so an embedded store is only aligned inside if it begins aligned outside.

tttrlib does this, on every path that lays an object down — including the one an
object takes when it outgrows its room and is relocated, which is a fresh
lay-down and not a move. The padding is the reason a compacted file still
carries a little ``Void``: at most a boundary's worth per object.

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
  it cannot represent, not as a broken file;
- bound every allocation by what the file can actually contain before believing
  a Data Size, since that number comes out of the file;
- treat a ``FileUID`` as a full ``uinteger``, whatever range the writer used;
- treat payload alignment and :ref:`cues <pto_cues>` as advisory — both are
  writer SHOULDs, so check the offset and seek to the nearest cue *at or
  before* the target rather than trusting either.

It does **not** need to understand ``dstore``, or any encoding at all, to list a
file's objects and read its tags. That is deliberate: everything the container
says about itself is EBML, so a listing tool is an EBML parser plus a table of
twenty-odd IDs.

A conformant **writer** must:

- give every object a ``FileUID`` that is non-zero and **unique in the file**;
- write every ``FileUID`` in **eight octets**, whatever its value;
- write the ``Segment`` Data Size in eight octets and keep it current;
- write ``PtoKind`` and ``PtoEncoding`` on every object;
- follow the two-``SeekHead`` commit, including the flushes;
- leave ``Void`` of zero or at least two octets, never one;
- never rewrite the live ``SeekHead``.

It **should** write ``FileData`` last with an over-wide size VINT, pad so the
payload starts 8-byte aligned, reserve ``Void`` after payloads it expects to
grow, and write ``MuxingApp`` and ``DateUTC`` so a file can be traced to what
made it.

.. _pto_fileuid:

The FileUID
~~~~~~~~~~~

A ``uinteger``, non-zero, and unique within the file — the same contract
Matroska gives ``TrackUID``. Random is the obvious way to get it and random is
**not** unique: a writer has to check a fresh uid against the objects already
there, which costs a scan of a list that is tens of entries long.

The **eight-octet width is a real requirement**, not a style. ``FileUID`` is the
one element ever rewritten where it lies: when an object outgrows its room it is
written afresh with a new uid, and the old uid is then written back over it to
keep the identity. Packed to the smallest width that held it, a replacement can
be *shorter* — and every sibling after it, ``PtoKind`` and ``PtoEncoding``
among them, is then read from the wrong offset. Nothing fails at the time of
writing; the object simply comes back later with an empty encoding.

.. note::

   **tttrlib mints uids below 2**\ :sup:`53`. That is a writer's choice the
   format permits, not a rule: the element is a full ``uinteger``, a reader must
   treat it as one, and a container from another writer may use the whole range
   and is read correctly here.

   The bound was introduced because a 64-bit integer did not reach every
   binding: a JavaScript ``Number`` and an R ``numeric`` are both IEEE doubles,
   so a wider uid came back a *nearby value* naming no object. Both are now
   fixed where the fix belonged — in the binding. JavaScript routes 64-bit
   scalars through ``BigInt``; R carries a uid as a character string, the only
   thing base R holds exactly. **A container using the whole range is read
   correctly everywhere.**

   The bound stays because it costs nothing observable — 9×10\ :sup:`15`
   identities, with uniqueness enforced rather than left to chance — and a uid
   that fits a double is one fewer thing for the next binding to get wrong.

Reading part of one
-------------------

The container exists so a multi-gigabyte payload is cheap to keep beside a table
that gets recomputed, and that has to hold on the read side too. Every entry
point below reads its part and not the whole:

.. code-block:: python

    f = tttrlib.PtoFile()
    f.open("run.pto")

    # what is in the embedded table, without decoding a single column
    tttrlib.pto_store_columns(f, uid)

    # two columns of a four-gigabyte table: two seeks
    tttrlib.pto_store(f, uid, columns=["Tau", "n_photons"])

    # rows 500 000-500 050 of it, which is what a table viewer asks for
    tttrlib.pto_store(f, uid, first_row=500_000, n_rows=50)

    # 4 KiB of any payload, at any offset -- short at the end, like a file read
    f.read(uid, at=1 << 20, n=4096)

    # events 10^6 .. 10^6+1000 of an embedded photon stream, via its cues
    tttrlib.pto_events("run.pto|m001.ptu", 1_000_000, 1000)

and reclaiming space, once a file has been edited enough to be worth it:

.. code-block:: python

    f.compact("run-lean.pto")                 # holes gone, payloads aligned
    f.compact("run-archive.pto", tight=True)  # no Void at all; unaligned
    f.compact("run-work.pto", reserve=0.25)   # room to keep editing in place

and on the way in, ``add_file`` embeds a file without holding it:

.. code-block:: python

    f.add_file("photons", "ptu", "m001.ptu", "/data/m001.ptu")

The same range is reachable through the reader-parameter mechanism, which is
what makes it work from every binding and from anything that opens a file by
name alone::

    TTTR("run.pto|m001.ptu", "PTO", '{"first_event": 1000, "n_events": 500}')

.. _pto_cues:

Cues
----

An index *into* a payload: "event 10\ :sup:`9` starts at byte X". Without one,
reaching the middle of an embedded photon stream means decoding everything
before it, which is the read-side version of the all-or-nothing the container
exists to avoid.

``Cues`` is a top-level child of ``Segment``, uses Matroska's ID for the same
reason everything else here does — the job is Matroska's, an index that says
where in a stream something is — and holds ``CuePoint`` masters::

    Cues                  0x1C53BB6B   master, top level
      CuePoint            0xBB         master
        PtoCueUID         0x1E54F030   uint   which object this indexes
        PtoCueEvent       0x1E54F031   uint   event ordinal
        PtoCueOffset      0x1E54F032   uint   byte offset into FileData
        PtoCueTime        0x1E54F033   uint   macro time there, optional

What sits inside a ``CuePoint`` is PTO's, because Matroska's cues address a
timecode in a track and these address an event ordinal in a payload.

Four rules, and the first is the one that matters:

**A cue is advisory.** A reader seeks to the nearest cue *at or before* the
event it wants and decodes forward from there. A cue that is wrong then costs a
slower decode and can never cost a wrong answer. Nothing may be returned on the
strength of a cue alone.

**Cues are written on demand, never automatically.** Building them is a pass
over a payload, so it is a thing a caller asks for
(:cpp:func:`tttrlib::io::PtoFile::build_cues`) when the payload is going to be
read in pieces often enough to be worth it. A container with no ``Cues``
element is fully valid.

**Spacing is the writer's choice.** One cue per 10\ :sup:`6` events on a
10\ :sup:`9`-event stream is a thousand cues — a few tens of kilobytes — and
bounds any subsequent decode to 10\ :sup:`6` records.

**A cue dies with the bytes it indexes.** Removing an object or replacing its
payload drops its cues. Compaction does not: it moves a payload without
changing a byte inside one, so an offset into it is still an offset into it.

``PtoCueTime`` is what makes a decode from a cue produce the same macro times as
a decode from the start. The overflow count at a byte offset is not recoverable
from the records there — it is the sum of every overflow record before it — so a
reader that starts at a cue gets macro times short by exactly that. The cue's
own macro time closes the gap: the first event decoded is the cue's event, and
the difference between what it should be and what it came out as applies to
every event after it.

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
- **External payloads.** An object whose ``FileData`` is replaced by a URI, for
  data too large to embed.
- **Signing.** A signature over the live ``SeekHead``, which covers the file
  transitively once per-object checksums exist.

An implementation of 1.0 will read a 1.x file, because all of these are new
element IDs and unknown IDs are skipped by size.

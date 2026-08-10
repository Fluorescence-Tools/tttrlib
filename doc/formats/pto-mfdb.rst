.. This file is GENERATED from okf/specs/pto-mfdb.md by
   build_tools/docs/generate_spec_rst.py -- do not edit it here.
   Edit the markdown and re-run: pixi run docs-specs

.. _pto_mfdb_format:

PTO.MFDB — the MMFDB profile of the photon container
====================================================

.. note::

   The target for analysis output: **one measurement is one file**, decomposable
   back into the original data with a guarantee. Current state lives in
   *data IO* and *burst companions*;
   the gap is in the *assessment*.

**Profile version 1.1.** Profiles PTO 1.0 (``DocType "pto"``). Read version 1.

Purpose
-------

PTO is a container and deliberately knows nothing about photons: it binds
opaque payloads, gives each a UID, and lets typed metadata reference them.
PTO.MFDB is the layer that says what goes in one — which kinds, which
encodings, which names, and what a reader may rely on.

It is named for the vocabulary, not for a program. Every controlled term comes
from the *MMFDB* dictionaries, so a ``.pto`` and a row in
the database say the same thing in the same words, and any tool that speaks that
vocabulary can write a conforming file. A profile named after one application
would have contradicted the tool-agnostic claim the vocabulary exists to make.

Its responsibility ends at the file. It does not define analysis. Where the
container and the profile disagree, the container wins — PTO.MFDB constrains
PTO, it never contradicts it.

Design principles
-----------------

- **The container is not ours.** PTO 1.0 is frozen. This profile adds no element
  ID, no framing, and no requirement a generic EBML parser could not skip.
- **No term is invented here.** A kind, an encoding, a step, a relation and a
  grain are all values of dictionary enumerations. Where upstream flrCIF has no
  term — it has none for photon files, detectors, anisotropy, bursts or typed
  analysis parameters — the term is defined in the MMFDB extension dictionary
  and cited from there. A writer needing a word it cannot find does not coin
  one; it adds one to the dictionary first.
- **The instrument file is the truth.** It goes in verbatim, is never decoded
  into a second copy beside itself, and comes back out byte-for-byte.
- **A file says what it counts.** Every table declares its grain, and relations
  between tables are declared keys. Nothing is joined by position.
- **Provenance is a serialised MMFDB subgraph**, not a second, parallel model.
- **Drift is detectable.** A file records the dictionary revision its terms came
  from, so a later reader can tell a renamed term from a typo.

What ChiSurf writes, and what it only reads
-------------------------------------------

The problem this profile closes is not that ChiSurf reads many formats — it must,
because instruments and other programs write many, and a reader is an import
path that costs nothing. It is that ChiSurf **wrote** many, for things that are
one thing.

The split is by direction, not by count:

.. list-table::
   :header-rows: 1

   * - what
     - ChiSurf writes
     - why not a file of its own
   * - a measurement — photons and everything derived from them
     - ``.pto``
     - the pieces are related, and a filename convention cannot say so
   * - a curve — decay, correlation, anisotropy, IRF, model, residual
     - a ``curve_point`` artifact
     - five arrays and two units; the differences between curve types are what the *units* and the *kind* say
   * - a table — bursts, dwells, pixels, molecules, tracks, states
     - an artifact at its grain
     - the grain is the difference; the storage is not
   * - a raster
     - TIFF, carried inside the container
     - a scientific raster stays readable by every other tool
   * - a project — datasets, fits, a session
     - ``.csp``
     - a different scope: many measurements
   * - metadata for deposition
     - mmCIF
     - an export, in the vocabulary this profile already uses

Everything else — the ``…4`` companion family, ``<source>.imaging.h5``, ``kristine``,
``pycorrfit``, Photon-HDF5, CSV, the vv/vh stack — is an **import source** or an
**export the user asks for by name**. Both are fine and both stay. What is not
fine is a *new* way for ChiSurf to write something that already has a home.

Before this, a curve could be saved as CSV, as YAML, through ``save_xy``, through
the vv/vh stack, or through one of the FCS writers: five ChiSurf-authored ways
to write the same five arrays, **none of which could say what the x axis was
in**. An FCS lag axis is milliseconds and a TCSPC axis is nanoseconds; nothing
about the numbers says which, and reading one for the other is a mistake that
surfaces as a diffusion time wrong by a factor of a million rather than as an
error.

``test/test_formats_are_consolidated.py`` holds a **shrinking** allow-list of the
modules still permitted to write a curve in a format of their own.

Target architecture
-------------------

Layering
~~~~~~~~

::

   flrCIF     upstream community vocabulary (samples, probes, distances, lifetimes)
      │       — has no term for a photon file, a detector, a burst, or a grain
   MMFDB      the extension dictionary: artifact kinds, formats, operation types,
      │       relations, grains, integrity — every term this profile uses
   PTO.MFDB   this profile: which of those a container carries, and where
      │
   PTO 1.0    EBML/RFC 8794, Matroska element ids — frozen

File layout
~~~~~~~~~~~

::

   EBML                DocType "pto"
   Segment
     SeekHead ×2       the atomic commit
     Info              Title, WritingApp, SegmentUUID
     Attachments
       AttachedFile    THE README — first object, ASCII, how to read this file
     Attachments
       AttachedFile    THE INSTRUMENT FILE — verbatim, immutable
     Attachments       the measurement's mmCIF metadata, when it has any
     Attachments       every derived artifact, appended, each with its own reserve
     Tags              artifacts, operations, edges, version stamps
     PtoAnnotations    notes for people

The instrument payload is the **first** object and is never rewritten, so its
offset is stable for the life of the file and no recomputation can disturb it.
Reserve belongs to the derived objects, which are the ones that change.

The file explains itself
~~~~~~~~~~~~~~~~~~~~~~~~

The **first object is a plain-ASCII README**, ``artifact_kind = readme``,
``data_format = text``. It is not documentation *about* the format; it is the
format telling a reader what it is, in the file, in compact language:

- that this is EBML (RFC 8794), and how an element is framed, so the bytes can
  be walked by hand;
- which element IDs matter, by number, so no table is needed elsewhere;
- what the kinds and encodings mean, and that ``dstore`` is a columnar table with
  a described header;
- **how to get the original instrument file back** — find the
  ``tttr_photon_stream`` object, write its ``FileData`` to a file, check it against
  the recorded SHA-256;
- that nothing is compressed, encrypted, or stored outside the file.

The reason is not tidiness. A container outlives the software that wrote it, and
the person who needs it most is the one for whom the library will not install.
A specification in another repository is no use to them; a paragraph at the
front of the file is.

It is the first *object*, not the first byte — the container reserves space for
its two indexes ahead of everything, so the text begins some kilobytes in and is
found with ``strings`` rather than ``head -c 4096``. Nothing can precede it without
changing the container format, which this profile does not do.

What the measurement is
~~~~~~~~~~~~~~~~~~~~~~~

Provenance says a burst table came from a photon stream by a burst search. It
does not say which sample, which dyes, which buffer, which instrument — and a
file that cannot answer those is a record of a *computation*, not of a
*measurement*.

So a container may carry an object of ``artifact_kind = sample_metadata``,
``data_format = cif``: an mmCIF block in the same vocabulary as everything else —
flrCIF for samples, probes and conditions, PDBx where it applies, ``mmfdb_*`` for
what neither covers. It is carried **whole**, as a block, rather than flattened
into tags, because a category with several rows (two probes, three detector
channels) is a loop and a tag is a name/value pair.

Two rules:

- **Every name is checked against the dictionaries before it is written.** Prose
  in a field that looks structured is worse than an absent field, because a
  later reader cannot tell the two apart.
- **A measurement with nothing to say says nothing.** No empty block is written.
  An empty one would claim the measurement was described when it was not.

An object is an artifact
~~~~~~~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - PTO
     - PTO.MFDB
     - Term source
   * - ``AttachedFile``
     - one artifact
     - —
   * - ``PtoKind``
     - ``artifact_kind``
     - ``_mmfdb_artifact.artifact_kind``
   * - ``PtoEncoding``
     - ``data_format``
     - ``_mmfdb_artifact.data_format``
   * - ``PtoRowCount``
     - ``row_count``
     - ``_mmfdb_artifact.row_count``
   * - ``FileMediaType``
     - ``mime_type``
     - ``_mmfdb_artifact.mime_type``
   * - ``FileName``
     - a label, not an identity
     - —
   * - ``FileUID``
     - container-local handle only
     - —

``FileUID`` is local to the file; the identity that survives export is
``_mmfdb_artifact.artifact_id``, carried as a tag. Tag names **are** mmCIF item
names, so an object's tags read as the artifact row they are.

Grain, and why position is never used
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A table declares ``_mmfdb_artifact.row_grain`` — what one row *is*: a ``photon``, a
``burst``, a ``dwell``, a ``pixel``, a ``molecule``, a ``track``, a ``frame``.

This is the fact a positional companion format cannot state, and the cost of not
stating it is concrete. A dwell subdivides a burst, so a dwell table is *finer*
than a burst table; a fused burst is made of several source bursts, so a fused
table is *coarser*. A format that can only carry one row per burst has nowhere
to put either, and both end up outside it — as extra files, or as a write into
another analysis's directory.

So relations are declared, not counted:

- an edge names its join columns with ``_mmfdb_edge.source_row_column`` and
  ``_mmfdb_edge.target_row_column``;
- a many-to-many or many-to-one relation is its own artifact,
  ``artifact_kind = row_mapping``, linked with ``relationship_type = maps_rows_of``;
- an artifact may have **several** parents. ``PtoTagUIDs`` carries a list, and an
  edge is one row per parent, so a fused burst naming two sources is ordinary.

A row an analysis skipped is **absent**, not a sentinel. Absence is information;
a placeholder row destroys it.

What a column is, and what it is in
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A column carries a name and a dtype, which is enough to read a table and not
enough to understand one. A burst duration is milliseconds; a lifetime is
nanoseconds; a TAC channel is picoseconds. Historically that was recorded only
in the column *name*, when whoever wrote it remembered — ``Duration (ms)`` and
``Tau`` sit in the same table, and ``Count Rate (KHz)`` capitalises the kilo.

So a column carries **one extensible description** rather than a growing list of
fields, and the name is an attribute of it:

::

   {"name": "Duration", "units": "milliseconds", "item": "_mmfdb_burst.duration"}

The keys are ``_mmfdb_column.*`` items — ``name``, ``units``, ``item``, ``description`` —
and ``units`` takes a ``_mmfdb_column.units`` term. Spellings follow mmCIF's
``ITEM_UNITS_LIST`` wherever it has the unit — 15 of the 28 are already its own,
including ``nanoseconds`` and ``counts``. Its 78 codes have an odd gap: it carries
``nanoseconds`` and ``femtoseconds`` but neither ``milliseconds`` nor ``picoseconds``,
and no concentration, rate multiple or count of photons. Those 13 are defined in
the MMFDB dictionary rather than invented per call site.

The symbol a person reads — ``ns``, ``kHz`` — is ``_mmfdb_units.symbol``, in the same
table, with the SI factor beside it. Nothing else may carry an abbreviation: a
display that invents one is how ``Count Rate (KHz)`` came to capitalise the kilo.

Two rules that matter more than they look:

- **The description travels with the column, not with the file.** A
  column-subset read gets the units too, which is the whole point — a caller
  reading two columns out of a four-gigabyte table still learns what they are.
- **No unit means the unit is unknown.** It does not mean dimensionless.
  ``dimensionless`` is a positive claim, for a ratio that genuinely has none — an
  efficiency, an anisotropy — and a writer that is unsure says nothing instead.

Units are not parsed back out of column names. That convention is what this
replaces: it is inconsistent, it is missing on exactly the columns that need it
most, and a regular expression over it would be the same convention with more
machinery on top and the same blind spots.

Provenance
~~~~~~~~~~

One operation per analysis, written as file-level tags using ``_mmfdb_operation.*``
item names: ``operation_type``, ``settings_json``, ``settings_hash``,
``software_package``, ``software_version``, ``started_at``, ``ended_at``, ``status``,
``dictionary_version``, ``dictionary_hash``.

``settings_hash`` is the identity of a run. Re-running an analysis with the same
settings **replaces** its artifact in place; changing a setting produces a new
one. This is what keeps one file from accumulating, and it removes the need to
encode parameters in a folder name.

The path must be reconstructible
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Provenance here is not a label saying which tool ran. It is enough to answer
*where did this number come from*, which means three things have to be present
on every derived object:

1. **What it came from** — ``_mmfdb_edge.source_node_id``, one tag per parent.
   Several is normal; a fused burst has more than one source.
2. **How it relates to it** — ``_mmfdb_edge.relationship_type``, a dictionary
   term (``derived_from``, ``calibrated_by``, ``maps_rows_of``…), plus the join
   columns when the grains differ.
3. **What was done, with what** — ``operation_type`` and the **complete**
   ``settings_json``, not a summary of it. A partial settings record is worse
   than none: it looks reproducible and is not.

These are three facts and are stored as three tags. They were briefly two: the
parent's UID was written *under* ``relationship_type``, because
``source_node_id`` — a column that has existed in the database schema since the
table was first created — was never declared in the dictionary. Asking a file
how a result related to what it came from therefore returned an integer, and
the relation itself was never recorded at all. Declared in
``mmfdb_flr_ext.dic`` 1.7.

``Measurement.lineage(ref)`` walks it, breadth-first from an object to the
instrument file; ``describe_lineage`` renders that as text. A container in which
some derived object cannot reach the primary data is malformed, and
``test/fio/test_pto.py`` checks every object in a real container rather than a
synthesised one.

Versions a file carries
~~~~~~~~~~~~~~~~~~~~~~~

Four, as file-level tags, because four things drift independently: the PTO
container version, the PTO.MFDB profile version, the MMFDB dictionary version and
hash, and the writing application's version. A reader finding a profile *read*
version above its own refuses the file; a higher *write* version is accepted and
unknown tags are skipped, exactly as EBML already treats unknown elements.

Rules
-----

1. The first object is a plain-ASCII README describing the container and how to
   recover the instrument file from it.
11. The instrument file is the first *payload* object, is stored verbatim, and is
   never updated or removed.
2. Every object carries ``artifact_id``, ``checksum`` and ``checksum_algorithm``.
   Extraction verifies the checksum and fails on mismatch.
3. Opening a file never hashes a payload. Verification is explicit.
4. Every controlled value resolves to an enumeration in the loaded dictionaries.
5. Every tabular object declares ``row_grain``.
6. No relation is expressed by row position; an edge joining rows names its key
   columns.
7. A skipped row is absent. Sentinel rows and interleaving are not part of this
   profile.
8. Tag names are mmCIF item names. The ``pto.`` prefix is reserved by the
   container; the profile does not use it.
9. A file records the container version, profile version, dictionary version and
   hash, and the writing application.
10. One writer at a time. Nothing is visible before commit.

Steering notes
--------------

Today's output is three rival containers and about thirty writers; the
*assessment* tracks the specifics. The direction of travel is:
the seam first, then the single-molecule writers, then the imaging writers, with
the legacy ``…4`` layout kept **readable** and produced only by an explicit export.

That export is lossy by construction for anything not at burst grain — it is a
compatibility shim, not the model — and it must say so rather than flatten
silently. Reproducing the interleave, the sentinel rows or the fixed grid inside
a ``.pto`` would import the workaround and lose the information a second time.

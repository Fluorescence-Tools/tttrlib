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

**Profile version 1.0.** Profiles PTO 1.0 (``DocType "pto"``). Read version 1.

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
       AttachedFile    THE INSTRUMENT FILE — first, verbatim, immutable
     Attachments       every derived artifact, appended, each with its own reserve
     Tags              artifacts, operations, edges, version stamps
     PtoAnnotations    notes for people

The instrument payload is the **first** object and is never rewritten, so its
offset is stable for the life of the file and no recomputation can disturb it.
Reserve belongs to the derived objects, which are the ones that change.

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

Versions a file carries
~~~~~~~~~~~~~~~~~~~~~~~

Four, as file-level tags, because four things drift independently: the PTO
container version, the PTO.MFDB profile version, the MMFDB dictionary version and
hash, and the writing application's version. A reader finding a profile *read*
version above its own refuses the file; a higher *write* version is accepted and
unknown tags are skipped, exactly as EBML already treats unknown elements.

Rules
-----

1. The instrument file is the first object, is stored verbatim, and is never
   updated or removed.
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

.. _fmt_picoquant_ht3:

PicoQuant HT3 (``.ht3``)
=========================

:Container id: ``1``
:Extension: ``.ht3``

What it is
----------

The HydraHarp's own binary container. Unlike PTU its header is a
fixed C structure rather than a tag list, which matters more than it sounds:
the ``Ident`` and ``FormatVersion`` fields are what select the record decoder,
so they must describe the records actually in the file.

Layout
------

A fixed binary header (``Ident``, ``FormatVersion``,
``CreatorName``, acquisition settings, per-channel input settings), then the
record stream.

Record encodings
----------------

HydraHarp v1 and v2 T2/T3, plus **SF compression** -- see below.

Notes
-----

**SF (Suren Felekyan) macro-time compression.** A plain HydraHarp v1
overflow record is an empty 32-bit word standing for exactly one macro-time
wraparound, so a long gap between photons costs thousands of records. The SF
variant puts a 24-bit *count* of wraparounds in that record's payload instead.

There is no header flag for it. tttrlib recognises SF by looking at the record
stream: a file whose header says HydraHarp v1 but whose overflow records carry a
non-zero payload is SF-compressed, because a genuine v1 payload is always zero.

The saving depends entirely on how sparse the stream is. On the files in the
test data the same photons occupy 12.0 MB as plain v1 and 1.34 MB as SF -- but
only 1.45x on a denser file. HydraHarp v2 also counts its overflows, so an
HHT3v2 file comes out byte-for-byte the same size as SF.

Reference data
--------------

`Sample files <https://gitlab.peulen.xyz/skf/tttr-data/-/tree/main/pq/ht3>`_ in the ``tttr-data`` repository.

.. seealso::

   :doc:`../file-formats` for the conversion matrix and what survives a
   transcode.

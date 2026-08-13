.. _fmt_picoquant_ptu:

PicoQuant PTU (``.ptu``)
=========================

:Container id: ``0``
:Extension: ``.ptu``

What it is
----------

PicoQuant's *unified* container, written by SymPhoTime and the
PicoHarp / TimeHarp / HydraHarp / MultiHarp acquisition software. It is the most
widely used TTTR format and the one tttrlib treats as canonical: its tagged
header -- a list of typed name/value entries -- is the shape tttrlib adopted for
every container's metadata, so a PTU header survives a conversion into any other
format more completely than the reverse.

Layout
------

``PQTTTR`` magic, a format version, then a sequence of typed tags
terminated by ``Header_End``. Each tag carries a name, an index (for repeated
tags), a type and a value. The record encoding is named by the
``TTResultFormat_TTTRRecType`` tag, and the records follow immediately.

Record encodings
----------------

Eight encodings: PicoHarp T2/T3, HydraHarp v1 and v2 T2/T3, and
the generic MultiHarp 150 / PicoHarp 330 T2/T3.

Notes
-----

Writing a PTU is the safest transcode target:
it can hold every record encoding tttrlib knows except SF compression, and its
tagged header carries arbitrary metadata forward.

Reference data
--------------

`Sample files <https://gitlab.peulen.xyz/skf/tttr-data/-/tree/main/pq/ptu>`_ in the ``tttr-data`` repository.

.. seealso::

   :doc:`../file-formats` for the conversion matrix and what survives a
   transcode.

.. _fmt_single_molecule_sm:

Single-molecule (SM) (``.sm``)
===============================

:Container id: ``7``
:Extension: ``.sm``

What it is
----------

A big-endian container used by several single-molecule
spectroscopy setups.

Layout
------

A big-endian header of length-prefixed strings, then the record
stream.

Record encodings
----------------

One encoding.

Notes
-----

``.sm`` files are accepted on their extension alone -- the contents
are not checked. ``isSMFile()`` exists and could be wired into detection, but
doing so would start rejecting files that load today.

Reference data
--------------

`Sample files <https://gitlab.peulen.xyz/skf/tttr-data/-/tree/main/sm>`_ in the ``tttr-data`` repository.

.. seealso::

   :doc:`../file-formats` for the conversion matrix and what survives a
   transcode.

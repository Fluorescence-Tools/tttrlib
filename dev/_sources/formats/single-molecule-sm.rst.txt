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

``.sm`` files are recognised from their contents: a big-endian ``uint32``
version of 2, followed by two length-prefixed strings whose lengths are
plausible and whose text is printable.

``isSMFile()`` used to read a native-endian ``uint64`` and compare it to 2. The
field is 32 bits and the format is big-endian, so on a little-endian machine the
first eight bytes of a real SM file read as 33554432 and the predicate rejected
every genuine file -- which is why detection fell back to trusting the
extension. With that fixed the check is real.

Reference data
--------------

`Sample files <https://gitlab.peulen.xyz/skf/tttr-data/-/tree/main/sm>`_ in the ``tttr-data`` repository.

.. seealso::

   :doc:`../file-formats` for the conversion matrix and what survives a
   transcode.

.. _fmt_photonscore_lincam:

Photonscore LINCam (``.photons``)
==================================

:Container id: ``8``
:Extension: ``.photons``

What it is
----------

The Photonscore LINCam wide-field detector's D7 container. The
detector is position-sensitive, so every photon carries an ``x``/``y``
coordinate as well as its arrival time.

Layout
------

A paged structure whose header carries the ``D7 Photons Data``
signature, with named datasets for the position and timing arrays.

Record encodings
----------------

None in the usual sense -- the file stores arrays.

Notes
-----

**Positions become marker events.** tttrlib's data model has no
per-photon coordinate, so ``x`` and ``y`` are carried as marker events
interleaved with the photons. That is a decision about the format, not a
limitation of the file: the information is preserved and can be read back.

Reference data
--------------

`Sample files <https://gitlab.peulen.xyz/skf/tttr-data/-/tree/main/photonscore>`_ in the ``tttr-data`` repository.

.. seealso::

   :doc:`../file-formats` for the conversion matrix and what survives a
   transcode.

.. _fmt_brighteyes_ttm:

BrightEyes-TTM (``.ttr``)
==========================

:Container id: ``planned``
:Extension: ``.ttr``

What it is
----------

The open-hardware time-tagging module from the Molecular
Microscopy and Spectroscopy group (IIT), used with SPAD-array detectors.
**Not yet supported by tttrlib.**

Layout
------

A bare little-endian ``uint16`` stream. No header, no magic, no
length -- which means it can never be identified from its contents, and the
acquisition parameters have to come from outside the file.

Each word is ``valid << 15 | ID << 8 | data``. ID 124 is the laser sync;
125, 126 and 127 are step bytes A, B and C with enable bits in bit 7; a word
with ``ID == 127`` ends a record.

Record encodings
----------------

The payload is a raw TDC code that needs a per-channel bin-width
calibration derived from the data itself.

Notes
-----

**Open design question.** The container API has nowhere to pass
``n_channels``, ``sysclk_MHz`` or ``laser_MHz``, and this format cannot work
without them. Settling that is part of the io interface work rather than
something to bolt on afterwards.

Reference data: an 82 MB sample from
`Zenodo 10.5281/zenodo.6782161 <https://doi.org/10.5281/zenodo.6782161>`_,
together with ``FORMAT.md`` and the vendor ``libttp`` reader source.

Reference data
--------------

`Sample files <https://gitlab.peulen.xyz/skf/tttr-data/-/tree/main/brighteyes>`_ in the ``tttr-data`` repository.

.. seealso::

   :doc:`../file-formats` for the conversion matrix and what survives a
   transcode.

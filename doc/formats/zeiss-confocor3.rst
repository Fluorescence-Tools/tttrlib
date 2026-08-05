.. _fmt_zeiss_confocor3:

Zeiss ConfoCor3 (``.raw``)
===========================

:Container id: ``6``
:Extension: ``.raw``

What it is
----------

The raw container written by Zeiss ConfoCor3 / LSM FCS systems. One
file per detection channel.

Layout
------

An ASCII banner -- ``Carl Zeiss ConfoCor3 - raw data file - version
3.000 - Channel 1`` -- which the settings structure overlays, so the channel
number arrives as an ASCII digit and the reader subtracts 48. The banner is also
what identifies the format.

Record encodings
----------------

One encoding.

Notes
-----

Detection used to fail on every genuine ConfoCor3 file: the sniffer
range-checked the settings fields without noticing they were characters. It now
matches the banner.

Reference data
--------------

`Sample files <https://gitlab.peulen.xyz/skf/tttr-data/-/tree/main/cz>`_ in the ``tttr-data`` repository.

.. seealso::

   :doc:`../file-formats` for the conversion matrix and what survives a
   transcode.

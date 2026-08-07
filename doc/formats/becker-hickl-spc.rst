.. _fmt_becker_hickl_spc:

Becker & Hickl SPC (``.spc``)
==============================

:Container id: ``2, 3, 4, 9``
:Extension: ``.spc``

What it is
----------

Four different formats sharing one extension: SPC-130, SPC-600 in
256-bin and 4096-bin mode, and the SPC-QC modules. All of them open with a
4-byte header, which is why telling them apart needs the record stream rather
than the filename.

Layout
------

A single 32-bit header word carrying the macro time clock, followed
by fixed-width records. SPC-QC stores its clock in femtoseconds.

Record encodings
----------------

One encoding per variant. SPC-QC has two, QC-x04 and QC-x06,
differing in how many bits the channel field takes.

Notes
-----

**The ``.set`` sidecar.** An SPC record file cannot store the CLSM
imaging geometry, so the vendor software keeps it in a companion ``.set`` file
beside the ``.spc``. tttrlib reads it for ``SP_IMG_X`` / ``SP_IMG_Y`` /
``SP_PIX_CLK``, and for SPC-QC also ``SP_TAC_R`` and ``SP_ADC_RE`` -- the QC
modules run their TAC independently of the macro clock, so the micro-time
resolution genuinely cannot be derived from the 4-byte header.

Those five are what a *photon reader* needs, and about four per cent of the
file; the rest is the hardware configuration the measurement was taken with.
:func:`tttrlib.read_set_file` and :func:`tttrlib.bh_set` return all of it --
see :ref:`bh_set_full`.

**SPC-600 must be named.** Its two modes cannot be distinguished from SPC-130 or
SPC-QC by content, so tttrlib will not guess: pass the container name
explicitly.

Reference data
--------------

`Sample files <https://gitlab.peulen.xyz/skf/tttr-data/-/tree/main/bh>`_ in the ``tttr-data`` repository.

.. seealso::

   :doc:`../file-formats` for the conversion matrix and what survives a
   transcode.

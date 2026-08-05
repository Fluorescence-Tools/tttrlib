.. _fmt_flim_labs_stt1:

FLIM LABS STT1 (``.bin``)
==========================

:Container id: ``planned``
:Extension: ``.bin``

What it is
----------

The spectroscopy time tagger written by FLIM LABS acquisition
software. **Not yet supported by tttrlib**, and the reason is data rather than
specification.

FLIM LABS writes *five* different ``.bin`` formats and only this one is a photon
stream. ``SP01`` (binned decay curves), ``SPF1`` (phasors), ``IT02`` (intensity
traces), ``FCS1`` (correlation curves) and ``IMG1`` (per-pixel binned decays)
are all analysis products -- there are no photons left in them, so tttrlib has
nothing to read them into.

Layout
------

``STT1`` magic (4 bytes), a little-endian ``uint32`` header length,
then a JSON header carrying the enabled channels and the laser period in
nanoseconds. All five FLIM LABS formats share this envelope.

Record encodings
----------------

17 bytes each: ``uint8`` event, ``float64`` micro time in
nanoseconds, ``float64`` macro time in nanoseconds. Event codes 70, 76 and 80
are ASCII ``F``, ``L`` and ``P`` for frame, line and pixel; anything else is a
channel index, reported as ``ch{event + 1}``.

The records are **not** sorted by arrival time -- the vendor's own reader sorts
them after loading.

Notes
-----

**No example file is published anywhere.** Searched across all 26
repositories in the `FLIM LABS GitHub organisation
<https://github.com/flim-labs>`_, its code search, and every committed binary:
``STT1`` appears exactly once in public code, in
`time_tagger_script.py <https://github.com/flim-labs/spectroscopy-py/blob/main/export_data_scripts/time_tagger_script.py>`_,
which is a reader with no accompanying data. The
`flim-labs PyPI package <https://pypi.org/project/flim-labs/>`_ that *writes*
these files is a Windows-only driver for their FPGA hardware and ships nothing.

The four published sample files -- ``SP01``, ``SPF1``, ``IT02``, ``FCS1`` -- are
mirrored in the reference data anyway: they verify the header envelope ``STT1``
shares, though not its records.

If you have an ``STT1`` file and can share it, please open an issue.

Reference data
--------------

`Sample files <https://gitlab.peulen.xyz/skf/tttr-data/-/tree/main/flimlabs>`_ in the ``tttr-data`` repository.

.. seealso::

   :doc:`../file-formats` for the conversion matrix and what survives a
   transcode.

.. _fmt_brighteyes_ttm:

BrightEyes-TTM (``.ttr``)
==========================

:Container id: ``10`` (``BRIGHTEYES-TTR``)
:Extension: ``.ttr``
:Read: yes
:Write: yes
:Identified from contents: never -- the container has to be named

What it is
----------

The open-hardware time-tagging module from the Molecular Microscopy and
Spectroscopy group (IIT): a Xilinx Kintex-7 TDC with roughly 30 ps resolution,
built for SPAD arrays of 25 or 49 elements on a scanning microscope.

.. code-block:: python

   import tttrlib

   # The container must be named -- see "Why it cannot be sniffed" below.
   data = tttrlib.TTTR("scan.ttr", "BRIGHTEYES-TTR")

   img = tttrlib.CLSMImage(
       tttr_data=data,
       marker_frame_start=[3], marker_line_start=2, marker_line_stop=2,
       marker_event_type=1, n_pixel_per_line=512,
       use_pixel_markers=True, marker_pixel=1,
   )

Layout
------

A bare little-endian ``uint16`` stream. No header, no magic, no length.

Each word is ``valid << 15 | ID << 8 | data``:

============  ==========================================================
ID            Meaning
============  ==========================================================
0..n-1        detector element; ``data`` is its raw TDC code
123           dummy
124           laser sync; ``data`` is the reference TDC code
125           step byte A (low 7 bits) + bit 7 = pixel clock
126           step byte B (mid 7 bits) + bit 7 = **frame** clock
127           step byte C (high 7 bits) + bit 7 = **line** clock; ends the record
============  ==========================================================

Records are variable length and delimited by ``ID == 127``.

Three things about this are easy to get wrong, and all three produce a
plausible-looking result rather than an error:

* **ID 126 is the frame clock and 127 the line clock**, not the other way
  round. The field names (``scan_enable``, ``line_enable``) invite the opposite
  reading; the vendor decoder does not.
* **The scanner enables are levels, and the markers are edges.** The enable
  stays asserted for as long as the scanner holds it, so emitting a marker for
  every set bit yields one per sample-clock tick instead of one per line.
* **The coarse counter is unwrapped at 65536**, not at the 2\ :sup:`21` the
  three 7-bit step bytes could hold, and *any* decrease is a wrap. This is the
  vendor's rule. Unwrapping at 2\ :sup:`21` stretches a 55-second acquisition
  to 1776 seconds.

The valid bit is not decoration: the FPGA emits detector words in aligned
pairs, exactly one of which has ``valid`` set. Ignoring it doubles the photon
count and invents arrivals at TDC code 0.

Why it cannot be sniffed
------------------------

Any file at all is a valid sequence of 16-bit words, so content detection would
match everything. ``.ttr`` takes no part in it, and the container must be named
or reached by its extension. This is a property of the format, not a gap in the
reader.

What the file does not contain
------------------------------

The sample clock, the laser repetition rate and the number of detector elements
are properties of the instrument, not of the file. They are supplied as
:ref:`container parameters <container_parameters>`, and the container declares
what it accepts so a caller can ask:

.. code-block:: python

   tttrlib.registry("file_container")["BRIGHTEYES-TTR"]["params_schema"]

========================  ========  =========================================
Parameter                 Default   Meaning
========================  ========  =========================================
``n_channels``            25        SPAD elements: 25 (5x5) or 49 (7x7)
``sysclk_MHz``            240       what a macro time tick is
``laser_MHz``             0         the period arrival times fold into
``tdc_ps_per_code``       0         crude linear stand-in for a calibration
``auto_calibrate_tdc``    false     measure the delay line from the data
``drop_filler``           true      discard the ``0x7FFF`` idle word
========================  ========  =========================================

A default is a guess: if the instrument differs, the times come out wrong rather
than absent, which is the failure mode worth being explicit about. A misspelled
parameter is refused rather than ignored, for the same reason.

.. _ttr_calibration:

Micro times: codes, and then times
-----------------------------------

By default a photon's micro time is its raw TDC code, differenced against the
laser code of its own record when there is one. That is what the file contains
and nothing more, and it is **not a duration** -- the delay line's taps are not
equally wide. The header says so, so downstream can refuse rather than fit
nonlinear codes that look like micro times:

==========================================  ===========================
``BrightEyes_MicroTimeCalibrated``          ``0`` or ``1``
``BrightEyes_MicroTimeUnit``                ``tdc_code`` or ``picoseconds``
==========================================  ===========================

Asking for a calibration turns the codes into arrival times:

.. code-block:: python

   data = tttrlib.TTTR(
       "scan.ttr", "BRIGHTEYES-TTR",
       '{"sysclk_MHz": 240, "laser_MHz": 80, "auto_calibrate_tdc": true}')

It is a separate, opt-in, clearly-named step because it is an **estimation**,
not parsing: the tap widths are measured from the code histogram of the file
itself -- over one clock period the arrival phase is uniform, so a tap that
collected twice the counts is twice as wide -- which means the same file read
over different subranges gives slightly different times. It also costs a second
pass over the data.

Three things change when it runs, and each is a decision the raw path
deliberately declines to make:

* Codes are converted through a measured per-channel table, not a scale factor.
* A photon is referenced to the **next** valid laser word, which is usually not
  the one in its own record. In the published 80 MHz sample only 27 % of photons
  have a laser word alongside them, so the same-record difference is not a
  micro time for most of the file.
* The TDC counts backwards -- it measures how long until the next clock edge --
  so the arrival time is the period minus that interval. Without the flip the
  decay comes out mirrored.

Micro times are then in bins of one nominal TDC least-significant bit (the clock
period over the 8-bit code space, 16.3 ps at 240 MHz), spanning one laser
period: 768 bins for an 80 MHz laser on a 240 MHz clock.

``tdc_ps_per_code`` is the vendor's crude fallback, a single scale for all
codes. It goes through the same pairing and the same flip -- those are
properties of the hardware, not of how the codes were scaled -- so it is an
approximate calibration rather than a different kind of answer.

Writing
-------

Macro times survive exactly, including the absolute offset. The counter in the
file is 16 bits and the reader recovers absolute time by counting decreases, so
the writer emits empty records -- which the hardware itself emits whenever a
tick passes and nothing happens -- to carry the counter across every gap larger
than one wrap.

Micro times are saturated to 8 bits, the width of the TDC payload. A photon that
arrived late reads as late, rather than aliasing back to early.

A photon on a channel the device does not have, or a marker that is not the
pixel, line or frame clock, is refused. There is no field for either, and a file
that quietly lacks them is worse than no file.

Verification
------------

The reader is checked photon-for-photon against the vendor's own ``libttp`` on
the 82 MB Zenodo sample: 3,898,599 photons, with macro times, routing channels
and micro times all identical. The writer is checked by round trip on the same
file -- all four event arrays bit-identical, markers included -- and by
reconstructing the 512x512 image from the re-read stream.

The calibration is checked by what it is for: binned into 64 bins of the laser
period, the calibrated decay is smooth to within 13 %, while the raw code
histogram deviates by 100 % at the widest tap. That comb is the delay line, not
the sample.

Reference data
--------------

An 82 MB sample from
`Zenodo 10.5281/zenodo.6782161 <https://doi.org/10.5281/zenodo.6782161>`_,
together with ``FORMAT.md`` and the vendor ``libttp`` reader source:
`sample files <https://gitlab.peulen.xyz/skf/tttr-data/-/tree/main/brighteyes>`_
in the ``tttr-data`` repository.

.. seealso::

   :doc:`../file-formats` for the conversion matrix and what survives a
   transcode.

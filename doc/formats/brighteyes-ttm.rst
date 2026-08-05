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
are properties of the instrument. ``TtrParams`` names them in one place --
defaults are 25 channels at 240 MHz -- so it is visible what a caller is
asserting rather than buried in defaults.

The TDC payload is a tapped-delay-line code whose bins are unequal, so a single
scale factor is an approximation; the proper conversion is a per-channel
bin-width (DNL) calibration derived from the code histogram. Left uncalibrated,
micro times stay in raw codes, which is honest: an uncalibrated code is not a
time.

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

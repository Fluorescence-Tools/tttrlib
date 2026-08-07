.. _fmt_flim_labs_stt1:

FLIM LABS time tagger (``.bin``)
=================================

:Container ids: ``11`` (``FLIMLABS-STT1``), ``12`` (``FLIMLABS-ITT1``)
:Extension: ``.bin``
:Read: yes
:Write: no -- see "Why it is read-only" below
:Identified from contents: by magic, always -- ``.bin`` claims nothing

What it is
----------

The time taggers written by the FLIM LABS acquisition software: ``STT1`` from
the Spectroscopy application, ``ITT1`` from Intensity Tracing and FCS.

.. code-block:: python

   import tttrlib

   data = tttrlib.TTTR("acquisition.bin")          # the magic is enough
   data = tttrlib.TTTR("acquisition.bin", "FLIMLABS-STT1")   # or say so

FLIM LABS write *five* different ``.bin`` formats and only these two are photon
streams. ``SP01`` (binned decay curves), ``SPF1`` (phasors), ``IT02`` (intensity
traces) and ``FCS1`` (correlation curves) are analysis products -- there are no
photons left in them, so tttrlib has nothing to read them into. They share the
envelope, which is exactly why the magic and not the extension decides.

Layout
------

Four ASCII magic bytes, a little-endian ``uint32`` header length, a UTF-8 JSON
header carrying the enabled channels and the laser period in nanoseconds, then
fixed-size records to the end of the file. All five FLIM LABS formats share this
envelope.

============  ===========================================================
Magic         Record
============  ===========================================================
``STT1``      17 bytes: ``uint8`` event, ``float64`` micro time (ns),
              ``float64`` macro time (ns)
``ITT1``      9 bytes: ``uint8`` event, ``float64`` time (ns)
============  ===========================================================

Seventeen bytes with a ``float64`` at offset 1 is not a layout any compiler will
produce -- it pads to 24 -- so the fields are read explicitly rather than cast.

Event codes 70, 76 and 80 are ASCII ``F``, ``L`` and ``P`` for frame, line and
pixel; anything else is a zero-based channel index, which the vendor GUI
displays as ``ch{event + 1}``. Markers keep those codes as their routing
channel here rather than being renumbered to 1/2/3: this hardware has detector
channels 1, 2 and 3, and giving a marker the same number as a detector invites
exactly one kind of bug. The reserved codes also mean a detector index of 70 or
above would be indistinguishable from a marker; no such hardware exists, but the
reader refuses rather than guesses if a header ever declares one.

Records are **not** in time order -- the per-channel FIFOs reach the file
interleaved -- so the reader sorts, and therefore reads the whole file into
memory first. It is the one container here that cannot stream.

Floating-point times, integer ticks
------------------------------------

This is the format's one real difficulty, and it is not a matter of care: both
times are ``float64`` nanoseconds, already calibrated and already absolute,
while tttrlib's model is an integer tick -- and the file does not say what tick
to use.

For ``STT1`` the tick is **one laser period**, which the header states, so a
macro time is a laser pulse count and the container behaves like a T3 file. That
keeps ``macro_time * resolution`` exact rather than approximate, and it is the
unit the instrument works in: the hardware histograms arrival times into 256
bins of the laser period, which is where the micro time resolution comes from
too. Nothing is lost that the instrument could measure. An ``STT1`` file whose
header does not state ``laser_period_ns`` is refused, because choosing a tick
there would rescale every time in the file without saying so.

For ``ITT1`` there is no micro time and no reason to believe the timestamps are
laser-gated -- quantising an FCS timestamp to the laser period would throw away
the resolution the measurement is for -- so the tick is **one picosecond**,
which covers 213 days before a ``uint64`` runs out.

Either way the choice is recorded in the header of the resulting object, since
it cannot be recovered from the file:

=====================================  ======================================
Tag                                    Meaning
=====================================  ======================================
``FlimLabs_MacroTimeUnit``             ``laser_pulse`` or ``picosecond``
``FlimLabs_LaserPeriod_ns``            as stated by the file
``FlimLabs_MacroTimeResidual_ns``      see below
``FlimLabs_Header``                    the file's JSON header, verbatim
=====================================  ======================================

``FlimLabs_MacroTimeResidual_ns`` says which of two readings of the file turned
out to be true, and both are handled. Near zero means its macro times are
already laser-pulse counts and the conversion was exact. Up to one laser period
means they are absolute arrival times, in which case the pulse is their whole
part and the micro time carries the remainder -- which is why the conversion
floors rather than rounds: rounding would push a photon arriving late in its
period onto the next pulse while its micro time still said late, moving it a
full period.

Why it is read-only
-------------------

Writing one means emitting the tick choice above as though the instrument had
made it. That choice has never been checked against a file the instrument wrote,
because no such file is published -- see below. An ``STT1`` writer is nearly
free and would be a useful round-trip aid; it should not exist before the reader
has seen real data.

.. warning::

   **No FLIM LABS sample file is published anywhere**, so this reader is
   spec-conformant rather than verified. Searched across all 26 repositories in
   the `FLIM LABS GitHub organisation <https://github.com/flim-labs>`_, its code
   search, and every committed binary: ``STT1`` appears exactly once in public
   code, in `time_tagger_script.py
   <https://github.com/flim-labs/spectroscopy-py/blob/main/export_data_scripts/time_tagger_script.py>`_,
   which is a reader with no accompanying data. The `flim-labs PyPI package
   <https://pypi.org/project/flim-labs/>`_ that *writes* these files is a
   Windows-only driver for their FPGA hardware and ships nothing.

   The specification is unambiguous and comes from two independent sources that
   agree -- the manuals and the vendor's own reader scripts -- and the tests
   exercise the reader against it with synthetic files. What they cannot catch
   is a misreading of what the instrument actually writes.

   **If you have an STT1 or ITT1 file and can share it, please open an issue.**

Reference data
--------------

`Sample files <https://gitlab.peulen.xyz/skf/tttr-data/-/tree/main/flimlabs>`_ in
the ``tttr-data`` repository: the format specification, the vendor manuals, the
vendor reader scripts, and the four published non-time-tagger samples
(``SP01``, ``SPF1``, ``IT02``, ``FCS1``). Those four verify the envelope the
time taggers share, though not their records.

.. seealso::

   :doc:`../file-formats` for the conversion matrix and what survives a
   transcode.

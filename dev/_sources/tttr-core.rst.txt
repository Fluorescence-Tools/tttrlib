.. _tttr_core_guide:

TTTR Core, Selections, Masks, and I/O
=====================================

The ``TTTR`` object is the foundation of tttrlib. It keeps the raw photon event
stream close to the vendor file: macro times, micro times, routing channels,
event types, and parsed header metadata remain available for every downstream
workflow.

Core Objects
------------

``TTTR``
   Reads and stores photon streams. Use it for vendor files, Photon-HDF5 files,
   in-memory arrays, slicing, joining, appending, writing, transcoding,
   histograms, burst search, and as input to ``Correlator`` and ``CLSMImage``.

``TTTRHeader``
   Stores parsed file metadata such as macro-time resolution, micro-time
   resolution, number of micro-time channels, record type, and vendor tags.

``TTTRMask``
   Builds boolean selections over a ``TTTR`` stream. It supports channel
   selection, count-rate selection, microtime-range selection, selected indices,
   selected ranges, and JSON serialization.

``TTTRSelection`` and ``TTTRRange``
   Represent dense/sparse/inverted selections and start/stop ranges. They are
   used directly in burst, CLSM, and pixel-level workflows.

Simple Path
-----------

Use the simple path when you need to load a file, inspect the photon arrays, and
select a channel:

.. code-block:: python

   import tttrlib

   tttr = tttrlib.TTTR("measurement.ptu", "PTU")
   print(tttr.header.macro_time_resolution)
   donor = tttr[tttr.get_selection_by_channel([0])]
   decay, bins = donor.get_microtime_histogram()

This is the right entry point for users coming from vendor software or a
notebook workflow.

Advanced Path
-------------

Use masks and ranges when a workflow needs reusable selections or serialized
state:

.. code-block:: python

   mask = tttrlib.TTTRMask(tttr)
   mask.select_microtime_ranges(tttr, [(100, 900)])
   selected_indices = mask.get_indices(selected=True)
   payload = mask.to_json()

   selection = tttrlib.TTTRSelection(0, 1000)
   selection.set_inverted(False)

Selection semantics matter:

* channel selections are based on routing channel numbers,
* microtime selections are histogram-channel ranges, not physical time unless
  multiplied by the header resolution,
* count-rate selections depend on the macro-time calibration,
* ``TTTRRange`` stores event-index ranges and can be mapped back to macro times.

Writing and Transcoding
-----------------------

Use writing examples when you need to preserve a modified photon stream:

* :doc:`auto_examples/tttr/plot_tttr_write`
* :doc:`auto_examples/tttr/plot_tttr_transcode`
* :doc:`auto_examples/beginner/plot_04_writing_files`

Always check the target format before writing. Some formats preserve rich
metadata, while others only support a fixed header. When converting files, keep a
copy of the source header and document which tags are preserved.

Connections to Other Workflows
------------------------------

.. list-table::
   :widths: 24 76
   :header-rows: 1

   * - Workflow
     - TTTR input role
   * - FCS/correlation
     - ``Correlator`` uses macro times, optional micro times, routing channels,
       and weights.
   * - Burst analysis
     - Burst search operates on photon times and returns ranges/selections.
   * - PDA
     - Experimental PDA histograms are computed from channel-specific photon
       counts in selected time windows.
   * - CLSM/FLIM
     - ``CLSMImage`` uses markers and photon indices to assign photons to
       frames, lines, and pixels.
   * - Decay fitting
     - Microtime histograms from selected photons become fitted decay arrays.

Troubleshooting
---------------

Wrong file type
   Pass the explicit file type when automatic detection is ambiguous, for
   example ``tttrlib.TTTR(path, "PTU")`` or ``tttrlib.TTTR(path, "SPC-130")``.

Empty selections
   Inspect ``tttr.used_routing_channels`` and microtime histogram bounds before
   applying a channel or microtime selection.

Unexpected time units
   Macro times and micro times are stored as raw counters. Convert with
   ``header.macro_time_resolution`` and ``header.micro_time_resolution``.

Examples
--------

* :doc:`auto_examples/beginner/plot_00_tttr_files`
* :doc:`auto_examples/beginner/plot_01_reading_files`
* :doc:`auto_examples/beginner/plot_02_basic_operations`
* :doc:`auto_examples/beginner/plot_03_selections`
* :doc:`auto_examples/tttr/plot_tttr_header`
* :doc:`auto_examples/tttr/plot_tttr_microtime_histogram`


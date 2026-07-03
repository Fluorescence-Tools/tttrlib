.. _burst_analysis_guide:

Burst Analysis
==============

Burst analysis groups photons into short time intervals where a molecule passes
through the observation volume. In tttrlib, burst workflows start from ``TTTR``
photon streams and return selections, ranges, and burst-level parameters that
can be used for FRET, PDA, decay histograms, or filtering.

Core Objects
------------

``BurstFilter``
   Runs burst search and filtering. It supports parameterized burst detection,
   filtering by burst size, duration, background, and dynamic parameter changes.

``BurstFeatureExtractor``
   Computes burst-level features that can be used for filtering, summaries, or
   downstream plots.

``TTTRRange`` and ``TTTRSelection``
   Store the photon index ranges that define bursts and derived selections.

Simple Path
-----------

Use the simple path when you want one burst list from one photon stream:

.. code-block:: python

   tttr = tttrlib.TTTR("single_molecule.spc", "SPC-130")
   bursts = tttr.get_ranges_by_time_window(
       minimum_window_length=0.0005,
       minimum_number_of_photons_in_time_window=30,
       macro_time_calibration=tttr.header.macro_time_resolution,
   )

Then compute burst sizes, durations, and channel counts from the returned ranges.

Advanced Path
-------------

Use ``BurstFilter`` when burst selection is an iterative part of the analysis:

* search with an initial count-rate threshold,
* merge short gaps,
* filter on photon count, duration, or background,
* relax thresholds and reapply filters,
* export burst parameters to JSON for reproducibility.

The advanced examples show how to combine burst search with ALEX/PIE channel
definitions, microtime gates, FRET observables, and serialized parameter sets.

Outputs and Checks
------------------

Every burst example should report:

* number of bursts,
* total photons assigned to bursts,
* accepted/rejected counts after each filter,
* burst size and duration distributions,
* channel definitions and microtime gates,
* enough parameters to reproduce the selection.

Troubleshooting
---------------

No bursts
   Check macro-time calibration and lower the photon threshold. A threshold in
   raw macro-time units is a common mistake.

Too many bursts
   Increase the minimum photon count, shorten the maximum gap, or apply
   background filtering.

Unexpected FRET populations
   Verify donor/acceptor routing channels and PIE/ALEX gates before computing
   burst observables.

Examples
--------

* :doc:`auto_examples/single_molecule/plot_01_burst_analysis`
* :doc:`auto_examples/single_molecule/plot_burst_analysis_json`
* :doc:`auto_examples/single_molecule/plot_burst_analysis_with_filter`
* :doc:`auto_examples/single_molecule/plot_burstfilter_dynamic_enhanced`
* :doc:`auto_examples/single_molecule/plot_fret_burst_analysis`
* :doc:`auto_examples/single_molecule/plot_complete_burst_analysis_pipeline`


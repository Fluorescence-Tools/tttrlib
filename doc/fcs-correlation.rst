.. _fcs_correlation_guide:

FCS and Correlation
===================

Correlation workflows measure how photon-count fluctuations relate across time.
tttrlib supports autocorrelation, cross-correlation, gated correlation, sliced
correlation, manually weighted event streams, and image correlation workflows.

Core Objects
------------

``Correlator``
   Computes correlation curves from ``TTTR`` objects or manually supplied event
   streams. Important settings are ``channels``, ``n_bins``, ``n_casc``,
   ``method``, normalization, and macro-time calibration.

``CorrelatorPhotonStream``
   Lower-level container for event times and weights. Use it when the data are
   already preselected or weighted outside the normal ``TTTR`` channel workflow.

Simple Path
-----------

Use a ``TTTR`` object and channel pair:

.. code-block:: python

   correlator = tttrlib.Correlator(
       tttr=tttr,
       channels=([0], [0]),
       n_bins=9,
       n_casc=18,
   )
   taus = correlator.x_axis
   correlation = correlator.correlation

This path uses the TTTR header to calibrate the macro-time axis.

What To Document In an FCS Result
---------------------------------

Correlation curves are sensitive to selection and normalization. A complete
tttrlib FCS example should state:

``channels``
   The channel pair, such as ``([0], [0])`` for autocorrelation or ``([0],
   [1])`` for cross-correlation.

Lag axis construction
   ``n_bins``, ``n_casc``, and whether the lag axis is multi-tau, arbitrary, or
   fine-correlation based.

Photon stream source
   Whether photons come directly from ``TTTR`` channel selection,
   ``CorrelatorPhotonStream``, a ``TTTRSelection``, or precomputed arrays.

Weights and gates
   Any per-photon weights, microtime gates, masks, or lifetime windows.

Normalization
   Whether raw pair counts or normalized correlation amplitudes are plotted.

Advanced Paths
--------------

Cross-correlation
   Pass different channel lists, for example ``channels=([0], [1])``.

Gated correlation
   Select photons by microtime or build weights before assigning event streams.
   Use this to compare prompt and delayed photons, lifetime gates, or PIE/ALEX
   windows.

Sliced correlation
   Split a measurement into time windows and compute a curve for each slice.
   This helps identify drift, adsorption, bleaching, or unstable focus.

Manual streams
   Use explicit event times and weights when the correlation input is not a
   simple routing-channel selection.

Manual Stream Workflow
----------------------

``CorrelatorPhotonStream`` is the explicit route for preselected or weighted
events:

.. code-block:: python

   stream = tttrlib.CorrelatorPhotonStream(
       macro_times=macro_times,
       weights=weights,
       macro_time_resolution=tttr.header.macro_time_resolution,
   )
   correlator = tttrlib.Correlator(
       photon_streams=(stream, stream),
       n_bins=9,
       n_casc=18,
   )

Use this path when the selection is produced by PDA, burst filtering, image
segmentation, or another analysis layer before correlation.

Image Correlation
-----------------

Image correlation examples use reconstructed CLSM/FLIM images rather than a
single photon stream. Keep the pixel size, frame time, stack handling, and image
mask with the result because they define the spatial and temporal axes.

Parameter Reference
-------------------

``n_bins``
   Number of linearly spaced bins per cascade.

``n_casc``
   Number of coarsening cascades. More cascades extend the lag-time axis.

``method``
   Correlation implementation. The main methods are the Wahl-style multi-tau
   implementation and the Laurence-style arbitrary-axis implementation.

Weights
   Per-event multipliers. Use weights for microtime gates or fractional
   channel assignments.

Normalization
   Converts raw pair counts into a correlation amplitude. Always report whether
   a plotted curve is normalized.

Fine correlation
   Combines macro and micro times to access shorter lag times. This requires
   correct micro-time calibration and careful interpretation.

Troubleshooting
---------------

Wrong amplitude
   Check normalization, count rates, selected channels, and measurement duration.

Missing short-time behavior
   Use fine correlation or inspect whether micro-time information was supplied.

Noisy slices
   Increase slice duration or reduce the number of fit parameters downstream.

Examples
--------

* :doc:`auto_examples/correlation/plot_normal_correlation`
* :doc:`auto_examples/correlation/plot_full_correlation`
* :doc:`auto_examples/correlation/plot_gated_correlation`
* :doc:`auto_examples/correlation/plot_sliced_correlation`
* :doc:`auto_examples/correlation/plot_correlation_cr_filter`
* :doc:`auto_examples/correlation/plot_confocor3_two_ch_correlation`
* :doc:`auto_examples/image_correlation/index`

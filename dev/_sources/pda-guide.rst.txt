.. _pda_guide:

Photon Distribution Analysis (PDA)
==================================

Photon Distribution Analysis compares the distribution of photons observed in
two channels with a model for species fractions, background, and the probability
that a photon is detected in channel 1 or channel 2. In smFRET this is commonly
used to connect photon-count histograms to FRET efficiency distributions.

Core Objects
------------

``Pda``
   Builds model S1/S2 photon-count matrices. Constructor inputs include
   ``hist2d_nmax``, ``hist2d_nmin``, ``background_ch1``, ``background_ch2``,
   ``pF``, and the implementation mode.

``PdaCallback``
   Reduces a two-dimensional S1/S2 matrix to a one-dimensional observable such
   as proximity ratio, FRET efficiency, or total photon count.

``PDA_DEFAULT`` and ``PDA_OPTIMIZED``
   Select the implementation. The optimized path is intended for larger
   histogram ranges and repeated model evaluations.

Concepts
--------

``pF``
   Probability distribution for the total number of fluorescence photons.

Species amplitudes
   Fractions of each modeled species. They should be normalized or documented
   before comparison to experimental histograms.

``probability_ch1``
   Probability that a photon from a species is detected in the first channel.
   In FRET workflows this is often connected to proximity ratio or FRET
   efficiency.

S1/S2 matrix
   The two-dimensional probability/count matrix for photons in channel 1 and
   channel 2. ``Pda.get_S1S2_matrix`` exposes this representation.

1D projection
   ``Pda.get_1dhistogram`` applies a callback or built-in projection settings to
   convert S1/S2 into a user-facing distribution.

Simple Path
-----------

Use a synthetic PDA model first:

.. code-block:: python

   import numpy as np
   import tttrlib

   pF = np.ones(80, dtype=float)
   pF /= pF.sum()

   pda = tttrlib.Pda(hist2d_nmax=80, hist2d_nmin=10, pF=pF)
   pda.append(0.6, 0.35)
   pda.append(0.4, 0.75)
   s1s2 = pda.s1s2

Then project ``s1s2`` into the observable used in the experiment.

What To Document In a PDA Result
--------------------------------

PDA output is only comparable when the model assumptions are explicit. A
complete result should report:

``hist2d_nmin`` and ``hist2d_nmax``
   Photon-count range used for the S1/S2 matrix. Changing either value changes
   both runtime and the tails of the distribution.

Background in channel 1 and channel 2
   Background shifts the low-count part of the histogram and can mimic a weak
   population if it is not constrained.

``pF``
   The total fluorescence photon-count distribution. State whether it is
   measured from the same data, simulated, smoothed, or imported from another
   calibration.

Species table
   For each species, report amplitude and ``probability_ch1``. In FRET
   examples, also state whether ``probability_ch1`` is a raw proximity ratio or
   a corrected FRET efficiency.

Projection callback
   Name the ``PdaCallback`` mapping and binning used to convert S1/S2 into the
   plotted one-dimensional distribution.

Advanced Path
-------------

Use the advanced path for experimental data:

1. Select photons and define channel 1/channel 2.
2. Use ``Pda.compute_experimental_histograms`` to obtain S1/S2 histograms,
   total signal histograms, and the TTTR indices used.
3. Build a model ``Pda`` object with matching histogram bounds and background.
4. Define a projection callback.
5. Optimize species amplitudes, channel probabilities, or physical parameters.
6. Compare model and experimental histograms in the same projection.

Experimental Histogram Workflow
-------------------------------

Experimental PDA starts from a photon stream rather than from ``pF`` alone:

.. code-block:: python

   histograms = tttrlib.Pda.compute_experimental_histograms(
       tttr,
       channels_ch1=[0],
       channels_ch2=[1],
       minimum_number_of_photons_per_burst=40,
       maximum_number_of_photons_per_burst=160,
   )

Keep the channel lists and photon-count limits next to the fitted model. A PDA
figure without these settings is not reproducible because the same ``Pda`` model
can look different under a different burst selection or S1/S2 range.

Optimized Implementation
------------------------

Use ``PDA_OPTIMIZED`` when the same model is evaluated many times, for example
during a least-squares or likelihood scan:

.. code-block:: python

   pda = tttrlib.Pda(
       hist2d_nmax=160,
       hist2d_nmin=40,
       pF=pF,
       implementation=tttrlib.PDA_OPTIMIZED,
   )

Validate the optimized path against ``PDA_DEFAULT`` on a small histogram before
using it in a long optimization.

Array Shape Checks
------------------

Every PDA example should print or assert:

* ``hist2d_nmax`` and ``hist2d_nmin``,
* S1/S2 matrix shape,
* number of species,
* length and normalization of ``pF``,
* experimental histogram shape,
* number of TTTR indices used for the PDA histogram.

Troubleshooting
---------------

Model/data mismatch
   Check that the model and experimental histograms use the same photon-count
   bounds and channel definitions.

Flat or empty projection
   Verify the callback range, number of bins, and whether the projection skips
   invalid S1/S2 cells.

Slow repeated fitting
   Use ``PDA_OPTIMIZED`` and keep ``hist2d_nmax`` as small as the data supports.

Examples
--------

* :doc:`auto_examples/single_molecule/plot_pda_synthetic`
* :doc:`auto_examples/single_molecule/plot_single_molecule_pda_1`
* :doc:`auto_examples/single_molecule/plot_single_molecule_pda_2`
* :doc:`modules/pda`

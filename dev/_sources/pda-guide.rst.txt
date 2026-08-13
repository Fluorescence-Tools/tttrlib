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
   Select the implementation. ``PDA_OPTIMIZED`` distributes species over OpenMP
   threads and is worth using when a model has several species; see
   `Optimized Implementation`_ for the one case where the two disagree.

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

.. _pda_index_convention:

Index Convention
----------------

Every S1/S2 matrix in tttrlib -- the model matrix from ``Pda.s1s2``, the
experimental matrix from ``Pda.compute_experimental_histograms``, and the
optional ``s1s2=`` argument of ``Pda.get_1dhistogram`` -- is stored so that

.. code-block:: text

   matrix[ch1, ch2] == p(S1 == ch1, S2 == ch2)

The **row is channel 1** and the **column is channel 2**. The projection
callback is likewise invoked as ``cb(ch1, ch2)``, in that order.

This matters more than it looks. A transposed matrix still projects to a
perfectly plausible histogram -- it is simply mirrored, ``E`` becoming
``1 - E``. A fit will happily absorb the mirroring by converging on
``1 - probability_ch1`` instead of ``probability_ch1``, and nothing in the
residuals will look wrong. If you build an S1/S2 matrix yourself, assert the
orientation on an asymmetric case before trusting a fitted value:

.. code-block:: python

   pda = tttrlib.Pda(hist2d_nmax=60, hist2d_nmin=10, pF=pF)
   pda.append(1.0, 0.9)              # 90 % of photons in channel 1
   row, col = np.unravel_index(pda.s1s2.argmax(), pda.s1s2.shape)
   assert row > col                  # the bright channel is the row

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

   s1s2, ps, tw_indices = tttrlib.Pda.compute_experimental_histograms(
       tttr_data=tttr,
       channels_1=[0, 8],                  # green detectors
       channels_2=[1, 9],                  # red detectors
       minimum_number_of_photons=40,
       maximum_number_of_photons=160,
       minimum_time_window_length=1.0,
   )

The three return values are:

``s1s2``
   The experimental count matrix, ``(nmax + 1, nmax + 1)``, in the same
   row-is-channel-1 layout as the model (see :ref:`pda_index_convention`), so
   it can be passed straight to ``get_1dhistogram(s1s2=s1s2.flatten())``.

``ps``
   ``P(S)``, the histogram over the total photon count per time window, indexed
   by that count. Commonly used as an approximation of ``pF`` when the
   background is low and low-count windows are discarded.

``tw_indices``
   Interleaved ``[start_0, stop_0, start_1, stop_1, ...]`` event indices of the
   accepted time windows. Window *w* covers the half-open range
   ``[tw_indices[2*w], tw_indices[2*w + 1])``, so the photons that entered the
   histogram are recoverable with ``tttr[start:stop]``.

``minimum_time_window_length`` is expressed in the unit of the macro-time
resolution reported by the file header -- milliseconds for the formats that
record it in milliseconds. An in-memory ``TTTR`` built from arrays has no
resolution, and the value is then interpreted in raw macro-time ticks.

Keep the channel lists and photon-count limits next to the fitted model. A PDA
figure without these settings is not reproducible because the same ``Pda`` model
can look different under a different burst selection or S1/S2 range.

Fitting Species Fractions
-------------------------

The projection from S1/S2 to the 1-D histogram is a fixed linear map, and the
model is linear in the species amplitudes. So the histogram of a mixture is just
the amplitude-weighted sum of the per-species histograms, and a fit that varies
only the fractions never has to re-evaluate the model:

.. code-block:: python

   x, rows = pda.get_1dhistogram_per_species(
       x_min=0.005, x_max=0.995, n_bins=81, log_x=False,
   )          # rows is (n_species, n_bins), each species at amplitude 1

   y = amplitudes @ rows          # exactly get_1dhistogram(), ~60x cheaper

Call it once and reweight inside the optimizer. Recall it when
``probabilities_ch1``, ``pF``, either background or the binning changes --
those *do* change the per-species matrices. Amplitudes do not.

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

The difference is that species are distributed over OpenMP threads, so the
benefit scales with the number of species and is nil for a single one. Both
paths share the same kernels and agree to machine precision.

.. warning::

   ``PDA_OPTIMIZED`` additionally carries a multi-molecule FFT correction that
   engages when ``pF[0] < 1e-15``, i.e. when the probability of observing zero
   fluorescence photons is negligible. That correction changes the *model*, not
   just its speed, and the two implementations then no longer agree. For an
   ordinary ``pF`` the branch is dead. Validate the optimized path against
   ``PDA_DEFAULT`` on your own ``pF`` before using it in a long optimization.

Array Shape Checks
------------------

Every PDA example should print or assert:

* ``hist2d_nmax`` and ``hist2d_nmin``,
* S1/S2 matrix shape,
* number of species,
* length and normalization of ``pF``,
* experimental histogram shape,
* number of TTTR indices used for the PDA histogram.

.. _pda_burst_likelihood:

Burst-Wise Likelihood (any number of channels)
-----------------------------------------------

``Pda`` is two-channel and histogram-based. ``PdaBurstLikelihood`` is neither: it
evaluates the likelihood of each burst directly, so it takes any number of
detection channels, needs no ``hist2d_nmax`` and no binning, and is a
maximum-likelihood objective rather than a :math:`\chi^2`.

.. code-block:: python

   counts = np.array([[12, 9, 4], [20, 3, 7]], dtype=np.int32)   # (bursts, K)

   like = tttrlib.PdaBurstLikelihood(
       counts,
       [1.5, 1.0, 0.8],          # mean background counts per channel
       pF,                       # optional P(n) for the signal photon number
   )

   ll = like.log_likelihood(np.array([0.45, 0.35, 0.20]))     # per burst
   total = like.total_log_likelihood(p_grid)                  # summed, per point

The constructor does the burst-side work once -- falling factorials, Poisson
series, the multinomial constant -- so a fit should build the object once and
call it with a new ``p`` each iteration. ``total_log_likelihood`` fuses the sum
over bursts into the matrix product, so the full ``(points, bursts)`` grid is
never allocated; use ``log_likelihood_grid`` only when you actually want it.

Three-colour PDA is the motivating case: with three dyes the transfer pathways
compete and cascade, so the channel probabilities are not three independent
two-colour experiments. Compute those probabilities however your model dictates
and pass them in as ``p``; this class is the counting statistics, not the
photophysics.

.. note::

   The background series is deliberately **not** truncated on Poisson tail mass.
   Where a channel collected far more photons than the model allows the terms
   grow before the Poisson factor turns them over, and there the background is
   the whole likelihood. ``log_background_correction`` is the untruncated
   per-burst oracle the fast path is validated against.

Troubleshooting
---------------

Model/data mismatch
   Check that the model and experimental histograms use the same photon-count
   bounds and channel definitions.

The fitted ``probability_ch1`` is the complement of what you expect
   The model and the data are in opposite orientations. Check the axis with the
   asymmetric assertion in :ref:`pda_index_convention` before touching the fit.
   A mirrored projection fits just as well as a correct one, so the residuals
   will not tell you.

Flat or empty projection
   Verify the callback range, number of bins, and whether the projection skips
   invalid S1/S2 cells.

``WARNING: Pda pF array shorter than hist2d_nmax + 1``
   ``pF`` must have ``hist2d_nmax + 1`` entries. It is zero-padded rather than
   rejected, which silently removes probability mass from the high-count tail.

Slow repeated fitting
   Keep ``hist2d_nmax`` as small as the data supports -- the model matrix and
   the background convolution both scale with its square. Reuse one ``Pda``
   object across iterations instead of rebuilding it: the projection caches the
   callback value of every cell, and the model scratch buffers are kept between
   evaluations. Use ``PDA_OPTIMIZED`` when the model has several species.

Examples
--------

* :doc:`auto_examples/single_molecule/plot_pda_synthetic`
* :doc:`auto_examples/single_molecule/plot_single_molecule_pda_1`
* :doc:`auto_examples/single_molecule/plot_single_molecule_pda_2`
* :doc:`modules/pda`

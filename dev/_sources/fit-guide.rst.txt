.. _fit_guide:

Fluorescence Decay Fitting
==========================

tttrlib exposes fluorescence decay fitting through three layers:

1. one-call helpers ``fit23``, ``fit24``, ``fit25``, and ``fit26``;
2. reusable classes ``Fit23``, ``Fit24``, ``Fit25``, and ``Fit26`` built on
   the shared ``Fit2x`` wrapper;
3. low-level ``DecayFit23``-``DecayFit26`` functions with ``DecayFitData``.

Use the highest-level API that gives enough control for the analysis. The
low-level API is useful for testing, migration, and advanced integrations, but
most user-facing notebooks should use the helpers or reusable classes.

Which Fit Should I Use?
-----------------------

.. list-table::
   :widths: 12 22 18 16 18 14
   :header-rows: 1

   * - Fit
     - Use when
     - Parameters
     - Typical ``fixed``
     - Result fields
     - Main example
   * - ``Fit23`` / ``fit23``
     - Single fluorescence lifetime with polarization-resolved decays and
       anisotropy-related outputs.
     - ``tau``, ``gamma``, ``r0``, ``rho``
     - ``[0, 1, 1, 0]`` when fitting lifetime and rotation time.
     - ``x[0]`` lifetime, ``x[3]`` rotation time, ``twoIstar``, optional
       ``model``.
     - :doc:`auto_examples/fluorescence_decay/plot_fit23`
   * - ``Fit24`` / ``fit24``
     - Bi-exponential lifetime model with two lifetimes, amplitude, scattered
       fraction, and offset.
     - ``tau1``, ``gamma``, ``tau2``, ``A2``, ``offset``
     - ``[0, 0, 0, 0, 0]`` for a full synthetic-data fit; fix ``offset`` when
       background is independently known.
     - ``x[0]`` and ``x[2]`` lifetimes, ``x[3]`` second amplitude,
       ``twoIstar``, optional ``model``.
     - :doc:`auto_examples/fluorescence_decay/plot_fit24`
   * - ``Fit25`` / ``fit25``
     - Select the best lifetime from four fixed candidates, optionally fitting
       ``gamma``.
     - ``tau1``-``tau4``, ``gamma``, ``r0``
     - ``[0, 0, 0, 0, 1, 1]`` when only selecting the best candidate lifetime.
     - ``x[0]`` selected lifetime, ``x[4]`` scattered fraction,
       ``twoIstar``, optional ``model``.
     - :doc:`auto_examples/fluorescence_decay/plot_fit25`
   * - ``Fit26`` / ``fit26``
     - Fit the fraction of two known reference patterns.
     - ``fraction_1``
     - ``[0]`` to optimize the first pattern fraction.
     - ``x[0]`` first fraction, ``x[1]`` complementary fraction,
       ``twoIstar``, optional ``model``.
     - :doc:`auto_examples/fluorescence_decay/plot_fit26`

Common Failure Modes
--------------------

.. list-table::
   :widths: 18 42 40
   :header-rows: 1

   * - Fit
     - Typical failure
     - Check first
   * - ``Fit23``
     - Unrealistic ``rho`` or lifetime when photon counts are low.
     - IRF alignment, fixed mask for ``r0``/``gamma``, and whether the Jordi
       parallel/perpendicular order is correct.
   * - ``Fit24``
     - Lifetimes exchange order or the second amplitude runs to a boundary.
     - Initial values, fixed offset/background, and whether a two-component
       model is identifiable from the photon counts.
   * - ``Fit25``
     - The selected lifetime is always the same candidate.
     - Candidate spacing, ``gamma`` fixed/free choice, and whether candidates
       cover the observed decay.
   * - ``Fit26``
     - Fitted fraction is outside the expected range or tracks one pattern only.
     - Pattern normalization, pattern similarity, and whether both references
       are measured with the same binning as ``data``.

Shared Inputs
-------------

``data``
   Experimental decay histogram. For polarization-resolved fits this is usually
   Jordi format: parallel decay followed by perpendicular decay.

``irf``
   Instrument response function in the same format and binning as ``data``.

``background``
   Background pattern in the same format as ``data``.

``dt``
   Width of one microtime channel.

``period``
   Excitation period, in the same time units as the lifetimes.

``fixed``
   Integer array where ``1`` means fixed and ``0`` means optimized.

Result Contract
---------------

Every high-level fit returns a dictionary with:

``x``
   Fitted parameter array. Some entries are outputs written by the low-level
   routine, so always use the fit-specific table.

``fixed``
   The fixed/free parameter mask used by the fit.

``twoIstar``
   Fit quality value.

``model``
   Optional model decay when ``include_model=True``.

Parameter Arrays
----------------

.. list-table::
   :widths: 12 44 44
   :header-rows: 1

   * - Fit
     - Input slots
     - Output interpretation
   * - ``Fit23``
     - ``x[:4] = [tau, gamma, r0, rho]``. Additional slots are reserved for
       scatter/objective flags and low-level outputs.
     - ``x[0]`` is the recovered lifetime, ``x[3]`` is the recovered rotational
       correlation time, and ``twoIstar`` reports fit quality.
   * - ``Fit24``
     - ``x[:5] = [tau1, gamma, tau2, A2, offset]``. ``A2`` is the second
       component amplitude.
     - ``x[0]`` and ``x[2]`` are the fitted lifetimes, ``x[3]`` is the second
       component fraction, ``x[4]`` is the offset.
   * - ``Fit25``
     - ``x[:6] = [tau1, tau2, tau3, tau4, gamma, r0]``. The four lifetimes are
       candidates rather than continuous lifetime parameters.
     - ``x[0]`` is overwritten with the selected lifetime; ``gamma`` may be
       optimized when the fixed mask allows it.
   * - ``Fit26``
     - ``x[0] = fraction_1`` for the first reference pattern.
     - ``x[0]`` is the fitted fraction of pattern 1 and ``x[1]`` is the
       complementary fraction of pattern 2.

Simple Helper API
-----------------

.. code-block:: python

   result = tttrlib.fit23(
       data=data,
       irf=irf,
       background=background,
       dt=dt,
       period=period,
       tau=3.0,
       gamma=0.01,
       r0=0.38,
       rho=1.0,
       include_model=True,
   )

Reusable Class API
------------------

Use a class when many decays share the same IRF, background, and instrument
settings:

.. code-block:: python

   fit23 = tttrlib.Fit23(
       dt=dt,
       irf=irf,
       background=background,
       period=period,
       convolution_stop=-1,
   )
   result = fit23(data, initial_values=np.array([3.0, 0.01, 0.38, 1.0]))

Low-Level API
-------------

Use ``DecayFitData`` and ``DecayFit23.fit`` only when you need exact control over
the arrays passed to the C++ layer:

.. code-block:: python

   params = tttrlib.DecayFitData(
       dt=dt,
       corrections=np.array([period, 1.0, 0.0, 0.0, len(irf) // 2 - 1]),
       irf=irf,
       background=background,
       data=data.astype(np.int32),
   )
   x = np.zeros(8, dtype=np.float64)
   x[:4] = [3.0, 0.01, 0.38, 1.0]
   fixed = np.array([0, 1, 1, 1], dtype=np.int16)
   two_istar = tttrlib.DecayFit23.fit(x, fixed, params)

Related Decay Tools
-------------------

``DecayConvolution``
   Convolves model decays with the instrument response function. Use it when
   building or validating custom models outside the helper fits.

``DecayPhasor``
   Computes phasor coordinates for decay histograms. Use it for fast lifetime
   screening, FLIM map inspection, and model-free comparisons.

``DecayFitData``
   Bundles data, IRF, background, corrections, and fit windows for the low-level
   ``DecayFit23``, ``DecayFit24``, ``DecayFit25``, and ``DecayFit26`` routines.

Troubleshooting
---------------

Unstable fit
   Check IRF alignment, background, ``fixed`` mask, bounds implied by the model,
   and whether the selected photons contain enough counts.

Wrong lifetime units
   Ensure ``dt``, ``period``, and initial lifetimes use the same units.

Poor model agreement
   Plot ``data``, ``irf``, ``background``, and the fitted ``model`` together.

Examples
--------

* :doc:`auto_examples/fluorescence_decay/plot_fit23`
* :doc:`auto_examples/fluorescence_decay/plot_fit23_usage_2`
* :doc:`auto_examples/fluorescence_decay/plot_fit_helpers`
* :doc:`auto_examples/fluorescence_decay/plot_fit24`
* :doc:`auto_examples/fluorescence_decay/plot_fit25`
* :doc:`auto_examples/fluorescence_decay/plot_fit26`
* :doc:`auto_examples/flim/plot_mle_lifetime`

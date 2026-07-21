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

Burst Search Algorithms
-----------------------

``TTTR.burst_search(..., mode=...)`` dispatches between five algorithms, and
``TTTR.burst_search_algorithms()`` returns the same list programmatically along
with each algorithm's parameter schema (labels, defaults, ranges and units), so
a GUI can build its controls without hard-coding anything.

They differ mainly in *what they assume a burst looks like*. The threshold
searches assume one rate scale; the tree and segmentation searches do not, which
is what lets them find dim and bright bursts in the same trace.

.. list-table::
   :header-rows: 1
   :widths: 16 30 26 28

   * - Mode
     - How it decides
     - Use it when
     - Origin
   * - ``sliding_window``
     - A photon is in a burst when ``m`` consecutive photons span less than
       ``T`` — one global count-rate threshold, one pass, no background model.
     - The default. Fastest, fully predictable, and hard to beat when bursts
       are bright and uniform.
     - The classical confocal burst search: Fries et al. (1998), *J. Phys.
       Chem. A* **102**, 6601, :doi:`10.1021/jp980965t`.
   * - ``cusum_sprt``
     - Locates burst edges by a cumulative-sum change point, then a sequential
       probability ratio test decides background vs. signal with explicit
       ``alpha``/``beta`` error rates.
     - You want edges placed by a test rather than a threshold, and can supply
       (or auto-estimate) a background rate.
     - CUSUM: Page (1954), *Biometrika* **41**, 100. SPRT: Wald (1945), *Ann.
       Math. Statist.* **16**, 117. Applied to single-molecule photon streams by
       Watkins & Yang (2005), *J. Phys. Chem. B* **109**, 617,
       :doi:`10.1021/jp0467548`.
   * - ``kalman``
     - Tracks the binned count rate with a Kalman filter using Poisson
       measurement noise, and flags bins whose innovation is large relative to
       the filter's own uncertainty.
     - The background drifts. It responds to a *change* in rate, so a slow drift
       is tracked and ignored rather than detected.
     - Kalman (1960), *J. Basic Eng.* **82**, 35, :doi:`10.1115/1.3662552`.
   * - ``maxtree``
     - Builds the component tree of the local log rate — every connected
       component at every level — and keeps those that are maximally stable and
       statistically plausible. No level is chosen globally.
     - Dim and bright bursts coexist, or overlapping transits need deblending
       (the tree separates them without a watershed pass).
     - Borrowed from image segmentation. Max-tree: Salembier, Oliveras & Garrido
       (1998), *IEEE Trans. Image Process.* **7**, 555,
       :doi:`10.1109/83.663500`. Stability criterion (MSER): Matas et al.
       (2004), *Image Vis. Comput.* **22**, 761,
       :doi:`10.1016/j.imavis.2004.02.006`. Rolling-ball baseline: Sternberg
       (1983), *Computer* **16**, 22, :doi:`10.1109/MC.1983.1654163`.
   * - ``bayesian_blocks``
     - Finds by dynamic programming the single most probable partition of the
       photon stream into constant-rate intervals. No bins, no window duration,
       no phase — the only detection parameter is ``p0``, a false-alarm
       probability.
     - Burst *extent* matters, or bursts sit near the detection limit where a
       misaligned window loses the most. Slowest of the five.
     - Gamma-ray astronomy, where it was built for time-tagged photon events
       from BATSE and Fermi — the same data model as a TTTR file. Scargle
       (1998), *ApJ* **504**, 405, :doi:`10.1086/306064`; Scargle et al. (2013),
       *ApJ* **764**, 167, :arxiv:`1304.2818`. Made affordable by PELT pruning:
       Killick, Fearnhead & Eckley (2012), *JASA* **107**, 1590,
       :doi:`10.1080/01621459.2012.737745`. Two-stage trigger after Fermi GBM:
       Meegan et al. (2009), *ApJ* **702**, 791,
       :doi:`10.1088/0004-637X/702/1/791`.

Choosing a detection statistic
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``maxtree`` and ``bayesian_blocks`` both test a candidate against the local
background, and ``significance_mode`` selects how:

.. list-table::
   :header-rows: 1
   :widths: 12 34 54

   * - Value
     - Statistic
     - Notes
   * - ``0``
     - Gaussian, :math:`(k-\mu)/\sqrt{\mu}`
     - The ``maxtree`` default, kept so earlier results reproduce exactly. It
       assumes the Poisson distribution is already normal, which is false at the
       counts typical here (~20 burst photons over ~2 expected), so a "4 sigma"
       threshold does not deliver the false-positive rate it appears to promise.
   * - ``1``
     - Exact Poisson upper tail
     - Correct when the background rate is known a priori.
   * - ``2``
     - Li & Ma (1983)
     - The ``bayesian_blocks`` default. Additionally propagates the uncertainty
       of a background that was *measured* rather than known, which is always
       the case here. Li & Ma (1983), *ApJ* **272**, 317, eq. 17,
       :doi:`10.1086/161295`.

With the default ``delta``, the choice matters more for ``maxtree`` than it used
to: Li & Ma reaches purity 0.994 against the Gaussian form's 0.964 on the
simulated benchmark, for about 0.02 less completeness.

Both searches also accept ``max_false_alarm_rate``, which replaces the sigma
threshold with a post-trials one expressed in expected spurious bursts per
second. Unlike a bare sigma, one such setting means the same thing on a 10 s and
a 1 h acquisition. The trials correction is approximate — verify it against a
background-only measurement before relying on the absolute number.

Cost
~~~~

Measured on 127k photons (20 s at ~6 kcps, 8 cores), with the burst counts
agreeing to within about 2% across the photon-resolved methods:

.. list-table::
   :header-rows: 1
   :widths: 26 14 14 46

   * - Mode
     - Mphotons/s
     - Parallel
     - Notes
   * - ``sliding_window``
     - ~1100
     - no
     - One comparison per photon. Nothing will beat it.
   * - ``cusum_sprt``
     - ~250
     - no
     - One sequential test per photon.
   * - ``maxtree``
     - ~20
     - partly
     - Rate signal and rolling-ball baseline are threaded; the component-tree
       sweep is inherently sequential, which caps the achievable speed-up.
   * - ``bayesian_blocks``
     - ~15
     - yes (~6x)
     - Dominated by the segmentation, which uses PELT pruning to stay
       near-linear. ``trigger_contrast`` is the main cost knob — see below.
   * - ``kalman``
     - ~10
     - no
     - Cost set by ``dt`` rather than by the photon count.

The exact searches are one to two orders of magnitude slower than
``sliding_window``, which is inherent — they do strictly more work per photon —
so use ``sliding_window`` for real-time or very large files and the others when
the detection limit or the burst boundaries matter more than throughput.

If ``bayesian_blocks`` is still too slow, raise ``trigger_contrast`` before
anything else: it controls how much of the stream reaches the expensive stage at
all. Going from 2.5 to 4 roughly doubles throughput for about 0.006 less
completeness. Lowering it below ~2 is counterproductive — the trigger then fires
on so much background that, once regions are padded and merged, the segmentation
covers essentially the whole stream and the trigger stops filtering anything.

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


References
----------

Primary sources for the algorithms above, in the order the table introduces them.

**Burst searches**

* Fries, J. R., Brand, L., Eggeling, C., Köllner, M. & Seidel, C. A. M. (1998).
  Quantitative identification of different single molecules by selective
  time-resolved confocal fluorescence spectroscopy. *J. Phys. Chem. A* **102**,
  6601–6613. :doi:`10.1021/jp980965t`
* Page, E. S. (1954). Continuous inspection schemes. *Biometrika* **41**,
  100–115. :doi:`10.1093/biomet/41.1-2.100`
* Wald, A. (1945). Sequential tests of statistical hypotheses.
  *Ann. Math. Statist.* **16**, 117–186. :doi:`10.1214/aoms/1177731118`
* Watkins, L. P. & Yang, H. (2005). Detection of intensity change points in
  time-resolved single-molecule measurements. *J. Phys. Chem. B* **109**,
  617–628. :doi:`10.1021/jp0467548`
* Kalman, R. E. (1960). A new approach to linear filtering and prediction
  problems. *J. Basic Eng.* **82**, 35–45. :doi:`10.1115/1.3662552`
* Salembier, P., Oliveras, A. & Garrido, L. (1998). Anti-extensive connected
  operators for image and sequence processing. *IEEE Trans. Image Process.*
  **7**, 555–570. :doi:`10.1109/83.663500`
* Matas, J., Chum, O., Urban, M. & Pajdla, T. (2004). Robust wide-baseline
  stereo from maximally stable extremal regions. *Image Vis. Comput.* **22**,
  761–767. :doi:`10.1016/j.imavis.2004.02.006`
* Sternberg, S. R. (1983). Biomedical image processing. *Computer* **16**,
  22–34. :doi:`10.1109/MC.1983.1654163`
* Scargle, J. D. (1998). Studies in astronomical time series analysis. V.
  Bayesian blocks, a new method to analyze structure in photon counting data.
  *ApJ* **504**, 405. :doi:`10.1086/306064`
* Scargle, J. D., Norris, J. P., Jackson, B. & Chiang, J. (2013). Studies in
  astronomical time series analysis. VI. Bayesian block representations.
  *ApJ* **764**, 167. :arxiv:`1304.2818`
* Killick, R., Fearnhead, P. & Eckley, I. A. (2012). Optimal detection of
  changepoints with a linear computational cost. *J. Am. Stat. Assoc.* **107**,
  1590–1598. :doi:`10.1080/01621459.2012.737745`
* Meegan, C. et al. (2009). The Fermi gamma-ray burst monitor. *ApJ* **702**,
  791. :doi:`10.1088/0004-637X/702/1/791`

**Detection statistics**

* Li, T.-P. & Ma, Y.-Q. (1983). Analysis methods for results in gamma-ray
  astronomy. *ApJ* **272**, 317. :doi:`10.1086/161295`
* Ofek, E. O. & Zackay, B. (2018). Optimal matched filter in the low-number
  count Poisson noise regime and implications for X-ray source detection.
  *A&A* **616**, A170. :arxiv:`1709.01524`

**Downstream burst analysis**

* Torella, J. P., Holden, S. J., Santoso, Y., Hohlbein, J. & Kapanidis, A. N.
  (2011). Identifying molecular dynamics in single-molecule FRET experiments
  with burst variance analysis. *Biophys. J.* **100**, 1568–1577.
  :doi:`10.1016/j.bpj.2011.01.066`

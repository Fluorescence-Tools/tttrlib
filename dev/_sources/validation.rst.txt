Validation against reference implementations
============================================

Every algorithm in tttrlib is A/B-tested against an **independent reference
implementation** -- the upstream code where it exists, a library that did not
serve as the source of the port otherwise, and an analytic or simulated known
answer only where neither exists. The A/B is a permanent test, not a one-off
script; the header of every validated kernel carries a ``// Validation:``
block after its include guard naming the reference, the metric and the test.

Where the reference is a package the benchmark suite can run, speed and
identity are checked together on the *same inputs* (``benchmarks/check_*.py``):
"faster" is only meaningful next to "the same answer".

Registers (the authoritative tables, kept with the code):

* ``okf/testing/algorithm-validation.md`` -- every module, per kernel: reference,
  metric, verdict, and the identity-and-speed checklists of the benchmark sets.
* ``okf/testing/math-kernel-validation.md`` -- the ``modules/math`` kernels.
* ``PERF.md`` -- measured numbers, with the identity checklist beside each
  benchmark set.

What is compared against what
-----------------------------

.. list-table::
   :header-rows: 1
   :widths: 28 40 32

   * - Kernel
     - Reference
     - Output identity
   * - Blind IRF (BIRFI)
     - VicidominiLab ``birfi``
     - IRFs correlate >= 0.99 (birfi's Adam fit not converged; tttrlib solves the model)
   * - ISM pixel reassignment / shift vectors
     - BrightEyes-ISM ``APR_lib``
     - bit-identical / 3e-16
   * - Focus-ISM
     - BrightEyes-ISM ``FocusISM_lib``
     - same background-fraction map to within noise
   * - s2ISM
     - VicidominiLab ``s2ISM`` (torch)
     - identical (2e-9)
   * - Watershed, marching squares
     - scikit-image (>= 0.25.1)
     - identical labels / segments in raster order
   * - Richardson-Lucy, Wiener
     - scikit-image, NumPy formula
     - identical (1e-15)
   * - k-means, HDBSCAN
     - scikit-learn, ChiSurf
     - identical centres/labels/partition; bit-exact with ChiSurf
   * - Kalman filter
     - filterpy, ChiSurf
     - identical (5e-16); bit-exact with ChiSurf
   * - HMM lattice
     - hmmlearn
     - forward/backward/Viterbi bit-identical
   * - Photon HMM (H2MM)
     - H2MM_C
     - log-likelihood 1e-9, Viterbi paths identical
   * - Photon HMM, variational Bayes (``fit_vb``)
     - hmmlearn ``VariationalCategoricalHMM`` (dense streams)
     - posterior 1e-4, lower bound 2e-10
   * - PDA
     - PAM ``PDA_histogram.cpp`` (compiled natively)
     - identical (2e-18)
   * - BurstML
     - original FRET_burstML MEX (compiled natively)
     - identical (3e-13)
   * - 2CDE, burst search, crosstalk correction
     - FRETBursts
     - identical
   * - 2D-FDC
     - Toru Kondo's ``TK_Create2DFDC_04.m`` (Octave)
     - identical pair counts
   * - Bayesian blocks
     - astropy
     - identical change points
   * - Correlator (wahl / laurence)
     - pycorrelate, multipletau, exact pair counting
     - rounding-exact
   * - Phasor
     - phasorpy
     - identical
   * - Record decoding (PTU HydraHarp/PicoHarp/TimeHarp/MultiHarp, HT3, SPC-130, SPC-630, SPC-QC, .sm)
     - ptufile, phconvert
     - photon-for-photon identical
   * - BrightEyes-TTM ``.ttr``
     - libttp (vendor parser)
     - photons, macro/micro times and marker edges identical
   * - Vectorial PSF (Richards-Wolf)
     - BrightEyes-ISM / PyFocus
     - 5e-5 of peak
   * - Decay convolution and fits
     - NumPy/scipy transcriptions of the integrals and likelihoods
     - 1e-13 .. 1e-16
   * - Simulator RNGs
     - Random123 Philox, pcg32, xoshiro256++, mt19937ar
     - bit-exact

Reading the register
--------------------

Verdicts are **PASS** (agrees to the stated metric), **PASS (bounded)** (agrees up
to a documented, understood difference -- e.g. a reference's unstable sort on
tied edges), **KNOWN-ANSWER** (no second implementation exists; validated
against a simulation with a known answer), or a filed finding in
``okf/BUGS.md`` with the test that pins it. Where the reference itself turned
out to be wrong or to have moved (a scikit-image marker-seed revert, ChiSurf's
``mem.py`` value/gradient mismatch), the register says so rather than loosening
a tolerance.

Reproducing
-----------

.. code-block:: bash

   # the A/B suites (reference libraries are optional imports; a missing one skips visibly)
   PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -m pytest test/python -k "ab_" -q

   # speed + identity against the packaged references
   cd benchmarks && ./build_envs.sh          # one venv per competitor, once
   python bench_vicidomini.py && KMP_DUPLICATE_LIB_OK=TRUE .venvs/vicidomini/bin/python competitors/bench_vicidomini.py && python check_vicidomini.py
   python bench_sciref.py     && .venvs/sciref/bin/python competitors/bench_sciref.py         && python check_sciref.py
   python bench_fret.py       && python competitors/bench_fret.py                             && python check_fret.py
   python bench_tttrlib.py    && .venvs/read/bin/python competitors/bench_phconvert.py        && python check_reading.py

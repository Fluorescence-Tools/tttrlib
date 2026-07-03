.. _workflow_index:

Workflow Index
==============

tttrlib is easiest to learn by starting from the analysis question rather than
from a class name. Each workflow below links the data model, the main API
objects, the relevant examples, and the checks that make the workflow
reproducible.

.. list-table::
   :widths: 18 31 27 24
   :header-rows: 1

   * - Workflow
     - Use when
     - Primary API
     - Examples
   * - :doc:`tttr-core`
     - You need to read files, inspect headers, slice photon streams, write data,
       or prepare selections for downstream analysis.
     - ``TTTR``, ``TTTRHeader``, ``TTTRMask``, ``TTTRSelection``, ``TTTRRange``
     - :doc:`auto_examples/beginner/index`, :doc:`auto_examples/tttr/index`
   * - :doc:`burst-analysis`
     - You need burst search, burst filtering, FRET burst summaries, or
       burst-level selections.
     - ``BurstFilter``, ``BurstFeatureExtractor``, ``TTTRRange``
     - :doc:`auto_examples/single_molecule/index`
   * - :doc:`pda-guide`
     - You need Photon Distribution Analysis for photon-count distributions,
       FRET efficiency projections, or model-to-data comparison.
     - ``Pda``, ``PdaCallback``
     - :doc:`auto_examples/single_molecule/plot_single_molecule_pda_1`
   * - :doc:`fcs-correlation`
     - You need autocorrelation, cross-correlation, gated correlation, sliced
       correlation, or manual event-stream correlation.
     - ``Correlator``, ``CorrelatorPhotonStream``
     - :doc:`auto_examples/correlation/index`
   * - :doc:`clsm-flim-guide`
     - You need CLSM/FLIM images, marker-based image construction, intensity
       maps, phasor maps, decay images, or image transformations.
     - ``CLSMSettings``, ``CLSMImage``
     - :doc:`auto_examples/flim/index`, :doc:`auto_examples/image_correlation/index`
   * - :doc:`localization-guide`
     - You need 2D Gaussian localization of isolated bright spots, beads, or
       emitters in camera or CLSM-derived images.
     - ``ImageLocalizer``, ``GaussianFitResult``
     - :doc:`auto_examples/microscopy_localization/plot_image_localization`
   * - :doc:`fit-guide`
     - You need fluorescence decay fitting, MLE lifetime fitting, model
       simulation, or low-level fit control.
     - ``Fit23``-``Fit26``, ``fit23``-``fit26``, ``DecayFitData``
     - :doc:`auto_examples/fluorescence_decay/index`

Decision Path
-------------

Start from the raw photon stream:

1. Read a ``TTTR`` object and inspect its header.
2. Select photons by channel, event type, time range, or microtime range.
3. Choose a workflow:

   * use :doc:`fcs-correlation` for fluctuation analysis in time,
   * use :doc:`burst-analysis` or :doc:`pda-guide` for single-molecule bursts and
     photon-count distributions,
   * use :doc:`clsm-flim-guide` for marker-based images and pixel-level analysis,
   * use :doc:`localization-guide` for Gaussian localization of isolated image
     spots,
   * use :doc:`fit-guide` for model-based decay fitting.

4. Verify each result with the workflow-specific checks listed in the guide.

Documentation Completeness
--------------------------

The documentation inventory in ``doc/api-coverage.yml`` lists required domains,
symbols, source pages, rendered pages, and examples. The CI docs job runs
``tools/check_docs_coverage.py`` after the Sphinx build so a newly exposed public
symbol cannot silently remain undocumented.

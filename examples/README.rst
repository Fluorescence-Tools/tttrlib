.. _examples:

Example Gallery
===============

This gallery is the best place to learn tttrlib through short, executable
analysis records. Each example loads data, states the relevant timing/channel
choices, runs one analysis step, and shows the resulting plot or table.

Quick Links
-----------

* :ref:`Beginner Tutorials <sphx_glr_auto_examples_beginner>`:
  reading files, slicing photon streams, selections, writing data, and basic
  microscopy.
* :ref:`Lifetime Analysis <sphx_glr_auto_examples_fluorescence_decay>`:
  decay fitting, convolution, IRF/background handling, and benchmarks.
* :ref:`Correlation <sphx_glr_auto_examples_correlation>`:
  FCS/FCCS autocorrelation, cross-correlation, and gated correlation workflows.
* :ref:`FLIM Imaging <sphx_glr_auto_examples_flim>`:
  pixel-wise lifetime analysis, phasors, segmentation, and image transforms.
* :ref:`Single Molecule <sphx_glr_auto_examples_single_molecule>`:
  burst search, FRET observables, microtime selections, and PDA examples.
* :ref:`TTTR objects <sphx_glr_auto_examples_tttr>`:
  file I/O, headers, transcoding, and microtime histograms.

How to Use These Examples
-------------------------

Browse online
   View rendered examples with source, plots, and explanatory text.

Run locally
   Download and run examples on your machine:

   .. code-block:: bash

      # Clone the repository
      git clone https://github.com/fluorescence-tools/tttrlib.git
      cd tttrlib/examples

      # Run a specific example
      python beginner/plot_01_reading_files.py

Some examples require sample data from
`peulen.xyz/downloads/tttr-data/ <https://peulen.xyz/downloads/tttr-data/>`_.

Example Categories
------------------

.. toctree::
   :maxdepth: 2
   :hidden:

   beginner/index
   correlation/index
   flim/index
   fluorescence_decay/index
   image_correlation/index
   miscellaneous/index
   release_highlights/index
   single_molecule/index
   tttr/index

**Getting Started:**

* :ref:`Beginner Tutorials <sphx_glr_auto_examples_beginner>` - start here if you're new to tttrlib.
* :ref:`Release Highlights <sphx_glr_auto_examples_release_highlights>` - what's new in recent versions.

**Analysis Workflows:**

* :ref:`Lifetime Analysis <sphx_glr_auto_examples_fluorescence_decay>` - TCSPC decay fitting and analysis.
* :ref:`Correlation <sphx_glr_auto_examples_correlation>` - FCS and FCCS calculations.
* :ref:`Single Molecule <sphx_glr_auto_examples_single_molecule>` - burst detection and FRET.

**Imaging:**

* :ref:`FLIM <sphx_glr_auto_examples_flim>` - fluorescence lifetime imaging.
* :ref:`Image Correlation <sphx_glr_auto_examples_image_correlation>` - spatial correlation analysis.

**Utilities:**

* :ref:`Miscellaneous <sphx_glr_auto_examples_miscellaneous>` - additional helper scripts.

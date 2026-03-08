.. _examples:

Example Gallery
===============

Welcome to the **tttrlib example gallery**! This is the best place to learn how to use the library through hands-on, executable examples.

.. rst-class:: lead

   Each example is a complete, runnable Python script with explanations and visualizations. Browse by category below.

Quick Links
-----------

.. grid:: 1 2 2 3
   :gutter: 3

   .. grid-item-card::
      :icon: fa-solid fa-graduation-cap
      :link: beginner/index
      :link-type: doc

      **Beginner Tutorials**
      
      Learn the basics: reading files, accessing data, and simple operations.

   .. grid-item-card::
      :icon: fa-solid fa-clock
      :link: fluorescence_decay/index
      :link-type: doc

      **Lifetime Analysis**
      
      Fluorescence decay fitting, microtime histograms, and lifetime distributions.

   .. grid-item-card::
      :icon: fa-solid fa-wave-square
      :link: correlation/index
      :link-type: doc

      **Correlation (FCS/FCCS)**
      
      Autocorrelation and cross-correlation analysis for diffusion studies.

   .. grid-item-card::
      :icon: fa-solid fa-microscope
      :link: flim/index
      :link-type: doc

      **FLIM Imaging**
      
      Fluorescence lifetime imaging microscopy and pixel-wise analysis.

   .. grid-item-card::
      :icon: fa-solid fa-image
      :link: microscopy_flim/index
      :link-type: doc

      **CLSM Imaging**
      
      Confocal laser scanning microscopy image reconstruction and analysis.

   .. grid-item-card::
      :icon: fa-solid fa-circle-nodes
      :link: localization/index
      :link-type: doc

      **Single Molecule**
      
      Burst analysis, FRET efficiency, and single-molecule detection.

How to Use These Examples
-------------------------

**Option 1: Browse Online (Recommended)**
   View the rendered examples with outputs, plots, and explanations right here.
   Click any category card above to explore.

**Option 2: Run Locally**
   Download and run examples on your machine:

   .. code-block:: bash

      # Clone the repository
      git clone https://github.com/fluorescence-tools/tttrlib.git
      cd tttrlib/examples

      # Run a specific example
      python beginner/plot_01_reading_files.py

   .. note::
      Some examples require sample data from `peulen.xyz/downloads/tttr-data/ <https://peulen.xyz/downloads/tttr-data/>`_

Example Categories
------------------

.. toctree::
   :maxdepth: 2
   :hidden:

   
   auto_examples/beginner/index
   auto_examples/correlation/index
   auto_examples/flim/index
   auto_examples/fluorescence_decay/index
   auto_examples/image_correlation/index
   auto_examples/microscopy_flim/index
   auto_examples/microscopy_localization/index
   auto_examples/miscellaneous/index
   auto_examples/release_highlights/index
   auto_examples/single_molecule/index
   auto_examples/tttr/index



**Getting Started:**

* :ref:`Beginner Tutorials <sphx_glr_auto_examples_beginner>` - Start here if you're new to tttrlib
* :ref:`Release Highlights <sphx_glr_auto_examples_release_highlights>` - What's new in recent versions

**Analysis Workflows:**

* :ref:`Lifetime Analysis <sphx_glr_auto_examples_fluorescence_decay>` - TCSPC decay fitting and analysis
* :ref:`Correlation <sphx_glr_auto_examples_correlation>` - FCS and FCCS calculations
* :ref:`Single Molecule <sphx_glr_auto_examples_localization>` - Burst detection and FRET

**Imaging:**

* :ref:`FLIM <sphx_glr_auto_examples_flim>` - Fluorescence lifetime imaging
* :ref:`CLSM <sphx_glr_auto_examples_microscopy_flim>` - Confocal microscopy workflows
* :ref:`ISM <sphx_glr_auto_examples_ism>` - Image scanning microscopy
* :ref:`Image Correlation <sphx_glr_auto_examples_image_correlation>` - Spatial correlation analysis

**Utilities:**

* :ref:`Miscellaneous <sphx_glr_auto_examples_miscellaneous>` - Additional helper scripts

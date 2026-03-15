.. _overview:

.. title:: tttrlib: Time-Resolved Fluorescence Analysis

.. toctree::
   :maxdepth: 2
   :hidden:

   getting-started
   install
   user_guide
   whats_new
   modules/index
   auto_examples/index

.. container:: landing-page

   .. div:: hero-section text-center
   
      .. image:: logos/tttrlib-logo.svg
         :width: 400px
         :alt: tttrlib logo
         :align: center
         :class: hero-logo

      tttrlib
      =======

      **Fast, modular, and open-source analysis for Time-Tagged Time-Resolved (TTTR) data.**

      Read, process, and analyze photon streams from PicoQuant, Becker & Hickl, and Photon-HDF5 files.
      Built with C++ speed and Python flexibility.

       .. grid:: 1 1 2 3
          :gutter: 2
          :class-container: hero-buttons

          .. grid-item::
             :class: text-center

             .. button-ref:: getting_started_detailed
                :color: primary
                :shadow:
                :expand:
                
                Getting Started

          .. grid-item::
             :class: text-center

             .. button-ref:: auto_examples/index
                :color: success
                :shadow:
                :expand:
                
                📚 Examples

          .. grid-item::
             :class: text-center

             .. button-link:: https://github.com/fluorescence-tools/tttrlib
                :color: secondary
                :shadow:
                :expand:
                
                View on GitHub

   .. div:: section-header

      Key Capabilities
      ----------------

   .. grid:: 1 2 3 3
      :gutter: 3

      .. grid-item-card::
         :icon: fa-solid fa-chart-line
         :link: modules/workflow_burst_analysis
         :link-type: doc

         Burst Analysis
         ^^^^^^^^^^^^^^
         Single-molecule burst detection, FRET efficiency, E-S histograms, and population analysis for ALEX/PIE data.

      .. grid-item-card::
         :icon: fa-solid fa-microscope
         :link: modules/imaging
         :link-type: doc

         Imaging (CLSM/FLIM)
         ^^^^^^^^^^^^^^^^^^^
         Construct images from TTTR streams, perform pixel-wise lifetime analysis, and export to standard formats.

      .. grid-item-card::
         :icon: fa-solid fa-wave-square
         :link: topics/correlation_analysis
         :link-type: doc

         Correlation (FCS/FCCS)
         ^^^^^^^^^^^^^^^^^^^^^^
         Fast autocorrelation and cross-correlation analysis for diffusion time and concentration measurements.

      .. grid-item-card::
         :icon: fa-solid fa-hourglass-half
         :link: topics/lifetime_analysis
         :link-type: doc

         Lifetime Analysis
         ^^^^^^^^^^^^^^^^^
         Generate microtime histograms, fit fluorescence decays, and analyze lifetime distributions.

      .. grid-item-card::
         :icon: fa-solid fa-file-import
         :link: auto_examples/beginner/plot_01_reading_files
         :link-type: doc

         Universal I/O
         ^^^^^^^^^^^^^
         Seamlessly read PTU, HT3, SPC, and Photon-HDF5 files without manual conversion.

      .. grid-item-card::
         :icon: fa-solid fa-images
         :link: auto_examples/index
         :link-type: doc

         Example Gallery
         ^^^^^^^^^^^^^^^
         Browse dozens of executable examples covering everything from basic I/O to advanced single-molecule workflows.

      .. grid-item-card::
         :icon: fa-solid fa-book
         :link: https://docs.peulen.xyz/tttrlib/stable/api/index.html

         API Reference
         ^^^^^^^^^^^^^
         Detailed C++ and Python API documentation, including classes, methods, and parameters.


   .. div:: section-header

      Installation
      ------------

   Get up and running in seconds using Conda or Pip.

   .. grid:: 1 1 2 2
      :gutter: 3

      .. grid-item-card:: Conda (Recommended)

         .. code-block:: bash

            conda install -c tpeulen -c conda-forge tttrlib

      .. grid-item-card:: Pip

         .. code-block:: bash

            pip install tttrlib

   .. div:: section-header

      Why tttrlib?
      ------------

   .. grid:: 1 3 3 3
      :gutter: 2

      .. grid-item-card:: 
         :class-header: bg-light

         🚀 High Performance
         ^^^^^^^^^^^^^^^^^^^
         Core algorithms written in C++ for maximum speed, handling gigabytes of photon data efficiently.

      .. grid-item-card::
         :class-header: bg-light

         🐍 Pythonic API
         ^^^^^^^^^^^^^^^
         Integrates seamlessly with NumPy, SciPy, and Matplotlib. Use familiar slicing and indexing syntax.

      .. grid-item-card::
         :class-header: bg-light

         🔧 Modular Design
         ^^^^^^^^^^^^^^^^^
         Build custom analysis pipelines by combining modular components for correlation, histograms, and bursts.


   .. div:: section-header

      Resources
      ---------

   .. grid:: 1 2 2 2
      :gutter: 3

      .. grid-item-card:: 📖 Read the Paper
         :link: https://doi.org/10.1093/bioinformatics/btaf025

         **tttrlib: modular software for integrating fluorescence spectroscopy, imaging, and molecular modeling.**
         *Bioinformatics* (2025).

      .. grid-item-card:: 📢 What's New?
         :link: whats_new
         :link-type: doc

         Check out the latest features, bug fixes, and improvements in the recent releases of tttrlib.

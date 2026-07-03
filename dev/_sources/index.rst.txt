.. _overview:

tttrlib
#######

Fast, modular, open-source analysis for time-tagged time-resolved (TTTR)
photon data.

**tttrlib** reads, processes, and writes photon streams from PicoQuant,
Becker & Hickl, and Photon-HDF5 files through one vendor-independent API. The
core algorithms are written in C++ and exposed to **Python, R, and Java** from a
single shared interface — plus an **ImageJ/Fiji plugin** for point-and-click FLIM
reconstruction. The Python binding integrates with NumPy, SciPy, Matplotlib, and
Jupyter-based workflows. See :doc:`languages` for the same examples in every
language.

Quick links
-----------

.. list-table::
   :widths: 30 70
   :header-rows: 0

   * - Start using tttrlib
     - :doc:`getting-started`
   * - Run small examples
     - :doc:`quickstart`
   * - Use Python, R, or Java
     - :doc:`languages`
   * - Reconstruct FLIM in ImageJ/Fiji
     - :doc:`imagej-plugin`
   * - Choose an analysis workflow
     - :doc:`workflows`
   * - Browse executable workflows
     - :doc:`auto_examples/index`
   * - Read the manual
     - :doc:`user_guide`
   * - Report a problem
     - `GitHub issues <https://github.com/Fluorescence-Tools/tttrlib/issues>`__
   * - Cite tttrlib
     - `Bioinformatics paper <https://doi.org/10.1093/bioinformatics/btaf025>`__

Project focus
-------------

tttrlib is designed for time-resolved fluorescence spectroscopy and imaging
workflows where photon streams need to stay close to their original timing
information:

* TTTR file I/O and metadata inspection.
* Photon selection by channel, event type, time window, and micro-time range.
* Fluorescence decay generation, convolution, phasor analysis, and fitting.
* FCS/FCCS autocorrelation and cross-correlation analysis.
* Single-molecule burst search and burst-level statistics.
* CLSM, FLIM, and image scanning microscopy reconstruction.
* Photon distribution and PDA-style analysis workflows.

Reproducible workflows
----------------------

The examples and notebooks are written as executable analysis records: load a
file, state the channel and timing selections, compute a result, and plot or
export it. This follows the same reproducibility-first documentation style used
by projects such as FRETBursts, while keeping tttrlib focused on a broader TTTR
data model.

Start with :doc:`getting-started` if you are new to the package. Use
:doc:`auto_examples/index` when you already know the analysis type you need.

Installation
------------

Install from PyPI:

.. code-block:: bash

   pip install tttrlib

Install with Conda or Mamba on macOS and Linux:

.. code-block:: bash

   mamba install -c conda-forge -c bioconda tttrlib

Install with Conda or Mamba on Windows:

.. code-block:: bash

   mamba install -c tpeulen tttrlib

For build-from-source instructions, see the repository's ``BUILDING.md`` file.

Documentation
-------------

.. toctree::
   :maxdepth: 2

   getting-started
   quickstart
   languages
   r-package
   imagej-plugin
   workflows
   tttr-core
   file-formats
   burst-analysis
   pda-guide
   fcs-correlation
   clsm-flim-guide
   fit-guide
   user_guide
   modules/index
   auto_examples/index
   troubleshooting
   docs-warning-burndown
   faq
   support
   whats_new
   glossary

.. toctree::
   :hidden:

   install
   configuration
   contents
   performance_guide
   roadmap
   changes

External resources
------------------

* `Source code <https://github.com/Fluorescence-Tools/tttrlib>`__
* `PyPI package <https://pypi.org/project/tttrlib/>`__
* `Conda package <https://anaconda.org/tpeulen/tttrlib>`__
* `tttrlib paper <https://doi.org/10.1093/bioinformatics/btaf025>`__

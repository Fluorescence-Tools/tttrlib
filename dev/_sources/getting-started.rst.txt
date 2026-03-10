.. _getting_started_detailed:

Getting Started with tttrlib
============================

The purpose of this guide is to illustrate some of the main features that **tttrlib** provides.
It assumes a very basic working knowledge of fluorescence spectroscopy (e.g., decay analysis, correlation spectroscopy).
Please refer to our :ref:`installation-instructions` for installing tttrlib.

**tttrlib** is an open-source library that supports a diverse set of experimental TTTR (Time-Tagged Time-Resolved) data.
It provides efficient tools for reading, preprocessing, and analyzing photon streams recorded by PicoQuant, Becker & Hickl, and open Photon-HDF instruments.

Installation
------------

We recommend using **Miniforge** (Conda-forge) for a robust, cross-platform Python environment.

1. **Install Miniforge**
   - Download from https://github.com/conda-forge/miniforge
   - Follow the installation instructions for your operating system (Linux, macOS, Windows).

2. **Create a new environment**

   .. code-block:: bash

      conda create -n tttrlib-env python=3.10
      conda activate tttrlib-env

3. **Install tttrlib**
   - **Linux/macOS**: Use Bioconda (ensure `conda-forge` has higher priority than `defaults`):

     .. code-block:: bash

        conda install -c conda-forge -c bioconda tttrlib

   - **Windows**: Use the custom channel:

     .. code-block:: bash

        conda install -c tpeulen -c conda-forge tttrlib

4. **(Optional) Install Jupyter** to run example notebooks:

   .. code-block:: bash

      conda install jupyter

5. **Alternative: pip**
   - For PyPI releases:

     .. code-block:: bash

        pip install tttrlib

   - For the latest development version:

     .. code-block:: bash

        git clone https://github.com/fluorescence-tools/tttrlib.git
        cd tttrlib
        pip install .

Quick Start: Browse the Example Gallery
----------------------------------------

The fastest way to learn tttrlib is through our **executable examples**.

.. button-ref:: auto_examples/index
   :color: primary
   :shadow:
   
   📚 Browse All Examples

The gallery includes:

* **Beginner tutorials** - Reading files, basic operations
* **FLIM analysis** - Lifetime fitting, image reconstruction  
* **FCS/FCCS** - Correlation analysis
* **Burst analysis** - Single-molecule detection
* **CLSM imaging** - Confocal microscopy workflows

Each example is a complete, runnable Python script with explanations.

Running Examples Locally
------------------------

- Download example TTTR files from https://www.peulen.xyz/downloads/tttr-data/ or use your own experimental data.
- Open a Python shell or Jupyter notebook.
- Try this minimal example:

  .. code-block:: python

      import tttrlib
      # Use type inference (recommended)
      tttr = tttrlib.TTTR("path/to/example.ptu")
      print(f"Number of events: {len(tttr)}")

      # If type inference fails:
      # tttr = tttrlib.TTTR("path/to/example.ptu", "PTU")

Opening TTTR Files and Accessing Data
-------------------------------------

**tttrlib** provides a simple, unified interface for TTTR files via :term:`TTTR` objects.
A :class:`~tttrlib.TTTR` object represents the photon stream contained in a file and provides direct access to:

- Macro times (experiment time / sync pulse number)
- Micro times (delay after excitation)
- Routing channels (detector IDs)
- Event types (photon, marker, etc.)

Example:

.. code-block:: python

    import tttrlib
    tttr1 = tttrlib.TTTR()               # create an empty TTTR object
    tttr2 = tttrlib.TTTR("filename.ptu") # load a TTTR file
    micro_times = tttr2.micro_times

**TTTR objects** can be used to compute correlation curves, fluorescence decays, photon distribution histograms, or to generate images from time-resolved confocal laser scanning microscopy (CLSM) data.

For example:

.. code-block:: python

    import tttrlib
    data = tttrlib.TTTR("clsm_filename.ptu")
    clsm = tttrlib.CLSMImage(data)
    intensity_image = clsm.intensity

Understanding TTTR File Formats
-------------------------------

TTTR files store photon-by-photon data streams. Supported formats include:

- PicoQuant: PTU, HT3
- Becker & Hickl: SPC
- Photon-HDF5: Open community standard

Each file encodes photon arrival information as integer arrays of macro times, micro times, and routing channels.
The file type can be inferred automatically or specified explicitly using the second argument of the constructor, e.g. `"PTU"`, `"SPC"`, or `"HDF5"`.

Basic Python and NumPy Concepts
-------------------------------

Some familiarity with NumPy array operations will help when working with tttrlib:

- **Slicing:** `tttr[:100]` selects the first 100 events
- **Boolean indexing:** `tttr[mask]` filters events
- **NumPy arrays:** photon data is exposed as NumPy arrays for fast numerical processing

Quick Reference
---------------

- **Read a file:**
  ``tttrlib.TTTR(filename, file_type=None)``

- **Get macro times:**
  ``tttr.macro_times``

- **Get micro times:**
  ``tttr.micro_times``

- **Select by channel:**
  ``tttr.get_selection_by_channel([0, 1])``

- **Slice events:**
  ``tttr[100:200]``

Next Steps
-----------

This section introduced how TTTR files are read and how photon data can be accessed.
To learn more, see:

- The :ref:`user_guide` for advanced tools and workflows
- The :ref:`general_examples` for practical scripts
- The :ref:`tutorial_menu` for step-by-step learning resources
- The :ref:`api_ref` for a complete reference of all public classes and methods

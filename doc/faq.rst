.. _faq:

===========================
Frequently Asked Questions
===========================

.. currentmodule:: tttrlib

This page provides answers to common questions about ``tttrlib``.

What does the project name mean?
--------------------------------
The name **tttrlib** stands for **time-tagged time-resolved library**.
TTTR data are widely used in **fluorescence spectroscopy**, **lifetime imaging (FLIM)**,
and other techniques requiring picosecond-to-nanosecond timing precision,
such as **LIDAR (Light Detection and Ranging)**.

What is tttrlib?
----------------
``tttrlib`` is a **file-format-agnostic**, **high-performance** C++ library with
Python bindings for reading, processing, and writing **time-tagged time-resolved (TTTR)**
photon data from **PicoQuant**, **Becker & Hickl**, and **Photon-HDF5** files.

It provides a fast, vendor-independent API for handling photon streams and enables
integration into advanced data-analysis pipelines for time-resolved fluorescence
spectroscopy and imaging.

**Key features include:**
- High-speed TTTR file reading (IO-limited)
- Multi-dimensional histogramming and correlation analysis
- Fluorescence decay generation and lifetime fitting
- Photon distribution analysis (FIDA/PCH)
- Burst and time-window selections
- FLIM and ISM image generation (adaptive pixel reassignment, focus-ISM)
- Open Photon-HDF5 support

Typical performance:
~40× faster decay histogramming and 2–5× faster burst selection vs pure Python.

How can I install tttrlib?
--------------------------
We recommend using **Miniforge** or **Mambaforge** with the fast *mamba* solver.

**macOS / Linux**
::

    mamba install -c bioconda tttrlib

**Windows**
::

    mamba install -c tpeulen tttrlib

**From PyPI (development version)**
::

    pip install git+https://github.com/fluorescence-tools/tttrlib
    # or the development branch
    pip install git+https://github.com/fluorescence-tools/tttrlib@development

**From source**
::

    git clone --recursive https://github.com/fluorescence-tools/tttrlib.git
    cd tttrlib
    pip install -e .

Precompiled binaries are available for Windows, Linux (x86, ARM64, PPCle), and macOS.
Legacy 32-bit and Python 2.7 are not supported.

How can I load my own datasets?
-------------------------------
``tttrlib`` supports:
- **PicoQuant:** PicoHarp, TimeHarp, HydraHarp (`.ptu`, `.ht3`, T2/T3)
- **Becker & Hickl:** `spc132`, `spc630` (256 & 4096 mode)
- **Photon-HDF5:** open standard format

If your instrument or file type is not supported:
1. Open a GitHub issue describing the format and instrument.
2. Provide a **small sample dataset** (<100 MB) and expected results.
3. Optionally describe your workflow or analysis steps.

This allows us to integrate and test the new format automatically.

How can I get help using tttrlib?
---------------------------------
Before asking for help:
1. **Check the documentation** and examples first.
2. If you still encounter issues, prepare a **minimal reproducible example** (under 10 lines) using:
   - Demo data shipped with ``tttrlib``, or
   - Synthetic data from ``numpy.random`` (with a fixed seed).

Include in your report:
- Import statements and test code
- Full traceback and error message
- ``tttrlib`` version and system details

For bug reports or feature requests, use the
`GitHub issue tracker <https://github.com/fluorescence-tools/tttrlib/issues>`_.

See also:
https://stackoverflow.com/help/mcve

How can I enable verbose or debug output?
-----------------------------------------
Set the environment variable ``TTTRLIB_VERBOSE=1`` to enable detailed logs.

+----------------------+----------------------------------+
| **Platform**         | **Command**                      |
+----------------------+----------------------------------+
| Linux/macOS (bash)   | ``export TTTRLIB_VERBOSE=1``     |
| Windows (CMD)        | ``set TTTRLIB_VERBOSE=1``        |
| Windows (PowerShell) | ``$env:TTTRLIB_VERBOSE = "1"``   |
+----------------------+----------------------------------+

Include these logs when reporting issues.

Where can I find examples?
--------------------------
The documentation includes runnable examples and Sphinx-Gallery tutorials.
To explore them locally, see the ``examples`` directory and run:

::

    python plot_01_reading_files.py

Each example demonstrates a fundamental operation:
- Reading TTTR files
- Basic manipulations and selections
- Burst searches and intensity traces
- Writing new TTTR files

.. note::
   The example scripts use small demo files included in the ``tttrlib`` test data.
   To run them successfully, you may need to adjust the **TTTR file path**
   or set the environment variable ``TTTRLIB_DATA`` to point to your local data directory.

   Example datasets can be downloaded from:
   **https://www.peulen.xyz/downloads/tttr-data/**

Where can I get community help?
-------------------------------
- GitHub issues:
  `<https://github.com/fluorescence-tools/tttrlib/issues>`_
- Discord community:
  `<https://discord.gg/ghP9QqWUaK>`_

Where can I cite tttrlib?
-------------------------
If you use ``tttrlib`` in your work, please cite:

> **Thomas-Otavio Peulen**, Katherina Hemmen, Annemarie Greife, Benjamin M. Webb,
> Suren Felekyan, Andrej Sali, Claus A. M. Seidel, Hugo Sanabria, Katrin G. Heinze.
> *“tttrlib: modular software for integrating fluorescence spectroscopy, imaging, and molecular modeling.”*
> **Bioinformatics** 41 (2): btaf025 (2025).
> https://doi.org/10.1093/bioinformatics/btaf025

What license does tttrlib use?
------------------------------
``tttrlib`` is released under the **BSD-3-Clause** license.
Copyright © 2007–2025, *tttrlib developers*.

---

.. image:: https://github.com/Fluorescence-Tools/tttrlib/blob/main/doc/logos/mashup.png?raw=true
   :alt: tttrlib FLIM overview
   :align: center
   :width: 600px

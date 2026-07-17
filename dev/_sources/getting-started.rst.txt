.. _getting_started_detailed:

Getting started
===============

This guide gives a short path from installation to first analysis. It assumes
basic familiarity with time-resolved fluorescence data, but it does not assume
knowledge of tttrlib's API.

What tttrlib provides
---------------------

tttrlib works with photon-by-photon TTTR streams. A loaded
``tttrlib.TTTR`` object exposes the core arrays used in most analyses:

* ``macro_times``: experiment time or sync-pulse index.
* ``micro_times``: delay after excitation.
* ``routing_channels``: detector or routing channel identifiers.
* ``event_types``: photon, marker, overflow, and related event classes.
* ``header``: instrument and file metadata.

From the same object you can select photons, build histograms, fit decays,
compute correlations, run burst searches, or reconstruct images.

Installation
------------

For a new environment, Miniforge with the ``mamba`` solver is recommended.

.. code-block:: bash

   conda create -n tttrlib-env python=3.11
   conda activate tttrlib-env

On macOS and Linux, install from Bioconda:

.. code-block:: bash

   mamba install -c conda-forge -c bioconda tttrlib

On Windows, install from the ``tpeulen`` channel:

.. code-block:: bash

   mamba install -c tpeulen tttrlib

You can also install from PyPI:

.. code-block:: bash

   pip install tttrlib

For a development checkout:

.. code-block:: bash

   git clone https://github.com/fluorescence-tools/tttrlib.git
   cd tttrlib
   pip install -e .

First file
----------

Load a TTTR file by filename. The file type is inferred when possible.

.. code-block:: python

   import tttrlib

   tttr = tttrlib.TTTR("path/to/photon_stream.ptu")
   print(len(tttr))
   print(tttr.header.json)

If type inference is not sufficient for a file, pass the file type explicitly:

.. code-block:: python

   tttr = tttrlib.TTTR("path/to/photon_stream.ptu", "PTU")

Common first operations
-----------------------

Access the raw timing arrays:

.. code-block:: python

   macro = tttr.macro_times
   micro = tttr.micro_times
   routing = tttr.routing_channels

Select photons from one or more routing channels:

.. code-block:: python

   selection = tttr.get_selection_by_channel([0, 1])
   selected = tttr[selection]

Create a micro-time decay histogram:

.. code-block:: python

   import numpy as np

   counts, edges = np.histogram(tttr.micro_times, bins=256)

Compute a correlation curve:

.. code-block:: python

   correlator = tttrlib.Correlator(channels=([1], [2]), tttr=tttr)
   taus = correlator.x_axis
   amplitudes = correlator.correlation

Reconstruct a CLSM intensity image:

.. code-block:: python

   image_data = tttrlib.TTTR("path/to/image.ptu")
   clsm = tttrlib.CLSMImage(image_data)
   clsm.fill(channels=[0, 1], micro_time_ranges=[[0, 16000]])
   intensity = clsm.intensity

Example-driven learning
-----------------------

The most useful documentation path is the executable example gallery. Each
example is a complete script with the selections, parameters, and plotting code
kept next to the result.

* :doc:`auto_examples/tttr/index` covers file reading, headers, selections, and
  transcoding.
* :doc:`auto_examples/correlation/index` covers autocorrelation and
  cross-correlation workflows.
* :doc:`auto_examples/fluorescence_decay/index` covers decay fitting and
  convolution.
* :doc:`auto_examples/flim/index` covers CLSM and FLIM image workflows.
* :doc:`auto_examples/single_molecule/index` covers burst selection, MCS, and
  PDA-oriented examples.

Supported file families
-----------------------

tttrlib currently supports:

* PicoQuant PTU and HT3 data, including T2 and T3 records.
* Becker & Hickl SPC data.
* Photon-HDF5 files.

When requesting support for another format, provide a small example file,
expected metadata, and at least one expected analysis result. That makes it
possible to add regression coverage.

Where to go next
----------------

* :doc:`quickstart` for short copy-paste examples.
* :doc:`user_guide` for the domain-oriented manual.
* :doc:`modules/index` for notebook-style topic pages.
* :doc:`auto_examples/index` for executable gallery scripts.
* :doc:`troubleshooting` if a file does not load or an analysis looks empty.

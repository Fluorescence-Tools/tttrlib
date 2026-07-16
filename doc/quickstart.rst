Quickstart
==========

This page collects the smallest useful snippets for loading TTTR data and
running common analyses.

Install
-------

With pip:

.. code-block:: console

   pip install tttrlib

With Conda or Mamba on macOS and Linux:

.. code-block:: console

   mamba install -c conda-forge -c bioconda tttrlib

With Conda or Mamba on Windows:

.. code-block:: console

   mamba install -c tpeulen tttrlib

Load data
---------

.. code-block:: python

   import tttrlib

   data = tttrlib.TTTR("photon_stream.ptu")
   print(len(data))

Access photon arrays
--------------------

.. code-block:: python

   macro_times = data.macro_times
   micro_times = data.micro_times
   routing_channels = data.routing_channels

Inspect metadata
----------------

.. code-block:: python

   print(data.header.json)
   print(data.header.to_csv())

Select photons
--------------

.. code-block:: python

   green = data[data.get_selection_by_channel([0])]
   red = data[data.get_selection_by_channel([1])]

Compute a correlation
---------------------

.. code-block:: python

   correlator = tttrlib.Correlator(channels=([0], [1]), tttr=data)
   taus = correlator.x_axis
   correlation = correlator.correlation

Build a decay histogram
-----------------------

.. code-block:: python

   import numpy as np

   counts, edges = np.histogram(data.micro_times, bins=256)

Create a CLSM intensity image
-----------------------------

.. code-block:: python

   image_data = tttrlib.TTTR("image.ptu")
   clsm = tttrlib.CLSMImage(image_data)
   clsm.fill(channels=[0, 1], micro_time_ranges=[[0, 16000]])
   intensity_image = clsm.intensity

Run a burst search
------------------

.. code-block:: python

   L, m, T = 30, 10, 1e-3
   ranges = data.burst_search(L=L, m=m, T=T)
   bursts = list(zip(ranges[0::2], ranges[1::2]))

Next steps
----------

* :doc:`getting-started` explains the first workflow in more detail.
* :doc:`auto_examples/index` contains executable examples and plots.
* :doc:`user_guide` groups the documentation by analysis domain.
* :doc:`troubleshooting` covers common loading and selection problems.

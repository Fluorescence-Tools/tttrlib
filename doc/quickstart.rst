Quickstart
==========

This page gets you up and running with `tttrlib` quickly. Install the package, load data, compute basic analyses, and see where to go next.

Installation
------------

- Conda (recommended):

  .. code-block:: console

     conda install -c tpeulen tttrlib

- Pip:

  .. code-block:: console

     pip install tttrlib

Minimal Examples
----------------

Load TTTR data and access arrays:

.. code-block:: python

   import tttrlib
   data = tttrlib.TTTR('photon_stream.ptu')
   macro_times = data.macro_times
   micro_times = data.micro_times
   routing_channels = data.routing_channels

Print header information:

.. code-block:: python

   import tttrlib
   data = tttrlib.TTTR('photon_stream.ptu')
   print(data.json)

Compute a correlation:

.. code-block:: python

   import tttrlib
   data = tttrlib.TTTR('photon_stream.ptu')
   correlator = tttrlib.Correlator(channels=([1], [2]), tttr=data)
   taus = correlator.x_axis
   correlation_amplitude = correlator.correlation

Create a simple FLIM image from CLSM data:

.. code-block:: python

   import tttrlib
   data = tttrlib.TTTR('image.ptu')
   clsm = tttrlib.CLSM(data)
   channels = [0, 1]
   prompt_range = [0, 16000]
   clsm.fill(channels=channels, micro_time_ranges=[prompt_range])
   intensity_image = clsm.intensity

Next Steps
----------

Explore the :doc:`Example Gallery <auto_examples/index>` for hands-on tutorials covering everything from basic I/O to advanced analysis workflows.

.. button-ref:: auto_examples/index
   :color: primary
   :shadow:
   
   📚 Browse Example Gallery

Additional resources:

- Learn key concepts in the :doc:`user_guide`
- See domain topics: :doc:`topics/lifetime_analysis` and :doc:`topics/correlation_analysis`

Notes
-----

- Verbose diagnostics can be enabled with the environment variable ``TTTRLIB_VERBOSE``.
- For installation details and platform-specific guidance, see :doc:`getting_started_detailed`.

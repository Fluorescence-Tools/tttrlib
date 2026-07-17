.. _languages:

tttrlib in your language
========================

tttrlib is one C++ core exposed to **Python**, **R**, and **Java** through a
shared `SWIG <https://www.swig.org/>`__ interface. The same objects, the same
algorithms, and the same file readers are available from every language — only
the surface syntax differs. Pick a language below; the tabs on this page stay in
sync, so every example is shown the way *you* would write it.

.. tab-set::

   .. tab-item:: Python
      :sync: python

      The reference binding — installed from conda-forge/Bioconda or PyPI, with
      NumPy arrays throughout. This is the most heavily tested surface.

   .. tab-item:: R
      :sync: r

      An installable R package. Arrays are native R vectors; methods are S4-style
      free functions, e.g. ``TTTR_get_macro_times(obj)`` rather than
      ``obj.macro_times``. See :doc:`the R install guide <r-package>`.

   .. tab-item:: Java
      :sync: java

      A self-contained JNI JAR (also shipped as an ImageJ/Fiji plugin). Arrays are
      Java primitives (``long[]``, ``int[]``, ``double[]``); large "output" getters
      use ``…_into(array)`` accessors. See :doc:`the ImageJ guide <imagej-plugin>`.

.. tip::

   **No code required?** The Java binding also ships as an **ImageJ/Fiji plugin**
   that opens PTU/HT3/SPC confocal files and reconstructs intensity, FastLifetime,
   phasor, and Number & Brightness images directly in Fiji — drop a single JAR
   into ``Fiji.app/plugins``. See :doc:`imagej-plugin`.


Install
-------

.. tab-set::

   .. tab-item:: Python
      :sync: python

      .. code-block:: bash

         # conda (recommended; pulls the compiled core)
         mamba install -c conda-forge -c bioconda tttrlib

         # or from PyPI
         pip install tttrlib

   .. tab-item:: R
      :sync: r

      .. code-block:: bash

         # conda
         mamba install -c conda-forge -c tpeulen r-tttrlib

      Building from source is described in :doc:`r-package`.

   .. tab-item:: Java
      :sync: java

      The easiest install is to download the ready-built plugin JAR
      ``tttrlib_imagej-<version>.jar`` from the latest
      `GitHub release <https://github.com/Fluorescence-Tools/tttrlib/releases>`__
      (under *Assets*). It bundles the native library for Linux, macOS (Intel +
      Apple Silicon), and Windows — drop it on the class path, or into
      ``Fiji.app/plugins`` to use it as an ImageJ plugin. Details in
      :doc:`imagej-plugin`.


Load a file and inspect the photon stream
-----------------------------------------

Every binding reads the same raw formats (PicoQuant PTU/HT3, Becker & Hickl
SPC, …) and exposes the same per-photon arrays.

.. tab-set::

   .. tab-item:: Python
      :sync: python

      .. code-block:: python

         import tttrlib

         data = tttrlib.TTTR("bh_spc132.spc", "SPC-130")
         print(data.size)                         # number of photons
         print(data.macro_times[0])               # first macro time
         print(data.micro_times[0])               # first micro time
         print(data.get_used_routing_channels())  # e.g. [0, 1, 8, 9]

   .. tab-item:: R
      :sync: r

      .. code-block:: r

         library(tttrlib)

         data <- TTTR("bh_spc132.spc", "SPC-130")
         TTTR_size(data)                        # number of photons
         TTTR_get_macro_time_at(data, 0L)       # first macro time
         TTTR_get_micro_time_at(data, 0L)       # first micro time
         TTTR_get_used_routing_channels(data)   # e.g. c(9, 8, 0, 1)

         macro <- TTTR_get_macro_times(data)    # full vector

   .. tab-item:: Java
      :sync: java

      .. code-block:: java

         import io.github.fluorescencetools.tttrlib.*;

         TTTR data = new TTTR("bh_spc132.spc", "SPC-130");
         long n = data.size();                       // number of photons
         System.out.println(data.get_macro_time_at(0));
         System.out.println(data.get_micro_time_at(0));

         long[] macro = new long[(int) n];           // fill a preallocated array
         data.get_macro_times_into(macro);

         int[] channels = new int[256];
         int nCh = data.get_used_routing_channels_into(channels);


Build a micro-time (decay) histogram
------------------------------------

.. tab-set::

   .. tab-item:: Python
      :sync: python

      .. code-block:: python

         hist, time_axis = data.get_microtime_histogram(micro_time_coarsening=1)
         print(len(hist), max(hist))

   .. tab-item:: R
      :sync: r

      .. code-block:: r

         # multi-output call returns list(NULL, histogram, time_axis)
         hist <- TTTR_get_microtime_histogram(data)[[2]]
         c(length(hist), max(hist))

   .. tab-item:: Java
      :sync: java

      .. code-block:: java

         double[] hist = new double[data.get_number_of_micro_time_channels()];
         data.get_microtime_histogram_into(hist, new VectorInt32(), 1);


Reconstruct a FLIM image (CLSM)
-------------------------------

Confocal image reconstruction and the fluorescence-lifetime (mean micro time)
image are shared across all three bindings.

.. tab-set::

   .. tab-item:: Python
      :sync: python

      .. code-block:: python

         img = tttrlib.CLSMImage(data, channels=[0], fill=True)
         intensity = img.get_intensity()           # (frames, lines, pixels)
         lifetime  = img.get_mean_micro_time(data)  # FastLifetime image
         phasor    = img.get_phasor(data)           # g/s coordinates

   .. tab-item:: R
      :sync: r

      .. code-block:: r

         img       <- CLSMImage(data, CLSMSettings(), NULL, TRUE, c(0L))
         intensity <- CLSMImage_get_intensity(img)
         lifetime  <- CLSMImage_get_mean_micro_time(img, data)
         phasor    <- CLSMImage_get_phasor_v(img, data)  # g/s, interleaved

   .. tab-item:: Java
      :sync: java

      .. code-block:: java

         VectorInt32 ch = new VectorInt32(); ch.add(0);
         CLSMImage img = new CLSMImage(data, new CLSMSettings(), null, true, ch);
         int nPix = img.getN_frames() * img.getN_lines() * img.getN_pixel();

         int[] intensity = new int[nPix];
         img.get_intensity_into(intensity);
         double[] lifetime = new double[nPix];
         img.get_mean_micro_time_into(lifetime, -1.0, 2, false, false);


How consistent are the bindings?
--------------------------------

Because all three share the same C++ core, they return **identical numbers** on
the same input. This is enforced in CI by a cross-language reference test suite
(``test/python/*/test_*cross_language*.py``, ``test/r/test_*.R``,
``test/java/*Test.java``) that asserts the same canonical values — photon counts,
routing channels, correlator curves, burst results, micro-time histograms, CLSM
intensity / lifetime / phasor, decay-fit results (fit23–fit26), and PDA
histograms — from Python, R, and Java.

.. note::

   The **Python** binding is the most mature and has the broadest test suite.
   The **R** and **Java** bindings expose the same core and are verified against
   the same reference values, but cover a smaller slice of the API. If you hit a
   gap, please `open an issue <https://github.com/Fluorescence-Tools/tttrlib/issues>`__.


Where next
----------

* :doc:`getting-started` and :doc:`quickstart` — the full Python walk-through.
* :doc:`r-package` — installing and using the R package.
* :doc:`imagej-plugin` — the ImageJ/Fiji plugin and the raw Java JAR.
* :doc:`modules/index` — the complete API reference.

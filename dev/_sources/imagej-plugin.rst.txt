.. _imagej_plugin:

tttrlib for Java & the ImageJ/Fiji plugin
=========================================

The Java binding is a self-contained JNI JAR that exposes the tttrlib C++ engine
to any JVM language, and doubles as an **ImageJ/Fiji plugin** for opening and
reconstructing confocal (CLSM) FLIM data.

.. note::

   **Maturity.** The Java binding and plugin share the tested C++ core with
   Python, but the Java layer is newer and less tested than the Python package.
   Please `report issues <https://github.com/fluorescence-tools/tttrlib/issues>`__.

Install
-------

Download ``tttrlib-imagej-plugin.jar`` from the
`GitHub releases <https://github.com/Fluorescence-Tools/tttrlib/releases>`__ page.
It bundles the native library for **Linux, macOS (Intel + Apple Silicon), and
Windows**, so no compilation or ``java.library.path`` setup is required.

- **As a library:** put the JAR on your class path. A small ``NativeLoader``
  extracts and loads the correct native at run time.
- **As a Fiji plugin:** drop the JAR into ``Fiji.app/plugins`` and restart. The
  commands appear under **Plugins ▸ tttrlib**.

Using the Java API
------------------

Array-returning C++ getters that use output pointers are exposed as
``…_into(array)`` accessors: preallocate a primitive array and pass it in.

.. code-block:: java

   import io.github.fluorescencetools.tttrlib.*;

   TTTR data = new TTTR("photon_stream.ptu");
   long n = data.size();

   long[] macro = new long[(int) n];
   data.get_macro_times_into(macro);        // fills the array, returns the count

   // Reconstruct a CLSM/FLIM image for routing channel 0
   VectorInt32 ch = new VectorInt32(); ch.add(0);
   CLSMImage img = new CLSMImage(data, new CLSMSettings(), null, true, ch);
   int nPix = img.getN_frames() * img.getN_lines() * img.getN_pixel();
   int[] intensity = new int[nPix];
   img.get_intensity_into(intensity);

Unlike R, Java carries 64-bit macro times as a native ``long`` (no precision
loss) and supports directors (subclassing ``PdaCallback``).

The ImageJ/Fiji plugin
----------------------

**Menu: Plugins ▸ tttrlib ▸ Open TTTR CLSM Image.**

.. figure:: https://github.com/Fluorescence-Tools/tttrlib/blob/main/docs/img/clsm_example.png?raw=true
   :alt: tttrlib ImageJ plugin example output
   :width: 100%

The plugin opens time-tagged time-resolved confocal files — PicoQuant PTU/HT3,
Becker & Hickl SPC, Leica SP5/SP8 — with the tttrlib C++ engine (via JNI) and
reconstructs, per routing channel:

* an **Intensity** image stack (photon counts, one slice per frame),
* a **FastLifetime** image stack (mean photon arrival time per pixel),
* a **Phasor** image (g and s coordinates per pixel),
* **Number & Brightness (N&B)** maps (per-pixel B, N and epsilon), and
* a **Decay** plot (aggregate micro-time histogram per group, exportable as text).

Channel groups
~~~~~~~~~~~~~~

The **Channel groups** field groups routing channels into colour channels with
the syntax ``groupA;groupB;...``, each group a comma-separated channel list. For
example ``1,3;2,4`` makes colour channel 1 from routing channels {1, 3} and
colour channel 2 from {2, 4}. Every requested image type is produced as a
multi-colour composite hyperstack, one channel per group.

PIE / micro-time ranges
~~~~~~~~~~~~~~~~~~~~~~~~

The **Micro-time ranges** field gates photons by arrival time with the syntax
``start,stop;start,stop;...`` — e.g. ``0,111;200,499;900,1200``. The plugin
reconstructs a separate image per (channel group × window). Combine with channel
groups for PIE/ALEX-style donor/acceptor prompt-and-delayed splitting.

Other options
~~~~~~~~~~~~~

* **Stack frames** collapses the frame dimension to a single frame.
* **Auto-correct IRF offset** estimates the instrument-response offset from the
  rise of the aggregate decay and applies it to FastLifetime (subtracting the
  offset) and Phasor (rotating each pixel phasor), so no separate IRF file is
  needed.
* **Number & Brightness (N&B)** computes per-pixel ``B = variance/mean``,
  ``N = mean²/variance`` and ``epsilon = B − 1`` across frames (≥ 2 frames
  required; unaffected by *Stack frames*).
* **Decay from Mask** (a second command) computes the decay of a selected ROI,
  one column per routing channel, as an exportable results table.

Source: `ext/java/imagej/ <https://github.com/Fluorescence-Tools/tttrlib/tree/main/ext/java/imagej>`__.

Testing status
--------------

The Java binding is verified against the same cross-language reference values as
Python and R (``test/java/TTTRSmokeTest.java``, ``CLSMTest.java``,
``DecayFitTest.java``, ``PdaTest.java``). See :doc:`languages` for a side-by-side
comparison of the three languages.

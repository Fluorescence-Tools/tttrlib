.. _r_package:

tttrlib for R (``r-tttrlib``)
=============================

R bindings for the tttrlib C++ engine, generated with SWIG from the same
language-neutral interface as the Python and Java bindings. You get native R
vectors in and out, and the full TTTR / correlation / decay / imaging API.

.. note::

   **Maturity.** The R (and Java) bindings are newer and less battle-tested than
   the Python package. The C++ core is shared and heavily tested through Python,
   but the R-specific marshalling has lighter coverage. Please
   `report issues <https://github.com/fluorescence-tools/tttrlib/issues>`__ — see
   :ref:`r_testing_status` below.

Installation
------------

Conda / Mamba (recommended)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The ``r-tttrlib`` package is built by ``recipes/r/`` (rattler-build) and
published to the project conda channel:

.. code-block:: bash

   mamba install -c conda-forge -c tpeulen r-tttrlib

Linux and macOS are supported; Windows is not built for R yet.

From source
~~~~~~~~~~~

Requires R (**4.4.x** — SWIG's R runtime is not yet compatible with R ≥ 4.5), a
C++17 compiler, CMake, SWIG ≥ 4.1, and HDF5.

.. code-block:: bash

   git clone --recursive https://github.com/fluorescence-tools/tttrlib.git
   cd tttrlib

   # 1. Generate the SWIG R wrapper + build the static C++ core
   cmake -S . -B build-r -DBUILD_PYTHON_INTERFACE=OFF \
                         -DBUILD_R_INTERFACE=ON -DBUILD_LIBRARY=ON
   cmake --build build-r --target tttrlibR tttrlibStatic

   # 2. Install the R package (recipes/r/build.sh automates this, including
   #    staging the generated wrapper and setting the include/link flags)
   R CMD INSTALL ext/r/pkg

Usage
-----

SWIG-R exposes C++ classes as **S4 objects**; methods are **generics called
function-style** (``Class_method(obj, ...)``), not ``obj$method()``.

.. code-block:: r

   library(tttrlib)

   # Read a photon stream
   tttr <- TTTR("photon_stream.ptu")
   n    <- TTTR_size(tttr)

   # Native R vectors
   macro   <- TTTR_get_macro_times(tttr)
   micro   <- TTTR_get_micro_times(tttr)
   routing <- TTTR_get_routing_channel(tttr)

   # A CLSM/FLIM image (routing channel 0)
   img       <- CLSMImage(tttr, CLSMSettings(), NULL, TRUE, c(0L))
   intensity <- CLSMImage_get_intensity(img)
   lifetime  <- CLSMImage_get_mean_micro_time(img, tttr)

Known limitations
~~~~~~~~~~~~~~~~~~

- **64-bit macro times** are carried through R's ``double``, so exact integer
  values above 2\ :sup:`53` lose precision (a limitation of R's numeric type).
  Java does not have this issue.
- **Directors** (subclassing ``PdaCallback`` in R) are not available; only the
  built-in callbacks are exposed.
- A few methods with pointer-typed default arguments are exposed through
  scalar-argument helpers (e.g. ``CLSMImage_get_phasor_v``) because R's overload
  dispatch cannot resolve the native signature.
- Multi-output getters return an R ``list`` (e.g. the micro-time histogram is
  ``TTTR_get_microtime_histogram(tttr)[[2]]``).

.. _r_testing_status:

Testing status
--------------

The bindings share the C++ core with Python, but the language-specific
marshalling needs more coverage. The cross-language test suite asserts
**identical reference values across Python, R and Java** for known files
(``test/r/test_tttr.R``, ``test_clsm.R``, ``test_decayfit.R``, ``test_pda.R``),
covering photon counts, routing channels, correlator curves, burst search,
micro-time histograms, CLSM intensity / mean-micro-time / phasor, the decay fits
(fit23–fit26), and PDA histograms. See :doc:`languages` for a side-by-side view.

Histograms
==========

``tttrlib`` offers some functions and classes for the creation of histograms. They are part of ``tttrlib`` to offer
more performance in direct comparison to general purpose routines for analysis methods as Photon Distribution Analysis
(PDA) that make heady use of repeated histogram calculations.

.. note::

    These function and ``classes`` are not intended as a substitution for more general functions, e.g.,
    offered by `NumPy <https://www.numpy.org/>`_ and are less tested. They offer performance advantages for
    histograms calculated on a logarithmic (log10) scale and a linear scale in direct comparison to NumPy.


One dimensional histograms
--------------------------


Multi dimensional histograms
----------------------------

In addition to simple one dimensional histograms ``tttrlib`` can calculate multidimensional histograms with an
arbitrary number of dimensions. For that, first a new object of the class ``doubleHistogram`` is created. Here,
*double* refers to the data type double - a floating point number. Next, axes are added to the Histogram by
calling the method ``setAxis``. The first argument is the number of the axis, the second the name of the axis,
the third and the fourth the range of the axis, the fifth the number of bins, and the sixth the type of the
axis. The ``Histogram`` class support arbitrary axis types. However, when initializing an axis via ``setAxis``
the axis type must be either *lin* or *log10* for a linear or an logarithmic axis with base 10. Next, the data
are assigned to the histogram by the ``setData`` method and the histogram is calculated by the ``update`` method.
A working example for 2D normal distributed data is shown below.

.. plot:: plots/histogram_2D.py
   :include-source:


Benchmark
---------

A direct comparison of the performance of the ``tttrlib`` and the Numpy histogram routines (Numpy version 1.13.3,
Linux) demonstrates that except for arbitrarily spaced histograms the ``tttrlib`` histogram routines outperform
numpy by at least a factor of 2 (1D log10 Histograms and 2D Histograms) or by a factor of ~40 (1D linear Histograms)

.. plot:: plots/histogram_benchmark.py
   :include-source:

The current histogram implementation in ``tttrlib`` is not particularly optimized for speed, e.g., by making use
of multiple cores. Nevertheless, in special cases it outperforms Numpy. This comparison demonstrates that Numpy is
optimized for general use cases.

.. note::

    As already stressed above, the histogram routines are (except for the rarely used case of arbitrarily spaced
    histograms primarily optimized for performance (with room for improvements). The routines are internally used.
    For other purposes than the applications tested for in ``tttrlib`` other libraries, e.g.
    `Boost histogram <https://github.com/boostorg/histogram>`_ are a better choice.


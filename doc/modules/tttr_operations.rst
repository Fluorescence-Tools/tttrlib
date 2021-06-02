
=================
Common operations
=================
Creating fluorescence decay histograms

plots/tttr_microtime_histogram.py

    Fluorescence decay histograms with different coarsening factors


Compute mean fluorescence lifetimes.
TODO

shift_macro_time

get_used_routing_channels

Slicing / subsets
=================
New TTTR objects can be created by slicing existing objects, if you are
interested a subset of the data.

.. code-block:: python

    data = tttrlib.TTTR('./data/bh/bh_spc132.spc', 'SPC-130')
    data_sliced = data[:10]

A slice of a ``TTTR`` object creates a copy, i.e., the routing channel, the
macro, and the micro times are copied including the header information.

Joining TTTRs
=============
``TTTR`` objects can be joined either by the append method or by using the ``+``
operator.

.. code-block:: python

    data = tttrlib.TTTR('./data/bh/bh_spc132.spc', 'SPC-130')
    d2 = data.append(
        other=data,
        shift_macro_time=True,
        macro_time_offset=0
    )
    d3 = data + data
    len(d2) == 2 * len(data)
    len(d3) == len(d2)

If ``shift_macro_time`` is set to True, which is the default, the macro times of the
data that are offset by the last macro time record in the first set in addition to
the value specified by ``macro_time_offset``. The parameter ``macro_time_offset``
is set to zero by default.

By appending TTTR objects to each other data that is splitted into multiple files
can be joined into a single TTTR object as follows


.. code-block:: python

    import os
    files = glob.glob('./data/bh/bh_spc132_smDNA/*.spc')
    sorted(glob.glob('*.spc'), key=os.path.getmtime)
    data = tttrlib.TTTR(files[0], 'SPC-130')
    for d in files[1:]:
        data.append(tttrlib.TTTR(d, 'SPC-130'))


.. note::
    In practice, take care that the files are ordered. The code above stiches the
    objects in the order as returned by the ``glob`` module. The glob module finds
    all the pathnames matching a specified pattern according to the rules used by
    the Unix shell, although results are returned in arbitrary order. Hence, we
    sort the data by creating time first. In case you need another ordering, e.g.
    lexical ordering adapt the code.


Selections
==========
A defining feature of TTTR data is that subsets can be selected and defined for
more detailed analysis. This is for instance exploited in single-molecule experimetns
There are different methods to access subsets of a TTTR object that are described
in this section.

Using selections
----------------
There is a set of functions and methods to select subsets of TTTR objects.
Beyond the the array processing capabilities either provided by the high-level
programming language or an library like `NumPy <http://www.numpy.org/>`_, ``tttrlib``
offers a set of functions and methods to select a subset of the data contained
in a TTTR file. There are two options to get selection for a subset of the data

    1. By *ranges*
    2. By *selection*

*Ranges* are lists of tuples marking the beginning and the end of a range.
*Selections* are list of integers, where the integers refer to the indices
of the event stream that was selected.


For instance, for the sequence of time events displayed in the following table

+--------+---+---+---+---+---+---+---+---+---+---+
|index   |0  |1  |2  |3  |4  |5  |6  |7  |8  |9  |
+--------+---+---+---+---+---+---+---+---+---+---+
|time    |1  |12 |13 |14 |15 |18 |20 |23 |30 |32 |
+--------+---+---+---+---+---+---+---+---+---+---+

the selection (1, 3, 5, 7) yields::

    12, 14, 18, 23

and the ranges (0, 2) and (7, 9) yield::

    (1, 12, 13), (23, 30, 32)

Depending on the specific application either ranges or selections are more useful.
For instance, single molecule bursts are usually defined by *ranges*, while detection
channels are usually selected by *selections*.

Channel selections
------------------
A very typical use case in TCSPC experiments (either in fluorescence lifetime
microscopy (FLIM) or multiparameteric fluorescence detection (MFD)) is to select
a subset of the registered events based on the detection channel. The experimental
example data provided by the file ``./examples/bh/bh_spc132.spc`` four detectors
were used to register the fluorescence signal with two polarizations in a 'green'
and 'red' spectral range. In the example file the detector numbers for the green
fluorescence were (0, 8) and (1, 9) for the red detection window.

The method 'get_selection_by_channel' provides an array that contains the indices
of the events when a the channel equals the channel number of the provided
arguments. To obtain the indices where the channel number. In the example below
the indices of the green (channel = 0 or channel = 8) and the indeces of the red
(channel = 1 or channel = 9) are saved in the variables ``green_indices``  and
``red_indices``, respectively.

.. code-block:: python

    import numpy as np
    import tttrlib

    data = tttrlib.TTTR('./examples/bh/bh_spc132.spc', 'SPC-130')

    green_indices = data.get_selection_by_channel([0, 8])
    red_indices = data.get_selection_by_channel([1, 9])

This examples needs to be adapted to the channel assignment dependent on the actual
experimental setup.

Selections can be made by channel with such a selection a new `TTTR` object can
be created.

.. code-block:: python

        data = tttrlib.TTTR('./data/bh/bh_spc132.spc', 'SPC-130')
        ch1_indeces = data.get_selection_by_channel([8])
        data_ch1 = tttrlib.TTTR(data, ch1_indeces)
        # alternatively
        data_ch1 = data[ch1_indeces]

The `TTTR` object can be operated on normally.

Count rate selections
---------------------
Another very common selection is based on the count rate. The count rate is
determined by the number of detected events within a given time window. The
selection by the method ``get_selection_by_count_rate`` returns all indices where
less photons were detected within a specified time window. The time window is
given by the number of macro time steps.

.. code-block:: python

    import numpy as np
    import tttrlib
    data = tttrlib.TTTR('./examples/bh/bh_spc132.spc', 'SPC-130')
    cr_selection = data.get_time_window_ranges(1, 30)

In the example shown above, the time window is 1200000 and 30 is the maximum
number of photons within that is permitted in a time window.

Such count rate selections are for instance used to detect bursts in single molecule
experiments or to generate filters for advanced FCS analysis :cite:`laurence2004`
(see also :ref:`Correlation:Count rate filer` and :ref:`Single Molecule:Burst selection`).

TTTR ranges
===========

STOP
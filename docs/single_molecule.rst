Single Molecule
===============

Histogram traces
++++++++++++++++

The function ``tttrlib.histogram_trace`` can be applied to a event time trace to
calculate the time-dependent counts for a given time window size. In
single-molecule FRET experiments such traces are particularly useful to illustrate
single-molecule bursts.

.. plot:: plots/single_molecule_mcs.py
   :include-source:

Above, for a donor and acceptor detection channel a histogram trace for is shown
in green and red.


Burst selection
+++++++++++++++

The function ``tttrlib.ranges_by_time_window`` can be used to define ranges in
a photon stream based on time windows and a minimum number of photons within
the time windows. The function has two main parameters that determine the
selection of the ranges besides the stream of time events provided by the
parameter ``time``:

    1. The minimum time window size ``tw_min``
    2. The minimum number of photons per time window ``n_ph_min``


Additional parameters to discriminate bursts are:

    3. The maximum allowed time window ``tw_max``
    4. The maximum number of photons per time windows ``n_ph_max``

The the parameters of the C function and the function header are shown below
.. note::

    void ranges_by_time_window(
            int **ranges, int *n_range,
            unsigned long long *time, int n_time,
            int tw_min, int tw_max,
            int n_ph_min, int n_ph_max
    )


For a given TTTR object the functionality is provided by the TTTR object's
method ``get_ranges_by_time_window``. A typical use case of this function is
to select single molecule events confocal single-molecule FRET experiments as
shown below.

.. plot:: plots/single_molecule_burst_selection.py
   :include-source:

Above, for a donor and acceptor detection channel a histogram trace for is shown
in green and red. In blue the range based selection is shown.



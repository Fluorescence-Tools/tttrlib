************
TTTR objects
************

Introduction
============
The library tttrlib facilitates the work with files containing time-tagged time
resolved data by providing a vendor independent C++ application programming interface
(API) for TTTR files that is wrapped by `SWIG <http://swig.org/>`_
(Simplified Wrapper and Interface Generator) for common scripting languages as
Python as target languages and non-scripting languages such as C# and Java including
Octave, Scilab and R. This way, the information provided by the TTTR files can be
conveniently accessed without the need of converting the files to alternative
vendor independent exchange file formats as `Photon-HDF <http://photon-hdf5.github.io/>`_.

Common in all TTTR file formats is that the time information is stored as a stream
of events. Every event contains information on the

.. highlights::

    1. Number of clock cycles
    2. Micro time
    3. Channel number
    4. Event type

The number of clock cycles since last detection event can be used to compute
the macro time. The macro time is the time since the start of the experiment.
In TCSPC experiments the macro time is typically the number of excitation sync pulses
of the laser exciting the sample. In continuous wave FCS experiments the macro time
is often the number of clock cycles of an external clock. In time-correlated
single photon counting (TCSPC) experiments, the micro time is usually the time when
a photon was detected since the last excitation laser pulse (macro time). The channel number
encodes the detector that detected the event (the photon). The event type is a special
identifier that informs whether the registered event is a photon or an other event (like
a line marker in confocal laser scanning microscopy). To save storage space and band-width
most TTTR formats do not provide the macro time directly. Typically, instead of directly
providing the macro time, the number of events since the last detected event are counted.


Create TTTR objects
===================
Create TTTR
-----------

Empty objects
    TTTR();

.. code-block:: python

    import tttrlib
    tttr = tttrlib.TTTR()


Attributes are read only events added to TTTR objects via
Append data

.. code-block:: python

    data2 = tttrlib.TTTR(
        macro_times,
        micro_times,
        routing_channels,
        event_types
    )

pass

.. code-block:: python

    data2 = tttrlib.TTTR(
        macro_times,
        micro_times,
        routing_channels,
        event_types
    )


TTTR-Objects
============

General features
----------------

The library tttrlib facilitates the work with files containing time-tagged time
resolved photon streams by providing a vendor independent C++ application
programming interface (API) for TTTR files that is wrapped by `SWIG <http://swig.org/>`_
(Simplified Wrapper and Interface Generator) for common scripting languages as
Python as target languages and non-scripting languages such as C# and Java including
Octave, Scilab and R. This way, the information provided by the TTTR files can be
conveniently accessed without the need of converting the files to alternative
vendor independent exchange file formats as `Photon-HDF <http://photon-hdf5.github.io/>`_.

Common in all TTTR file formats is that the photon data is stored as a stream
of events. Every event contains information on the

.. highlights::

    1. Macro time
    2. Micro time
    3. Channel number
    4. Event type

The macro time counts the number of clock cycles since the start of the
experiment. In TCSPC experiments the macro time is typically the number of
excitation sync pulses of the laser exciting the sample. In continuous wave FCS
experiments the macro time is often the number of clock cycles of an external
clock. In time-correlated single photon counting (TCSPC) experiments, the micro
time is usually the time when a photon was detected since the last excitation
laser pulse (macro time). The channel number encodes the detector that detected
the event (the photon). The event type is a special identifier that informs whether
the registered event is a photon or an other event (like a line marker in confocal
laser scanning microscopy).To save storage space and band-width most TTTR formats
do not provide the macro time directly. Typically, instead of directly providing
the macro time, the number of events since the last detected event are counted.

For convenience, tttrlib provides a unified way of accessing the TTTR data
irrespectively of the vendor data format via the class :class:`TTTR`. In memory
the TTTR data are handled by tttrlib in :class:`TTTR` objects as follows:

    1. Macro time - 64 bit unsigned integer
    2. Micro time - 32 bit unsigned integer
    3. Channel number - 16 bit integer
    4. Event type - 16 bit integer
    5. TTTR Header data

TTTR files are handled by creating :class:`TTTR` objects that contain the TTTR
data of the files. The data contained in the files can be accessed by the methods
provided by the TTTR class. Opening and and accessing the information contained in
TTTR files is demonstrated by the Python code examples shown below. A TTTR object
provides four getter methods to access the data contained in the associated file.

.. code-block:: python

    import tttrlib
    data = tttrlib.TTTR('./examples/BH/BH_SPC132.spc', 'SPC-132')
    macro_time = data.get_macro_time()
    micro_time = data.get_micro_time()
    channel = data.get_channel()
    event_type = data.get_event_type()

In Python, the getter methods return `NumPy <http://www.numpy.org/>`_ arrays.


Some manufactures inject special markers that relate to ""events"" into the TTTR
stream. Some manufacturers use the same routing channel numbers for markers and
for photon detection channels. To distinguish photons from markers, ``TTTR``
objects present for every event an additional event type specifier. The event
types stored in TTTR objects help to distinguish such events from normal photon
events. Currently the following events are considered.


.. _event-types:
.. table:: Table of event type identifiers
    :widths: auto

    +--------------------------+--------+----------------+
    | Event type               | Event type number       |
    +==========================+========+================+
    |Photon event              |0                        |
    +--------------------------+-------------------------+
    |Special event             |1                        |
    +--------------------------+-------------------------+

Overflow events that are used in some file formats are implicitly considered
when reading the files. The precise type of a special event depends often on
channel number associated to the event. A use case for the decoding of imaging
data is presented in the :ref:`Imaging:Confocal laser scanning` section.

.. note::
    The units of the macro time and the micro time *depend* on the specific
    instrument settings.

In some TTTR containers certain record types contain no photon information but
count for instance the number of overflows since the last TTTR record, the total
number macro time, micro time, routing channel numbers is usually smaller than
the number of TTTR records in a file. This can be inspected on the command line
for a ``tttrlib`` build debug mode :ref:`Installation:Compliation` when
opening the BH-SPC132 file provided in the test folder.

.. code-block:: none

    READING TTTR FILE
    -- Filename: ./data/BH/BH_SPC132.spc
    -- Container type: 2
    -- TTTR record type: 7
    -- Number of records: 299999
    -- Allocating memory for 299999 TTTR records.
    -- Resulting number of TTTR entries: 152312
    -- Used routing channels: 9, 8, 0, 1,

In the example file contains 299999 entries whereas overall only 152312 TTTR
entries are stored. The example file corresponds to a single-molecule measurement
where a large fraction of the entries are overflow records.

Create TTTR objects
-------------------

Opening files
+++++++++++++

In Python, first, the tttrlib module needs to be imported. Next, a TTTR object
needs to be created. When creating a new TTTR object, the file name and the file
type can be passed to the object's constructor. If a TTTR object is created this
way, by default, the data contained in the TTTR file is read into the TTTR object.
The TTTR file type is either specified by a number or by passing a string to the
TTTR object's constructor.

.. _supported-file-types:
.. table:: Table of supported file types and corresponding identifiers
    :widths: auto

    +--------------------------+--------+----------------+
    | File type                | Number | Identifier     |
    +==========================+========+================+
    |PicoQuant, PTU            |0       |'PTU'           |
    +--------------------------+--------+----------------+
    |PicoQuant, HT3            |1       |'HT3'           |
    +--------------------------+--------+----------------+
    |Becker&Hickl, SPC130      |2       |'SPC-130'       |
    +--------------------------+--------+----------------+
    |Becker&Hickl, SPC630-256  |3       |'SPC-630-256'   |
    +--------------------------+--------+----------------+
    |Becker&Hickl, SPC630-4096 |4       |'SPC-630-4096'  |
    +--------------------------+--------+----------------+
    |Photon-HDF5               |5       |'PHOTON-HDF5'   |
    +--------------------------+--------+----------------+

The two different approaches of initializing TTTR objects. A TTTR object that
contains the data in a TTTR file can be initialized by the filename and the
data type as specified in above (see :ref:`supported-file-types`). Both
Alternatively, TTTR objects are initialized by the filename and the file type
identifier as displayed in the table above (see :ref:`supported-file-types`).
Both approaches are equivalent and demonstrated for the Becker&Hickl SPC-130 and
the PicoQuant PTU file supplied in the example folder in the Python code below.


.. code-block:: python

    import tttrlib
    ptu = tttrlib.TTTR('./test/data/PQ/PTU/PQ_PTU_HH_T3.ptu', 0)
    ptu = tttrlib.TTTR('./test/data/PQ/PTU/PQ_PTU_HH_T3.ptu', 'PTU')

    spc132 = tttrlib.TTTR('./test/data/BH/BH_SPC132.spc', 2)
    spc132 = tttrlib.TTTR('./test/data/BH/BH_SPC132.spc', 'SPC-130')

Beyond opening files and processing the content contained in a TTTR file TTTR
objects can be created that contain initially no data. Moreover, TTTR objects can
be created based on existing files and selection.

TTTR-Header
===========

Accessing header data
---------------------

Most TTTR container contain meta-data that can be accessed through ``tttrlib``.
For that, a ``TTTR`` object provides a header attribute. The header attribute is
of the type :class:`.Header`.

.. code-block:: python

    import tttrlib
    data = tttrlib.TTTR('./test/data/BH/BH_SPC132.spc', 'SPC-130')
    # the header can be accesses by the method get_header or as an property
    header = data.get_header()

The most important attributes of the header are the :py:attr:`micro_time_resolution`
and :py:attr:`macro_time_resolution`. Becker&Hickl Spc132 files files contain
only a limited amount of information in the first record (32 bit).

PicoQuant PTU and HT3 files provide more extensive information in their header
that can be accessed via the :py:attr:`data` attribute of a header object. The
data attribute of a header object is a map that can be accesses like a normal
Python dictionary.

.. code-block:: python

    import tttrlib
    data = tttrlib.TTTR('./test/data/PQ/PTU/PQ_PTU_HH_T3.PTU', 'PTU')
    # the header can be accesses by the method get_header or as an property
    header = data.get_header()
    header_data = header.data
    print(header.data.keys())

The last statement prints the keys of the map.


Time calibration data
---------------------

Essential for the analysis of TTTR data is the time calibration (time resolution)
of the macro and the micro times in addition to the number of possible micro time
channels. In many functions the micro and macro time calibration are transparently
handeled, meaning there is no need to worry. The ``macro_time`` and the
``micro_time`` TTTR attributes correspond the to raw uncalibrated data.

The macro and micro time resolution is accessed as follows.

.. code-block:: python

    import tttrlib
    data = tttrlib.TTTR('./data/PQ/PTU/PQ_PTU_HH_T3.PTU', 'PTU')
    macro_time_resolution = data.header.macro_time_resolution
    micro_time_resolution = data.header.micro_time_resolution

The number of micro time channels can be accessed as displayed below.

.. code-block:: python

    import tttrlib
    data = tttrlib.TTTR('./data/PQ/PTU/PQ_PTU_HH_T3.PTU', 'PTU')
    # the header can be accesses by the method get_header or as an property
    header = data.get_header()
    macro_time_resolution = data.header.macro_time_resolution
    # macro_time_resolution = 12.5 ns
    micro_time_resolution = data.header.micro_time_resolution
    # micro_time_resolution = 4 ps
    data.header.number_of_micro_time_channels
    # will return 8129
    data.get_number_of_micro_time_channels()
    # will return 3125


.. note::
    The effective number of micro time channels, i.e., the number of micro time
    channels can be smaller than the actual number of micro time channels. For instance
    at a micro time channel resolution of 4 ps and macro time resolution of 12.5 ns
    effectively only 3125 micro time channels will be filled with photons.


Selections
----------

Using selections
++++++++++++++++

Based on an existing TTTR object and a selection a new TTTR object can be created.
That only contains the selected elements. Beyond the the array processing
capabilities either provided by the high-level programming language or an library
like `NumPy <http://www.numpy.org/>`_, ``tttrlib`` offers a set of functions and
methods to select a subset of the data contained in a TTTR file. There are two
options to get selection for a subset of the data

    1. By *ranges*
    2. By *selection*

*Ranges* are lists of subsequential tuples marking the beginning and the end of
a range. *Selections* are list of integers, where the integers refer to the indices
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
++++++++++++++++++

A very typical use case in TCSPC experiments (either in fluorescence lifetime
microscopy (FLIM) or multiparameteric fluorescence detection (MFD)) is to select
a subset of the registered events based on the detection channel. The experimental
example data provided by the file ``./examples/BH/BH_SPC132.spc`` four detectors
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

    data = tttrlib.TTTR('./examples/BH/BH_SPC132.spc', 'SPC-130')

    green_indices = data.get_selection_by_channel(np.array([0, 8]))
    red_indices = data.get_selection_by_channel(np.array([1, 9]))

This examples needs to be adapted to the channel assignment dependent on the actual
experimental setup.

Selections can be made by channel with such a selection a new `TTTR` object can
be created.

.. code-block:: python

        data = tttrlib.TTTR('./data/BH/BH_SPC132.spc', 'SPC-130')
        ch1_indeces = data.get_selection_by_channel(np.array([8]))
        data_ch1 = tttrlib.TTTR(data, ch1_indeces)

The `TTTR` object can be operated on normally.

Count rate selections
+++++++++++++++++++++

Another very common selection is based on the count rate. The count rate is
determined by the number of detected events within a given time window. The
selection by the method ``get_selection_by_count_rate`` returns all indices where
less photons were detected within a specified time window. The time window is
given by the number of macro time steps.

.. code-block:: python

    import numpy as np
    import tttrlib
    data = tttrlib.TTTR('./examples/BH/BH_SPC132.spc', 'SPC-130')
    cr_selection = data.get_ranges_by_count_rate(1, 30)

In the example shown above, the time window is 1200000 and 30 is the maximum
number of photons within that is permitted in a time window.




"""
=======================
Working with TTTR files
=======================

To open TTTR files import the ``tttrlib`` library.

"""
import pathlib
import tttrlib

data = tttrlib.TTTR('../../tttr-data/bh/bh_spc132.spc', 'SPC-130')

"""
TTTR attributes
===============
For convenience, tttrlib provides a unified way of accessing the TTTR data
independently of the vendor data format via the class :class:`TTTR`. In memory
the TTTR data are handled by tttrlib in :class:`TTTR` objects as follows:

    1. Macro time - 64 bit unsigned integer
    2. Micro time - 16 bit unsigned integer
    3. Channel number - 8 bit signed integer
    4. Event type - 8 bit signed integer
    5. TTTR Header data - dedicated class

TTTR files are handled by creating :class:`TTTR` objects that contain the TTTR
data of the files. The data contained in the files can be accessed by the methods
provided by the TTTR class. Opening and and accessing the information contained in
TTTR files is demonstrated by the Python code examples shown below. A TTTR object
provides getter methods to access the data contained in the associated file.

"""

macro_time = data.get_macro_time()
micro_time = data.get_micro_time()
channel = data.get_routing_channel()
event_type = data.get_event_type()
header = data.get_header()

"""
In Python, the getter methods for the macro time, micro time, the channel, and the
event type return `NumPy <http://www.numpy.org/>`_ arrays. The returned header is
of the type :class:`TTTRHeader`.

Alternatively, the data can be accesses by corresponding the (write-only) attributes.
"""
macro_time = data.macro_times
micro_time = data.micro_times
channel = data.routing_channels
event_type = data.event_types
header = data.header

"""
For a more detailed description on the meaning of these attributes see :ref:`_tttr-attributes`.
A detailed description on the meta data in :class:`TTTRHeader`is given in :ref:`_TTTR-Header`.

.. _tttr-attributes:
.. table:: Description of the most important TTTR object attributes
    :widths: auto

    +----------------+----------------------------------------------------------------------------------+
    | Name           | Description                                                                      |
    +================+========+=========================================================================+
    |Macro time      | Number of macro time clock cycles since the start of the recording               |
    +----------------+----------------------------------------------------------------------------------+
    |Micro time      | Number of micro time channels since last macro time (unused in PicoQuant T2 mode)|
    +----------------+----------------------------------------------------------------------------------+
    |Channel number  | Channel number (an identifier) of the event                                      |
    +----------------+----------------------------------------------------------------------------------+
    |Event type      | Type of the event                                                                |
    +----------------+----------------------------------------------------------------------------------+
    |Header          | Contains meta data of TTTR objects                                               |
    +----------------+----------------------------------------------------------------------------------+

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
for a ``tttrlib`` build *debug* mode :ref:`Installation:Compliation` when
opening the BH-SPC132 file provided in the test folder.

.. code-block:: none

    READING TTTR FILE
    -- Filename: ./data/bh/bh_spc132.spc
    -- Container type: 2
    -- TTTR record type: 7
    -- Number of records: 299999
    -- Allocating memory for 299999 TTTR records.
    -- Resulting number of TTTR entries: 152312
    -- Used routing channels: 9, 8, 0, 1,

In the example file contains 299999 entries whereas overall only 152312 TTTR
entries are stored. The example file corresponds to a single-molecule measurement
where a large fraction of the entries are overflow records.

"""

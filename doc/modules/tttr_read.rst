Reading TTTR-files
------------------
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

    spc132 = tttrlib.TTTR('./test/data/bh/bh_spc132.spc', 2)
    spc132 = tttrlib.TTTR('./test/data/bh/bh_spc132.spc', 'SPC-130')

Beyond opening files and processing the content contained in a TTTR file TTTR
objects can be created that contain initially no data. Moreover, TTTR objects can
be created based on existing files and selection.

IF the container type is not specified `tttrlib` will try to infer the container
type based on the file extension.

.. code-block:: python

    import tttrlib
    ptu = tttrlib.TTTR('./test/data/PQ/PTU/PQ_PTU_HH_T3.ptu')

This only works for PTU, HT3, and for HDF files. For SPC files the TTTR record
types need to be specified. Below is a table with different record types and
supported containers.

.. _supported-record-types:
.. table:: Table of record types and supported container
    :widths: auto

    +---------------------------+--------+---------------------+
    | Record type               | Number | Supported container |
    +===========================+========+=====================+
    |PQ_RECORD_TYPE_HHT2v2      |1       |'PTU', HT2           |
    +---------------------------+--------+---------------------+
    |PQ_RECORD_TYPE_HHT2v1      |2       |'PTU', HT2           |
    +---------------------------+--------+---------------------+
    |PQ_RECORD_TYPE_HHT3v1      |3       |'PTU', HT3           |
    +---------------------------+--------+---------------------+
    |PQ_RECORD_TYPE_HHT3v2      |4       |'PTU', HT3           |
    +---------------------------+--------+---------------------+
    |PQ_RECORD_TYPE_PHT3        |5       |'PTU', HT3           |
    +---------------------------+--------+---------------------+
    |PQ_RECORD_TYPE_PHT2        |6       |'PTU', HT2           |
    +---------------------------+--------+---------------------+
    |BH_RECORD_TYPE_SPC130      |7       |'SPC'                |
    +---------------------------+--------+---------------------+
    |BH_RECORD_TYPE_SPC600_256  |8       |'SPC'                |
    +---------------------------+--------+---------------------+
    |BH_RECORD_TYPE_SPC600_4096 |9       |'SPC'                |
    +---------------------------+--------+---------------------+

For PicoQuant hardware/software the use of PTU files is heavily encouraged. As PTU
container / files offer the broadest support of different record types and meta
data.

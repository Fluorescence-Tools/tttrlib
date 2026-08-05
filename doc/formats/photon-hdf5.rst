.. _fmt_photon_hdf5:

Photon-HDF5 (``.h5, .hdf5``)
=============================

:Container id: ``5``
:Extension: ``.h5, .hdf5``

What it is
----------

The open, vendor-neutral photon-data format
(`photon-hdf5.org <https://photon-hdf5.org>`_). Unlike every other container
here it stores *decoded arrays* rather than an encoded record stream, so there
is no record type to speak of and any encoding is acceptable on write.

Layout
------

HDF5 groups: ``/photon_data/timestamps``, ``/photon_data/detectors``,
``/photon_data/nanotimes``, with ``/setup`` and ``/identity`` describing the
measurement.

Record encodings
----------------

None -- the arrays are already decoded.

Notes
-----

This is the format to write when the goal is for something other
than tttrlib to read the result.

Reference data
--------------

`Sample files <https://gitlab.peulen.xyz/skf/tttr-data/-/tree/main/hdf>`_ in the ``tttr-data`` repository.

.. seealso::

   :doc:`../file-formats` for the conversion matrix and what survives a
   transcode.

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

HDF5 groups. The photons are ``/photon_data/timestamps``,
``/photon_data/detectors`` and ``/photon_data/nanotimes``; everything else
describes them.

Record encodings
----------------

None -- the arrays are already decoded.

Metadata
--------

Photon-HDF5 carries a great deal that cannot be reconstructed from the photons,
and tttrlib reads all of it into header tags rather than a fixed list of groups:

* ``/setup`` -- channel counts, excitation and detection wavelengths, laser
  repetition rates, and the nested ``/setup/detectors``
* ``/identity`` and ``/provenance`` -- software, versions, original filename,
  creation and modification times
* ``/sample`` -- sample and buffer names, dye names
* ``/photon_data/measurement_specs`` and its nested ``detectors_specs`` --
  measurement type, ALEX periods, which detector is which spectral,
  polarization or split channel
* ``/photon_data/timestamps_specs`` and ``nanotimes_specs`` -- the resolutions
* root-level ``description`` and ``acquisition_duration``
* vendor extensions such as ``/user/picoquant``

Each value becomes a ``<group>.<name>`` tag, addressed by its *immediate*
parent group, so ``/photon_data/timestamps_specs/timestamps_unit`` is
``timestamps_specs.timestamps_unit``. Root-level values are addressed by name
alone. The four values tttrlib itself understands -- the two resolutions and
the micro time channel count -- are additionally promoted to their canonical
tag names.

Metadata is told apart from measurements by size: a one-dimensional dataset
longer than 1024 elements is a photon array, not a description of one. No field
in the specification comes close to that, and every photon array is far beyond
it.

Notes
-----

This is the format to write when the goal is for something other
than tttrlib to read the result.

On write, values that were read from a source Photon-HDF5 file are written back
out, so a round trip does not quietly re-describe the instrument as a
single-spot default.

Reference data
--------------

`Sample files <https://gitlab.peulen.xyz/skf/tttr-data/-/tree/main/hdf>`_ in the ``tttr-data`` repository.

.. seealso::

   :doc:`../file-formats` for the conversion matrix and what survives a
   transcode.

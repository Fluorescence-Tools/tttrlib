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

What survives a write
---------------------

The writer carries metadata across rather than regenerating it: the ``/setup``
description of the instrument including its array fields, and the ``/sample``,
``/provenance`` and ``/identity`` groups. Those are the parts a reader cannot
reconstruct from the photons, and dropping them was the point of writing a
Photon-HDF5 file rather than an SPC.

Only fields the specification defines are written as real fields. Every field
carries its official description into the file as a ``TITLE`` attribute, and a
validator compares that text against the specification — so a field tttrlib
invented a description for would make the whole file invalid.

Everything else is kept rather than dropped. It goes into
``/user/tttrlib/metadata_json`` as a single JSON object keyed by the same
``group.field`` address the reader uses — ``measurement_specs.measurement_type``,
the ``detectors_specs`` channel mapping, a vendor group such as
``picoquant.hardware_name``, and the source file's own name. ``/user`` is the
one place the format sets aside for application data: a validator skips the
whole subtree, so preserving this costs no conformance and none of it pretends
to be a standard field.

The two halves partition. A value written as a real field is not repeated in
the JSON, because a value stated twice is one that can disagree with itself —
which the detector counts would, since those are derived from the photons
actually written rather than copied from the source.

One field does lose detail: ``/setup/excitation_alternated`` is written as a
single flag, so a source that alternated two excitation sources independently
comes back with one value rather than two.

``/setup/detectors`` is written from the data rather than from the header: every
detector ID that appears in ``/photon_data/detectors``, with the number of
photons on it. Mandatory since v0.5, and the counts have to agree with what was
actually written — a header carried over from a source file describes that
file's detectors, not this one's.

Files written by tttrlib pass ``phconvert.hdf5.assert_valid_photon_hdf5``, the
reference implementation's validator, which checks rather more than the prose
suggests: the mandatory fields, the exact description text on every node,
scalar-versus-array shapes, and that the detector counts match the photons. The
test suite runs it.

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

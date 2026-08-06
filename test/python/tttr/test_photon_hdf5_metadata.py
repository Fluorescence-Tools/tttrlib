"""Photon-HDF5 metadata reaches the header.

A Photon-HDF5 file carries a great deal that cannot be reconstructed from the
photons -- what the sample was, what the excitation did, which detector is which
channel. The reader used to take four groups (/setup, /identity and the two
*_specs) and drop the rest on the floor.
"""
import json
import os

import numpy as np
import pytest
import tttrlib
from test_settings import DATA_ROOT, DATA_AVAILABLE  # type: ignore

pytestmark = pytest.mark.skipif(not DATA_AVAILABLE, reason="test data not available")


def _sample():
    for dirpath, _, files in os.walk(DATA_ROOT):
        for fn in sorted(files):
            if fn.lower().endswith((".h5", ".hdf5")):
                return os.path.join(dirpath, fn)
    return None


@pytest.fixture(scope="module")
def data():
    p = _sample()
    if p is None:
        pytest.skip("no Photon-HDF5 file in the test data")
    return tttrlib.TTTR(p, "PHOTON-HDF5")


@pytest.fixture(scope="module")
def tags(data):
    return {t["name"]: t for t in json.loads(data.header.json)["tags"]}


def test_reads_the_photons(data):
    assert data.n_valid_events > 0


def test_every_event_is_a_photon(data):
    """The reader must write the event-type column, not leave it to chance.

    Photon-HDF5 has no marker stream -- /photon_data is timestamps plus
    detectors, and the spec calls them photons -- so reading every row as a
    photon is the only thing a reader can do. (The spec does allow markers to be
    stored as rows with their own detector ID, but provides no flag saying which
    ID that is, so the distinction is not recoverable. See read_hdf_file.)

    The bug this pins was not the choice but the omission: the event store
    allocates with a default-init allocator, so a column the reader never writes
    holds whatever was in that memory. It went unnoticed because the one
    published sample is 22 MB and gets fresh zeroed pages from the OS; a smaller
    file, or a reused block, would have produced arbitrary bytes. Every consumer
    downstream filters markers out, so the failure mode was photons silently
    disappearing from an analysis.
    """
    import numpy as np
    assert set(np.unique(np.asarray(data.event_types)).tolist()) == {0}


def test_the_written_file_validates_against_phconvert(data, tmp_path):
    """Conformance judged by the reference implementation, not by our own reading.

    phconvert is the Photon-HDF5 reference tool, and its validator is stricter
    than the prose suggests: besides the mandatory fields it checks that every
    node's TITLE attribute matches the official description for that path
    exactly, that scalars are scalars and arrays are arrays, and that
    /setup/detectors/counts agrees with what is actually in
    /photon_data/detectors. A file can be perfectly readable and still not be a
    Photon-HDF5 file, which is why this asks something other than ourselves.
    """
    ph5 = pytest.importorskip("phconvert.hdf5", reason="phconvert not installed")
    tables = pytest.importorskip("tables")

    out = str(tmp_path / "conformance.hdf5")
    assert data.write(out, "PHOTON-HDF5")

    handle = tables.open_file(out)
    try:
        # warnings=False: the optional fields it would grumble about
        # (excitation_wavelengths on a file that never had them, author,
        # measurement_specs) are optional, and their absence is not a defect.
        ph5.assert_valid_photon_hdf5(handle, warnings=False)
    finally:
        handle.close()


def test_the_detectors_group_declares_every_id(data, tmp_path):
    """Mandatory since v0.5, and the piece the writer used to omit entirely.

    It is also the only place a Photon-HDF5 file can admit that an ID belongs to
    something other than a photon detector -- a monitor channel, or a marker the
    acquisition hardware saved.
    """
    tables = pytest.importorskip("tables")
    import numpy as np

    out = str(tmp_path / "detectors.hdf5")
    assert data.write(out, "PHOTON-HDF5")

    handle = tables.open_file(out)
    try:
        ids = handle.root.setup.detectors.id.read()
        counts = handle.root.setup.detectors.counts.read()
        written = handle.root.photon_data.detectors.read()
    finally:
        handle.close()

    unique, expected = np.unique(written, return_counts=True)
    assert ids.tolist() == unique.tolist()
    # Counts derived from the data, not carried over from a source header: a
    # header describes the file it came from, and the validator checks these
    # against the photons actually written.
    assert counts.tolist() == expected.tolist()


def test_the_metadata_survives_a_round_trip(data, tmp_path):
    """The sample, the provenance and the instrument description come back.

    The writer used to emit only the mandatory fields, so a round trip through
    Photon-HDF5 dropped the sample name, the buffer, the original filename and
    the excitation wavelengths -- everything a reader cannot reconstruct from
    the photons, which is the whole reason the format carries it.
    """
    import tttrlib as _t

    out = str(tmp_path / "roundtrip.hdf5")
    assert data.write(out, "PHOTON-HDF5")
    back = _t.TTTR(out, "PHOTON-HDF5")

    def value(tttr, name):
        entries = [t for t in json.loads(tttr.header.json)["tags"] if t["name"] == name]
        return [t["value"] for t in sorted(entries, key=lambda t: t.get("idx", 0))]

    for name in ("sample.sample_name", "sample.buffer_name", "provenance.filename",
                 "setup.excitation_wavelengths", "setup.detection_wavelengths"):
        source = value(data, name)
        if not source:
            continue
        # Arrays reach the header as one tag per element, indexed. They have to
        # be gathered back into one dataset on the way out -- written as they
        # come, a two-element list becomes the same dataset name twice.
        assert value(back, name) == source, name


def test_carries_more_than_setup_and_identity(tags):
    """/sample, /provenance and the measurement specs are the point."""
    groups = {n.split(".")[0] for n in tags if "." in n}
    for expected in ("setup", "identity", "sample", "provenance", "measurement_specs"):
        assert expected in groups, f"{expected} missing; got {sorted(groups)}"


def test_nested_groups_are_reached(tags):
    """detectors_specs sits under /photon_data/measurement_specs, two deep."""
    assert any(n.startswith("detectors_specs.") for n in tags), sorted(tags)


def test_root_level_fields_have_no_leading_dot(tags):
    """Values outside any group are addressed by name alone."""
    assert not any(n.startswith(".") for n in tags), \
        [n for n in tags if n.startswith(".")]
    assert "acquisition_duration" in tags or "description" in tags


def test_photon_arrays_are_not_pulled_in_as_metadata(tags, data):
    """The walk tells metadata from measurements by size, so 22M timestamps
    must not arrive one tag at a time."""
    assert len(tags) < 500, f"{len(tags)} tags -- a photon array leaked in"
    for leaked in ("photon_data.timestamps", "photon_data.detectors",
                   "photon_data.nanotimes"):
        assert leaked not in tags


def test_the_resolutions_are_promoted_to_their_canonical_tags(data):
    """timestamps_unit and tcspc_unit are what the rest of tttrlib reads.

    find_tag matches the stored index exactly and defaults to -1, so promoting
    a scalar stored at index 0 needs the index spelled out. Without it every
    Photon-HDF5 file read back with a macro time resolution of -1.
    """
    assert data.header.macro_time_resolution > 0
    assert data.header.micro_time_resolution > 0
    assert data.header.number_of_micro_time_channels > 0

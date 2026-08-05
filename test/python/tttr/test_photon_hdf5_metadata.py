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

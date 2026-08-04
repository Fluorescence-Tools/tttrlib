"""The format table must agree with the code it replaces.

TTTRFormat.h collapses six hand-maintained copies of the same knowledge into one
description per format. That is only an improvement while the single copy still
says what the six said, so this asserts it against the public behaviour of each
of them. A divergence shows up here rather than as a file that quietly stops
being recognised.
"""
import pytest
import tttrlib


def test_names_match_the_container_name_table():
    """registry('file_container') is derived from the format table."""
    formats = tttrlib.registry("file_container")
    assert set(formats) == set(tttrlib.TTTR.get_supported_container_names())


def test_container_ints_are_the_historical_ones():
    """0-999 belong to built-in formats permanently; user code stored these."""
    expected = {
        "PTU": 0, "HT3": 1, "SPC-130": 2, "SPC-600_256": 3, "SPC-600_4096": 4,
        "PHOTON-HDF5": 5, "CZ-RAW": 6, "SM": 7, "PHOTONS": 8, "SPC-QC": 9,
    }
    got = {n: e["container_type"] for n, e in tttrlib.registry("file_container").items()}
    assert got == expected


def test_every_builtin_format_is_marked_stable():
    """`stable` tells a consumer which container ints it may persist."""
    for name, entry in tttrlib.registry("file_container").items():
        assert entry.get("stable", True) is True, name


@pytest.mark.parametrize("name,ext", [
    ("PTU", ".ptu"), ("HT3", ".ht3"), ("SPC-130", ".spc"),
    ("PHOTON-HDF5", ".h5,.hdf5"), ("CZ-RAW", ".raw"), ("SM", ".sm"),
    ("PHOTONS", ".photons"), ("SPC-QC", ".spc"),
])
def test_extensions_are_unchanged(name, ext):
    assert tttrlib.registry("file_container")[name]["extensions"] == ext

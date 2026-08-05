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


# --- the four dispatchers now derived from the same table -------------------

@pytest.mark.parametrize("filename,expected", [
    ("x.ptu", 0), ("x.ht3", 1), ("x.spc", 2), ("x.h5", 5), ("x.hdf5", 5),
    ("x.raw", 6), ("x.sm", 7), ("x.photons", 8),
    ("x.nope", -1), ("no-extension-at-all", -1),
])
def test_extension_to_container_is_unchanged(filename, expected):
    """`.spc` resolves to SPC-130 (2) because it is the lowest id claiming it."""
    assert tttrlib.inferTTTRContainerTypeFromExtension(filename) == expected


@pytest.mark.parametrize("container,expected", [
    (0, "ptu"), (1, "ht3"),
    (2, "spc"), (3, "spc"), (4, "spc"), (9, "spc"),
    (5, "hdf5"),          # listed as ".h5,.hdf5" but written as "hdf5"
    (6, "raw"), (7, "sm"), (8, "photons"),
    (999, ""),
])
def test_canonical_write_extension_is_unchanged(container, expected):
    assert tttrlib.tttrContainerCanonicalExtension(container) == expected


@pytest.mark.parametrize("name,records,default", [
    # PTU accepts every PicoQuant encoding EXCEPT SF-HT3: that compression
    # exists only in HT3 containers, which is the one asymmetry in the rules.
    ("PTU",          [2, 4, 1, 3, 6, 5, 12, 13],     4),
    ("HT3",          [2, 4, 1, 3, 6, 5, 12, 13, 14], 4),
    ("SPC-130",      [7],      7),
    ("SPC-600_256",  [8],      8),
    ("SPC-600_4096", [9],      9),
    ("SPC-QC",       [15, 16], 15),
    ("CZ-RAW",       [10],     10),
    ("SM",           [11],     11),
    # Photon-HDF5 stores decoded arrays, so any encoding is acceptable and none
    # is canonical -- an empty list means "any".
    ("PHOTON-HDF5",  [],      -1),
    ("PHOTONS",      [],      -1),
])
def test_record_type_rules_are_unchanged(name, records, default):
    entry = tttrlib.registry("file_container")[name]
    assert sorted(entry["record_types"]) == sorted(records)
    assert entry["default_record_type"] == default

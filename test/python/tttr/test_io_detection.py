"""File-type detection: what it recognises, and what it must keep refusing.

Detection asks the format table for every format claiming the file's extension,
in ascending container id, and takes the first whose sniffer accepts. If none
does, it asks every format that can identify itself from bytes. The extension is
a hint, not the answer.
"""
import os
import shutil
import tempfile

import pytest
import tttrlib
from test_settings import DATA_ROOT, DATA_AVAILABLE  # type: ignore

pytestmark = pytest.mark.skipif(not DATA_AVAILABLE, reason="test data not available")

PTU = 0
HT3 = 1
SPC130 = 2
PHOTON_HDF5 = 5
CZ_RAW = 6
SM = 7
SPCQC = 9


def _first(suffix):
    for dirpath, _, files in os.walk(DATA_ROOT):
        for fn in sorted(files):
            if fn.lower().endswith(suffix):
                return os.path.join(dirpath, fn)
    return None


@pytest.mark.parametrize("suffix,expected", [
    (".ptu", PTU), (".ht3", HT3), (".hdf5", PHOTON_HDF5),
    (".raw", CZ_RAW), (".sm", SM),
])
def test_formats_are_detected_from_their_own_files(suffix, expected):
    path = _first(suffix)
    if path is None:
        pytest.skip(f"no {suffix} file in the test data")
    assert tttrlib.inferTTTRFileType(path) == expected


def test_carl_zeiss_confocor3_is_recognised_by_its_banner():
    """Regression: every ConfoCor3 file in the data used to be undetected.

    The sniffer read the settings struct from offset 0 and range-checked its
    fields -- but a ConfoCor3 file opens with the ASCII banner "Carl Zeiss
    ConfoCor3 - raw data file - version 3.000 - Channel 1", which the struct
    overlays, so those checks were being applied to characters and rejected
    genuine files.
    """
    path = _first(".raw")
    if path is None:
        pytest.skip("no .raw file in the test data")
    with open(path, "rb") as fh:
        assert fh.read(20) == b"Carl Zeiss ConfoCor3"
    assert tttrlib.isCZConfocor3File(path) is True
    assert tttrlib.inferTTTRFileType(path) == CZ_RAW


@pytest.mark.parametrize("suffix,expected", [
    (".ptu", PTU), (".ht3", HT3), (".hdf5", PHOTON_HDF5), (".raw", CZ_RAW),
])
@pytest.mark.parametrize("newname", ["mystery.dat", "no_extension_at_all"])
def test_contents_win_when_the_name_says_nothing(suffix, expected, newname):
    """A correctly formatted file with an unhelpful name is still identified."""
    src = _first(suffix)
    if src is None:
        pytest.skip(f"no {suffix} file in the test data")
    with tempfile.TemporaryDirectory() as tmp:
        dst = os.path.join(tmp, newname)
        shutil.copy(src, dst)
        assert tttrlib.inferTTTRFileType(dst) == expected


def test_nothing_is_detected_as_something():
    """The fallback must not turn junk into a format."""
    with tempfile.TemporaryDirectory() as tmp:
        junk = os.path.join(tmp, "junk.dat")
        with open(junk, "wb") as fh:
            fh.write(bytes(range(256)) * 16)
        assert tttrlib.inferTTTRFileType(junk) == -1

        empty = os.path.join(tmp, "empty.dat")
        open(empty, "wb").close()
        assert tttrlib.inferTTTRFileType(empty) == -1


def test_spc_resolves_to_spc130_before_spcqc():
    """Ambiguous extension: lowest container id claiming it goes first."""
    assert tttrlib.inferTTTRContainerTypeFromExtension("x.spc") == SPC130
    ids = {n: e["container_type"] for n, e in tttrlib.registry("file_container").items()}
    assert ids["SPC-130"] < ids["SPC-QC"]

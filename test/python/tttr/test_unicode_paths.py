"""A path with non-ASCII characters is a path.

Reading has been Unicode-safe for a long time: `open_file()` converts the
UTF-8 path to UTF-16 and calls `_wfsopen` on Windows, because the narrow
`fopen` there takes the active code page and cannot name a file whose path is
outside it. Several *writers* called `fopen` directly, so on Windows a file
could be read from a directory it could not be written back into -- and the
failure is silent in the sense that it looks like a permission or path problem
rather than an encoding one.

POSIX passes these already: bytes are bytes there. The test exists for the
Windows runners, where it is the only thing that would notice a writer going
around `open_file()` again.
"""
import os

import numpy as np
import pytest

import tttrlib
from test_settings import DATA_AVAILABLE, settings  # type: ignore

pytestmark = pytest.mark.skipif(not DATA_AVAILABLE, reason="test data not available")

# German, Japanese and Cyrillic: none of them fit one Windows code page at
# once, so a path holding all three cannot be represented in the narrow API
# whatever the machine is set to.
UNICODE_DIR = "Meßdaten_日本語_Данные"


def _source():
    named = settings.get("ht3_sf_filename")
    if not named or not os.path.exists(named):
        pytest.skip("no source file to transcode")
    return tttrlib.TTTR(named)


@pytest.fixture
def unicode_dir(tmp_path):
    d = tmp_path / UNICODE_DIR
    try:
        d.mkdir()
    except (OSError, UnicodeError) as e:
        pytest.skip("this filesystem will not hold the name: %s" % e)
    return d


@pytest.mark.parametrize("suffix", [".ptu", ".ht3", ".spc", ".sm", ".pto"])
def test_a_container_writes_and_reads_back_under_a_unicode_path(unicode_dir, suffix):
    """The round trip, per container: each has its own header writer, and it
    was the header writers that called fopen directly."""
    src = _source()
    out = str(unicode_dir / ("Meßung_測定" + suffix))

    src.write(out)
    assert os.path.exists(out), "nothing was written to " + out

    back = tttrlib.TTTR(out)
    assert len(back) == len(src)
    np.testing.assert_array_equal(
        np.asarray(back.macro_times), np.asarray(src.macro_times))


def test_the_written_file_is_not_empty(unicode_dir):
    """A writer that could not open its target used to leave a zero-length
    file behind rather than say so, which reads as a successful write."""
    src = _source()
    out = str(unicode_dir / "Meßung_測定.ptu")
    src.write(out)
    assert os.path.getsize(out) > 0

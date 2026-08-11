# SPDX-License-Identifier: BSD-3-Clause
"""What every entry point does when the file is not there.

This is not a hypothetical. The CI data set is a subset of the published one,
so a path a test names can simply be absent on the runner, and
`TTTRHeader(path)` answered that by segfaulting: open_file() returns nullptr
for a path that is not there, and the readers seek and read it unchecked. A
crash takes the whole pytest session with it, which is how one missing file
hid every other failure in its group.

Nothing here needs test data -- that is the point. A missing file must produce
an error or an empty object, never a crash, whichever the entry point already
promises.
"""
import os
import tempfile

import pytest

import tttrlib


@pytest.fixture
def absent(tmp_path):
    """A path that certainly does not exist, inside a directory that does."""
    return str(tmp_path / "not_here.spc")


@pytest.fixture
def absent_in_absent_dir():
    """A path whose parent directory does not exist either."""
    return os.path.join(tempfile.gettempdir(), "tttrlib-no-such-dir", "not_here.spc")


def test_a_header_from_a_missing_file_is_empty_rather_than_fatal(absent):
    header = tttrlib.TTTRHeader(absent, 2)
    # The container type survives because that is what the caller passed; the
    # rest is whatever a default header holds. What matters is reaching here.
    assert header.tttr_container_type == 2


def test_a_header_from_a_missing_directory_is_survivable(absent_in_absent_dir):
    tttrlib.TTTRHeader(absent_in_absent_dir, 2)


def test_a_header_with_no_container_type_named_is_survivable(absent):
    tttrlib.TTTRHeader(absent)


def test_container_records_reports_nothing_to_read(absent):
    info = tttrlib.container_records(absent, 2)
    assert info.n_records == 0
    assert info.record_type == -1


def test_reading_records_from_a_missing_file_refuses(absent):
    # This one already declined properly, and the message names the file.
    with pytest.raises(ValueError, match="not_here.spc"):
        tttrlib.container_read_records(absent, 0, 10, 2)


def test_opening_a_missing_file_yields_no_events(absent):
    # TTTR reports on stderr and hands back an empty object rather than
    # raising. Pinned as it is: the claim here is that it does not crash.
    assert tttrlib.TTTR(absent).size() == 0


def test_the_whole_chunked_read_survives_a_missing_file(absent):
    """The sequence test_record_streams drives, on a file that is not there.

    container_records says zero records, so the decode loop never runs and the
    header read is reached with nothing decoded -- exactly the path that
    crashed on every runner whose data set lacked the file.
    """
    info = tttrlib.container_records(absent, 2)
    data = tttrlib.TTTR()
    state = tttrlib.TTTRDecodeState()
    # n_records is 0, so the loop body never runs -- as on a runner whose data
    # set lacks the file -- and the header read below is reached with nothing
    # decoded. That is the sequence that segfaulted.
    for first in range(0, int(info.n_records), 4999):
        raw = tttrlib.container_read_records(absent, first, 4999, 2)
        if len(raw):
            tttrlib.decode_records(raw, info.record_type, state, data)
    data.set_header(tttrlib.TTTRHeader(absent, 2))
    assert data.size() == 0

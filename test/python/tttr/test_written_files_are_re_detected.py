"""A file tttrlib wrote must be openable by name.

The gap this closes: every round-trip test in the suite passed the container
type explicitly — `TTTR(path, container)` — which is exactly the path that
works. Nothing checked `TTTR(path)`, the way a user actually opens a file, so a
writer could emit bytes its own detector did not recognise and no test noticed.

`.sm` did. Three separate discrepancies against a real file, each enough on its
own: the version defaulted to 1 where real files carry 2 and the detector
requires 2; every counted string was written a byte longer than its content,
counting a terminating null the format does not have; and the `simple` field
defaulted to empty, so that extra byte was an unprintable 0x00 exactly where
the detector checks for printable characters. `write()` returned True and
`TTTR(path)` returned an empty object with no exception.
"""
import glob

import numpy as np
import pytest

import tttrlib

SM = 7


def _source():
    hits = sorted(glob.glob("tttr-data/**/*.ht3", recursive=True))
    if not hits:
        pytest.skip("no test data")
    return tttrlib.TTTR(hits[0])


class TestSmIsReDetected:

    def test_a_written_sm_opens_by_name(self, tmp_path):
        """Without the container type. This is what a user does."""
        src = _source()
        out = str(tmp_path / "written.sm")
        assert src.write(out, None, SM)

        back = tttrlib.TTTR(out)
        assert len(back) == len(src), "opened by name but came back empty"
        np.testing.assert_array_equal(np.asarray(back.macro_times),
                                      np.asarray(src.macro_times))

    def test_a_real_sm_still_round_trips_by_name(self, tmp_path):
        """The fix changed the byte layout of every counted string, so the
        format's own files have to survive it too."""
        hits = sorted(glob.glob("tttr-data/**/*.sm", recursive=True))
        if not hits:
            pytest.skip("no .sm test data")
        src = tttrlib.TTTR(hits[0])
        out = str(tmp_path / "roundtrip.sm")
        assert src.write(out, None, SM)

        back = tttrlib.TTTR(out)
        assert len(back) == len(src)
        np.testing.assert_array_equal(np.asarray(back.macro_times),
                                      np.asarray(src.macro_times))

    def test_the_header_matches_what_a_real_file_carries(self, tmp_path):
        """Pinned as bytes, because each of the three defects was invisible at
        every level above them: the events were always correct."""
        src = _source()
        out = tmp_path / "written.sm"
        src.write(str(out), None, SM)
        head = out.read_bytes()[:14]

        # big-endian version 2, then a counted comment, then a counted "Simple"
        assert head[0:4] == b"\x00\x00\x00\x02", "version must be 2"
        comment_len = int.from_bytes(head[4:8], "big")
        simple_at = 8 + comment_len
        rest = out.read_bytes()[simple_at:simple_at + 4 + 6]
        assert int.from_bytes(rest[0:4], "big") == 6, "no trailing null in the count"
        assert rest[4:10] == b"Simple"

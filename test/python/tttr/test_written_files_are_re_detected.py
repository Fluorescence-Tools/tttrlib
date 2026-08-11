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


HT3 = 1


def _first(pattern):
    hits = sorted(glob.glob(f"tttr-data/**/{pattern}", recursive=True))
    if not hits:
        pytest.skip(f"no {pattern} test data")
    return tttrlib.TTTR(hits[0])


class TestAFormatSurvivesItsOwnRoundTrip:
    """`.sm` written from a `.sm`, `.ht3` from a `.ht3` — a format against
    itself, which is the case that has to hold before any other does.

    Read `TTTR(path)` here rather than `TTTR(path, container)`: passing the
    container is what every other round-trip test in the suite does, and it is
    exactly the path that kept working while detection was broken."""

    def test_sm_round_trips_by_name(self, tmp_path):
        src = _first("*.sm")
        out = str(tmp_path / "roundtrip.sm")
        assert src.write(out, None, SM)

        back = tttrlib.TTTR(out)
        assert len(back) == len(src), "written by name and came back empty"
        np.testing.assert_array_equal(np.asarray(back.macro_times),
                                      np.asarray(src.macro_times))
        np.testing.assert_array_equal(np.asarray(back.micro_times),
                                      np.asarray(src.micro_times))

    def test_ht3_round_trips_by_name(self, tmp_path):
        """Its own format, its own extension, its own record type. Here so the
        `.sm` case above is not the only thing holding this property — the two
        are different files and different record encodings, and a change to one
        writer must not be checked through the other."""
        src = _first("*.ht3")
        out = str(tmp_path / "roundtrip.ht3")
        assert src.write(out, None, HT3)

        back = tttrlib.TTTR(out)
        assert len(back) == len(src)
        np.testing.assert_array_equal(np.asarray(back.macro_times),
                                      np.asarray(src.macro_times))


class TestATranscodeIsAlsoReDetected:
    """A *different* thing from the round trips above, and labelled as one.

    `TTTR.write` supports transcoding — reading one format and writing another
    — and the events are re-encoded into the target's record layout on the way.
    It is worth testing because it is how the defect was found (an HT3 source
    has no `version` tag, so the SM writer took its default, which was wrong),
    but it must not stand in for a format's own round trip: a `.ht3` and a
    `.sm` are different files with different record types, and proving one
    through the other proves neither."""

    def test_an_ht3_written_as_sm_opens_by_name(self, tmp_path):
        src = _first("*.ht3")
        out = str(tmp_path / "transcoded.sm")
        assert src.write(out, None, SM)

        back = tttrlib.TTTR(out)
        assert len(back) == len(src), "opened by name but came back empty"
        np.testing.assert_array_equal(np.asarray(back.macro_times),
                                      np.asarray(src.macro_times))

    def test_the_transcode_is_stored_as_sm_not_as_the_source_format(self, tmp_path):
        """What "different types" means concretely: the file must describe
        itself as SM, not carry the source's container and record type."""
        import json
        src = _first("*.ht3")
        out = str(tmp_path / "transcoded.sm")
        src.write(out, None, SM)

        header = json.loads(tttrlib.TTTR(out).header.json)
        assert header["MeasDesc_ContainerType"] == SM
        assert header["MeasDesc_RecordType"] != json.loads(src.header.json)["MeasDesc_RecordType"]

class TestTheSmHeaderBytes:

    def test_the_header_matches_what_a_real_file_carries(self, tmp_path):
        """Pinned as bytes, because each of the three defects was invisible at
        every level above them: the events were always correct."""
        src = _first("*.sm")
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

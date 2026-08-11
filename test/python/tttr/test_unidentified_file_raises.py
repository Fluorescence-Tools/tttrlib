"""An unidentifiable file must raise, not read as zero photons.

`TTTR(path)` used to print "File ... not supported." to stderr and hand back a
TTTR with no events. A caller in a script, a notebook or a GUI then computed a
count rate, a correlation or a lifetime from nothing and saw no error at all —
anything that captured stderr, or simply was not watching it, got a
measurement that appeared to run and produced no signal (BUGS 2026-08-11, the
half the entry called "the dangerous one").

Detection failure is now an exception naming the path.
"""

import pathlib
import tempfile
import unittest

import tttrlib


class TestUnidentifiedFileRaises(unittest.TestCase):

    def setUp(self):
        self.tmp = pathlib.Path(tempfile.mkdtemp())

    def test_a_file_of_an_unknown_format_raises(self):
        p = self.tmp / "not_a_photon_file.bin"
        p.write_bytes(b"this is not any TTTR container" * 100)
        with self.assertRaises(Exception) as caught:
            tttrlib.TTTR(str(p))
        self.assertIn(str(p), str(caught.exception))

    def test_a_missing_file_raises(self):
        with self.assertRaises(Exception):
            tttrlib.TTTR(str(self.tmp / "does_not_exist.ptu"))

    def test_the_message_says_what_failed_and_how_to_proceed(self):
        p = self.tmp / "mystery.dat"
        p.write_bytes(b"\x00\x01\x02\x03" * 256)
        with self.assertRaises(Exception) as caught:
            tttrlib.TTTR(str(p))
        msg = str(caught.exception)
        # The two things stderr never said: which step failed, and the way out.
        self.assertIn("could not be determined", msg)
        self.assertIn("container type", msg)

    def test_an_unknown_container_name_raises(self):
        p = self.tmp / "x.ptu"
        p.write_bytes(b"\x00" * 64)
        with self.assertRaises(Exception):
            tttrlib.TTTR(str(p), "NoSuchContainerFormat")

    def test_a_real_file_is_unaffected(self):
        from test_settings import settings, DATA_AVAILABLE  # type: ignore
        if not DATA_AVAILABLE:
            self.skipTest("Data directory not found")
        t = tttrlib.TTTR(settings["spc132_filename"], "SPC-130")
        self.assertGreater(len(t), 0)


if __name__ == "__main__":
    unittest.main()

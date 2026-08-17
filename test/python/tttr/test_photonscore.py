# SPDX-License-Identifier: BSD-3-Clause
"""Round-trip tests for the Photonscore LINCam ".photons" (D7) reader.

A synthetic ".photons" file is written with :mod:`d7_writer` and read back
through tttrlib. Each photon's (x, y) position is stored in the flat TTTR
stream as two marker events (MARKER_POSITION_X / MARKER_POSITION_Y) that
precede the photon, so no photonscore-specific code path is needed downstream:
positions, decay and an image are recovered with standard TTTR accessors.
"""
import os
import sys
import tempfile
import unittest

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import tttrlib
from d7_writer import write_photons

# Routing channels used for the position markers (see include/info.h)
MARKER_POSITION_X = 0
MARKER_POSITION_Y = 1
RECORD_PHOTON = 0
RECORD_MARKER = 1


def _make_sample(path, n=500, position_bits=12, tac_bits=12, tac_channel_ps=27.0):
    rng = np.random.RandomState(0)
    pos_range = 1 << position_bits
    tac_range = 1 << tac_bits
    x = rng.randint(0, pos_range, n).astype(np.int64)
    y = rng.randint(0, pos_range, n).astype(np.int64)
    dt = rng.randint(0, tac_range, n).astype(np.int64)
    ms = np.sort(rng.randint(0, 100000, n)).astype(np.int64)
    write_photons(
        path,
        [
            ("/photons/x", 2, x.tolist()),
            ("/photons/y", 2, y.tolist()),
            ("/photons/dt", 2, dt.tolist()),
            ("/photons/ms", 3, ms.tolist()),
        ],
        {
            "/photons/PositionBits": str(position_bits),
            "/photons/TacBits": str(tac_bits),
            "/photons/TacChannel": str(tac_channel_ps),
        },
    )
    return x, y, dt, ms


class TestPhotonscore(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.path = os.path.join(self.tmp, "synthetic.photons")
        self.x, self.y, self.dt, self.ms = _make_sample(self.path)
        self.n = len(self.x)

    def test_detection(self):
        self.assertTrue(tttrlib.isPhotonsFile(self.path))
        # PS_PHOTONS_CONTAINER == 8 (container ids are not exposed as constants)
        self.assertEqual(tttrlib.inferTTTRFileType(self.path), 8)
        self.assertIn("photons", tttrlib.get_supported_filetypes())
        self.assertIn("PHOTONS", tttrlib.TTTR.get_supported_container_names())

    def test_stream_layout(self):
        t = tttrlib.TTTR(self.path)
        self.assertEqual(t.get_tttr_container_type(), "PHOTONS")
        # 2 position markers + 1 photon per photon
        self.assertEqual(len(t.macro_times), 3 * self.n)
        et = np.asarray(t.event_types)
        self.assertEqual(int((et == RECORD_PHOTON).sum()), self.n)
        self.assertEqual(int((et == RECORD_MARKER).sum()), 2 * self.n)

    def test_positions_and_decay_recovered(self):
        t = tttrlib.TTTR(self.path)
        et = np.asarray(t.event_types)
        rc = np.asarray(t.routing_channels)
        mi = np.asarray(t.micro_times)
        mt = np.asarray(t.macro_times)

        x = mi[(et == RECORD_MARKER) & (rc == MARKER_POSITION_X)]
        y = mi[(et == RECORD_MARKER) & (rc == MARKER_POSITION_Y)]
        dt = mi[et == RECORD_PHOTON]
        ms = mt[et == RECORD_PHOTON]

        np.testing.assert_array_equal(x, self.x)
        np.testing.assert_array_equal(y, self.y)
        np.testing.assert_array_equal(dt, self.dt)
        np.testing.assert_array_equal(ms, self.ms)

    def test_calibration(self):
        t = tttrlib.TTTR(self.path)
        h = t.get_header()
        self.assertAlmostEqual(h.macro_time_resolution, 1e-3)
        import json
        tags = {d["name"]: d.get("value") for d in json.loads(h.json)["tags"]}
        self.assertAlmostEqual(tags["MeasDesc_Resolution"], 27.0e-12)
        self.assertEqual(tags["MeasDesc_NumberMicrotimes"], 4096)
        self.assertEqual(tags["Photons_PositionRange"], 4096.0)

    def test_photonsfile_reads_ours(self):
        """The public reference decoder (photonsfile, alex1075/photonsfile --
        the Python port of Photonscore's Apache-2.0 d7 library) must read the
        Python-written and the C++-written .photons identically to tttrlib:
        every dataset (x, y, dt, ms) and the attributes. No proprietary sample
        exists in the test data; the reference runs on our bytes."""
        try:
            import photonsfile
        except ImportError:
            self.skipTest("photonsfile not installed")
        out = os.path.join(self.tmp, "written.photons")
        tttrlib.TTTR(self.path).write(out)
        for path in (self.path, out):
            with self.subTest(file=os.path.basename(path)):
                with photonsfile.PhotonsFile(path) as pf:
                    ph = pf.photons(("x", "y", "dt", "ms"))
                    self.assertEqual(pf.position_bits, 12)
                    self.assertEqual(pf.tac_bits, 12)
                    self.assertAlmostEqual(float(pf.tac_channel), 27.0)
                np.testing.assert_array_equal(np.asarray(ph["x"]), self.x)
                np.testing.assert_array_equal(np.asarray(ph["y"]), self.y)
                np.testing.assert_array_equal(np.asarray(ph["dt"]), self.dt)
                np.testing.assert_array_equal(np.asarray(ph["ms"]), self.ms)
                # and tttrlib on the same file agrees with the reference field for field
                t = tttrlib.TTTR(path)
                et = np.asarray(t.event_types); rc = np.asarray(t.routing_channels); mi = np.asarray(t.micro_times)
                np.testing.assert_array_equal(mi[et == RECORD_PHOTON], np.asarray(ph["dt"]))
                np.testing.assert_array_equal(mi[(et == RECORD_MARKER) & (rc == MARKER_POSITION_X)], np.asarray(ph["x"]))
                np.testing.assert_array_equal(mi[(et == RECORD_MARKER) & (rc == MARKER_POSITION_Y)], np.asarray(ph["y"]))

    def test_cpp_writer_roundtrip(self):
        # Read, then write a new .photons file with the C++ writer, then read
        # it back. Exercises photonscore::write_photons via TTTR.write.
        t = tttrlib.TTTR(self.path)
        out = os.path.join(self.tmp, "written.photons")
        t.write(out)
        self.assertTrue(tttrlib.isPhotonsFile(out))

        t2 = tttrlib.TTTR(out)
        np.testing.assert_array_equal(t2.macro_times, t.macro_times)
        np.testing.assert_array_equal(t2.micro_times, t.micro_times)
        np.testing.assert_array_equal(t2.routing_channels, t.routing_channels)
        np.testing.assert_array_equal(t2.event_types, t.event_types)

        # Positions survive the round trip.
        et = np.asarray(t2.event_types)
        rc = np.asarray(t2.routing_channels)
        mi = np.asarray(t2.micro_times)
        np.testing.assert_array_equal(
            mi[(et == RECORD_MARKER) & (rc == MARKER_POSITION_X)], self.x)
        np.testing.assert_array_equal(
            mi[(et == RECORD_MARKER) & (rc == MARKER_POSITION_Y)], self.y)

    def test_image_reconstruction(self):
        # No special case: bin the position markers with numpy.
        t = tttrlib.TTTR(self.path)
        et = np.asarray(t.event_types)
        rc = np.asarray(t.routing_channels)
        mi = np.asarray(t.micro_times)
        x = mi[(et == RECORD_MARKER) & (rc == MARKER_POSITION_X)]
        y = mi[(et == RECORD_MARKER) & (rc == MARKER_POSITION_Y)]
        img, _, _ = np.histogram2d(
            y, x, bins=[64, 64], range=[[0, 4096], [0, 4096]])
        self.assertEqual(int(img.sum()), self.n)


if __name__ == "__main__":
    unittest.main()

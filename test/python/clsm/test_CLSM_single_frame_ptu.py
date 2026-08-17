"""
Single-frame FLIM PTU from a PicoHarp (SymPhoTime export).

The file is a PicoHarp T3 record stream: markers are the special channel 15
with the marker bits in the micro time (256 line starts, 256 line stops, one
frame start), photons are on channel 1. Until 2026-08-17 the decoder misread
this record type (markers by ``dtime == 0``, channel-15 markers as photons),
and this test pinned what fell out of that: 653 dtime-0 photons on channel 1
taken for line-start markers, hence a "salvaged" 1 x 652 x 256 frame with
1.2 % of the photons dropped. With the format decoded as ptufile decodes it
the file is one clean 256 x 256 frame holding every photon; the default
routine recognises PicoHarp T3 markers (channel 15 + micro-time bits) and
gives the same image as ``reading_routine='SP8'``.
"""

from __future__ import division

import os
import unittest

import numpy as np
import tttrlib

from test_settings import settings, DATA_AVAILABLE  # type: ignore

single_frame_filename = settings.get("clsm_single_frame_ptu_filename")

DATA_PRESENT = (
    DATA_AVAILABLE
    and single_frame_filename is not None
    and os.path.exists(single_frame_filename)
)

# Pinned reference values (identical to ptufile's decode). Photons are on
# routing channel 1; channel 15 carries the markers.
N_EVENTS = 722915          # 722 402 photons + 513 markers
SHAPE = (1, 256, 256)
SUM_CHANNEL_1 = 722402     # every photon lands in the frame


@unittest.skipIf(not DATA_PRESENT, "Single-frame PTU test file not available")
class TestSingleFramePTU(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.data = tttrlib.TTTR(single_frame_filename, 'PTU')

    def test_file_reads_all_photons(self):
        self.assertEqual(self.data.get_n_valid_events(), N_EVENTS)
        self.assertEqual(sorted(np.unique(self.data.routing_channels)), [1, 15])

    def test_reconstructs_one_full_frame(self):
        """One 256 x 256 frame with every photon (was 0 frames, then 652 lines)."""
        image = tttrlib.CLSMImage(self.data, channels=[1], fill=True)
        self.assertEqual(image.n_frames, 1)
        self.assertEqual(image.shape, SHAPE)
        self.assertEqual(image.intensity.sum(), SUM_CHANNEL_1)

    def test_default_routine_matches_sp8_routine(self):
        """PicoHarp T3 markers are found by the default routine as by 'SP8'."""
        a = tttrlib.CLSMImage(self.data, channels=[1], fill=True)
        b = tttrlib.CLSMImage(self.data, channels=[1], fill=True, reading_routine="SP8")
        self.assertEqual(a.shape, b.shape)
        np.testing.assert_array_equal(np.asarray(a.intensity), np.asarray(b.intensity))
        self.assertEqual(a.n_lines, 256)
        self.assertEqual(len(a[0]), 256)

    def test_default_channels_include_every_used_channel(self):
        """Without an explicit channel list all used channels are filled."""
        image = tttrlib.CLSMImage(self.data, fill=True)
        self.assertEqual(image.shape, SHAPE)
        self.assertGreaterEqual(image.intensity.sum(), SUM_CHANNEL_1)


if __name__ == '__main__':
    unittest.main()

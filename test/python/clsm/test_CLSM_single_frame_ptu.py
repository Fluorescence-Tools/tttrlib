"""
Regression: single-frame FLIM PTU (line markers, no frame marker).

Some PicoHarp/SymPhoTime PTU acquisitions declare a frame-start marker in the
header but never emit one -- the whole acquisition is a single frame. Before the
fix in ``CLSMImage::remove_incomplete_frames`` such files reconstructed to an
empty ``0 x 256 x 256`` stack: ``create_frames`` did synthesize one frame from
the line markers, but that frame carried fewer lines than the header-declared
``ImgHdr_PixY`` (256) and was then discarded as incomplete.

The fix salvages the frame(s) with the most lines when *nothing* is complete and
adopts that line count. This export has 652 real scan lines, i.e. more than the
nominal 256, so the reconstruction is 1 x 652 x 256.
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

# Pinned reference values. The photons are on routing channel 1; channel 15
# carries the marker/sync events.
N_EVENTS = 722915
SHAPE = (1, 652, 256)
SUM_CHANNEL_1 = 713854


@unittest.skipIf(not DATA_PRESENT, "Single-frame PTU test file not available")
class TestSingleFramePTU(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.data = tttrlib.TTTR(single_frame_filename, 'PTU')

    def test_file_reads_all_photons(self):
        self.assertEqual(self.data.get_n_valid_events(), N_EVENTS)
        self.assertEqual(sorted(np.unique(self.data.routing_channels)), [1, 15])

    def test_reconstructs_a_frame_instead_of_an_empty_stack(self):
        """The regression itself: this used to be 0 x 256 x 256."""
        image = tttrlib.CLSMImage(self.data, channels=[1], fill=True)
        self.assertGreater(image.n_frames, 0)
        self.assertEqual(image.shape, SHAPE)
        self.assertEqual(image.intensity.sum(), SUM_CHANNEL_1)

    def test_line_count_overrides_the_header(self):
        """n_lines is adopted from the salvaged frame, not ImgHdr_PixY (256)."""
        image = tttrlib.CLSMImage(self.data, channels=[1], fill=True)
        self.assertEqual(image.n_lines, 652)
        self.assertEqual(len(image[0]), 652)

    def test_default_channels_include_every_used_channel(self):
        """Without an explicit channel list all used channels are filled."""
        image = tttrlib.CLSMImage(self.data, fill=True)
        self.assertEqual(image.shape, SHAPE)
        self.assertGreaterEqual(image.intensity.sum(), SUM_CHANNEL_1)


if __name__ == '__main__':
    unittest.main()

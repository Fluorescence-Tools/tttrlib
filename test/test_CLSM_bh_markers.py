"""
Integration tests for BH SPC pixel marker-based binning.

Tests verify that:
1. Pixel marker mode correctly bins photons by marker time intervals
2. Default time-based binning still works when pixel markers disabled
3. Edge cases (photons before/after markers, flyback region) are handled
"""

from __future__ import division

import unittest
import numpy as np
import tttrlib


class TestBHPixelMarkerBinning(unittest.TestCase):
    """Tests for pixel marker-based binning in CLSMImage."""

    def create_synthetic_tttr(self):
        """
        Creates a TTTR object with 1 frame, 2 lines, and pixel markers.
        Line 1: [100, 500], Pixel markers at 200, 300, 400. Photons at 250, 350, 450.
        Line 2: [600, 1000], Pixel markers at 700, 800, 900. Photons at 750, 850, 950.
        """
        # Times:
        # F: 0
        # L1: 100, P: 200, Phot: 250, P: 300, Phot: 350, P: 400, Phot: 450, LStop: 500
        # L2: 600, P: 700, Phot: 750, P: 800, Phot: 850, P: 900, Phot: 950, LStop: 1000
        macro_times = np.array(
            [
                0,
                100,
                200,
                250,
                300,
                350,
                400,
                450,
                500,
                600,
                700,
                750,
                800,
                850,
                900,
                950,
                1000,
            ],
            dtype=np.uint64,
        )
        micro_times = np.zeros(len(macro_times), dtype=np.uint16)
        # Channels: 4=Frame, 1=LineStart, 2=LineStop, 8=Pixel, 0=Photon
        routing_channels = np.array(
            [4, 1, 8, 0, 8, 0, 8, 0, 2, 1, 8, 0, 8, 0, 8, 0, 2], dtype=np.int8
        )
        # Types: 1=Marker, 0=Photon
        event_types = np.array(
            [1, 1, 1, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1], dtype=np.int8
        )

        data = tttrlib.TTTR(macro_times, micro_times, routing_channels, event_types)
        return data

    def test_pixel_marker_binning_assigns_to_correct_bins(self):
        """Verify photons are assigned to correct pixels based on marker times."""
        tttr = self.create_synthetic_tttr()

        # Configure CLSM settings
        clsm = tttrlib.CLSMImage(
            tttr_data=tttr,
            marker_frame_start=[4],
            marker_line_start=1,
            marker_line_stop=2,
            n_pixel_per_line=3,
            use_pixel_markers=True,
            marker_pixel=8,
            settings={"n_lines": 2},
        )
        clsm.fill()

        # Check intensity image
        intensity = clsm.intensity
        # Shape should be (n_frames, n_lines, n_pixel)
        # We expect at least 1 frame (might be 2 due to EOF, but we check the first one)
        self.assertGreaterEqual(intensity.shape[0], 1)
        self.assertEqual(intensity.shape[1], 2)
        self.assertEqual(intensity.shape[2], 3)

        # Line 0:
        # Pixel 0 should have 1 photon (from time 250)
        # Pixel 1 should have 1 photon (from time 350)
        # Pixel 2 should have 1 photon (from time 450)
        self.assertEqual(intensity[0, 0, 0], 1)
        self.assertEqual(intensity[0, 0, 1], 1)
        self.assertEqual(intensity[0, 0, 2], 1)

        # Line 1:
        # Pixel 0 should have 1 photon (from time 750)
        # Pixel 1 should have 1 photon (from time 850)
        # Pixel 2 should have 1 photon (from time 950)
        self.assertEqual(intensity[0, 1, 0], 1)
        self.assertEqual(intensity[0, 1, 1], 1)
        self.assertEqual(intensity[0, 1, 2], 1)

    def test_flyback_pixels_discarded(self):
        """Verify photons in flyback region are not assigned to valid pixels."""
        tttr = self.create_synthetic_tttr()

        # Set n_pixel_per_line=2, so pixel 2 (from time 450/950) is flyback
        clsm = tttrlib.CLSMImage(
            tttr_data=tttr,
            marker_frame_start=[4],
            marker_line_start=1,
            marker_line_stop=2,
            n_pixel_per_line=2,
            use_pixel_markers=True,
            marker_pixel=8,
            settings={"n_lines": 2},
        )
        clsm.fill()

        intensity = clsm.intensity
        self.assertEqual(intensity.shape[1], 2)
        self.assertEqual(intensity.shape[2], 2)

        # Total photons in image should be 4 (2 per line)
        self.assertEqual(np.sum(intensity[0]), 4)
        self.assertEqual(intensity[0, 0, 0], 1)
        self.assertEqual(intensity[0, 0, 1], 1)

    def test_default_time_binning_unaffected(self):
        """Verify time-based binning works when pixel markers disabled."""
        tttr = self.create_synthetic_tttr()

        # With use_pixel_markers=False, it should use time-based binning
        # Line 0 duration is 500 - 100 = 400.
        # With n_pixel_per_line=2, each pixel is 200 units.
        # Pixel 0: [100, 300) -> Photons at 250.
        # Pixel 1: [300, 500) -> Photons at 350, 450.

        clsm = tttrlib.CLSMImage(
            tttr_data=tttr,
            marker_frame_start=[4],
            marker_line_start=1,
            marker_line_stop=2,
            n_pixel_per_line=2,
            use_pixel_markers=False,
            settings={"n_lines": 2},
        )
        clsm.fill()

        intensity = clsm.intensity
        self.assertEqual(intensity[0, 0, 0], 1)  # 250
        self.assertEqual(intensity[0, 0, 1], 2)  # 350 and 450


if __name__ == "__main__":
    unittest.main()

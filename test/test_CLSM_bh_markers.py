"""
Integration tests for BH SPC pixel marker-based binning.

Tests verify that:
1. Pixel marker mode correctly bins photons by marker time intervals
2. Default time-based binning still works when pixel markers disabled
3. Edge cases (photons before/after markers, flyback region) are handled
4. Real BH SPC data files can be loaded and processed
"""

from __future__ import division

import os
import unittest
import numpy as np

try:
    import tttrlib
except ImportError:
    tttrlib = None

try:
    from test_settings import settings, DATA_AVAILABLE
except ImportError:
    settings = None
    DATA_AVAILABLE = False


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


class TestBHTruncatedRecordingRecovery(unittest.TestCase):
    """Tests for recovering truncated BH SPC-130 recordings."""

    def create_truncated_tttr(self):
        """
        Creates a TTTR object simulating a truncated BH recording.

        Structure:
        - Frame marker at time 0
        - Line 1: start at 100, photons at 150, 200
        - Line 2: start at 300, photons at 350, 400
        - Line 3: start at 500, photons at 550, 600
        - NO closing frame marker (simulates truncation)
        - NO line start after line 3 (last line has no following marker)
        """
        # BH marker channels: Frame=4, Line=2, Pixel=1
        # Event types: 1=Marker, 0=Photon

        macro_times = np.array(
            [
                0,  # Frame marker
                100,  # Line 1 start
                150,  # Photon
                200,  # Photon
                300,  # Line 2 start
                350,  # Photon
                400,  # Photon
                500,  # Line 3 start
                550,  # Photon
                600,  # Photon
                800,  # Extra event to ensure last line is captured
            ],
            dtype=np.uint64,
        )

        micro_times = np.zeros(len(macro_times), dtype=np.uint16)

        routing_channels = np.array(
            [
                4,  # Frame marker (channel 4)
                2,  # Line marker (channel 2)
                0,  # Photon (channel 0)
                0,  # Photon
                2,  # Line marker
                0,  # Photon
                0,  # Photon
                2,  # Line marker
                0,  # Photon
                0,  # Photon
                127,  # Extra marker
            ],
            dtype=np.int8,
        )

        event_types = np.array(
            [
                1,  # Marker
                1,  # Marker
                0,  # Photon
                0,  # Photon
                1,  # Marker
                0,  # Photon
                0,  # Photon
                1,  # Marker
                0,  # Photon
                0,  # Photon
                1,  # Extra marker
            ],
            dtype=np.int8,
        )

        data = tttrlib.TTTR(macro_times, micro_times, routing_channels, event_types)
        return data

    def test_truncated_recording_recovers_last_line(self):
        """Verify that photons in the last line are recovered even without closing markers."""
        # This test requires CLSMImage code changes that may not be in the
        # conda package artifact from an earlier build. Only run from source.
        import sys
        # Skip if running from conda/miniforge environment (likely using conda package)
        exe_lower = sys.executable.lower()
        if 'conda' in exe_lower or 'miniforge' in exe_lower:
            self.skipTest("Requires source build - conda package may be outdated")

        tttr = self.create_truncated_tttr()

        # Use BH SPC-130 reading routine with explicit settings
        # The truncated line recovery only triggers for BH_SPC130 reading routine
        clsm = tttrlib.CLSMImage(
            tttr_data=tttr,
            reading_routine="BH_SPC130",
            marker_frame_start=[4],
            marker_line_start=2,
            marker_line_stop=255,  # No stop marker (BH style)
            marker_event_type=1,
            n_pixel_per_line=2,
            settings={"n_lines": 3},
            skip_before_first_frame_marker=True,
            skip_after_last_frame_marker=False,  # Key: include data after last frame marker
        )
        clsm.fill()

        intensity = clsm.intensity

        # Should have 1 frame
        self.assertEqual(intensity.shape[0], 1, "Should have exactly 1 frame")

        # Should have 3 lines (including the last one without following marker)
        self.assertEqual(
            intensity.shape[1], 3, "Should have 3 lines (last line recovered)"
        )

        # Should have 2 pixels per line
        self.assertEqual(intensity.shape[2], 2, "Should have 2 pixels per line")

        # Each line should have 2 photons total
        self.assertEqual(np.sum(intensity[0, 0, :]), 2, "Line 0 should have 2 photons")
        self.assertEqual(np.sum(intensity[0, 1, :]), 2, "Line 1 should have 2 photons")
        self.assertEqual(
            np.sum(intensity[0, 2, :]), 2, "Line 2 should have 2 photons (recovered)"
        )

        # Total photons should be 6
        self.assertEqual(np.sum(intensity), 6, "Total should be 6 photons")

    def test_truncated_with_reading_routine_bh_spc130(self):
        """Verify that reading_routine='BH_SPC130' automatically handles truncation."""
        # This test requires CLSMImage code changes that may not be in the
        # conda package artifact from an earlier build. Only run from source.
        import sys
        exe_lower = sys.executable.lower()
        if 'conda' in exe_lower or 'miniforge' in exe_lower:
            self.skipTest("Requires source build - conda package may be outdated")

        tttr = self.create_truncated_tttr()

        # Use the high-level reading_routine parameter
        clsm = tttrlib.CLSMImage(
            tttr_data=tttr,
            reading_routine="BH_SPC130",
            n_pixel_per_line=2,
            settings={"n_lines": 3},
        )
        clsm.fill()

        intensity = clsm.intensity

        # Should recover all 3 lines with 2 photons each
        self.assertEqual(intensity.shape[0], 1, "Should have 1 frame")
        self.assertEqual(intensity.shape[1], 3, "Should have 3 lines")
        self.assertEqual(
            np.sum(intensity[0, :, :]), 6, "Should have 6 total photons in the image"
        )


if __name__ == "__main__":
    unittest.main()


@unittest.skipIf(settings is None, "settings module not available")
class TestBHRealData(unittest.TestCase):
    """Tests for loading and processing real BH SPC data files."""

    @classmethod
    def setUpClass(cls):
        """Load the real BH SPC data files."""
        data_root = os.environ.get("TTTRLIB_DATA", "tttr-data")
        
        spc_m1_path = os.path.join(data_root, "imaging", "bh", "spcm", "FocalCheck_A1_20x_8xzoom_750nm_m1.spc")
        set_m1_path = os.path.join(data_root, "imaging", "bh", "spcm", "FocalCheck_A1_20x_8xzoom_750nm_m1.set")
        
        if not os.path.exists(spc_m1_path) or not os.path.exists(set_m1_path):
            raise unittest.SkipTest(f"Real BH SPC test data not found at {data_root}")
        
        cls.spc_m1_path = spc_m1_path
        cls.set_m1_path = set_m1_path
        
        cls.tttr = tttrlib.TTTR(spc_m1_path)
        cls.header = cls.tttr.header

    def test_load_bh_spc_file(self):
        """Verify BH SPC file can be loaded."""
        self.assertIsNotNone(self.tttr)
        self.assertGreater(len(self.tttr.macro_times), 0)

    def test_bh_spc_header_parsing(self):
        """Verify BH .set file header is parsed and contains expected tags."""
        header = self.tttr.header
        
        json_str = header.get_json()
        
        self.assertIsInstance(json_str, str, "Header JSON should be a string")
        self.assertIn('ImgHdr', json_str, "Header should contain ImgHdr tags")
        self.assertIn('PixX', json_str, "Header should contain pixel width info")
        self.assertIn('PixY', json_str, "Header should contain pixel height info")

    def test_bh_spc_reading_routine(self):
        """Verify CLSMImage works with BH_SPC130 reading routine."""
        clsm = tttrlib.CLSMImage(
            tttr_data=self.tttr,
            reading_routine="BH_SPC130",
            n_pixel_per_line=256,
        )
        clsm.fill()
        
        intensity = clsm.intensity
        
        self.assertGreater(intensity.shape[0], 0, "Should have at least one frame")
        self.assertGreater(intensity.shape[1], 0, "Should have lines")
        self.assertGreater(intensity.shape[2], 0, "Should have pixels")
        
        total_photons = np.sum(intensity)
        self.assertGreater(total_photons, 0, "Should have photons in the image")

    def test_bh_spc_auto_settings(self):
        """Verify CLSMImage auto-detects settings from BH .set file."""
        clsm = tttrlib.CLSMImage(
            tttr_data=self.tttr,
            reading_routine="BH_SPC130",
        )
        
        n_pixel = int(clsm.n_pixel)
        self.assertGreater(n_pixel, 0, "Should auto-detect pixels per line")


@unittest.skipIf(settings is None, "settings module not available")
class TestBHSettingsFromJSON(unittest.TestCase):
    """Tests for BH settings loaded from JSON configuration."""

    @classmethod
    def setUpClass(cls):
        """Check if JSON settings for BH are available."""
        if settings is None:
            raise unittest.SkipTest("settings module not available")
        
        cls.has_bh_settings = hasattr(settings, 'bh_spcm_clsm_m1_filename')

    def test_bh_json_settings_exist(self):
        """Verify BH settings are defined in settings.json."""
        if not self.has_bh_settings:
            self.skipTest("BH SPC settings not in settings.json")
        
        data_root = os.environ.get("TTTRLIB_DATA", "tttr-data")
        
        spc_file = getattr(settings, 'bh_spcm_clsm_m1_filename', None)
        if spc_file:
            spc_path = os.path.join(data_root, spc_file)
            self.assertTrue(os.path.exists(spc_path), f"BH SPC file should exist: {spc_path}")

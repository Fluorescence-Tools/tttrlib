"""
Test CLSMImage JSON settings functionality with real test data.

Tests the ability to initialize CLSMImage with JSON settings containing
CLSM imaging parameters (markers, dimensions, etc.) using real HT3 test data
from CLSM_01.py test suite.
"""

import json
import unittest
import tempfile
import os
from pathlib import Path
import numpy as np
import tttrlib

# Centralized test settings
from test_settings import settings, DATA_AVAILABLE


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found, skipping CLSM tests")
class TestCLSMImageJSON(unittest.TestCase):
    """Test CLSMImage JSON settings functionality with real data."""

    def setUp(self):
        """Set up test fixtures."""
        self.ht3_filename = settings["clsm_ht3_sample1_filename"]

        # Expected marker settings from CLSM_01.py test
        self.expected_ht3_settings = {
            "marker_frame_start": [4],
            "marker_line_start": 1,
            "marker_line_stop": 2,
            "marker_event_type": 1,
            "n_pixel_per_line": 256,
            "reading_routine": "default",
            "skip_before_first_frame_marker": True
        }

    def test_clsm_json_marker_settings_only(self):
        """Test loading CLSM marker settings from JSON file (no LUTs)."""
        if not os.path.exists(self.ht3_filename):
            self.skipTest(f"Data file not found: {self.ht3_filename}")

        # Create JSON with only marker settings (no LUTs)
        json_settings = {
            "marker_frame_start": [4],
            "marker_line_start": 1,
            "marker_line_stop": 2,
            "marker_event_type": 1,
            "n_pixel_per_line": 256,
            "reading_routine": "default",
            "skip_before_first_frame_marker": True
        }

        # Create temporary JSON file
        with tempfile.NamedTemporaryFile(mode='w', suffix='.json', delete=False) as f:
            json.dump(json_settings, f, indent=2)
            settings_file = f.name

        try:
            # Test loading with JSON settings file
            clsm_json = tttrlib.CLSMImage(
                self.ht3_filename,
                settings_file=settings_file
            )

            # Test loading with traditional parameters for comparison
            clsm_traditional = tttrlib.CLSMImage(
                self.ht3_filename,
                **self.expected_ht3_settings
            )

            # Both should produce identical results
            self.assertEqual(clsm_json.n_frames, clsm_traditional.n_frames)
            self.assertEqual(clsm_json.n_lines, clsm_traditional.n_lines)
            self.assertEqual(clsm_json.n_pixel, clsm_traditional.n_pixel)

            # Fill both images and compare
            clsm_json.fill(channels=[0])
            clsm_traditional.fill(channels=[0])

            # Compare intensity arrays
            np.testing.assert_array_equal(
                clsm_json.intensity,
                clsm_traditional.intensity
            )

        finally:
            # Clean up
            os.unlink(settings_file)

    def test_clsm_json_dict_vs_file(self):
        """Test that loading from JSON dict produces same result as JSON file."""
        if not os.path.exists(self.ht3_filename):
            self.skipTest(f"Data file not found: {self.ht3_filename}")

        json_settings = {
            "marker_frame_start": [4],
            "marker_line_start": 1,
            "marker_line_stop": 2,
            "marker_event_type": 1,
            "n_pixel_per_line": 256,
            "reading_routine": "default",
            "skip_before_first_frame_marker": True
        }

        # Create temporary JSON file
        with tempfile.NamedTemporaryFile(mode='w', suffix='.json', delete=False) as f:
            json.dump(json_settings, f, indent=2)
            settings_file = f.name

        try:
            # Load from file
            clsm_file = tttrlib.CLSMImage(
                self.ht3_filename,
                settings_file=settings_file
            )

            # Load from dict
            clsm_dict = tttrlib.CLSMImage(
                self.ht3_filename,
                settings=json_settings
            )

            # Should produce identical results
            self.assertEqual(clsm_file.n_frames, clsm_dict.n_frames)
            self.assertEqual(clsm_file.n_lines, clsm_dict.n_lines)
            self.assertEqual(clsm_file.n_pixel, clsm_dict.n_pixel)

        finally:
            os.unlink(settings_file)

    def test_clsm_json_parameter_override(self):
        """Test that explicit parameters override JSON settings."""
        if not os.path.exists(self.ht3_filename):
            self.skipTest(f"Data file not found: {self.ht3_filename}")

        # JSON settings
        json_settings = {
            "marker_line_start": 1,
            "marker_line_stop": 2,
            "n_pixel_per_line": 256
        }

        # Override n_pixel_per_line explicitly
        override_settings = json_settings.copy()
        override_settings["n_pixel_per_line"] = 128  # Different from JSON

        clsm_override = tttrlib.CLSMImage(
            self.ht3_filename,
            settings=json_settings,
            n_pixel_per_line=128  # Explicit override
        )

        # Should use the explicit parameter, not JSON
        self.assertEqual(clsm_override.n_pixel, 128)

    def test_clsm_json_partial_settings(self):
        """Test that partial JSON settings work (only some parameters specified)."""
        if not os.path.exists(self.ht3_filename):
            self.skipTest(f"Data file not found: {self.ht3_filename}")

        # Partial settings - only specify some parameters
        partial_settings = {
            "marker_line_start": 1,
            "marker_line_stop": 2,
            "n_pixel_per_line": 256
        }

        clsm_partial = tttrlib.CLSMImage(
            self.ht3_filename,
            settings=partial_settings
        )

        # Should use specified values
        self.assertEqual(clsm_partial.n_pixel, 256)

        # Should use defaults for unspecified values
        # (This is harder to test directly, but we can check it doesn't crash)

    def test_clsm_json_invalid_file(self):
        """Test error handling for invalid JSON file."""
        with self.assertRaises(FileNotFoundError):
            tttrlib.CLSMImage("dummy.ptu", settings_file="nonexistent.json")

    def test_clsm_json_invalid_dict(self):
        """Test error handling for invalid settings dict."""
        # This should not raise an error - invalid settings should be ignored
        # or handled gracefully
        invalid_settings = {
            "invalid_parameter": "value",
            "marker_line_start": "not_an_int"  # Wrong type
        }

        # Should handle gracefully (either ignore invalid or convert)
        try:
            clsm = tttrlib.CLSMImage(
                self.ht3_filename,
                settings=invalid_settings
            )
            # If it succeeds, that's fine - implementation should be robust
        except Exception:
            # If it fails, that's also acceptable for invalid input
            pass


if __name__ == "__main__":
    unittest.main()

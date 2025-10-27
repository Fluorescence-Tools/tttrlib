"""
Unit tests for Aberior STED PTU file analysis.

Tests CLSMImage functionality with Aberior STED PTU files including
FRC (Fourier Ring Correlation) analysis.
"""

import glob
import json
import os
import unittest
import numpy as np
import tttrlib
from pathlib import Path

# Load settings
settings_path = os.path.join(os.path.dirname(__file__), "settings.json")
settings = json.load(open(settings_path))

# Determine the repository root (two levels up from this file)
repo_root = Path(__file__).resolve().parents[1]
# Get data root from environment variable or use default from settings
env_root = os.getenv("TTTRLIB_DATA")
if env_root:
    env_root = env_root.strip().strip('\'"')
    # Use os.path.abspath to preserve drive letters (avoid UNC path conversion)
    data_root = Path(os.path.abspath(env_root))
else:
    # Use the data_root from settings directly (already absolute path like V:\tttr-data)
    data_root_str = settings.get("data_root", "./tttr-data")
    if os.path.isabs(data_root_str):
        # If already absolute, use it directly to preserve drive letters
        data_root = Path(data_root_str)
    else:
        # If relative, resolve against repo root
        data_root = Path(os.path.abspath(str(repo_root / data_root_str)))

# Helper function to get full path
def get_data_path(rel_path):
    # Use os.path.join and abspath to preserve drive letters
    path_str = os.path.abspath(os.path.join(str(data_root), rel_path))
    path = Path(path_str)
    if not path.exists():
        print(f"WARNING: File {path} does not exist")
    return path_str

# Get STED file
sted_file = get_data_path(settings.get("clsm_sted_filename", "imaging/aberior/pq-mfd-sted/DA_exc561nm15prozent_STED775_100prozent_01.ptu"))

# Get STED reading parameters from settings (use auto-detection by default)
aberior_mfd_sted_reading_parameters = settings.get("aberior_mfd_sted_reading_parameters", {})

# Try to find multiple STED files in the aberior directory
fn_pattern = os.path.join(str(data_root), "imaging/aberior/pq-mfd-sted/*.ptu")
fns = glob.glob(fn_pattern)[:5]

# If no files found, use the single STED file
if not fns:
    fns = [sted_file]

# Determine if data directory exists
DATA_AVAILABLE = data_root.is_dir() and len(fns) > 0 and os.path.exists(fns[0])
if not DATA_AVAILABLE:
    print(f"WARNING: STED data files not found in {data_root}")


@unittest.skipIf(not DATA_AVAILABLE, "STED data files not found, skipping Aberior STED tests")
class TestAberiorSTED(unittest.TestCase):
    """Test CLSMImage with Aberior STED PTU files."""

    @classmethod
    def setUpClass(cls):
        """Load STED files."""
        cls.sted_files = fns
        cls.primary_file = fns[0]
        
        # Verify file exists
        if not os.path.exists(cls.primary_file):
            raise FileNotFoundError(f"STED test file not found: {cls.primary_file}")

    def test_sted_file_loading(self):
        """Test loading STED PTU file."""
        d = tttrlib.TTTR(self.primary_file)
        self.assertIsNotNone(d)
        self.assertGreater(len(d), 0)

    def test_sted_header_access(self):
        """Test accessing STED file header."""
        d = tttrlib.TTTR(self.primary_file)
        header_json = d.header.get_json()
        self.assertIsNotNone(header_json)
        self.assertGreater(len(header_json), 0)

    def test_sted_routing_channels(self):
        """Test reading routing channels from STED file."""
        d = tttrlib.TTTR(self.primary_file)
        channels = d.used_routing_channels
        self.assertIsNotNone(channels)
        self.assertGreater(len(channels), 0)

    def test_sted_microtime_histogram(self):
        """Test microtime histogram calculation."""
        d = tttrlib.TTTR(self.primary_file)
        hist = d.microtime_histogram
        self.assertIsNotNone(hist)
        self.assertGreater(len(hist), 0)

    def test_sted_clsm_image_creation(self):
        """Test CLSMImage creation with STED settings."""
        d = tttrlib.TTTR(self.primary_file)
        img = tttrlib.CLSMImage(d, **aberior_mfd_sted_reading_parameters)
        
        self.assertIsNotNone(img)
        self.assertGreater(img.n_frames, 0)
        self.assertGreater(img.n_lines, 0)
        self.assertGreater(img.n_pixel, 0)

    def test_sted_clsm_image_fill_ungated(self):
        """Test filling CLSMImage with ungated data."""
        d = tttrlib.TTTR(self.primary_file)
        img = tttrlib.CLSMImage(d, **aberior_mfd_sted_reading_parameters)
        
        channels = [0, 1]
        img.fill(channels=channels)
        
        self.assertIsNotNone(img.intensity)
        self.assertEqual(img.intensity.shape, (img.n_frames, img.n_lines, img.n_pixel))
        self.assertGreater(img.intensity.sum(), 0)

    def test_sted_clsm_image_fill_gated(self):
        """Test filling CLSMImage with gated microtime data."""
        d = tttrlib.TTTR(self.primary_file)
        img = tttrlib.CLSMImage(d, **aberior_mfd_sted_reading_parameters)
        
        channels = [0, 1]
        gate_range = (5000, 24000)
        img.fill(channels=channels, micro_time_ranges=[gate_range])
        
        self.assertIsNotNone(img.intensity)
        self.assertEqual(img.intensity.shape, (img.n_frames, img.n_lines, img.n_pixel))
        self.assertGreater(img.intensity.sum(), 0)



if __name__ == '__main__':
    unittest.main(verbosity=2)


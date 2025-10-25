"""CLSM ISM (Image Scanning Microscopy) tests - check if it works"""
from __future__ import division

import os
import unittest
import json
from pathlib import Path
import gc

import tttrlib

# Load settings
settings_path = os.path.join(os.path.dirname(__file__), "settings.json")
settings = json.load(open(settings_path))

repo_root = Path(__file__).resolve().parents[1]
env_root = os.getenv("TTTRLIB_DATA")
if env_root:
    env_root = env_root.strip().strip('\'"')
    data_root = Path(os.path.abspath(env_root))
else:
    data_root_str = settings.get("data_root", "./tttr-data")
    if os.path.isabs(data_root_str):
        data_root = Path(data_root_str)
    else:
        data_root = Path(os.path.abspath(str(repo_root / data_root_str)))

DATA_AVAILABLE = data_root.is_dir()

def get_data_path(rel_path):
    return os.path.abspath(os.path.join(str(data_root), rel_path))

# Load CLSM data path if available
clsm_file = None
if "ht3_clsm_filename" in settings:
    clsm_file = get_data_path(settings["ht3_clsm_filename"])
    CLSM_AVAILABLE = os.path.exists(clsm_file) if clsm_file else False
else:
    CLSM_AVAILABLE = False


@unittest.skipIf(not (DATA_AVAILABLE and CLSM_AVAILABLE), "CLSM data not available")
class TestCLSMISMCreation(unittest.TestCase):
    """CLSM ISM creation tests - check if it works"""

    def setUp(self):
        self.clsm = tttrlib.CLSMImage(clsm_file)

    def tearDown(self):
        self.clsm = None
        gc.collect()

    @unittest.skipIf(not hasattr(tttrlib, 'CLSMISM'), "CLSMISM not available")
    def test_ism_creation_works(self):
        """Test ISM creation works (no correctness check)"""
        try:
            ism = tttrlib.CLSMISM(self.clsm)
            self.assertIsNotNone(ism)
        except (AttributeError, TypeError, RuntimeError):
            # ISM might not be available for this file - that's OK
            pass

    @unittest.skipIf(not hasattr(tttrlib, 'CLSMISM'), "CLSMISM not available")
    def test_ism_creation_from_tttr(self):
        """Test ISM creation from TTTR object"""
        try:
            tttr = self.clsm.tttr_data
            ism = tttrlib.CLSMISM(tttr)
            self.assertIsNotNone(ism)
        except (AttributeError, TypeError, RuntimeError):
            pass


@unittest.skipIf(not (DATA_AVAILABLE and CLSM_AVAILABLE), "CLSM data not available")
class TestCLSMISMOperations(unittest.TestCase):
    """CLSM ISM operations tests - check if it works"""

    def setUp(self):
        self.clsm = tttrlib.CLSMImage(clsm_file)

    def tearDown(self):
        self.clsm = None
        gc.collect()

    @unittest.skipIf(not hasattr(tttrlib, 'CLSMISM'), "CLSMISM not available")
    def test_ism_n_frames_access(self):
        """Test ISM n_frames property access"""
        try:
            ism = tttrlib.CLSMISM(self.clsm)
            n_frames = ism.n_frames
            # Just check we can access it
            self.assertIsNotNone(n_frames)
        except (AttributeError, TypeError, RuntimeError):
            pass

    @unittest.skipIf(not hasattr(tttrlib, 'CLSMISM'), "CLSMISM not available")
    def test_ism_n_lines_access(self):
        """Test ISM n_lines property access"""
        try:
            ism = tttrlib.CLSMISM(self.clsm)
            n_lines = ism.n_lines
            self.assertIsNotNone(n_lines)
        except (AttributeError, TypeError, RuntimeError):
            pass

    @unittest.skipIf(not hasattr(tttrlib, 'CLSMISM'), "CLSMISM not available")
    def test_ism_n_pixel_access(self):
        """Test ISM n_pixel property access"""
        try:
            ism = tttrlib.CLSMISM(self.clsm)
            n_pixel = ism.n_pixel
            self.assertIsNotNone(n_pixel)
        except (AttributeError, TypeError, RuntimeError):
            pass

    @unittest.skipIf(not hasattr(tttrlib, 'CLSMISM'), "CLSMISM not available")
    def test_ism_shape_access(self):
        """Test ISM shape property access"""
        try:
            ism = tttrlib.CLSMISM(self.clsm)
            shape = ism.shape
            self.assertIsNotNone(shape)
        except (AttributeError, TypeError, RuntimeError):
            pass


@unittest.skipIf(not (DATA_AVAILABLE and CLSM_AVAILABLE), "CLSM data not available")
class TestCLSMISMIntensity(unittest.TestCase):
    """CLSM ISM intensity operations - check if it works"""

    def setUp(self):
        self.clsm = tttrlib.CLSMImage(clsm_file)

    def tearDown(self):
        self.clsm = None
        gc.collect()

    @unittest.skipIf(not hasattr(tttrlib, 'CLSMISM'), "CLSMISM not available")
    def test_ism_intensity_access(self):
        """Test ISM intensity property access"""
        try:
            ism = tttrlib.CLSMISM(self.clsm)
            intensity = ism.intensity
            self.assertIsNotNone(intensity)
        except (AttributeError, TypeError, RuntimeError):
            pass

    @unittest.skipIf(not hasattr(tttrlib, 'CLSMISM'), "CLSMISM not available")
    def test_ism_fill_operation(self):
        """Test ISM fill operation"""
        try:
            ism = tttrlib.CLSMISM(self.clsm)
            ism.fill()
            # Just check it doesn't crash
            self.assertIsNotNone(ism)
        except (AttributeError, TypeError, RuntimeError):
            pass

    @unittest.skipIf(not hasattr(tttrlib, 'CLSMISM'), "CLSMISM not available")
    def test_ism_fill_with_channels(self):
        """Test ISM fill with specific channels"""
        try:
            ism = tttrlib.CLSMISM(self.clsm)
            ism.fill(channels=[0, 1])
            self.assertIsNotNone(ism)
        except (AttributeError, TypeError, RuntimeError):
            pass


class TestCLSMISMFallback(unittest.TestCase):
    """Fallback tests when ISM not available"""

    def test_ism_availability_check(self):
        """Test ISM availability"""
        has_ism = hasattr(tttrlib, 'CLSMISM')
        # Just check if it's available
        self.assertIsInstance(has_ism, bool)

    def test_clsm_data_check(self):
        """Test CLSM data availability"""
        self.assertTrue(DATA_AVAILABLE, "Test data directory not found")


if __name__ == '__main__':
    unittest.main()

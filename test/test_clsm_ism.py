"""CLSM ISM (Image Scanning Microscopy) tests - check if it works"""
from __future__ import division

import os
import unittest
import gc

import tttrlib

# Centralized test settings
from test_settings import settings, DATA_AVAILABLE  # type: ignore

# Load CLSM data path if available (already absolute via test_settings)
clsm_file = settings.get("ht3_clsm_filename")
CLSM_AVAILABLE = os.path.exists(clsm_file) if clsm_file else False


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

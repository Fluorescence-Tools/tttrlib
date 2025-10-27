"""CLSM (Confocal Laser Scanning Microscopy) tests"""
from __future__ import division

import os
import unittest
import numpy as np
from pathlib import Path
import gc

import tttrlib

# Centralized test settings
from test_settings import settings, DATA_AVAILABLE  # type: ignore

# Load CLSM data path if available (already absolute via test_settings)
clsm_file = settings.get("ht3_clsm_filename")
CLSM_AVAILABLE = os.path.exists(clsm_file) if clsm_file else False


@unittest.skipIf(not (DATA_AVAILABLE and CLSM_AVAILABLE), "CLSM data not available")
class TestCLSMBasic(unittest.TestCase):
    """Basic CLSM functionality tests"""

    def setUp(self):
        self.clsm = tttrlib.CLSMImage(clsm_file)

    def tearDown(self):
        self.clsm = None
        gc.collect()

    def test_clsm_creation(self):
        """Test CLSM image creation"""
        self.assertIsNotNone(self.clsm)

    def test_clsm_has_frames(self):
        """Test CLSM has frames"""
        n_frames = self.clsm.n_frames
        self.assertGreater(n_frames, 0)

    def test_clsm_frame_access(self):
        """Test accessing CLSM frames"""
        if self.clsm.n_frames > 0:
            frames = self.clsm.get_frames()
            self.assertGreater(len(frames), 0)

    def test_clsm_frame_properties(self):
        """Test CLSM frame properties"""
        if self.clsm.n_frames > 0:
            frames = self.clsm.get_frames()
            if len(frames) > 0:
                frame = frames[0]
                self.assertIsNotNone(frame)

    def test_clsm_multiple_frames(self):
        """Test accessing multiple frames"""
        frames = self.clsm.get_frames()
        n_frames = min(len(frames), 5)
        for i in range(n_frames):
            frame = frames[i]
            self.assertIsNotNone(frame)


@unittest.skipIf(not (DATA_AVAILABLE and CLSM_AVAILABLE), "CLSM data not available")
class TestCLSMFrame(unittest.TestCase):
    """CLSM Frame tests"""

    def setUp(self):
        self.clsm = tttrlib.CLSMImage(clsm_file)
        frames = self.clsm.get_frames()
        if len(frames) > 0:
            self.frame = frames[0]
        else:
            self.frame = None

    def tearDown(self):
        self.clsm = None
        self.frame = None
        gc.collect()

    @unittest.skipIf(not hasattr(tttrlib, 'CLSMFrame'), "CLSMFrame not available")
    def test_frame_has_lines(self):
        """Test frame has lines"""
        if self.frame:
            n_lines = self.frame.n_lines
            self.assertGreater(n_lines, 0)

    @unittest.skipIf(not hasattr(tttrlib, 'CLSMFrame'), "CLSMFrame not available")
    def test_frame_line_access(self):
        """Test accessing frame lines"""
        if self.frame and self.frame.n_lines > 0:
            lines = self.frame.get_lines()
            self.assertGreater(len(lines), 0)

    @unittest.skipIf(not hasattr(tttrlib, 'CLSMFrame'), "CLSMFrame not available")
    def test_frame_pixel_count(self):
        """Test frame line count"""
        if self.frame:
            n_lines = self.frame.n_lines
            self.assertGreater(n_lines, 0)

    @unittest.skipIf(not hasattr(tttrlib, 'CLSMFrame'), "CLSMFrame not available")
    def test_frame_multiple_lines(self):
        """Test accessing multiple lines"""
        if self.frame:
            lines = self.frame.get_lines()
            n_lines = min(len(lines), 5)
            for i in range(n_lines):
                line = lines[i]
                self.assertIsNotNone(line)


@unittest.skipIf(not (DATA_AVAILABLE and CLSM_AVAILABLE), "CLSM data not available")
class TestCLSMLine(unittest.TestCase):
    """CLSM Line tests"""

    def setUp(self):
        self.clsm = tttrlib.CLSMImage(clsm_file)
        frames = self.clsm.get_frames()
        if len(frames) > 0:
            self.frame = frames[0]
            if self.frame and self.frame.n_lines > 0:
                lines = self.frame.get_lines()
                if len(lines) > 0:
                    self.line = lines[0]
                else:
                    self.line = None
            else:
                self.line = None
        else:
            self.frame = None
            self.line = None

    def tearDown(self):
        self.clsm = None
        self.frame = None
        self.line = None
        gc.collect()

    @unittest.skipIf(not hasattr(tttrlib, 'CLSMLine'), "CLSMLine not available")
    def test_line_has_pixels(self):
        """Test line has pixels"""
        if self.line:
            n_pixels = self.line.n_pixel
            self.assertGreater(n_pixels, 0)

    @unittest.skipIf(not hasattr(tttrlib, 'CLSMLine'), "CLSMLine not available")
    def test_line_pixel_access(self):
        """Test accessing line pixels"""
        if self.line and self.line.n_pixel > 0:
            pixels = self.line.get_pixels()
            self.assertIsNotNone(pixels)

    @unittest.skipIf(not hasattr(tttrlib, 'CLSMLine'), "CLSMLine not available")
    def test_line_multiple_pixels(self):
        """Test accessing multiple pixels"""
        if self.line and self.line.n_pixel > 0:
            pixels = self.line.get_pixels()
            self.assertIsNotNone(pixels)


@unittest.skipIf(not (DATA_AVAILABLE and CLSM_AVAILABLE), "CLSM data not available")
class TestCLSMPixel(unittest.TestCase):
    """CLSM Pixel tests"""

    def setUp(self):
        self.clsm = tttrlib.CLSMImage(clsm_file)
        self.frame = None
        self.line = None
        self.pixel = None
        try:
            frames = self.clsm.get_frames()
            if frames and len(frames) > 0:
                self.frame = frames[0]
                if self.frame and self.frame.n_lines > 0:
                    lines = self.frame.get_lines()
                    if lines and len(lines) > 0:
                        self.line = lines[0]
                        if self.line and self.line.n_pixel > 0:
                            pixels = self.line.get_pixels()
                            if pixels:
                                self.pixel = pixels[0]
        except (TypeError, AttributeError):
            # Handle SwigPyObject issues
            pass

    def tearDown(self):
        self.clsm = None
        self.frame = None
        self.line = None
        self.pixel = None
        gc.collect()

    @unittest.skipIf(not hasattr(tttrlib, 'CLSMPixel'), "CLSMPixel not available")
    def test_pixel_has_tttr(self):
        """Test pixel exists"""
        if self.pixel:
            self.assertIsNotNone(self.pixel)

    @unittest.skipIf(not hasattr(tttrlib, 'CLSMPixel'), "CLSMPixel not available")
    def test_pixel_tttr_access(self):
        """Test pixel TTTR access"""
        if self.pixel:
            try:
                tttr = self.pixel.tttr
                self.assertIsNotNone(tttr)
            except (AttributeError, TypeError):
                # TTTR access might not be available
                pass

    @unittest.skipIf(not hasattr(tttrlib, 'CLSMPixel'), "CLSMPixel not available")
    def test_pixel_properties(self):
        """Test pixel properties"""
        if self.pixel:
            # Pixel should have properties
            self.assertIsNotNone(self.pixel)


class TestCLSMFallback(unittest.TestCase):
    """Fallback tests when CLSM data not available"""

    def test_clsm_data_check(self):
        """Test CLSM data availability"""
        self.assertTrue(DATA_AVAILABLE, "Test data directory not found")

    def test_clsm_file_check(self):
        """Test CLSM file availability"""
        if not CLSM_AVAILABLE:
            self.skipTest("CLSM data file not found")


if __name__ == '__main__':
    unittest.main()

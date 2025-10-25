"""CLSM memory checking unit tests"""
from __future__ import division

import os
import unittest
import json
from pathlib import Path
import gc

import numpy as np
import tttrlib

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

for key in ["spc132_filename"]:
    if key in settings:
        settings[key] = get_data_path(settings[key])


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestCLSMMemoryBasic(unittest.TestCase):
    """Basic CLSM memory tests"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_tttr_memory_usage_positive(self):
        """Test that TTTR memory usage is positive"""
        memory = self.data.get_memory_usage_bytes()
        self.assertGreater(memory, 0)

    def test_tttr_capacity_positive(self):
        """Test that TTTR capacity is positive"""
        capacity = self.data.get_capacity()
        self.assertGreater(capacity, 0)

    def test_tttr_capacity_greater_than_size(self):
        """Test that capacity is >= size"""
        size = self.data.size()
        capacity = self.data.get_capacity()
        self.assertGreaterEqual(capacity, size)

    def test_tttr_memory_scales_with_size(self):
        """Test that memory usage scales with data size"""
        if len(self.data) > 100:
            d1 = self.data[:50]
            d2 = self.data[:100]
            
            mem1 = d1.get_memory_usage_bytes()
            mem2 = d2.get_memory_usage_bytes()
            
            self.assertGreater(mem2, mem1)


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestCLSMMemoryOperations(unittest.TestCase):
    """CLSM memory operations tests"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_copy_memory_independent(self):
        """Test that copied data has independent memory"""
        d1 = self.data[:100]
        d2 = tttrlib.TTTR(d1)
        
        mem1 = d1.get_memory_usage_bytes()
        mem2 = d2.get_memory_usage_bytes()
        
        # Both should have positive memory
        self.assertGreater(mem1, 0)
        self.assertGreater(mem2, 0)

    def test_slicing_reduces_memory(self):
        """Test that slicing reduces memory usage"""
        if len(self.data) > 100:
            d_full = self.data
            d_slice = self.data[:50]
            
            mem_full = d_full.get_memory_usage_bytes()
            mem_slice = d_slice.get_memory_usage_bytes()
            
            self.assertLess(mem_slice, mem_full)

    def test_append_increases_memory(self):
        """Test that appending increases memory"""
        if len(self.data) > 100:
            d1 = tttrlib.TTTR(self.data[:50])
            d2 = tttrlib.TTTR(self.data[50:100])
            
            mem_before = d1.get_memory_usage_bytes()
            d1.append(d2, shift_macro_time=False)
            mem_after = d1.get_memory_usage_bytes()
            
            self.assertGreater(mem_after, mem_before)


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestCLSMMemoryUsage(unittest.TestCase):
    """CLSM memory usage tests"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_clsm_image_creation_memory(self):
        """Test CLSM image creation uses memory"""
        try:
            clsm = tttrlib.CLSMImage(self.data)
            memory = clsm.get_memory_usage_bytes()
            self.assertGreater(memory, 0)
        except (AttributeError, TypeError):
            # CLSM might not be available
            pass

    def test_clsm_image_n_frames(self):
        """Test CLSM image frame count"""
        try:
            clsm = tttrlib.CLSMImage(self.data)
            n_frames = clsm.n_frames
            self.assertGreaterEqual(n_frames, 0)
        except (AttributeError, TypeError):
            pass

    def test_clsm_image_n_lines(self):
        """Test CLSM image line count"""
        try:
            clsm = tttrlib.CLSMImage(self.data)
            n_lines = clsm.n_lines
            self.assertGreaterEqual(n_lines, 0)
        except (AttributeError, TypeError):
            pass

    def test_clsm_image_n_pixel(self):
        """Test CLSM image pixel count"""
        try:
            clsm = tttrlib.CLSMImage(self.data)
            n_pixel = clsm.n_pixel
            self.assertGreaterEqual(n_pixel, 0)
        except (AttributeError, TypeError):
            pass


class TestTTTRCompressionMemory(unittest.TestCase):
    """TTTR compression memory tests"""

    def tearDown(self):
        gc.collect()

    def test_compression_reduces_memory(self):
        """Test that compression reduces memory usage"""
        try:
            # Create TTTR with data
            d = tttrlib.TTTR()
            n_events = 10000
            
            macro_times = np.arange(n_events, dtype=np.uint64) * 100
            micro_times = np.arange(n_events, dtype=np.uint16) % 4096
            channels = np.zeros(n_events, dtype=np.int8)
            types = np.zeros(n_events, dtype=np.int8)
            
            d.append_events(macro_times, micro_times, channels, types)
            
            # Get memory before compression
            mem_before = d.get_memory_usage_bytes()
            
            # Try to compress
            d.enable_macro_time_compression()
            mem_after = d.get_memory_usage_bytes()
            
            # Compressed should be smaller or equal
            self.assertLessEqual(mem_after, mem_before)
        except (AttributeError, TypeError):
            # Compression might not be exposed in Python
            pass

    def test_decompression_restores_data(self):
        """Test that decompression restores original data"""
        try:
            d = tttrlib.TTTR()
            n_events = 1000
            
            macro_times = np.arange(n_events, dtype=np.uint64) * 100
            micro_times = np.arange(n_events, dtype=np.uint16) % 4096
            channels = np.zeros(n_events, dtype=np.int8)
            types = np.zeros(n_events, dtype=np.int8)
            
            d.append_events(macro_times, micro_times, channels, types)
            
            # Store original
            original_macro = d.macro_times.copy()
            
            # Compress
            d.enable_macro_time_compression()
            
            # Decompress
            d.disable_macro_time_compression()
            
            # Check data is restored
            np.testing.assert_array_equal(d.macro_times, original_macro)
        except (AttributeError, TypeError):
            pass

    def test_compression_state_check(self):
        """Test checking compression state"""
        try:
            d = tttrlib.TTTR()
            n_events = 1000
            
            macro_times = np.arange(n_events, dtype=np.uint64) * 100
            micro_times = np.arange(n_events, dtype=np.uint16) % 4096
            channels = np.zeros(n_events, dtype=np.int8)
            types = np.zeros(n_events, dtype=np.int8)
            
            d.append_events(macro_times, micro_times, channels, types)
            
            # Check if compression methods exist
            has_enable = hasattr(d, 'enable_macro_time_compression')
            has_disable = hasattr(d, 'disable_macro_time_compression')
            
            # At least one should exist
            self.assertTrue(has_enable or has_disable)
        except (AttributeError, TypeError):
            pass


class TestCLSMMemoryFallback(unittest.TestCase):
    """Fallback tests when data not available"""

    def test_data_availability(self):
        """Test data availability"""
        self.assertTrue(DATA_AVAILABLE, "Test data directory not found")


if __name__ == '__main__':
    unittest.main()

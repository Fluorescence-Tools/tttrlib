"""TTTR construction and initialization tests"""
from __future__ import division

import os
import unittest
import numpy as np
import gc

import tttrlib

# Centralized test settings
from test_settings import settings, DATA_AVAILABLE  # type: ignore


class TestTTTREmptyConstruction(unittest.TestCase):
    """Empty TTTR construction tests"""

    def tearDown(self):
        gc.collect()

    def test_empty_tttr_creation(self):
        """Test creating empty TTTR"""
        d = tttrlib.TTTR()
        self.assertEqual(len(d), 0)

    def test_empty_tttr_size(self):
        """Test empty TTTR size"""
        d = tttrlib.TTTR()
        self.assertEqual(d.size(), 0)

    def test_empty_tttr_capacity(self):
        """Test empty TTTR capacity"""
        d = tttrlib.TTTR()
        capacity = d.get_capacity()
        self.assertGreaterEqual(capacity, 0)

    def test_empty_tttr_arrays(self):
        """Test empty TTTR arrays"""
        d = tttrlib.TTTR()
        
        self.assertEqual(len(d.macro_times), 0)
        self.assertEqual(len(d.micro_times), 0)
        self.assertEqual(len(d.routing_channels), 0)
        self.assertEqual(len(d.event_types), 0)


class TestTTTRArrayConstruction(unittest.TestCase):
    """TTTR construction from arrays"""

    def tearDown(self):
        gc.collect()

    def test_construct_from_numpy_arrays(self):
        """Test constructing TTTR from numpy arrays"""
        n = 100
        macro_times = np.arange(n, dtype=np.uint64) * 100
        micro_times = np.arange(n, dtype=np.uint16) % 4096
        channels = np.arange(n, dtype=np.int8) % 4
        types = np.zeros(n, dtype=np.int8)
        
        d = tttrlib.TTTR()
        d.append_events(macro_times, micro_times, channels, types)
        
        self.assertEqual(len(d), n)

    def test_construct_preserves_data(self):
        """Test that construction preserves data"""
        n = 50
        macro_times = np.arange(n, dtype=np.uint64) * 100
        micro_times = np.arange(n, dtype=np.uint16) % 4096
        channels = np.zeros(n, dtype=np.int8)
        types = np.zeros(n, dtype=np.int8)
        
        d = tttrlib.TTTR()
        d.append_events(macro_times, micro_times, channels, types)
        
        np.testing.assert_array_equal(d.macro_times, macro_times)
        np.testing.assert_array_equal(d.micro_times, micro_times)

    def test_construct_from_lists(self):
        """Test constructing TTTR from lists"""
        d = tttrlib.TTTR()
        
        for i in range(10):
            d.append_event(i * 100, i % 4096, i % 4, 0)
        
        self.assertEqual(len(d), 10)

    def test_construct_mixed_types(self):
        """Test constructing with mixed data types"""
        d = tttrlib.TTTR()
        
        # Mix of different event types
        for i in range(20):
            event_type = i % 3
            d.append_event(i * 100, i % 4096, i % 4, event_type)
        
        self.assertEqual(len(d), 20)


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestTTTRFileConstruction(unittest.TestCase):
    """TTTR file construction tests"""

    def tearDown(self):
        gc.collect()

    def test_load_file_basic(self):
        """Test loading file"""
        d = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')
        self.assertGreater(len(d), 0)

    def test_load_file_has_data(self):
        """Test loaded file has valid data"""
        d = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')
        
        self.assertGreater(len(d.macro_times), 0)
        self.assertGreater(len(d.micro_times), 0)

    def test_load_file_arrays_consistent(self):
        """Test loaded file arrays are consistent"""
        d = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')
        
        n = len(d)
        self.assertEqual(len(d.macro_times), n)
        self.assertEqual(len(d.micro_times), n)
        self.assertEqual(len(d.routing_channels), n)
        self.assertEqual(len(d.event_types), n)


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestTTTRCopyConstruction(unittest.TestCase):
    """TTTR copy construction tests"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_copy_constructor_basic(self):
        """Test copy constructor"""
        d1 = self.data[:100]
        d2 = tttrlib.TTTR(d1)
        
        self.assertEqual(len(d1), len(d2))

    def test_copy_constructor_data_identical(self):
        """Test copy constructor preserves data"""
        d1 = self.data[:100]
        d2 = tttrlib.TTTR(d1)
        
        np.testing.assert_array_equal(d1.macro_times, d2.macro_times)
        np.testing.assert_array_equal(d1.micro_times, d2.micro_times)

    def test_copy_constructor_independence(self):
        """Test copy constructor creates independent copy"""
        d1 = tttrlib.TTTR(self.data[:100])
        d2 = tttrlib.TTTR(d1)
        
        # Modify d2
        d2.shift_macro_time(1000)
        
        # d1 should be unchanged
        self.assertNotEqual(d1.macro_times[0], d2.macro_times[0])

    def test_copy_from_slice(self):
        """Test copy from slice"""
        if len(self.data) > 200:
            d1 = self.data[50:150]
            d2 = tttrlib.TTTR(d1)
            
            self.assertEqual(len(d2), 100)


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestTTTRSelectionConstruction(unittest.TestCase):
    """TTTR construction from selection"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_construct_from_indices(self):
        """Test constructing from indices"""
        if len(self.data) > 100:
            indices = np.array([0, 10, 20, 30, 40], dtype=np.int32)
            d = self.data[indices]
            
            self.assertEqual(len(d), 5)

    def test_construct_from_indices_preserves_order(self):
        """Test that index construction preserves order"""
        if len(self.data) > 100:
            indices = np.array([40, 30, 20, 10, 0], dtype=np.int32)
            d = self.data[indices]
            
            # Should have same order as indices
            for i, idx in enumerate(indices):
                self.assertEqual(d.macro_times[i], self.data.macro_times[idx])

    def test_construct_from_boolean_mask(self):
        """Test constructing from boolean mask"""
        if len(self.data) > 100:
            mask = np.zeros(len(self.data), dtype=bool)
            mask[::10] = True
            
            indices = np.where(mask)[0].astype(np.int32)
            d = self.data[indices]
            
            self.assertGreater(len(d), 0)


if __name__ == '__main__':
    unittest.main()

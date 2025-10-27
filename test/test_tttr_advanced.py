"""Advanced TTTR functionality tests"""
from __future__ import division

import os
import unittest
import json
import numpy as np
from pathlib import Path
import gc

import tttrlib

# Centralized test settings
from test_settings import settings, DATA_AVAILABLE  # type: ignore


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestTTTRComplexFiltering(unittest.TestCase):
    """Complex filtering scenarios"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_filter_chain_channels_then_count_rate(self):
        """Test chaining channel filter with count rate filter"""
        channels = self.data.get_used_routing_channels()
        if len(channels) > 0:
            # Filter by channel
            filtered1 = self.data.get_tttr_by_channel([channels[0]])
            
            # Then filter by count rate
            filtered2 = filtered1.get_tttr_by_count_rate(10.0e-3, 50)
            
            self.assertGreater(len(filtered2), 0)
            self.assertLessEqual(len(filtered2), len(filtered1))

    def test_filter_by_multiple_channels_then_slice(self):
        """Test filtering by multiple channels then slicing"""
        channels = self.data.get_used_routing_channels()
        if len(channels) > 1:
            # Filter by multiple channels
            filtered = self.data.get_tttr_by_channel(channels[:2])
            
            # Then slice
            sliced = filtered[:100]
            
            self.assertGreater(len(sliced), 0)
            self.assertLessEqual(len(sliced), 100)

    def test_filter_preserves_temporal_order(self):
        """Test that filtering preserves temporal order"""
        channels = self.data.get_used_routing_channels()
        if len(channels) > 0:
            filtered = self.data.get_tttr_by_channel([channels[0]])
            
            if len(filtered) > 1:
                # Check macro times are monotonic
                diffs = np.diff(filtered.macro_times)
                self.assertTrue(np.all(diffs >= 0))

    def test_empty_filter_result(self):
        """Test filtering that results in empty set"""
        # Try to filter by channel that likely doesn't exist
        filtered = self.data.get_tttr_by_channel([127])
        
        # Should return valid TTTR (possibly empty)
        self.assertIsNotNone(filtered)


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestTTTRStatistics(unittest.TestCase):
    """Statistical operations on TTTR data"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_mean_microtime_consistency(self):
        """Test mean microtime is consistent"""
        mean_mt = self.data.get_mean_microtime()
        
        # Mean should be within valid range
        self.assertGreater(mean_mt, 0)
        self.assertLess(mean_mt, 2**16)

    def test_mean_microtime_subset_vs_full(self):
        """Test mean microtime on subset vs full data"""
        if len(self.data) > 100:
            mean_full = self.data.get_mean_microtime()
            
            subset = self.data[:100]
            mean_subset = subset.get_mean_microtime()
            
            # Both should be valid
            self.assertGreater(mean_full, 0)
            self.assertGreater(mean_subset, 0)

    def test_histogram_sum_equals_count(self):
        """Test that histogram sum equals event count"""
        h, t = self.data.get_microtime_histogram(64)
        
        # Sum of histogram should equal number of events
        histogram_sum = np.sum(h)
        self.assertEqual(histogram_sum, len(self.data))

    def test_histogram_bins_cover_range(self):
        """Test that histogram bins are valid"""
        h, t = self.data.get_microtime_histogram(64)
        
        # Time axis should be monotonically increasing
        self.assertGreater(len(t), 0)
        self.assertGreater(len(h), 0)
        self.assertEqual(len(h), len(t))

    def test_histogram_monotonic_time_axis(self):
        """Test that histogram time axis is monotonic"""
        h, t = self.data.get_microtime_histogram(64)
        
        # Time axis should be monotonically increasing
        diffs = np.diff(t)
        self.assertTrue(np.all(diffs > 0))


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestTTTRDataTransformation(unittest.TestCase):
    """Data transformation operations"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_shift_and_verify(self):
        """Test shifting macro times and verify consistency"""
        d = tttrlib.TTTR(self.data[:100])
        original_macro = d.macro_times.copy()
        original_diff = np.diff(original_macro)
        
        d.shift_macro_time(1000)
        
        # Differences should be preserved
        new_diff = np.diff(d.macro_times)
        np.testing.assert_array_equal(original_diff, new_diff)

    def test_shift_negative_and_positive(self):
        """Test shifting with both negative and positive values"""
        d1 = tttrlib.TTTR(self.data[:100])
        d2 = tttrlib.TTTR(self.data[:100])
        
        d1.shift_macro_time(1000)
        d2.shift_macro_time(-500)
        
        # Both should have valid macro times
        self.assertGreater(np.min(d1.macro_times), 0)
        self.assertGreater(np.min(d2.macro_times), 0)

    def test_append_maintains_order(self):
        """Test that append maintains temporal order"""
        if len(self.data) > 200:
            d1 = tttrlib.TTTR(self.data[:100])
            d2 = tttrlib.TTTR(self.data[100:200])
            
            d1.append(d2, shift_macro_time=False)
            
            # Macro times should be monotonic
            diffs = np.diff(d1.macro_times)
            self.assertTrue(np.all(diffs >= 0))

    def test_append_with_shift(self):
        """Test append with macro time shifting"""
        if len(self.data) > 200:
            d1 = tttrlib.TTTR(self.data[:100])
            d2 = tttrlib.TTTR(self.data[100:200])
            
            original_len = len(d1)
            
            d1.append(d2, shift_macro_time=True)
            
            # Length should increase
            self.assertEqual(len(d1), original_len + len(d2))


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestTTTRChannelOperations(unittest.TestCase):
    """Channel-specific operations"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_get_used_channels(self):
        """Test getting used routing channels"""
        channels = self.data.get_used_routing_channels()
        
        self.assertGreater(len(channels), 0)
        self.assertIsInstance(channels, (list, tuple, np.ndarray))

    def test_channel_filter_count(self):
        """Test that channel filtering reduces event count"""
        channels = self.data.get_used_routing_channels()
        
        if len(channels) > 1:
            # Filter to single channel
            filtered = self.data.get_tttr_by_channel([channels[0]])
            
            # Should have fewer events
            self.assertLess(len(filtered), len(self.data))

    def test_all_channels_filter(self):
        """Test filtering by all channels"""
        channels = self.data.get_used_routing_channels()
        
        # Filter by all channels
        filtered = self.data.get_tttr_by_channel(channels)
        
        # Should have all or most events
        self.assertGreaterEqual(len(filtered), len(self.data) * 0.9)

    def test_channel_filter_data_integrity(self):
        """Test that channel filtering preserves data integrity"""
        channels = self.data.get_used_routing_channels()
        
        if len(channels) > 0:
            ch = channels[0]
            filtered = self.data.get_tttr_by_channel([ch])
            
            # All events should have the selected channel
            self.assertTrue(np.all(filtered.routing_channels == ch))


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestTTTRMemoryOperations(unittest.TestCase):
    """Memory-related operations"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_size_vs_capacity(self):
        """Test size vs capacity relationship"""
        size = self.data.size()
        capacity = self.data.get_capacity()
        
        self.assertLessEqual(size, capacity)
        self.assertGreater(capacity, 0)

    def test_memory_usage_positive(self):
        """Test that memory usage is positive"""
        memory = self.data.get_memory_usage_bytes()
        
        self.assertGreater(memory, 0)

    def test_memory_scales_with_size(self):
        """Test that memory usage scales with data size"""
        if len(self.data) > 100:
            d1 = self.data[:50]
            d2 = self.data[:100]
            
            mem1 = d1.get_memory_usage_bytes()
            mem2 = d2.get_memory_usage_bytes()
            
            # Larger dataset should use more memory
            self.assertGreater(mem2, mem1)

    def test_size_method(self):
        """Test size() method"""
        size = self.data.size()
        
        self.assertEqual(size, len(self.data))
        self.assertGreater(size, 0)


if __name__ == '__main__':
    unittest.main()

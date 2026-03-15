"""Extended TTTR operations tests"""
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
class TestTTTRFiltering(unittest.TestCase):
    """TTTR filtering operations"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_filter_by_single_channel(self):
        """Test filtering by single channel"""
        channels = self.data.get_used_routing_channels()
        if len(channels) > 0:
            ch = channels[0]
            filtered = self.data.get_tttr_by_channel([ch])
            self.assertGreater(len(filtered), 0)
            self.assertTrue(np.all(filtered.routing_channels == ch))

    def test_filter_by_multiple_channels(self):
        """Test filtering by multiple channels"""
        channels = self.data.get_used_routing_channels()
        if len(channels) > 1:
            ch_subset = channels[:min(2, len(channels))]
            filtered = self.data.get_tttr_by_channel(ch_subset)
            self.assertGreater(len(filtered), 0)

    def test_filter_preserves_order(self):
        """Test that filtering preserves event order"""
        channels = self.data.get_used_routing_channels()
        if len(channels) > 0:
            filtered = self.data.get_tttr_by_channel([channels[0]])
            # Macro times should be in ascending order
            if len(filtered) > 1:
                self.assertTrue(np.all(np.diff(filtered.macro_times) >= 0))

    def test_filter_empty_result(self):
        """Test filtering with channel that doesn't exist"""
        # Try to filter by a channel number that likely doesn't exist
        filtered = self.data.get_tttr_by_channel([127])
        # Should return empty or valid TTTR
        self.assertIsNotNone(filtered)

    def test_count_rate_filter_basic(self):
        """Test count rate filtering"""
        time_window = 10.0e-3
        n_ph_max = 50
        filtered = self.data.get_tttr_by_count_rate(time_window, n_ph_max)
        self.assertGreater(len(filtered), 0)

    def test_count_rate_filter_inverted(self):
        """Test inverted count rate filtering"""
        time_window = 10.0e-3
        n_ph_max = 50
        filtered = self.data.get_tttr_by_count_rate(time_window, n_ph_max, invert=True)
        self.assertGreater(len(filtered), 0)

    def test_count_rate_different_windows(self):
        """Test count rate with different time windows"""
        for time_window in [1.0e-3, 5.0e-3, 10.0e-3]:
            filtered = self.data.get_tttr_by_count_rate(time_window, 50)
            self.assertGreater(len(filtered), 0)


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestTTTRHistograms(unittest.TestCase):
    """TTTR histogram operations"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_microtime_histogram_basic(self):
        """Test basic microtime histogram"""
        h, t = self.data.get_microtime_histogram(64)
        self.assertGreater(len(h), 0)
        self.assertEqual(len(h), len(t))

    def test_microtime_histogram_various_bins(self):
        """Test microtime histogram with various bin counts"""
        for n_bins in [8, 16, 32, 64]:
            h, t = self.data.get_microtime_histogram(n_bins)
            self.assertGreater(len(h), 0)
            self.assertEqual(len(h), len(t))

    def test_microtime_histogram_values(self):
        """Test microtime histogram values are positive"""
        h, t = self.data.get_microtime_histogram(32)
        self.assertTrue(np.all(h >= 0))

    def test_mean_microtime_basic(self):
        """Test mean microtime calculation"""
        mean_mt = self.data.get_mean_microtime()
        self.assertGreater(mean_mt, 0)

    def test_mean_microtime_subset(self):
        """Test mean microtime on subset"""
        subset = self.data[:100]
        mean_mt = subset.get_mean_microtime()
        self.assertGreater(mean_mt, 0)


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestTTTRMacroTimeOperations(unittest.TestCase):
    """TTTR macro time operations"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_macro_time_range(self):
        """Test macro time range"""
        if len(self.data) > 0:
            min_mt = np.min(self.data.macro_times)
            max_mt = np.max(self.data.macro_times)
            self.assertLess(min_mt, max_mt)

    def test_macro_time_shift_preserves_range(self):
        """Test that shifting preserves time range"""
        d = tttrlib.TTTR(self.data[:100])
        original_range = np.max(d.macro_times) - np.min(d.macro_times)
        
        d.shift_macro_time(1000)
        new_range = np.max(d.macro_times) - np.min(d.macro_times)
        
        self.assertEqual(original_range, new_range)

    def test_macro_time_monotonic(self):
        """Test that macro times are monotonically increasing"""
        if len(self.data) > 1:
            diffs = np.diff(self.data.macro_times)
            self.assertTrue(np.all(diffs >= 0))

    def test_macro_time_resolution(self):
        """Test macro time resolution from header"""
        resolution = self.data.header.macro_time_resolution
        self.assertGreater(resolution, 0)


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestTTTRMicroTimeOperations(unittest.TestCase):
    """TTTR micro time operations"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_micro_time_range(self):
        """Test micro time range"""
        if len(self.data) > 0:
            min_mt = np.min(self.data.micro_times)
            max_mt = np.max(self.data.micro_times)
            self.assertGreaterEqual(min_mt, 0)
            self.assertGreater(max_mt, min_mt)

    def test_micro_time_resolution(self):
        """Test micro time resolution from header"""
        resolution = self.data.header.micro_time_resolution
        self.assertGreater(resolution, 0)

    def test_micro_time_channels(self):
        """Test number of micro time channels"""
        n_channels = self.data.header.number_of_micro_time_channels
        self.assertGreater(n_channels, 0)

    def test_micro_time_values_in_range(self):
        """Test micro time values are in valid range"""
        micro_times = self.data.micro_times
        max_microtime = 2 ** 16  # Typical max for 16-bit
        self.assertTrue(np.all(micro_times < max_microtime))


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestTTTRDataIntegrity(unittest.TestCase):
    """TTTR data integrity checks"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_array_lengths_match(self):
        """Test that all arrays have same length"""
        n = len(self.data)
        self.assertEqual(len(self.data.macro_times), n)
        self.assertEqual(len(self.data.micro_times), n)
        self.assertEqual(len(self.data.routing_channels), n)
        self.assertEqual(len(self.data.event_types), n)

    def test_copy_independence(self):
        """Test that copies are independent"""
        d1 = self.data[:100]
        d2 = tttrlib.TTTR(d1)
        
        # Modify d2 (if possible)
        d2.shift_macro_time(1000)
        
        # d1 should be unchanged
        self.assertNotEqual(d1.macro_times[0], d2.macro_times[0])

    def test_slicing_consistency(self):
        """Test slicing consistency"""
        indices = np.array([0, 10, 20, 30], dtype=np.int32)
        sliced = self.data[indices]
        
        for i, idx in enumerate(indices):
            self.assertEqual(sliced.macro_times[i], self.data.macro_times[idx])
            self.assertEqual(sliced.micro_times[i], self.data.micro_times[idx])

    def test_append_consistency(self):
        """Test append consistency"""
        d1 = tttrlib.TTTR(self.data[:50])
        d2 = tttrlib.TTTR(self.data[50:100])
        
        d1.append(d2, shift_macro_time=False)
        
        self.assertEqual(len(d1), 100)
        np.testing.assert_array_equal(d1.macro_times[:50], self.data.macro_times[:50])


if __name__ == '__main__':
    unittest.main()

#!/usr/bin/env python3
"""
Unit tests for enhanced BurstFilter dynamic filtering capabilities
"""
import unittest
import numpy as np

import tttrlib

# Centralized test settings
from test_settings import settings, DATA_AVAILABLE, get_data_path, DATA_ROOT  # type: ignore


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found, skipping enhanced BurstFilter tests")
class TestBurstFilterEnhanced(unittest.TestCase):

    def setUp(self):
        """Set up test data for each test"""
        self.data_file = settings["spc132_filename"]
        self.data = tttrlib.TTTR(self.data_file, 'SPC-130')

    def test_burstfilter_enhanced_methods_exist(self):
        """Test that enhanced BurstFilter methods are available"""
        burst_filter = tttrlib.BurstFilter(self.data)

        # Check that enhanced methods exist
        required_methods = [
            'reset_to_raw_bursts', 'reapply_filters', 'clear_filters',
            'find_bursts', 'filter_by_size', 'filter_by_duration',
            'merge_bursts', 'get_bursts', 'set_burst_parameters'
        ]

        for method in required_methods:
            self.assertTrue(hasattr(burst_filter, method),
                          f"Method {method} not found in BurstFilter")

    def test_dynamic_filtering_workflow(self):
        """Test the enhanced dynamic filtering workflow"""
        bf = tttrlib.BurstFilter(self.data)

        # 1. Set burst search parameters
        bf.set_burst_parameters(min_photons=30, window_photons=10, window_time_max=1e-3)

        # 2. Find initial bursts (stored as raw_bursts)
        bursts = bf.find_bursts()
        self.assertIsInstance(bursts, np.ndarray)
        initial_burst_count = len(bf.get_bursts())

        # 3. Apply filters (these now track state)
        bf.filter_by_size(min_size=50, max_size=1000)
        after_size_filter = len(bf.get_bursts())

        # 4. Apply duration filter
        bf.filter_by_duration(min_duration=1e-4, max_duration=1e-2)
        after_duration_filter = len(bf.get_bursts())

        # 5. Change filter parameters - bursts can "reappear"
        bf.filter_by_size(min_size=25, max_size=1000)  # Lower threshold
        after_changed_size = len(bf.get_bursts())

        # Should potentially have more bursts with lower threshold
        self.assertGreaterEqual(after_changed_size, after_duration_filter)

        # 6. Reapply entire pipeline with updated parameters
        bf.reapply_filters()  # Reapplies all filters with current settings

        # 7. Reset to raw bursts if needed
        bf.reset_to_raw_bursts()
        after_reset = len(bf.get_bursts())
        self.assertEqual(after_reset, initial_burst_count)

        # 8. Clear all filters
        bf.clear_filters()
        after_clear = len(bf.get_bursts())
        self.assertEqual(after_clear, initial_burst_count)

    def test_filter_reapplication(self):
        """Test that reapply_filters works correctly"""
        bf = tttrlib.BurstFilter(self.data)
        bf.set_burst_parameters(min_photons=30, window_photons=10, window_time_max=1e-3)

        # Find bursts
        bf.find_bursts()
        initial_count = len(bf.get_bursts())

        # Apply a filter
        bf.filter_by_size(min_size=100, max_size=1000)
        filtered_count = len(bf.get_bursts())
        self.assertLessEqual(filtered_count, initial_count)

        # Change parameters and reapply
        bf.filter_by_size(min_size=50, max_size=1000)  # Less restrictive
        bf.reapply_filters()
        reapply_count = len(bf.get_bursts())
        self.assertGreaterEqual(reapply_count, filtered_count)

    def test_reset_to_raw_bursts(self):
        """Test reset_to_raw_bursts functionality"""
        bf = tttrlib.BurstFilter(self.data)
        bf.set_burst_parameters(min_photons=30, window_photons=10, window_time_max=1e-3)

        # Find bursts
        bf.find_bursts()
        original_count = len(bf.get_bursts())

        # Apply filters
        bf.filter_by_size(min_size=100, max_size=1000)
        bf.filter_by_duration(min_duration=1e-3, max_duration=1e-1)
        filtered_count = len(bf.get_bursts())

        # Reset to raw bursts
        bf.reset_to_raw_bursts()
        reset_count = len(bf.get_bursts())

        self.assertEqual(reset_count, original_count)
        self.assertNotEqual(reset_count, filtered_count)

    def test_clear_filters(self):
        """Test clear_filters functionality"""
        bf = tttrlib.BurstFilter(self.data)
        bf.set_burst_parameters(min_photons=30, window_photons=10, window_time_max=1e-3)

        # Find bursts and apply filters
        bf.find_bursts()
        original_count = len(bf.get_bursts())

        bf.filter_by_size(min_size=100, max_size=1000)
        bf.filter_by_duration(min_duration=1e-3, max_duration=1e-1)
        filtered_count = len(bf.get_bursts())

        # Clear filters
        bf.clear_filters()
        cleared_count = len(bf.get_bursts())

        self.assertEqual(cleared_count, original_count)
        self.assertNotEqual(cleared_count, filtered_count)

    def test_merge_bursts(self):
        """Test merge_bursts functionality"""
        bf = tttrlib.BurstFilter(self.data)
        bf.set_burst_parameters(min_photons=20, window_photons=5, window_time_max=1e-3)

        # Find bursts
        bf.find_bursts()
        before_merge = len(bf.get_bursts())

        # Merge nearby bursts
        bf.merge_bursts(max_gap=10)
        after_merge = len(bf.get_bursts())

        # Merge should not increase burst count
        self.assertLessEqual(after_merge, before_merge)


if __name__ == '__main__':
    unittest.main()

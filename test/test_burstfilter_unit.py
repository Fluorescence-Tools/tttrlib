#!/usr/bin/env python3
"""
Unit tests for BurstFilter functionality
"""
import unittest
import numpy as np

import tttrlib

# Centralized test settings
from test_settings import settings, DATA_AVAILABLE, get_data_path, DATA_ROOT  # type: ignore


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found, skipping BurstFilter tests")
class TestBurstFilter(unittest.TestCase):

    def setUp(self):
        """Set up test data for each test"""
        self.data_file = settings["spc132_filename"]
        self.data = tttrlib.TTTR(self.data_file, 'SPC-130')

    def test_burstfilter_creation(self):
        """Test basic BurstFilter creation and parameter setting"""
        burst_filter = tttrlib.BurstFilter(self.data)
        self.assertIsNotNone(burst_filter)

        # Test parameter setting
        burst_filter.set_burst_parameters(
            min_photons=30,
            window_photons=10,
            window_time_max=0.5e-3
        )
        # If there were getters, we would test them here
        # For now, just verify the object is still valid
        self.assertIsNotNone(burst_filter)

    def test_find_bursts(self):
        """Test find_bursts method"""
        burst_filter = tttrlib.BurstFilter(self.data)
        burst_filter.set_burst_parameters(
            min_photons=30,
            window_photons=10,
            window_time_max=0.5e-3
        )

        bursts = burst_filter.find_bursts()
        self.assertIsInstance(bursts, np.ndarray)
        self.assertGreater(len(bursts), 0)
        self.assertEqual(bursts.ndim, 1)  # Should be 1D array

    def test_filter_by_size(self):
        """Test filter_by_size method"""
        burst_filter = tttrlib.BurstFilter(self.data)
        burst_filter.set_burst_parameters(
            min_photons=30,
            window_photons=10,
            window_time_max=0.5e-3
        )

        # First find bursts
        burst_filter.find_bursts()

        # Apply size filter
        filtered = burst_filter.filter_by_size(50, 1000)
        self.assertIsInstance(filtered, np.ndarray)

    def test_get_bursts(self):
        """Test get_bursts method"""
        burst_filter = tttrlib.BurstFilter(self.data)
        burst_filter.set_burst_parameters(
            min_photons=30,
            window_photons=10,
            window_time_max=0.5e-3
        )

        # Find bursts first
        burst_filter.find_bursts()

        # Get current bursts
        current = burst_filter.get_bursts()
        self.assertIsInstance(current, np.ndarray)

    def test_burst_parameters_edge_cases(self):
        """Test burst filter with different parameter combinations"""
        burst_filter = tttrlib.BurstFilter(self.data)

        # Test with minimal parameters
        burst_filter.set_burst_parameters(
            min_photons=10,
            window_photons=5,
            window_time_max=1e-3
        )
        bursts = burst_filter.find_bursts()
        self.assertIsInstance(bursts, np.ndarray)

        # Test with larger window
        burst_filter.set_burst_parameters(
            min_photons=50,
            window_photons=20,
            window_time_max=2e-3
        )
        bursts = burst_filter.find_bursts()
        self.assertIsInstance(bursts, np.ndarray)


if __name__ == '__main__':
    unittest.main()

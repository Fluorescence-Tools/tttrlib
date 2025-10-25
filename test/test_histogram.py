"""Comprehensive histogram tests"""
from __future__ import division

import os
import unittest
import json
import numpy as np
from pathlib import Path
import gc

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
class TestHistogramBasic(unittest.TestCase):
    """Basic histogram functionality tests"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_histogram_returns_two_arrays(self):
        """Test that histogram returns two arrays"""
        h, t = self.data.get_microtime_histogram(64)
        
        self.assertIsInstance(h, np.ndarray)
        self.assertIsInstance(t, np.ndarray)

    def test_histogram_arrays_same_length(self):
        """Test that histogram and time arrays have same length"""
        h, t = self.data.get_microtime_histogram(64)
        
        self.assertEqual(len(h), len(t))

    def test_histogram_arrays_not_empty(self):
        """Test that histogram arrays are not empty"""
        h, t = self.data.get_microtime_histogram(64)
        
        self.assertGreater(len(h), 0)
        self.assertGreater(len(t), 0)

    def test_histogram_values_non_negative(self):
        """Test that histogram values are non-negative"""
        h, t = self.data.get_microtime_histogram(64)
        
        self.assertTrue(np.all(h >= 0))

    def test_histogram_time_axis_monotonic(self):
        """Test that time axis is monotonically increasing"""
        h, t = self.data.get_microtime_histogram(64)
        
        if len(t) > 1:
            diffs = np.diff(t)
            self.assertTrue(np.all(diffs > 0))


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestHistogramBinCounts(unittest.TestCase):
    """Histogram bin count tests"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_histogram_sum_equals_event_count(self):
        """Test that histogram sum equals total event count"""
        h, t = self.data.get_microtime_histogram(64)
        
        histogram_sum = np.sum(h)
        self.assertEqual(histogram_sum, len(self.data))

    def test_histogram_sum_equals_event_count_various_bins(self):
        """Test histogram sum for various bin counts"""
        for n_bins in [8, 16, 32, 64, 128]:
            h, t = self.data.get_microtime_histogram(n_bins)
            histogram_sum = np.sum(h)
            self.assertEqual(histogram_sum, len(self.data))

    def test_histogram_no_negative_bins(self):
        """Test that no histogram bins are negative"""
        h, t = self.data.get_microtime_histogram(64)
        
        self.assertTrue(np.all(h >= 0))
        self.assertFalse(np.any(h < 0))

    def test_histogram_bins_are_integers(self):
        """Test that histogram bins contain integer counts"""
        h, t = self.data.get_microtime_histogram(64)
        
        # Histogram values should be integers (or very close to integers)
        self.assertTrue(np.allclose(h, np.round(h)))


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestHistogramDataRange(unittest.TestCase):
    """Histogram data range tests"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_histogram_covers_data_range(self):
        """Test that histogram has valid range"""
        h, t = self.data.get_microtime_histogram(64)
        
        # Time axis should be monotonically increasing
        self.assertGreater(len(t), 0)
        if len(t) > 1:
            diffs = np.diff(t)
            self.assertTrue(np.all(diffs > 0))

    def test_histogram_time_axis_reasonable(self):
        """Test that time axis values are reasonable"""
        h, t = self.data.get_microtime_histogram(64)
        
        # Time values should be positive or zero
        self.assertTrue(np.all(t >= 0))
        
        # Time values should be within reasonable range (16-bit)
        self.assertTrue(np.all(t < 2**16))

    def test_histogram_bin_width_consistent(self):
        """Test that bin widths are reasonably consistent"""
        h, t = self.data.get_microtime_histogram(64)
        
        if len(t) > 1:
            bin_widths = np.diff(t)
            # Check that bin widths don't vary too much
            mean_width = np.mean(bin_widths)
            max_deviation = np.max(np.abs(bin_widths - mean_width))
            
            # Allow up to 50% deviation
            self.assertLess(max_deviation, mean_width * 0.5)


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestHistogramVariousBins(unittest.TestCase):
    """Histogram tests with various bin counts"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_histogram_8_bins(self):
        """Test histogram with 8 bins"""
        h, t = self.data.get_microtime_histogram(8)
        self.assertEqual(np.sum(h), len(self.data))

    def test_histogram_16_bins(self):
        """Test histogram with 16 bins"""
        h, t = self.data.get_microtime_histogram(16)
        self.assertEqual(np.sum(h), len(self.data))

    def test_histogram_32_bins(self):
        """Test histogram with 32 bins"""
        h, t = self.data.get_microtime_histogram(32)
        self.assertEqual(np.sum(h), len(self.data))

    def test_histogram_64_bins(self):
        """Test histogram with 64 bins"""
        h, t = self.data.get_microtime_histogram(64)
        self.assertEqual(np.sum(h), len(self.data))

    def test_histogram_128_bins(self):
        """Test histogram with 128 bins"""
        h, t = self.data.get_microtime_histogram(128)
        self.assertEqual(np.sum(h), len(self.data))

    def test_histogram_256_bins(self):
        """Test histogram with 256 bins"""
        h, t = self.data.get_microtime_histogram(256)
        self.assertEqual(np.sum(h), len(self.data))


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestHistogramSubsets(unittest.TestCase):
    """Histogram tests on data subsets"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_histogram_full_data(self):
        """Test histogram on full dataset"""
        h, t = self.data.get_microtime_histogram(64)
        self.assertEqual(np.sum(h), len(self.data))

    def test_histogram_first_half(self):
        """Test histogram on first half of data"""
        if len(self.data) > 100:
            subset = self.data[:len(self.data)//2]
            h, t = subset.get_microtime_histogram(64)
            self.assertEqual(np.sum(h), len(subset))

    def test_histogram_second_half(self):
        """Test histogram on second half of data"""
        if len(self.data) > 100:
            subset = self.data[len(self.data)//2:]
            h, t = subset.get_microtime_histogram(64)
            self.assertEqual(np.sum(h), len(subset))

    def test_histogram_small_subset(self):
        """Test histogram on small subset"""
        if len(self.data) > 50:
            subset = self.data[:50]
            h, t = subset.get_microtime_histogram(64)
            self.assertEqual(np.sum(h), len(subset))

    def test_histogram_large_subset(self):
        """Test histogram on large subset"""
        if len(self.data) > 1000:
            subset = self.data[:1000]
            h, t = subset.get_microtime_histogram(64)
            self.assertEqual(np.sum(h), len(subset))


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestHistogramConsistency(unittest.TestCase):
    """Histogram consistency tests"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_histogram_repeated_calls_same_result(self):
        """Test that repeated calls give same result"""
        h1, t1 = self.data.get_microtime_histogram(64)
        h2, t2 = self.data.get_microtime_histogram(64)
        
        np.testing.assert_array_equal(h1, h2)
        np.testing.assert_array_equal(t1, t2)

    def test_histogram_different_bin_counts_same_sum(self):
        """Test that different bin counts give same total sum"""
        sums = []
        for n_bins in [8, 16, 32, 64, 128]:
            h, t = self.data.get_microtime_histogram(n_bins)
            sums.append(np.sum(h))
        
        # All sums should be equal
        self.assertTrue(all(s == sums[0] for s in sums))

    def test_histogram_copy_data_same_histogram(self):
        """Test that copy of data gives same histogram"""
        data_copy = tttrlib.TTTR(self.data)
        
        h1, t1 = self.data.get_microtime_histogram(64)
        h2, t2 = data_copy.get_microtime_histogram(64)
        
        np.testing.assert_array_equal(h1, h2)


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestMeanMicrotime(unittest.TestCase):
    """Mean microtime tests"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_mean_microtime_valid(self):
        """Test that mean microtime is valid"""
        mean_mt = self.data.get_mean_microtime()
        
        self.assertGreater(mean_mt, 0)
        self.assertLess(mean_mt, 2**16)

    def test_mean_microtime_within_range(self):
        """Test that mean microtime is positive"""
        mean_mt = self.data.get_mean_microtime()
        
        # Mean should be positive
        self.assertGreater(mean_mt, 0)

    def test_mean_microtime_subset(self):
        """Test mean microtime on subset"""
        if len(self.data) > 100:
            subset = self.data[:100]
            mean_mt = subset.get_mean_microtime()
            
            self.assertGreater(mean_mt, 0)
            self.assertLess(mean_mt, 2**16)

    def test_mean_microtime_repeated_calls(self):
        """Test that repeated calls give same result"""
        mean1 = self.data.get_mean_microtime()
        mean2 = self.data.get_mean_microtime()
        
        self.assertEqual(mean1, mean2)


if __name__ == '__main__':
    unittest.main()

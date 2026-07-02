"""SWIG-exposed TTTRMask tests - split for memory efficiency"""
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
class TestTTTRMaskSWIGCoverage(unittest.TestCase):
    """TTTRMask SWIG-exposed methods"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_mask_flip(self):
        mask = tttrlib.TTTRMask()
        mask.set_mask([True, False, True, False, True])
        original = mask.get_mask()
        self.assertListEqual(list(original), [1, 0, 1, 0, 1])
        mask.flip()
        flipped = mask.get_mask()
        self.assertListEqual(list(flipped), [0, 1, 0, 1, 0])

    def test_mask_get_indices(self):
        mask = tttrlib.TTTRMask()
        mask.set_mask([True, False, True, False, True, False])
        selected = mask.get_indices(selected=True)
        self.assertEqual(list(selected), [1, 3, 5])

    def test_mask_select_microtime_ranges(self):
        mask = tttrlib.TTTRMask()
        mask.set_tttr(self.data)
        micro_time_ranges = [(100, 200)]
        mask.select_microtime_ranges(self.data, micro_time_ranges)
        self.assertEqual(mask.size(), len(self.data))

    def test_mask_select_count_rate(self):
        mask = tttrlib.TTTRMask()
        mask.set_tttr(self.data)
        mask.select_count_rate(self.data, 10.0e-3, 60, invert=False)
        self.assertEqual(mask.size(), len(self.data))


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestTTTRMaskAdvanced(unittest.TestCase):
    """Advanced TTTRMask operations"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_mask_flip_twice(self):
        mask = tttrlib.TTTRMask()
        mask.set_mask([True, False, True, False])
        original = mask.get_mask().copy()
        mask.flip()
        mask.flip()
        flipped_twice = mask.get_mask()
        np.testing.assert_array_equal(original, flipped_twice)

    def test_mask_microtime_boundaries(self):
        mask = tttrlib.TTTRMask()
        mask.set_tttr(self.data)
        min_mt = int(np.min(self.data.micro_times))
        max_mt = int(np.max(self.data.micro_times))
        mask.select_microtime_ranges(self.data, [(min_mt, max_mt)])
        self.assertEqual(mask.size(), len(self.data))

    def test_mask_count_rate_small_window(self):
        mask = tttrlib.TTTRMask()
        mask.set_tttr(self.data)
        mask.select_count_rate(self.data, 1.0e-6, 1, invert=False)
        self.assertEqual(mask.size(), len(self.data))

    def test_mask_count_rate_large_window(self):
        mask = tttrlib.TTTRMask()
        mask.set_tttr(self.data)
        mask.select_count_rate(self.data, 1.0, 1000, invert=False)
        self.assertEqual(mask.size(), len(self.data))


class TestTTTRMaskBasic(unittest.TestCase):
    """Basic TTTRMask operations without data"""

    def tearDown(self):
        gc.collect()

    def test_mask_empty_creation(self):
        mask = tttrlib.TTTRMask()
        self.assertEqual(mask.size(), 0)

    def test_mask_set_get_simple(self):
        mask = tttrlib.TTTRMask()
        mask.set_mask([True, False, True])
        result = mask.get_mask()
        self.assertEqual(list(result), [1, 0, 1])

    def test_mask_flip_empty(self):
        mask = tttrlib.TTTRMask()
        mask.flip()
        self.assertEqual(mask.size(), 0)

    def test_mask_all_true(self):
        mask = tttrlib.TTTRMask()
        mask.set_mask([True, True, True, True])
        indices = mask.get_indices(selected=False)
        self.assertEqual(len(indices), 4)

    def test_mask_all_false(self):
        mask = tttrlib.TTTRMask()
        mask.set_mask([False, False, False, False])
        indices = mask.get_indices(selected=True)
        self.assertEqual(len(indices), 4)


if __name__ == '__main__':
    unittest.main()

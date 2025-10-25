"""SWIG-exposed TTTRRange tests - split for memory efficiency"""
from __future__ import division

import unittest
import gc
import numpy as np

import tttrlib


class TestTTTRRangeSWIGCoverage(unittest.TestCase):
    """TTTRRange SWIG-exposed methods"""

    def tearDown(self):
        gc.collect()

    def test_range_shrink_to_fit(self):
        r = tttrlib.TTTRRange()
        for i in range(100):
            r.insert(i)
        r.shrink_to_fit()
        indices = list(r.tttr_indices)
        self.assertEqual(len(indices), 100)

    def test_range_operator_add_assign(self):
        r1 = tttrlib.TTTRRange()
        r1.insert(1)
        r1.insert(2)
        r2 = tttrlib.TTTRRange()
        r2.insert(4)
        r1 += r2
        indices = list(r1.tttr_indices)
        self.assertEqual(len(indices), 3)

    def test_range_copy_assignment(self):
        r1 = tttrlib.TTTRRange()
        r1.insert(1)
        r1.insert(2)
        r2 = tttrlib.TTTRRange()
        r2 = r1
        self.assertEqual(r1, r2)

    def test_range_get_tttr_indices(self):
        r = tttrlib.TTTRRange()
        r.insert(5)
        r.insert(10)
        indices = r.get_tttr_indices()
        np.testing.assert_array_equal(indices, [5, 10])

    def test_range_size_method(self):
        r = tttrlib.TTTRRange()
        self.assertEqual(r.size(), 0)
        r.insert(1)
        self.assertEqual(r.size(), 1)


class TestTTTRRangeAdvanced(unittest.TestCase):
    """Advanced TTTRRange operations"""

    def tearDown(self):
        gc.collect()

    def test_range_insert_random_order(self):
        r = tttrlib.TTTRRange()
        values = [50, 10, 100, 5, 75, 25, 90]
        for v in values:
            r.insert(v)
        indices = list(r.tttr_indices)
        self.assertEqual(indices, sorted(indices))

    def test_range_equality_after_operations(self):
        r1 = tttrlib.TTTRRange()
        for i in range(5):
            r1.insert(i)
        r2 = tttrlib.TTTRRange()
        r2 = r1
        self.assertEqual(r1, r2)
        self.assertFalse(r1 != r2)

    def test_range_strip_partial(self):
        r = tttrlib.TTTRRange()
        for i in range(10):
            r.insert(i)
        to_remove = [1, 3, 5]
        r.strip(to_remove)
        indices = list(r.tttr_indices)
        for val in to_remove:
            self.assertNotIn(val, indices)


class TestTTTRRangeBasic(unittest.TestCase):
    """Basic TTTRRange operations"""

    def tearDown(self):
        gc.collect()

    def test_range_empty_creation(self):
        r = tttrlib.TTTRRange()
        self.assertEqual(r.size(), 0)

    def test_range_single_insert(self):
        r = tttrlib.TTTRRange()
        r.insert(5)
        self.assertEqual(r.size(), 1)
        self.assertEqual(r.start, 5)
        self.assertEqual(r.stop, 5)

    def test_range_multiple_inserts(self):
        r = tttrlib.TTTRRange()
        for i in [1, 3, 2, 5, 4]:
            r.insert(i)
        indices = list(r.tttr_indices)
        self.assertEqual(indices, [1, 2, 3, 4, 5])

    def test_range_clear(self):
        r = tttrlib.TTTRRange()
        r.insert(1)
        r.insert(2)
        r.clear()
        self.assertEqual(r.size(), 0)

    def test_range_start_stop_properties(self):
        r = tttrlib.TTTRRange(10, 20)
        self.assertEqual(r.start, 10)
        self.assertEqual(r.stop, 20)


if __name__ == '__main__':
    unittest.main()

"""TTTR selection and indexing tests"""
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
class TestTTTRIndexing(unittest.TestCase):
    """TTTR indexing operations"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_single_index_access(self):
        """Test single index access"""
        if len(self.data) > 0:
            item = self.data[0]
            self.assertEqual(len(item), 1)

    def test_negative_index_access(self):
        """Test negative index access"""
        if len(self.data) > 0:
            item = self.data[-1]
            self.assertEqual(len(item), 1)

    def test_slice_basic(self):
        """Test basic slicing"""
        if len(self.data) > 10:
            item = self.data[0:10]
            self.assertEqual(len(item), 10)

    def test_slice_with_step(self):
        """Test slicing with step"""
        if len(self.data) > 20:
            item = self.data[0:20:2]
            self.assertEqual(len(item), 10)

    def test_slice_negative_step(self):
        """Test slicing with negative step"""
        if len(self.data) > 20:
            item = self.data[20:0:-1]
            self.assertEqual(len(item), 20)

    def test_array_indexing(self):
        """Test array indexing"""
        if len(self.data) > 10:
            indices = np.array([0, 2, 4, 6, 8], dtype=np.int32)
            item = self.data[indices]
            self.assertEqual(len(item), 5)

    def test_tuple_indexing(self):
        """Test tuple indexing"""
        if len(self.data) > 10:
            item = self.data[(0, 2, 4, 6, 8)]
            self.assertEqual(len(item), 5)

    def test_list_indexing(self):
        """Test list indexing via numpy array"""
        if len(self.data) > 10:
            indices = np.array([0, 2, 4, 6, 8], dtype=np.int32)
            item = self.data[indices]
            self.assertEqual(len(item), 5)


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestTTTRSelection(unittest.TestCase):
    """TTTR selection operations"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_selection_by_channel(self):
        """Test selection by channel"""
        channels = self.data.get_used_routing_channels()
        if len(channels) > 0:
            selected = self.data.get_tttr_by_channel([channels[0]])
            self.assertGreater(len(selected), 0)

    def test_selection_by_multiple_channels(self):
        """Test selection by multiple channels"""
        channels = self.data.get_used_routing_channels()
        if len(channels) > 1:
            selected = self.data.get_tttr_by_channel(channels[:2])
            self.assertGreater(len(selected), 0)

    def test_selection_preserves_data(self):
        """Test that selection preserves data integrity"""
        if len(self.data) > 100:
            indices = np.array([0, 10, 20, 30, 40], dtype=np.int32)
            selected = self.data[indices]
            
            for i, idx in enumerate(indices):
                self.assertEqual(selected.macro_times[i], self.data.macro_times[idx])
                self.assertEqual(selected.micro_times[i], self.data.micro_times[idx])

    def test_empty_selection(self):
        """Test empty selection"""
        indices = np.array([], dtype=np.int32)
        selected = self.data[indices]
        self.assertEqual(len(selected), 0)

    def test_duplicate_indices(self):
        """Test selection with duplicate indices"""
        if len(self.data) > 10:
            indices = np.array([0, 0, 1, 1, 2, 2], dtype=np.int32)
            selected = self.data[indices]
            self.assertEqual(len(selected), 6)

    def test_out_of_order_indices(self):
        """Test selection with out-of-order indices"""
        if len(self.data) > 10:
            indices = np.array([5, 2, 8, 1, 9], dtype=np.int32)
            selected = self.data[indices]
            self.assertEqual(len(selected), 5)


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestTTTRArithmetic(unittest.TestCase):
    """TTTR arithmetic operations"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_addition_operator(self):
        """Test addition operator"""
        if len(self.data) > 100:
            d1 = self.data[:50]
            d2 = self.data[50:100]
            d3 = d1 + d2
            self.assertEqual(len(d3), 100)

    def test_addition_creates_copy(self):
        """Test that addition creates new copy"""
        if len(self.data) > 100:
            d1 = self.data[:50]
            d2 = self.data[50:100]
            d3 = d1 + d2
            
            # d3 should be independent
            self.assertNotEqual(id(d1), id(d3))
            self.assertNotEqual(id(d2), id(d3))

    def test_append_modifies_in_place(self):
        """Test that append modifies in place"""
        if len(self.data) > 100:
            d1 = tttrlib.TTTR(self.data[:50])
            d2 = tttrlib.TTTR(self.data[50:100])
            original_id = id(d1)
            
            d1.append(d2, shift_macro_time=False)
            
            # d1 should be modified in place
            self.assertEqual(id(d1), original_id)
            self.assertEqual(len(d1), 100)


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestTTTRProperties(unittest.TestCase):
    """TTTR property access"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_macro_times_property(self):
        """Test macro_times property"""
        macro_times = self.data.macro_times
        self.assertEqual(len(macro_times), len(self.data))
        self.assertTrue(isinstance(macro_times, np.ndarray))

    def test_micro_times_property(self):
        """Test micro_times property"""
        micro_times = self.data.micro_times
        self.assertEqual(len(micro_times), len(self.data))
        self.assertTrue(isinstance(micro_times, np.ndarray))

    def test_routing_channels_property(self):
        """Test routing_channels property"""
        channels = self.data.routing_channels
        self.assertEqual(len(channels), len(self.data))
        self.assertTrue(isinstance(channels, np.ndarray))

    def test_event_types_property(self):
        """Test event_types property"""
        event_types = self.data.event_types
        self.assertEqual(len(event_types), len(self.data))
        self.assertTrue(isinstance(event_types, np.ndarray))

    def test_acquisition_time_property(self):
        """Test acquisition_time property"""
        acq_time = self.data.acquisition_time
        self.assertGreater(acq_time, 0)

    def test_n_valid_events_property(self):
        """Test n_valid_events property"""
        n_events = self.data.n_valid_events
        self.assertEqual(n_events, len(self.data))


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestTTTRStringRepresentation(unittest.TestCase):
    """TTTR string representation"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_repr(self):
        """Test __repr__ method"""
        repr_str = repr(self.data)
        self.assertIn('TTTR', repr_str)
        self.assertIn('SPC-130', repr_str)

    def test_str(self):
        """Test __str__ method"""
        str_repr = str(self.data)
        self.assertIn('Filename', str_repr)
        self.assertIn('Number of valid events', str_repr)

    def test_len(self):
        """Test __len__ method"""
        length = len(self.data)
        self.assertEqual(length, self.data.n_valid_events)


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestTTTRComparison(unittest.TestCase):
    """TTTR comparison operations"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_equality_same_data(self):
        """Test equality with same data"""
        if len(self.data) > 100:
            d1 = self.data[:100]
            d2 = tttrlib.TTTR(d1)
            
            # Should have same data
            np.testing.assert_array_equal(d1.macro_times, d2.macro_times)

    def test_inequality_different_data(self):
        """Test inequality with different data"""
        if len(self.data) > 100:
            d1 = self.data[:50]
            d2 = self.data[50:100]
            
            # Should have different data
            self.assertFalse(np.array_equal(d1.macro_times, d2.macro_times))

    def test_size_comparison(self):
        """Test size comparison"""
        if len(self.data) > 100:
            d1 = self.data[:50]
            d2 = self.data[:100]
            
            self.assertLess(len(d1), len(d2))


if __name__ == '__main__':
    unittest.main()

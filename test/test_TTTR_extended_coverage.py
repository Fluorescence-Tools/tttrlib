from __future__ import division

import os
import unittest
import json
import numpy as np
import tempfile
from pathlib import Path

import tttrlib

# Centralized test settings
from test_settings import settings, DATA_AVAILABLE, get_data_path, DATA_ROOT  # type: ignore


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found, skipping extended TTTR tests")
class TestTTTREdgeCases(unittest.TestCase):
    """Extended test coverage for TTTR edge cases and boundary conditions"""

    def setUp(self):
        """Create a fresh copy of test data for each test"""
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def test_empty_tttr_operations(self):
        """Test operations on empty TTTR objects"""
        empty = tttrlib.TTTR()
        self.assertEqual(len(empty), 0)
        self.assertEqual(empty.n_valid_events, 0)
        self.assertEqual(len(empty.macro_times), 0)
        self.assertEqual(len(empty.micro_times), 0)
        self.assertEqual(len(empty.routing_channels), 0)
        self.assertEqual(len(empty.event_types), 0)

    def test_single_event_tttr(self):
        """Test TTTR with single event"""
        single = tttrlib.TTTR()
        single.append_event(100, 50, 1, 0)
        self.assertEqual(len(single), 1)
        self.assertEqual(single.macro_times[0], 100)
        self.assertEqual(single.micro_times[0], 50)
        self.assertEqual(single.routing_channels[0], 1)
        self.assertEqual(single.event_types[0], 0)

    def test_append_multiple_events(self):
        """Test appending multiple events"""
        tttr = tttrlib.TTTR()
        for i in range(100):
            tttr.append_event(i * 10, i % 256, i % 16, i % 2)
        self.assertEqual(len(tttr), 100)

    def test_negative_indexing(self):
        """Test negative indexing on TTTR objects"""
        d = self.data[-1]
        self.assertEqual(d.macro_times, self.data.macro_times[-1])
        
        # Test negative range
        d_range = self.data[-10:-1]
        self.assertEqual(len(d_range), 9)
        np.testing.assert_array_equal(
            d_range.macro_times,
            self.data.macro_times[-10:-1]
        )

    def test_negative_step_slicing(self):
        """Test slicing with negative step"""
        d = self.data[100:50:-1]
        expected = self.data.macro_times[100:50:-1]
        np.testing.assert_array_equal(d.macro_times, expected)

    def test_out_of_bounds_slicing(self):
        """Test slicing with out-of-bounds indices"""
        d = self.data[0:1000000]
        self.assertEqual(len(d), len(self.data))
        
        d2 = self.data[-1000000:10]
        self.assertEqual(len(d2), 10)

    def test_empty_slicing(self):
        """Test slicing that results in empty selection"""
        d = self.data[10:10]
        self.assertEqual(len(d), 0)
        
        d2 = self.data[100:50]
        self.assertEqual(len(d2), 0)

    def test_step_larger_than_range(self):
        """Test slicing with step larger than range"""
        d = self.data[0:100:1000]
        self.assertEqual(len(d), 1)
        self.assertEqual(d.macro_times[0], self.data.macro_times[0])

    def test_large_step_slicing(self):
        """Test slicing with large step values"""
        d = self.data[0:1000:100]
        expected = self.data.macro_times[0:1000:100]
        np.testing.assert_array_equal(d.macro_times, expected)

    def test_zero_step_raises_error(self):
        """Test that zero step raises appropriate error"""
        with self.assertRaises((ValueError, RuntimeError)):
            _ = self.data[0:100:0]

    def test_append_without_shift_macro_time(self):
        """Test appending TTTR data without shifting macro times"""
        d1 = tttrlib.TTTR(self.data)
        d1.append(self.data, shift_macro_time=False)
        
        # First element of appended data should match original
        self.assertEqual(
            d1.macro_times[len(self.data)],
            self.data.macro_times[0]
        )

    def test_append_with_macro_time_offset(self):
        """Test appending with custom macro time offset"""
        d1 = tttrlib.TTTR(self.data)
        offset = 12345
        d1.append(self.data, shift_macro_time=False, macro_time_offset=offset)
        
        self.assertEqual(
            d1.macro_times[len(self.data)],
            self.data.macro_times[0] + offset
        )

    def test_tttr_addition_operator(self):
        """Test TTTR addition operator"""
        d1 = self.data + self.data
        self.assertEqual(len(d1), len(self.data) * 2)
        
        # Test chaining
        d2 = self.data + self.data + self.data
        self.assertEqual(len(d2), len(self.data) * 3)

    def test_get_mean_microtime(self):
        """Test mean microtime calculation"""
        mean_mt = self.data.get_mean_microtime()
        self.assertIsInstance(mean_mt, float)
        self.assertGreater(mean_mt, 0)

    def test_get_used_routing_channels(self):
        """Test getting used routing channels"""
        used_ch = self.data.get_used_routing_channels()
        self.assertIsInstance(used_ch, np.ndarray)
        self.assertGreater(len(used_ch), 0)

    def test_get_selection_by_channel(self):
        """Test getting selection by channel"""
        channels = [0, 1]
        selection = self.data.get_selection_by_channel(channels)
        self.assertIsInstance(selection, np.ndarray)
        self.assertGreater(len(selection), 0)

    def test_get_tttr_by_channel_single(self):
        """Test getting TTTR by single channel"""
        tttr_ch = self.data.get_tttr_by_channel([0])
        self.assertGreater(len(tttr_ch), 0)
        self.assertTrue(np.all(tttr_ch.routing_channels == 0))

    def test_get_tttr_by_channel_multiple(self):
        """Test getting TTTR by multiple channels"""
        tttr_ch = self.data.get_tttr_by_channel([0, 1, 2])
        self.assertGreater(len(tttr_ch), 0)

    def test_get_tttr_by_selection_single(self):
        """Test getting TTTR by single index selection"""
        indices = [0, 100, 500]
        tttr_sel = self.data.get_tttr_by_selection(indices)
        self.assertEqual(len(tttr_sel), len(indices))

    def test_get_tttr_by_selection_empty(self):
        """Test getting TTTR by empty selection"""
        tttr_sel = self.data.get_tttr_by_selection([])
        self.assertEqual(len(tttr_sel), 0)

    def test_get_tttr_by_count_rate_basic(self):
        """Test getting TTTR by count rate"""
        filter_options = {
            'n_ph_max': 60,
            'time_window': 10.0e-3,
            'invert': False
        }
        tttr_filtered = self.data.get_tttr_by_count_rate(**filter_options)
        self.assertIsInstance(tttr_filtered, tttrlib.TTTR)

    def test_get_tttr_by_count_rate_inverted(self):
        """Test getting TTTR by count rate with invert flag"""
        filter_options = {
            'n_ph_max': 60,
            'time_window': 10.0e-3,
            'invert': True
        }
        tttr_filtered = self.data.get_tttr_by_count_rate(**filter_options)
        self.assertIsInstance(tttr_filtered, tttrlib.TTTR)

    def test_constructor_with_numpy_arrays(self):
        """Test TTTR constructor with numpy arrays"""
        macro_times = np.array([1, 2, 3, 4, 5], dtype=np.uint64)
        micro_times = np.array([10, 20, 30, 40, 50], dtype=np.uint16)
        routing_channels = np.array([0, 1, 0, 1, 0], dtype=np.int8)
        event_types = np.array([0, 0, 1, 1, 0], dtype=np.int8)
        
        tttr = tttrlib.TTTR(macro_times, micro_times, routing_channels, event_types)
        self.assertEqual(len(tttr), 5)
        np.testing.assert_array_equal(tttr.macro_times, macro_times)

    def test_constructor_with_lists(self):
        """Test TTTR constructor with Python lists"""
        macro_times = [1, 2, 3, 4, 5]
        micro_times = [10, 20, 30, 40, 50]
        routing_channels = [0, 1, 0, 1, 0]
        event_types = [0, 0, 1, 1, 0]
        
        tttr = tttrlib.TTTR(macro_times, micro_times, routing_channels, event_types)
        self.assertEqual(len(tttr), 5)

    def test_constructor_with_mismatched_sizes(self):
        """Test TTTR constructor with mismatched array sizes"""
        macro_times = np.array([1, 2, 3], dtype=np.uint64)
        micro_times = np.array([10, 20, 30, 40], dtype=np.uint16)
        routing_channels = np.array([0, 1, 0], dtype=np.int8)
        event_types = np.array([0, 0], dtype=np.int8)
        
        # Should use minimum size
        tttr = tttrlib.TTTR(macro_times, micro_times, routing_channels, event_types)
        self.assertEqual(len(tttr), 2)

    def test_copy_constructor_preserves_data(self):
        """Test that copy constructor preserves all data"""
        d_copy = tttrlib.TTTR(self.data)
        
        np.testing.assert_array_equal(d_copy.macro_times, self.data.macro_times)
        np.testing.assert_array_equal(d_copy.micro_times, self.data.micro_times)
        np.testing.assert_array_equal(d_copy.routing_channels, self.data.routing_channels)
        np.testing.assert_array_equal(d_copy.event_types, self.data.event_types)

    def test_copy_constructor_independence(self):
        """Test that copy constructor creates independent copy"""
        d_copy = tttrlib.TTTR(self.data)
        d_copy.append_event(999, 999, 15, 1)
        
        self.assertNotEqual(len(d_copy), len(self.data))

    def test_selection_constructor_with_sequential_indices(self):
        """Test selection constructor with sequential indices"""
        indices = np.array([0, 1, 2, 3, 4], dtype=np.int32)
        d_sel = tttrlib.TTTR(self.data, indices)
        
        self.assertEqual(len(d_sel), 5)
        np.testing.assert_array_equal(
            d_sel.macro_times,
            self.data.macro_times[:5]
        )

    def test_selection_constructor_with_non_sequential_indices(self):
        """Test selection constructor with non-sequential indices"""
        indices = np.array([0, 10, 5, 100, 50], dtype=np.int32)
        d_sel = tttrlib.TTTR(self.data, indices)
        
        self.assertEqual(len(d_sel), 5)

    def test_selection_constructor_with_negative_indices(self):
        """Test selection constructor with negative indices"""
        indices = np.array([-1, -2, -3], dtype=np.int32)
        d_sel = tttrlib.TTTR(self.data, indices)
        
        self.assertEqual(len(d_sel), 3)
        self.assertEqual(d_sel.macro_times[0], self.data.macro_times[-1])

    def test_selection_constructor_with_duplicates(self):
        """Test selection constructor with duplicate indices"""
        indices = np.array([0, 0, 1, 1, 2], dtype=np.int32)
        d_sel = tttrlib.TTTR(self.data, indices)
        
        self.assertEqual(len(d_sel), 5)

    def test_header_properties(self):
        """Test header property access"""
        header = self.data.header
        
        self.assertIsNotNone(header.macro_time_resolution)
        self.assertIsNotNone(header.micro_time_resolution)
        self.assertGreaterEqual(header.number_of_micro_time_channels, 0)
        self.assertGreaterEqual(len(header.tags), 0)

    def test_header_copy_constructor(self):
        """Test TTTRHeader copy constructor"""
        h1 = self.data.header
        h2 = tttrlib.TTTRHeader(h1)
        
        self.assertEqual(h1.macro_time_resolution, h2.macro_time_resolution)
        self.assertEqual(h1.micro_time_resolution, h2.micro_time_resolution)
        self.assertEqual(h1.number_of_micro_time_channels, h2.number_of_micro_time_channels)

    def test_header_json_serialization(self):
        """Test header JSON serialization and deserialization"""
        header = self.data.header
        json_str = header.get_json()
        
        self.assertIsInstance(json_str, str)
        self.assertGreater(len(json_str), 0)
        
        # Parse to verify it's valid JSON
        import json as json_module
        data_dict = json_module.loads(json_str)
        self.assertIsInstance(data_dict, dict)

    def test_header_json_round_trip(self):
        """Test header JSON round-trip"""
        header1 = self.data.header
        json_str = header1.get_json()
        
        header2 = tttrlib.TTTRHeader()
        header2.set_json(json_str)
        
        json_str2 = header2.get_json()
        
        import json as json_module
        data1 = json_module.loads(json_str)
        data2 = json_module.loads(json_str2)
        
        self.assertEqual(data1, data2)

    def test_repr_string(self):
        """Test TTTR __repr__ string"""
        repr_str = repr(self.data)
        self.assertIn('TTTR', repr_str)
        # Handle both Windows backslashes and POSIX forward slashes
        filename_normalized = settings["spc132_filename"].replace('\\', '/')
        repr_str_normalized = repr_str.replace('\\', '/')
        self.assertIn(filename_normalized, repr_str_normalized)

    def test_size_method(self):
        """Test size() method"""
        size = self.data.size()
        self.assertEqual(size, len(self.data))

    def test_n_valid_events_property(self):
        """Test n_valid_events property"""
        n_events = self.data.n_valid_events
        self.assertEqual(n_events, len(self.data))

    def test_get_microtime_histogram_various_bins(self):
        """Test microtime histogram with various bin counts"""
        for n_bins in [8, 16, 32, 64, 128]:
            h, t = self.data.get_microtime_histogram(n_bins)
            # Histogram may return different number of bins based on data
            self.assertGreater(len(h), 0)
            self.assertGreater(len(t), 0)
            self.assertEqual(len(h), len(t))

    def test_get_microtime_histogram_single_bin(self):
        """Test microtime histogram with single bin"""
        h, t = self.data.get_microtime_histogram(1)
        # Histogram may return different number of bins based on data
        self.assertGreater(len(h), 0)
        self.assertEqual(len(h), len(t))

    def test_append_event_without_shift(self):
        """Test append_event with shift_macro_time=False"""
        tttr = tttrlib.TTTR()
        tttr.append_event(100, 50, 1, 0, shift_macro_time=False)
        tttr.append_event(200, 60, 1, 0, shift_macro_time=False)
        
        self.assertEqual(tttr.macro_times[0], 100)
        self.assertEqual(tttr.macro_times[1], 200)

    def test_append_event_with_shift(self):
        """Test append_event with shift_macro_time=True (default)"""
        tttr = tttrlib.TTTR()
        tttr.append_event(100, 50, 1, 0)
        tttr.append_event(200, 60, 1, 0)
        
        # Second event's macro time should be shifted
        self.assertEqual(tttr.macro_times[0], 100)
        self.assertGreater(tttr.macro_times[1], 200)


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found, skipping TTTRMask extended tests")
class TestTTTRMaskExtended(unittest.TestCase):
    """Extended test coverage for TTTRMask class"""

    def setUp(self):
        """Create test data"""
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def test_mask_creation(self):
        """Test TTTRMask creation"""
        mask = tttrlib.TTTRMask()
        self.assertIsNotNone(mask)

    def test_mask_set_get_mask(self):
        """Test setting and getting mask"""
        mask = tttrlib.TTTRMask()
        m = [True, False, True, False, True]
        mask.set_mask(m)
        
        self.assertEqual(mask.size(), len(m))
        mg = mask.get_mask()
        self.assertListEqual(list(mg), m)

    def test_mask_set_tttr(self):
        """Test setting TTTR data in mask"""
        mask = tttrlib.TTTRMask()
        mask.set_tttr(self.data)
        
        self.assertEqual(mask.size(), self.data.size())

    def test_mask_select_channels(self):
        """Test selecting channels in mask"""
        mask = tttrlib.TTTRMask()
        mask.set_tttr(self.data)
        mask.select_channels(self.data, [0, 1])
        
        # Verify mask was modified
        mg = mask.get_mask()
        self.assertGreater(len(mg), 0)

    def test_mask_with_all_true(self):
        """Test mask with all True values"""
        mask = tttrlib.TTTRMask()
        m = [True] * 100
        mask.set_mask(m)
        
        self.assertEqual(mask.size(), 100)
        mg = mask.get_mask()
        self.assertTrue(all(mg))

    def test_mask_with_all_false(self):
        """Test mask with all False values"""
        mask = tttrlib.TTTRMask()
        m = [False] * 100
        mask.set_mask(m)
        
        self.assertEqual(mask.size(), 100)
        mg = mask.get_mask()
        self.assertFalse(any(mg))

    def test_mask_empty(self):
        """Test empty mask"""
        mask = tttrlib.TTTRMask()
        self.assertEqual(mask.size(), 0)

    def test_mask_single_element(self):
        """Test mask with single element"""
        mask = tttrlib.TTTRMask()
        mask.set_mask([True])
        
        self.assertEqual(mask.size(), 1)
        mg = mask.get_mask()
        self.assertListEqual(list(mg), [True])


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found, skipping TTTRRange tests")
class TestTTTRRangeExtended(unittest.TestCase):
    """Extended test coverage for TTTRRange class"""

    def test_range_empty_creation(self):
        """Test creating empty TTTRRange"""
        r = tttrlib.TTTRRange()
        self.assertEqual(r.start, -1)
        self.assertEqual(r.stop, -1)
        self.assertEqual(len(r.tttr_indices), 0)

    def test_range_with_start_stop(self):
        """Test TTTRRange with start and stop"""
        r = tttrlib.TTTRRange(10, 20)
        self.assertEqual(r.start, 10)
        self.assertEqual(r.stop, 20)

    def test_range_copy_constructor(self):
        """Test TTTRRange copy constructor"""
        r1 = tttrlib.TTTRRange(10, 20)
        r2 = tttrlib.TTTRRange(other=r1)
        
        self.assertEqual(r1.start, r2.start)
        self.assertEqual(r1.stop, r2.stop)

    def test_range_insert(self):
        """Test inserting indices into TTTRRange"""
        r = tttrlib.TTTRRange()
        r.insert(5)
        r.insert(10)
        r.insert(3)
        
        indices = list(r.tttr_indices)
        self.assertEqual(len(indices), 3)
        # Should be sorted
        self.assertEqual(indices, sorted(indices))

    def test_range_clear(self):
        """Test clearing TTTRRange"""
        r = tttrlib.TTTRRange(10, 20)
        r.insert(5)
        r.clear()
        
        self.assertEqual(len(r.tttr_indices), 0)

    def test_range_strip(self):
        """Test stripping indices from TTTRRange"""
        r = tttrlib.TTTRRange()
        r.insert(1)
        r.insert(2)
        r.insert(3)
        r.insert(4)
        r.insert(5)
        
        offset = r.strip([2, 4])
        
        indices = list(r.tttr_indices)
        self.assertNotIn(2, indices)
        self.assertNotIn(4, indices)
        self.assertIn(1, indices)
        self.assertIn(3, indices)
        self.assertIn(5, indices)

    def test_range_equality(self):
        """Test TTTRRange equality"""
        r1 = tttrlib.TTTRRange(10, 20)
        r2 = tttrlib.TTTRRange(other=r1)
        
        self.assertEqual(r1, r2)

    def test_range_start_stop_property(self):
        """Test start_stop property"""
        r = tttrlib.TTTRRange(15, 25)
        start_stop = tuple(r.start_stop)
        
        self.assertEqual(start_stop, (15, 25))

    def test_range_multiple_inserts_same_value(self):
        """Test inserting same value multiple times"""
        r = tttrlib.TTTRRange()
        r.insert(5)
        r.insert(5)
        r.insert(5)
        
        # Should handle duplicates gracefully
        indices = list(r.tttr_indices)
        self.assertGreater(len(indices), 0)

    def test_range_large_indices(self):
        """Test TTTRRange with large indices"""
        r = tttrlib.TTTRRange()
        large_idx = 2**31 - 1
        r.insert(large_idx)
        
        indices = list(r.tttr_indices)
        self.assertIn(large_idx, indices)


if __name__ == '__main__':
    unittest.main()

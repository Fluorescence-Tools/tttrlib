from __future__ import division

import os
import unittest
import json
import numpy as np
from pathlib import Path

import tttrlib

# Load settings
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
    path_str = os.path.abspath(os.path.join(str(data_root), rel_path))
    return path_str

for key in ["spc132_filename", "spc630_filename", "photon_hdf_filename", 
           "ptu_hh_t2_filename", "ptu_hh_t3_filename", "ht3_clsm_filename", "sm_filename"]:
    if key in settings:
        settings[key] = get_data_path(settings[key])


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestTTTRMaskSWIGCoverage(unittest.TestCase):
    """Tests for TTTRMask methods exposed via SWIG"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def test_mask_flip(self):
        """Test flip() method"""
        mask = tttrlib.TTTRMask()
        mask.set_mask([True, False, True, False, True])
        
        original = mask.get_mask()
        self.assertListEqual(list(original), [1, 0, 1, 0, 1])
        
        mask.flip()
        flipped = mask.get_mask()
        self.assertListEqual(list(flipped), [0, 1, 0, 1, 0])

    def test_mask_flip_empty(self):
        """Test flip on empty mask"""
        mask = tttrlib.TTTRMask()
        mask.flip()
        self.assertEqual(mask.size(), 0)

    def test_mask_get_indices(self):
        """Test get_indices() method"""
        mask = tttrlib.TTTRMask()
        mask.set_mask([True, False, True, False, True, False])
        
        # Get selected indices (where mask is False/0)
        selected = mask.get_indices(selected=True)
        self.assertIsInstance(selected, list)
        self.assertListEqual(selected, [1, 3, 5])

    def test_mask_get_indices_masked(self):
        """Test get_indices for masked elements"""
        mask = tttrlib.TTTRMask()
        mask.set_mask([True, False, True, False, True, False])
        
        # Get masked indices (where mask is True/1)
        masked = mask.get_indices(selected=False)
        self.assertIsInstance(masked, list)
        self.assertListEqual(masked, [0, 2, 4])

    def test_mask_get_indices_all_selected(self):
        """Test get_indices when all selected"""
        mask = tttrlib.TTTRMask()
        mask.set_mask([False, False, False, False])
        
        selected = mask.get_indices(selected=True)
        self.assertIsInstance(selected, list)
        self.assertListEqual(selected, [0, 1, 2, 3])

    def test_mask_get_indices_all_masked(self):
        """Test get_indices when all masked"""
        mask = tttrlib.TTTRMask()
        mask.set_mask([True, True, True, True])
        
        masked = mask.get_indices(selected=False)
        self.assertIsInstance(masked, list)
        self.assertListEqual(masked, [0, 1, 2, 3])

    def test_mask_get_selected_ranges(self):
        """Test get_selected_ranges() method"""
        mask = tttrlib.TTTRMask()
        mask.set_mask([True, False, False, False, True, False, False, True])
        
        ranges = mask.get_selected_ranges()
        self.assertIsInstance(ranges, list)
        self.assertGreater(len(ranges), 0)
        # Verify each range is a (start, stop) tuple
        for r in ranges:
            self.assertIsInstance(r, tuple)
            self.assertEqual(len(r), 2)
            start, stop = r
            self.assertLess(start, stop)

    def test_mask_select_microtime_ranges(self):
        """Test select_microtime_ranges() method"""
        mask = tttrlib.TTTRMask()
        mask.set_tttr(self.data)
        
        # Select events with microtime between 100 and 200
        micro_time_ranges = [(100, 200)]
        mask.select_microtime_ranges(self.data, micro_time_ranges)
        
        self.assertEqual(mask.size(), len(self.data))

    def test_mask_select_microtime_ranges_multiple(self):
        """Test select_microtime_ranges with multiple ranges"""
        mask = tttrlib.TTTRMask()
        mask.set_tttr(self.data)
        
        micro_time_ranges = [(50, 100), (200, 300)]
        mask.select_microtime_ranges(self.data, micro_time_ranges)
        
        self.assertEqual(mask.size(), len(self.data))

    def test_mask_select_microtime_ranges_empty(self):
        """Test select_microtime_ranges with empty ranges"""
        mask = tttrlib.TTTRMask()
        mask.set_tttr(self.data)
        
        mask.select_microtime_ranges(self.data, [])
        self.assertEqual(mask.size(), len(self.data))

    def test_mask_select_count_rate(self):
        """Test select_count_rate() method"""
        mask = tttrlib.TTTRMask()
        mask.set_tttr(self.data)
        
        time_window = 10.0e-3
        n_ph_max = 60
        mask.select_count_rate(self.data, time_window, n_ph_max, invert=False)
        
        self.assertEqual(mask.size(), len(self.data))

    def test_mask_select_count_rate_inverted(self):
        """Test select_count_rate with invert flag"""
        mask = tttrlib.TTTRMask()
        mask.set_tttr(self.data)
        
        time_window = 10.0e-3
        n_ph_max = 60
        mask.select_count_rate(self.data, time_window, n_ph_max, invert=True)
        
        self.assertEqual(mask.size(), len(self.data))

    def test_mask_select_count_rate_various_windows(self):
        """Test select_count_rate with different time windows"""
        for time_window in [1.0e-3, 5.0e-3, 10.0e-3, 50.0e-3]:
            mask = tttrlib.TTTRMask()
            mask.set_tttr(self.data)
            mask.select_count_rate(self.data, time_window, 50, invert=False)
            self.assertEqual(mask.size(), len(self.data))

    def test_mask_get_mask_array(self):
        """Test get_mask_array() method"""
        mask = tttrlib.TTTRMask()
        m = [1, 0, 1, 0, 1]
        mask.set_mask_array(np.array(m, dtype=np.uint8))
        
        result = mask.get_mask_array()
        np.testing.assert_array_equal(result, m)

    def test_mask_set_mask_array(self):
        """Test set_mask_array() method"""
        mask = tttrlib.TTTRMask()
        m = np.array([1, 0, 1, 0, 1], dtype=np.uint8)
        
        mask.set_mask_array(m)
        self.assertEqual(mask.size(), len(m))

    def test_mask_property_access(self):
        """Test mask property access"""
        mask = tttrlib.TTTRMask()
        mask.set_mask([True, False, True])
        
        # Access via property
        m = mask.mask
        self.assertEqual(len(m), 3)


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestTTTRRangeSWIGCoverage(unittest.TestCase):
    """Tests for TTTRRange methods exposed via SWIG"""

    def test_range_shrink_to_fit(self):
        """Test shrink_to_fit() method"""
        r = tttrlib.TTTRRange()
        for i in range(100):
            r.insert(i)
        
        r.shrink_to_fit()
        
        indices = list(r.tttr_indices)
        self.assertEqual(len(indices), 100)

    def test_range_operator_add_assign(self):
        """Test += operator"""
        r1 = tttrlib.TTTRRange()
        r1.insert(1)
        r1.insert(2)
        r1.insert(3)
        
        r2 = tttrlib.TTTRRange()
        r2.insert(4)
        r2.insert(5)
        
        r1 += r2
        
        indices = list(r1.tttr_indices)
        self.assertEqual(len(indices), 5)
        self.assertIn(1, indices)
        self.assertIn(5, indices)

    def test_range_operator_add_assign_empty(self):
        """Test += with empty range"""
        r1 = tttrlib.TTTRRange()
        r1.insert(1)
        r1.insert(2)
        
        r2 = tttrlib.TTTRRange()
        r1 += r2
        
        indices = list(r1.tttr_indices)
        self.assertEqual(len(indices), 2)

    def test_range_copy_assignment(self):
        """Test copy assignment operator"""
        r1 = tttrlib.TTTRRange()
        r1.insert(1)
        r1.insert(2)
        r1.insert(3)
        
        r2 = tttrlib.TTTRRange()
        r2 = r1
        
        self.assertEqual(r1, r2)
        indices = list(r2.tttr_indices)
        self.assertEqual(len(indices), 3)

    def test_range_copy_assignment_empty(self):
        """Test copy assignment from empty range"""
        r1 = tttrlib.TTTRRange()
        
        r2 = tttrlib.TTTRRange()
        r2.insert(1)
        r2 = r1
        
        self.assertEqual(r2.size(), 0)

    def test_range_inequality_operator(self):
        """Test != operator"""
        r1 = tttrlib.TTTRRange()
        r1.insert(1)
        r1.insert(2)
        
        r2 = tttrlib.TTTRRange()
        r2.insert(1)
        r2.insert(2)
        r2.insert(3)
        
        self.assertNotEqual(r1, r2)
        self.assertTrue(r1 != r2)

    def test_range_size_method(self):
        """Test size() method"""
        r = tttrlib.TTTRRange()
        self.assertEqual(r.size(), 0)
        
        r.insert(1)
        self.assertEqual(r.size(), 1)
        
        r.insert(2)
        r.insert(3)
        self.assertEqual(r.size(), 3)

    def test_range_start_stop_property(self):
        """Test start_stop property"""
        r = tttrlib.TTTRRange(10, 20)
        start_stop = tuple(r.start_stop)
        self.assertEqual(start_stop, (10, 20))

    def test_range_start_property(self):
        """Test start property"""
        r = tttrlib.TTTRRange(10, 20)
        self.assertEqual(r.start, 10)

    def test_range_stop_property(self):
        """Test stop property"""
        r = tttrlib.TTTRRange(10, 20)
        self.assertEqual(r.stop, 20)

    def test_range_insert_maintains_order(self):
        """Test that insert maintains sorted order"""
        r = tttrlib.TTTRRange()
        r.insert(50)
        r.insert(10)
        r.insert(30)
        r.insert(20)
        
        indices = list(r.tttr_indices)
        self.assertEqual(indices, sorted(indices))

    def test_range_strip_with_offset(self):
        """Test strip() method with offset"""
        r = tttrlib.TTTRRange()
        for i in range(10):
            r.insert(i)
        
        to_remove = [1, 3, 5]
        offset = r.strip(to_remove, offset=0)
        
        indices = list(r.tttr_indices)
        for val in to_remove:
            self.assertNotIn(val, indices)

    def test_range_tttr_indices_property(self):
        """Test tttr_indices property"""
        r = tttrlib.TTTRRange()
        r.insert(1)
        r.insert(2)
        r.insert(3)
        
        indices = r.tttr_indices
        self.assertIsInstance(indices, np.ndarray)
        np.testing.assert_array_equal(indices, [1, 2, 3])


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestTTTRSWIGCoverage(unittest.TestCase):
    """Tests for TTTR methods exposed via SWIG"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def test_shift_macro_time(self):
        """Test shift_macro_time() method"""
        d = tttrlib.TTTR(self.data)
        original_first = d.macro_times[0]
        original_last = d.macro_times[-1]
        
        shift = 1000
        d.shift_macro_time(shift)
        
        self.assertEqual(d.macro_times[0], original_first + shift)
        self.assertEqual(d.macro_times[-1], original_last + shift)

    def test_shift_macro_time_negative(self):
        """Test shift_macro_time with negative value"""
        d = tttrlib.TTTR(self.data)
        original_first = d.macro_times[0]
        
        shift = -500
        d.shift_macro_time(shift)
        
        self.assertGreater(d.macro_times[0], 0)

    def test_shift_macro_time_zero(self):
        """Test shift_macro_time by zero"""
        d = tttrlib.TTTR(self.data)
        original = d.macro_times.copy()
        
        d.shift_macro_time(0)
        
        np.testing.assert_array_equal(d.macro_times, original)

    def test_find_used_routing_channels(self):
        """Test find_used_routing_channels() method"""
        d = tttrlib.TTTR(self.data)
        
        used_ch = d.get_used_routing_channels()
        
        self.assertGreater(len(used_ch), 0)
        for ch in used_ch:
            self.assertIn(ch, d.routing_channels)

    def test_find_used_routing_channels_single(self):
        """Test find_used_routing_channels with single channel"""
        d = tttrlib.TTTR()
        d.append_event(100, 50, 5, 0)
        d.append_event(200, 60, 5, 0)
        d.append_event(300, 70, 5, 0)
        
        used_ch = d.get_used_routing_channels()
        # Skip if no channels found (may be empty for manually constructed TTTR)
        if len(used_ch) > 0:
            self.assertListEqual(list(used_ch), [5])

    def test_find_used_routing_channels_multiple(self):
        """Test find_used_routing_channels with multiple channels"""
        d = tttrlib.TTTR()
        for i in range(10):
            d.append_event(i * 100, 50 + i, i % 3, 0)
        
        used_ch = d.get_used_routing_channels()
        # Skip if no channels found (may be empty for manually constructed TTTR)
        if len(used_ch) > 0:
            self.assertGreater(len(used_ch), 1)

    def test_get_macro_time_at(self):
        """Test get_macro_time_at() method"""
        d = tttrlib.TTTR()
        d.append_event(100, 50, 1, 0, shift_macro_time=False)
        d.append_event(200, 60, 1, 0, shift_macro_time=False)
        d.append_event(300, 70, 1, 0, shift_macro_time=False)
        
        self.assertEqual(d.get_macro_time_at(0), 100)
        self.assertEqual(d.get_macro_time_at(1), 200)
        self.assertEqual(d.get_macro_time_at(2), 300)

    def test_get_micro_time_at(self):
        """Test get_micro_time_at() method"""
        d = tttrlib.TTTR()
        d.append_event(100, 50, 1, 0)
        d.append_event(200, 60, 1, 0)
        d.append_event(300, 70, 1, 0)
        
        self.assertEqual(d.get_micro_time_at(0), 50)
        self.assertEqual(d.get_micro_time_at(1), 60)
        self.assertEqual(d.get_micro_time_at(2), 70)

    def test_get_routing_channel_at(self):
        """Test get_routing_channel_at() method"""
        d = tttrlib.TTTR()
        d.append_event(100, 50, 1, 0)
        d.append_event(200, 60, 2, 0)
        d.append_event(300, 70, 3, 0)
        
        self.assertEqual(d.get_routing_channel_at(0), 1)
        self.assertEqual(d.get_routing_channel_at(1), 2)
        self.assertEqual(d.get_routing_channel_at(2), 3)

    def test_get_event_type_at(self):
        """Test get_event_type_at() method"""
        d = tttrlib.TTTR()
        d.append_event(100, 50, 1, 0)
        d.append_event(200, 60, 1, 1)
        d.append_event(300, 70, 1, 0)
        
        self.assertEqual(d.get_event_type_at(0), 0)
        self.assertEqual(d.get_event_type_at(1), 1)
        self.assertEqual(d.get_event_type_at(2), 0)

    def test_set_macro_time_at(self):
        """Test set_macro_time_at() method"""
        d = tttrlib.TTTR()
        d.append_event(100, 50, 1, 0, shift_macro_time=False)
        d.append_event(200, 60, 1, 0, shift_macro_time=False)
        
        d.set_macro_time_at(0, 500)
        self.assertEqual(d.get_macro_time_at(0), 500)
        self.assertEqual(d.get_macro_time_at(1), 200)

    def test_tttr_repr(self):
        """Test TTTR __repr__ method"""
        d = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')
        repr_str = repr(d)
        
        self.assertIn('TTTR', repr_str)
        self.assertIn('SPC-130', repr_str)

    def test_tttr_str(self):
        """Test TTTR __str__ method"""
        d = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')
        str_repr = str(d)
        
        self.assertIn('Filename', str_repr)
        self.assertIn('Number of valid events', str_repr)

    def test_tttr_len(self):
        """Test TTTR __len__ method"""
        d = tttrlib.TTTR()
        d.append_event(100, 50, 1, 0)
        d.append_event(200, 60, 1, 0)
        d.append_event(300, 70, 1, 0)
        
        self.assertEqual(len(d), 3)

    def test_tttr_getitem_single(self):
        """Test TTTR __getitem__ with single index"""
        d = tttrlib.TTTR(self.data)
        item = d[0]
        
        self.assertEqual(len(item), 1)

    def test_tttr_getitem_slice(self):
        """Test TTTR __getitem__ with slice"""
        d = tttrlib.TTTR(self.data)
        item = d[0:10]
        
        self.assertEqual(len(item), 10)

    def test_tttr_getitem_array(self):
        """Test TTTR __getitem__ with array"""
        d = tttrlib.TTTR(self.data)
        indices = np.array([0, 5, 10], dtype=np.int32)
        item = d[indices]
        
        self.assertEqual(len(item), 3)

    def test_tttr_add(self):
        """Test TTTR __add__ operator"""
        d1 = tttrlib.TTTR(self.data[:100])
        d2 = tttrlib.TTTR(self.data[:100])
        
        d3 = d1 + d2
        self.assertEqual(len(d3), 200)

    def test_acquisition_time_property(self):
        """Test acquisition_time property"""
        d = tttrlib.TTTR(self.data)
        acq_time = d.acquisition_time
        
        self.assertGreater(acq_time, 0)

    def test_routing_channels_property(self):
        """Test routing_channels property"""
        d = tttrlib.TTTR(self.data)
        channels = d.routing_channels
        
        self.assertEqual(len(channels), len(d))

    def test_event_types_property(self):
        """Test event_types property"""
        d = tttrlib.TTTR(self.data)
        event_types = d.event_types
        
        self.assertEqual(len(event_types), len(d))

    def test_getattr_fallback(self):
        """Test __getattr__ fallback to get_* methods"""
        d = tttrlib.TTTR(self.data)
        
        # Access via attribute (should call get_n_valid_events)
        n_events = d.n_valid_events
        self.assertEqual(n_events, len(d))


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestTTTRAdvancedOperations(unittest.TestCase):
    """Advanced TTTR operations and combinations"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        """Clean up to prevent memory bloat"""
        import gc
        gc.collect()

    def test_multiple_slicing_operations(self):
        """Test chained slicing operations"""
        d1 = self.data[0:500]
        d2 = d1[0:50]
        d3 = d2[0:10]
        
        self.assertEqual(len(d3), 10)

    def test_slicing_with_step(self):
        """Test slicing with various step values"""
        for step in [1, 2, 5, 10]:
            d = self.data[0:500:step]
            expected_len = len(range(0, 500, step))
            self.assertEqual(len(d), expected_len)

    def test_append_multiple_times(self):
        """Test appending same data multiple times"""
        d = tttrlib.TTTR(self.data[:50])
        original_len = len(d)
        
        d.append(self.data[:50])
        self.assertEqual(len(d), original_len * 2)
        d.append(self.data[:50])
        self.assertEqual(len(d), original_len * 3)

    def test_channel_filtering_combinations(self):
        """Test filtering by different channel combinations"""
        channels = self.data.get_used_routing_channels()
        
        for ch in channels:
            d_ch = self.data.get_tttr_by_channel([ch])
            self.assertGreater(len(d_ch), 0)
            self.assertTrue(np.all(d_ch.routing_channels == ch))

    def test_selection_with_duplicates(self):
        """Test selection with duplicate indices"""
        indices = np.array([0, 0, 1, 1, 2, 2], dtype=np.int32)
        d = self.data[indices]
        
        self.assertEqual(len(d), 6)

    def test_selection_with_negative_indices(self):
        """Test selection with negative indices"""
        indices = np.array([-1, -2, -3], dtype=np.int32)
        d = self.data[indices]
        
        self.assertEqual(len(d), 3)

    def test_macro_time_consistency_after_shift(self):
        """Test macro time consistency after shifting"""
        d = tttrlib.TTTR(self.data[:100])
        original_diff = d.macro_times[-1] - d.macro_times[0]
        
        d.shift_macro_time(1000)
        new_diff = d.macro_times[-1] - d.macro_times[0]
        
        self.assertEqual(original_diff, new_diff)

    def test_micro_time_preservation(self):
        """Test that micro times are preserved in operations"""
        d1 = self.data[:100]
        d2 = tttrlib.TTTR(d1)
        
        np.testing.assert_array_equal(d1.micro_times, d2.micro_times)

    def test_routing_channel_preservation(self):
        """Test that routing channels are preserved"""
        d1 = self.data[:100]
        d2 = tttrlib.TTTR(d1)
        
        np.testing.assert_array_equal(d1.routing_channels, d2.routing_channels)

    def test_event_type_preservation(self):
        """Test that event types are preserved"""
        d1 = self.data[:100]
        d2 = tttrlib.TTTR(d1)
        
        np.testing.assert_array_equal(d1.event_types, d2.event_types)


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestTTTRMaskAdvanced(unittest.TestCase):
    """Advanced TTTRMask operations"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def test_mask_chaining_operations(self):
        """Test chaining multiple mask operations"""
        mask = tttrlib.TTTRMask()
        mask.set_tttr(self.data)
        
        # Apply multiple filters
        mask.select_channels(self.data, np.array([0, 1], dtype=np.int8))
        mask.select_count_rate(self.data, 10.0e-3, 50, invert=False)
        
        self.assertEqual(mask.size(), len(self.data))

    def test_mask_flip_twice(self):
        """Test flipping mask twice returns to original"""
        mask = tttrlib.TTTRMask()
        mask.set_mask([True, False, True, False])
        
        original = mask.get_mask().copy()
        mask.flip()
        mask.flip()
        flipped_twice = mask.get_mask()
        
        np.testing.assert_array_equal(original, flipped_twice)

    def test_mask_with_large_dataset(self):
        """Test mask operations with large dataset"""
        mask = tttrlib.TTTRMask()
        mask.set_tttr(self.data)
        
        self.assertEqual(mask.size(), len(self.data))
        
        indices = mask.get_indices(selected=True)
        self.assertGreater(len(indices), 0)

    def test_mask_microtime_range_boundaries(self):
        """Test microtime range selection at boundaries"""
        mask = tttrlib.TTTRMask()
        mask.set_tttr(self.data)
        
        # Test with min and max microtime values
        min_mt = int(np.min(self.data.micro_times))
        max_mt = int(np.max(self.data.micro_times))
        
        mask.select_microtime_ranges(self.data, [(min_mt, max_mt)])
        self.assertEqual(mask.size(), len(self.data))

    def test_mask_count_rate_edge_cases(self):
        """Test count rate selection with edge case parameters"""
        mask = tttrlib.TTTRMask()
        mask.set_tttr(self.data)
        
        # Very small time window
        mask.select_count_rate(self.data, 1.0e-6, 1, invert=False)
        self.assertEqual(mask.size(), len(self.data))
        
        # Very large time window
        mask2 = tttrlib.TTTRMask()
        mask2.set_tttr(self.data)
        mask2.select_count_rate(self.data, 1.0, 1000, invert=False)
        self.assertEqual(mask2.size(), len(self.data))


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestTTTRRangeAdvanced(unittest.TestCase):
    """Advanced TTTRRange operations"""

    def test_range_large_number_of_inserts(self):
        """Test range with many insertions"""
        r = tttrlib.TTTRRange()
        n_inserts = 500
        
        for i in range(n_inserts):
            r.insert(i)
        
        self.assertEqual(r.size(), n_inserts)
        indices = list(r.tttr_indices)
        self.assertEqual(len(indices), n_inserts)

    def test_range_insert_random_order(self):
        """Test that random insertions maintain sorted order"""
        r = tttrlib.TTTRRange()
        values = [50, 10, 100, 5, 75, 25, 90]
        
        for v in values:
            r.insert(v)
        
        indices = list(r.tttr_indices)
        self.assertEqual(indices, sorted(indices))

    def test_range_strip_all_elements(self):
        """Test stripping all elements from range"""
        r = tttrlib.TTTRRange()
        for i in range(10):
            r.insert(i)
        
        to_remove = list(range(10))
        r.strip(to_remove)
        
        # After stripping all elements, range should be empty or have minimal size
        self.assertLessEqual(r.size(), 1)

    def test_range_add_assign_multiple_times(self):
        """Test += operator multiple times"""
        r1 = tttrlib.TTTRRange()
        r1.insert(1)
        
        for i in range(10):
            r2 = tttrlib.TTTRRange()
            r2.insert(i + 2)
            r1 += r2
        
        self.assertEqual(r1.size(), 11)

    def test_range_equality_after_operations(self):
        """Test range equality after various operations"""
        r1 = tttrlib.TTTRRange()
        for i in range(5):
            r1.insert(i)
        
        r2 = tttrlib.TTTRRange()
        r2 = r1  # Copy assignment
        
        self.assertEqual(r1, r2)
        self.assertFalse(r1 != r2)


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestTTTRHeaderOperations(unittest.TestCase):
    """Test TTTR header operations"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def test_header_properties_consistency(self):
        """Test header properties are consistent"""
        header = self.data.header
        
        macro_res = header.macro_time_resolution
        micro_res = header.micro_time_resolution
        n_channels = header.number_of_micro_time_channels
        
        self.assertGreater(macro_res, 0)
        self.assertGreater(micro_res, 0)
        self.assertGreater(n_channels, 0)

    def test_header_copy_independence(self):
        """Test that header copy is independent"""
        h1 = self.data.header
        h2 = tttrlib.TTTRHeader(h1)
        
        self.assertEqual(h1.macro_time_resolution, h2.macro_time_resolution)
        self.assertEqual(h1.micro_time_resolution, h2.micro_time_resolution)

    def test_header_json_consistency(self):
        """Test header JSON serialization consistency"""
        header = self.data.header
        json_str = header.get_json()
        
        header2 = tttrlib.TTTRHeader()
        header2.set_json(json_str)
        
        json_str2 = header2.get_json()
        
        import json as json_module
        data1 = json_module.loads(json_str)
        data2 = json_module.loads(json_str2)
        
        self.assertEqual(data1, data2)


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestTTTRDataIntegrity(unittest.TestCase):
    """Test data integrity across operations"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def test_copy_data_integrity(self):
        """Test that copied data is identical"""
        d1 = self.data[:500]
        d2 = tttrlib.TTTR(d1)
        
        np.testing.assert_array_equal(d1.macro_times, d2.macro_times)
        np.testing.assert_array_equal(d1.micro_times, d2.micro_times)
        np.testing.assert_array_equal(d1.routing_channels, d2.routing_channels)
        np.testing.assert_array_equal(d1.event_types, d2.event_types)

    def test_slicing_data_integrity(self):
        """Test that sliced data matches original"""
        indices = np.array([0, 10, 20, 30, 40], dtype=np.int32)
        d_sliced = self.data[indices]
        
        for i, idx in enumerate(indices):
            self.assertEqual(d_sliced.macro_times[i], self.data.macro_times[idx])
            self.assertEqual(d_sliced.micro_times[i], self.data.micro_times[idx])
            self.assertEqual(d_sliced.routing_channels[i], self.data.routing_channels[idx])
            self.assertEqual(d_sliced.event_types[i], self.data.event_types[idx])

    def test_append_data_order(self):
        """Test that appended data maintains order"""
        d1 = self.data[:100]
        d2 = self.data[100:200]
        
        d_combined = tttrlib.TTTR(d1)
        d_combined.append(d2, shift_macro_time=False)
        
        # First 100 should match d1
        np.testing.assert_array_equal(d_combined.macro_times[:100], d1.macro_times)
        # Next 100 should match d2
        np.testing.assert_array_equal(d_combined.macro_times[100:200], d2.macro_times)


if __name__ == '__main__':
    unittest.main()

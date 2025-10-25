"""Tests inspired by example files - compression, memory, and batch operations"""
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


class TestTTTRBatchOperations(unittest.TestCase):
    """Tests for batch operations inspired by memory_optimization_example.py"""

    def tearDown(self):
        gc.collect()

    def test_append_events_basic(self):
        """Test basic append_events with arrays"""
        data = tttrlib.TTTR()
        n_events = 1000
        
        macro_times = np.arange(n_events, dtype=np.uint64) * 100
        micro_times = np.arange(n_events, dtype=np.uint16) % 4096
        channels = np.arange(n_events, dtype=np.int8) % 4
        types = np.zeros(n_events, dtype=np.int8)
        
        data.append_events(macro_times, micro_times, channels, types)
        
        self.assertEqual(len(data), n_events)

    def test_append_events_preserves_data(self):
        """Test that append_events preserves data correctly"""
        data = tttrlib.TTTR()
        n_events = 100
        
        macro_times = np.arange(n_events, dtype=np.uint64) * 100
        micro_times = np.arange(n_events, dtype=np.uint16) % 4096
        channels = np.arange(n_events, dtype=np.int8) % 4
        types = np.zeros(n_events, dtype=np.int8)
        
        data.append_events(macro_times, micro_times, channels, types)
        
        np.testing.assert_array_equal(data.macro_times, macro_times)
        np.testing.assert_array_equal(data.micro_times, micro_times)

    def test_reserve_capacity(self):
        """Test reserve() for pre-allocation"""
        data = tttrlib.TTTR()
        n_events = 10000
        
        data.reserve(n_events)
        capacity = data.get_capacity()
        
        self.assertGreaterEqual(capacity, n_events)

    def test_shrink_to_fit(self):
        """Test shrink_to_fit() after operations"""
        data = tttrlib.TTTR()
        n_events = 1000
        
        macro_times = np.arange(n_events, dtype=np.uint64) * 100
        micro_times = np.arange(n_events, dtype=np.uint16) % 4096
        channels = np.zeros(n_events, dtype=np.int8)
        types = np.zeros(n_events, dtype=np.int8)
        
        data.append_events(macro_times, micro_times, channels, types)
        
        # Shrink to fit
        data.shrink_to_fit()
        
        # Capacity should be close to size
        capacity = data.get_capacity()
        self.assertLessEqual(capacity, n_events * 1.1)  # Allow 10% overhead

    def test_get_memory_usage(self):
        """Test get_memory_usage_bytes()"""
        data = tttrlib.TTTR()
        n_events = 1000
        
        macro_times = np.arange(n_events, dtype=np.uint64) * 100
        micro_times = np.arange(n_events, dtype=np.uint16) % 4096
        channels = np.zeros(n_events, dtype=np.int8)
        types = np.zeros(n_events, dtype=np.int8)
        
        data.append_events(macro_times, micro_times, channels, types)
        
        memory = data.get_memory_usage_bytes()
        self.assertGreater(memory, 0)

    def test_get_capacity(self):
        """Test get_capacity()"""
        data = tttrlib.TTTR()
        n_events = 1000
        
        macro_times = np.arange(n_events, dtype=np.uint64) * 100
        micro_times = np.arange(n_events, dtype=np.uint16) % 4096
        channels = np.zeros(n_events, dtype=np.int8)
        types = np.zeros(n_events, dtype=np.int8)
        
        data.append_events(macro_times, micro_times, channels, types)
        
        capacity = data.get_capacity()
        size = data.size()
        
        self.assertGreaterEqual(capacity, size)

    def test_append_event_single(self):
        """Test append_event() for single events"""
        data = tttrlib.TTTR()
        
        for i in range(100):
            data.append_event(i * 100, i % 4096, i % 4, 0)
        
        self.assertEqual(len(data), 100)

    def test_multiple_batch_appends(self):
        """Test multiple batch append operations"""
        data = tttrlib.TTTR()
        
        for batch in range(5):
            n_events = 100
            macro_times = np.arange(n_events, dtype=np.uint64) * 100 + batch * 10000
            micro_times = np.arange(n_events, dtype=np.uint16) % 4096
            channels = np.zeros(n_events, dtype=np.int8)
            types = np.zeros(n_events, dtype=np.int8)
            
            data.append_events(macro_times, micro_times, channels, types)
        
        self.assertEqual(len(data), 500)


class TestTTTRCompressionPlaceholder(unittest.TestCase):
    """Placeholder for compression tests - methods not exposed in Python bindings"""

    def test_placeholder(self):
        """Placeholder test"""
        # Compression methods are not exposed in Python bindings
        # This is a placeholder for future implementation
        pass


class TestTTTRSelect(unittest.TestCase):
    """Tests for select() method inspired by examples"""

    def tearDown(self):
        gc.collect()

    def test_select_basic(self):
        """Test basic select() operation"""
        data = tttrlib.TTTR()
        n_events = 1000
        
        macro_times = np.arange(n_events, dtype=np.uint64) * 100
        micro_times = np.arange(n_events, dtype=np.uint16) % 4096
        channels = np.arange(n_events, dtype=np.int8) % 4
        types = np.zeros(n_events, dtype=np.int8)
        
        data.append_events(macro_times, micro_times, channels, types)
        
        # Select every 10th event
        selection = np.arange(0, n_events, 10, dtype=np.int32)
        selected = data.select(selection)
        
        self.assertEqual(len(selected), len(selection))

    def test_select_preserves_data(self):
        """Test that select() preserves selected data"""
        data = tttrlib.TTTR()
        n_events = 100
        
        macro_times = np.arange(n_events, dtype=np.uint64) * 100
        micro_times = np.arange(n_events, dtype=np.uint16) % 4096
        channels = np.zeros(n_events, dtype=np.int8)
        types = np.zeros(n_events, dtype=np.int8)
        
        data.append_events(macro_times, micro_times, channels, types)
        
        # Select specific indices
        indices = np.array([0, 10, 20, 30], dtype=np.int32)
        selected = data.select(indices)
        
        for i, idx in enumerate(indices):
            self.assertEqual(selected.macro_times[i], macro_times[idx])

    def test_select_empty(self):
        """Test select() with empty selection"""
        data = tttrlib.TTTR()
        n_events = 100
        
        macro_times = np.arange(n_events, dtype=np.uint64) * 100
        micro_times = np.arange(n_events, dtype=np.uint16) % 4096
        channels = np.zeros(n_events, dtype=np.int8)
        types = np.zeros(n_events, dtype=np.int8)
        
        data.append_events(macro_times, micro_times, channels, types)
        
        # Empty selection
        selection = np.array([], dtype=np.int32)
        selected = data.select(selection)
        
        self.assertEqual(len(selected), 0)

    def test_select_with_shrink(self):
        """Test select() followed by shrink_to_fit()"""
        data = tttrlib.TTTR()
        n_events = 1000
        
        macro_times = np.arange(n_events, dtype=np.uint64) * 100
        micro_times = np.arange(n_events, dtype=np.uint16) % 4096
        channels = np.zeros(n_events, dtype=np.int8)
        types = np.zeros(n_events, dtype=np.int8)
        
        data.append_events(macro_times, micro_times, channels, types)
        
        # Select and shrink
        selection = np.arange(0, n_events, 10, dtype=np.int32)
        selected = data.select(selection)
        selected.shrink_to_fit()
        
        self.assertEqual(len(selected), len(selection))


class TestTTTRGetArrays(unittest.TestCase):
    """Tests for get_*() methods inspired by examples"""

    def tearDown(self):
        gc.collect()

    def test_get_macro_times(self):
        """Test get_macro_times()"""
        data = tttrlib.TTTR()
        n_events = 100
        
        macro_times = np.arange(n_events, dtype=np.uint64) * 100
        micro_times = np.arange(n_events, dtype=np.uint16) % 4096
        channels = np.zeros(n_events, dtype=np.int8)
        types = np.zeros(n_events, dtype=np.int8)
        
        data.append_events(macro_times, micro_times, channels, types)
        
        result = data.get_macro_times()
        np.testing.assert_array_equal(result, macro_times)

    def test_get_micro_times(self):
        """Test get_micro_times()"""
        data = tttrlib.TTTR()
        n_events = 100
        
        macro_times = np.arange(n_events, dtype=np.uint64) * 100
        micro_times = np.arange(n_events, dtype=np.uint16) % 4096
        channels = np.zeros(n_events, dtype=np.int8)
        types = np.zeros(n_events, dtype=np.int8)
        
        data.append_events(macro_times, micro_times, channels, types)
        
        result = data.get_micro_times()
        np.testing.assert_array_equal(result, micro_times)

    def test_get_routing_channel(self):
        """Test get_routing_channel()"""
        data = tttrlib.TTTR()
        n_events = 100
        
        macro_times = np.arange(n_events, dtype=np.uint64) * 100
        micro_times = np.arange(n_events, dtype=np.uint16) % 4096
        channels = np.arange(n_events, dtype=np.int8) % 4
        types = np.zeros(n_events, dtype=np.int8)
        
        data.append_events(macro_times, micro_times, channels, types)
        
        result = data.get_routing_channel()
        np.testing.assert_array_equal(result, channels)

    def test_get_event_type(self):
        """Test get_event_type()"""
        data = tttrlib.TTTR()
        n_events = 100
        
        macro_times = np.arange(n_events, dtype=np.uint64) * 100
        micro_times = np.arange(n_events, dtype=np.uint16) % 4096
        channels = np.zeros(n_events, dtype=np.int8)
        types = np.arange(n_events, dtype=np.int8) % 2
        
        data.append_events(macro_times, micro_times, channels, types)
        
        result = data.get_event_type()
        np.testing.assert_array_equal(result, types)


if __name__ == '__main__':
    unittest.main()

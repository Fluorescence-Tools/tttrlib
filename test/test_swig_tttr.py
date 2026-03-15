"""SWIG-exposed TTTR tests - split for memory efficiency"""
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
class TestTTTRSWIGCoverage(unittest.TestCase):
    """TTTR SWIG-exposed methods"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_shift_macro_time(self):
        d = tttrlib.TTTR(self.data[:100])
        original_first = d.macro_times[0]
        shift = 1000
        d.shift_macro_time(shift)
        self.assertEqual(d.macro_times[0], original_first + shift)

    def test_find_used_routing_channels(self):
        d = tttrlib.TTTR(self.data)
        used_ch = d.get_used_routing_channels()
        self.assertGreater(len(used_ch), 0)

    def test_get_macro_time_at(self):
        d = tttrlib.TTTR()
        d.append_event(100, 50, 1, 0, shift_macro_time=False)
        d.append_event(200, 60, 1, 0, shift_macro_time=False)
        self.assertEqual(d.get_macro_time_at(0), 100)
        self.assertEqual(d.get_macro_time_at(1), 200)

    def test_tttr_len(self):
        d = tttrlib.TTTR()
        d.append_event(100, 50, 1, 0)
        d.append_event(200, 60, 1, 0)
        self.assertEqual(len(d), 2)

    def test_tttr_getitem_slice(self):
        d = self.data[0:50]
        self.assertEqual(len(d), 50)

    def test_acquisition_time_property(self):
        d = tttrlib.TTTR(self.data[:100])
        acq_time = d.acquisition_time
        self.assertGreater(acq_time, 0)


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestTTTRAdvancedOperations(unittest.TestCase):
    """Advanced TTTR operations"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_multiple_slicing(self):
        d1 = self.data[0:200]
        d2 = d1[0:50]
        d3 = d2[0:10]
        self.assertEqual(len(d3), 10)

    def test_channel_filtering(self):
        channels = self.data.get_used_routing_channels()
        for ch in channels:
            d_ch = self.data.get_tttr_by_channel([ch])
            self.assertGreater(len(d_ch), 0)

    def test_data_preservation(self):
        d1 = self.data[:100]
        d2 = tttrlib.TTTR(d1)
        np.testing.assert_array_equal(d1.micro_times, d2.micro_times)

    def test_slicing_with_step(self):
        for step in [1, 2, 5]:
            d = self.data[0:100:step]
            expected_len = len(range(0, 100, step))
            self.assertEqual(len(d), expected_len)

    def test_append_operation(self):
        d1 = tttrlib.TTTR(self.data[:50])
        d2 = tttrlib.TTTR(self.data[50:100])
        d1.append(d2, shift_macro_time=False)
        self.assertEqual(len(d1), 100)


class TestTTTRBasic(unittest.TestCase):
    """Basic TTTR operations"""

    def tearDown(self):
        gc.collect()

    def test_tttr_empty_creation(self):
        d = tttrlib.TTTR()
        self.assertEqual(len(d), 0)

    def test_tttr_append_event(self):
        d = tttrlib.TTTR()
        d.append_event(100, 50, 1, 0)
        self.assertEqual(len(d), 1)
        self.assertEqual(d.macro_times[0], 100)
        self.assertEqual(d.micro_times[0], 50)

    def test_tttr_multiple_events(self):
        d = tttrlib.TTTR()
        for i in range(10):
            d.append_event(i * 100, 50 + i, 1, 0)
        self.assertEqual(len(d), 10)

    def test_tttr_routing_channels_property(self):
        d = tttrlib.TTTR()
        d.append_event(100, 50, 1, 0)
        d.append_event(200, 60, 2, 0)
        channels = d.routing_channels
        self.assertEqual(len(channels), 2)

    def test_tttr_event_types_property(self):
        d = tttrlib.TTTR()
        d.append_event(100, 50, 1, 0)
        d.append_event(200, 60, 1, 1)
        event_types = d.event_types
        self.assertEqual(len(event_types), 2)


if __name__ == '__main__':
    unittest.main()

from __future__ import division

import unittest
import numpy as np
import tempfile

import tttrlib

# Centralized test settings
from test_settings import settings, DATA_AVAILABLE  # type: ignore

data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found, skipping TTTRMask tests")
class Tests(unittest.TestCase):

    def test_set_tttr(self):
        mask = tttrlib.TTTRMask()
        mask.set_tttr(data)
        self.assertEqual(mask.size(), data.size())

    def test_set_get_mask(self):
        m = [True, False, False]
        mask = tttrlib.TTTRMask()
        mask.set_mask(m)
        self.assertEqual(mask.size(), len(m))
        mg = mask.get_mask()
        self.assertListEqual(list(mg), m)

    def test_get_indices(self):
        mask = tttrlib.TTTRMask()
        mask.set_tttr(data)
        mask.select_channels(data, [0, 1])

    def test_get_selected_ranges(self):
        pass

    def test_select_microtime_ranges(self):
        pass

    def test_select_count_rate(self):
        pass

    def test_select_channels(self):
        pass

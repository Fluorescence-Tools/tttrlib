"""Canonical cross-language reference values.

The SAME assertions are checked in the R (``test/r/test_tttr.R``) and Java
(``test/java/TTTRSmokeTest.java``) test suites, so the three language bindings
are verified to read identical data from the same file.

Reference file: ``bh/bh_spc132.spc`` read as container type ``SPC-130``.
"""
from __future__ import annotations

import unittest

import tttrlib

from test_settings import settings, DATA_AVAILABLE, get_data_path  # type: ignore

# Canonical values (source of truth for all three language bindings).
REF_SIZE = 183657
REF_N_MICRO_CHANNELS = 4096
REF_MACRO_FIRST = 56916
REF_SUM_MICRO = 242477881
REF_SUM_MACRO = 443406877425185
REF_SUM_ROUTING = 880650
REF_CORRCURVE_SIZE = 16  # CorrelatorCurve(n_bins=3, n_casc=5)
# burst_search(L=30, m=10, T=1e-3, mode="sliding_window") -> flat [start, stop, ...]
REF_BURST_LEN = 586
REF_BURST_SUM = 59237329
# micro-time histogram (coarsening = 1)
REF_HIST_LEN = 4096
REF_HIST_PEAK_CHAN = 814
REF_HIST_PEAK_VAL = 676
# index getters (event 0) and header
REF_MICRO_AT_0 = 1440
REF_ROUTING_AT_0 = 9
REF_MICRO_RES = 3.2958984375e-12
# sub-selections
REF_BY_CHANNEL_0 = 56499
REF_BY_CHANNEL_8 = 79468
REF_USED_CHANNELS = [0, 1, 8, 9]


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class CrossLanguageReference(unittest.TestCase):
    def setUp(self):
        self.data = tttrlib.TTTR(get_data_path(settings["spc132_filename"]), "SPC-130")

    def test_size(self):
        self.assertEqual(self.data.size(), REF_SIZE)
        self.assertEqual(self.data.get_n_valid_events(), REF_SIZE)

    def test_micro_time_channels(self):
        self.assertEqual(self.data.get_number_of_micro_time_channels(), REF_N_MICRO_CHANNELS)

    def test_macro_times(self):
        mt = self.data.macro_times
        self.assertEqual(len(mt), REF_SIZE)
        self.assertEqual(int(mt[0]), REF_MACRO_FIRST)
        self.assertEqual(int(mt.sum()), REF_SUM_MACRO)

    def test_micro_times(self):
        self.assertEqual(int(self.data.micro_times.sum()), REF_SUM_MICRO)

    def test_routing_channels(self):
        self.assertEqual(int(self.data.routing_channels.sum()), REF_SUM_ROUTING)

    def test_correlator_curve(self):
        cc = tttrlib.CorrelatorCurve()
        cc.n_bins = 3
        cc.n_casc = 5
        self.assertEqual(cc.size(), REF_CORRCURVE_SIZE)

    def test_burst_search(self):
        import numpy as np
        r = np.asarray(self.data.burst_search(30, 10, 1e-3, "sliding_window"))
        self.assertEqual(len(r), REF_BURST_LEN)
        self.assertEqual(int(r.sum()), REF_BURST_SUM)

    def test_microtime_histogram(self):
        import numpy as np
        hist, _ = self.data.get_microtime_histogram(micro_time_coarsening=1)
        hist = np.asarray(hist)
        self.assertEqual(len(hist), REF_HIST_LEN)
        self.assertEqual(int(np.argmax(hist)), REF_HIST_PEAK_CHAN)
        self.assertEqual(int(hist.max()), REF_HIST_PEAK_VAL)

    def test_index_getters(self):
        self.assertEqual(int(self.data.get_macro_time_at(0)), REF_MACRO_FIRST)
        self.assertEqual(int(self.data.get_micro_time_at(0)), REF_MICRO_AT_0)
        self.assertEqual(int(self.data.get_routing_channel_at(0)), REF_ROUTING_AT_0)

    def test_micro_time_resolution(self):
        self.assertAlmostEqual(self.data.header.micro_time_resolution, REF_MICRO_RES, places=18)

    def test_tttr_by_channel(self):
        self.assertEqual(self.data.get_tttr_by_channel([0]).size(), REF_BY_CHANNEL_0)
        self.assertEqual(self.data.get_tttr_by_channel([8]).size(), REF_BY_CHANNEL_8)

    def test_used_routing_channels(self):
        self.assertEqual(sorted(int(x) for x in self.data.get_used_routing_channels()), REF_USED_CHANNELS)

    def test_header_json(self):
        self.assertIn("MeasDesc_ContainerType", self.data.header.get_json())


if __name__ == "__main__":
    unittest.main()

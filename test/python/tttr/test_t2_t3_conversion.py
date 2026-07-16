# SPDX-License-Identifier: BSD-3-Clause
"""Tests for T2 <-> T3 record-mode conversion.

T2 records carry a single fine time tag (no micro time); T3 records reference
each photon to a sync period (macro time = sync index, micro time = dtime).
``TTTR.t2_to_t3`` and ``TTTR.t3_to_t2`` re-derive one representation from the
other. T2 -> T3 -> T2 is lossless for a fixed sync period; T3 -> T2 preserves
absolute arrival times at TAC resolution but drops the dtime/sync split.
"""
import os
import sys
import tempfile
import unittest

import numpy as np
import tttrlib

sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
from test_settings import settings, DATA_AVAILABLE  # type: ignore

# tttrlib record-type constants (see include/TTTRHeaderTypes.h)
PQ_RECORD_TYPE_HHT2v2 = 1
PQ_RECORD_TYPE_HHT3v2 = 4


def _synthetic_t2(n=1000, seed=1, n_channels=4):
    rng = np.random.RandomState(seed)
    time_tags = np.cumsum(rng.randint(1, 50, n)).astype(np.uint64)
    micro = np.zeros(n, np.uint16)
    channels = rng.randint(0, n_channels, n).astype(np.int8)
    event_types = np.zeros(n, np.int8)
    t2 = tttrlib.TTTR(time_tags, micro, channels, event_types)
    t2.header.tttr_record_type = PQ_RECORD_TYPE_HHT2v2
    return t2, time_tags, channels


class TestT2T3Synthetic(unittest.TestCase):

    def test_t2_to_t3_derivation(self):
        t2, tt, _ = _synthetic_t2()
        period = 128
        t3 = t2.t2_to_t3(sync_period=period)
        self.assertEqual(t3.header.tttr_record_type, PQ_RECORD_TYPE_HHT3v2)
        np.testing.assert_array_equal(
            np.asarray(t3.macro_times).astype(np.uint64), tt // period)
        np.testing.assert_array_equal(
            np.asarray(t3.micro_times).astype(np.uint64), tt % period)

    def test_t2_t3_t2_roundtrip_lossless(self):
        t2, tt, ch = _synthetic_t2()
        period = 256
        t3 = t2.t2_to_t3(sync_period=period)
        t2b = t3.t3_to_t2()
        self.assertEqual(t2b.header.tttr_record_type, PQ_RECORD_TYPE_HHT2v2)
        np.testing.assert_array_equal(
            np.asarray(t2b.macro_times).astype(np.uint64), tt)
        self.assertEqual(np.asarray(t2b.micro_times).max(initial=0), 0)
        np.testing.assert_array_equal(t2b.routing_channels, ch)

    def test_sync_rate_matches_sync_period(self):
        t2, tt, _ = _synthetic_t2()
        # With a 1 s macro-time resolution, a 1/128 Hz "rate" -> 128-unit period.
        t2.header.set_macro_time_resolution(1.0)
        by_rate = t2.t2_to_t3(sync_rate=1.0 / 128)
        by_period = t2.t2_to_t3(sync_period=128)
        np.testing.assert_array_equal(by_rate.macro_times, by_period.macro_times)
        np.testing.assert_array_equal(by_rate.micro_times, by_period.micro_times)

    def test_t2_to_t3_write_ptu(self):
        t2, tt, _ = _synthetic_t2()
        period = 64
        t3 = t2.t2_to_t3(sync_period=period)
        fn = tempfile.mktemp(suffix=".ptu")
        try:
            self.assertTrue(t3.write(fn))
            back = tttrlib.TTTR(fn)
            self.assertEqual(back.header.tttr_record_type, PQ_RECORD_TYPE_HHT3v2)
            np.testing.assert_array_equal(
                np.asarray(back.macro_times).astype(np.uint64), tt // period)
            np.testing.assert_array_equal(
                np.asarray(back.micro_times).astype(np.uint64), tt % period)
        finally:
            if os.path.isfile(fn):
                os.unlink(fn)

    def test_invalid_period_returns_none(self):
        t2, _, _ = _synthetic_t2()
        # No sync_rate and no sync_period, header has no usable rate.
        self.assertIsNone(t2.t2_to_t3())


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestT2T3RealData(unittest.TestCase):

    def test_real_t3_to_t2_preserves_arrival_time(self):
        fn = settings["ptu_hh_t3_filename"]
        if not os.path.isfile(fn):
            self.skipTest("missing data file: %s" % fn)
        t3 = tttrlib.TTTR(fn, "PTU")
        n_micro = t3.header.get_effective_number_of_micro_time_channels()
        expected = (np.asarray(t3.macro_times).astype(np.uint64) * n_micro
                    + np.asarray(t3.micro_times).astype(np.uint64))
        t2 = t3.t3_to_t2()
        self.assertEqual(t2.header.tttr_record_type, PQ_RECORD_TYPE_HHT2v2)
        np.testing.assert_array_equal(
            np.asarray(t2.macro_times).astype(np.uint64), expected)
        self.assertEqual(np.asarray(t2.micro_times).max(initial=0), 0)

    def test_real_t3_t2_t3_roundtrip(self):
        fn = settings["ptu_hh_t3_filename"]
        if not os.path.isfile(fn):
            self.skipTest("missing data file: %s" % fn)
        t3 = tttrlib.TTTR(fn, "PTU")
        n_micro = t3.header.get_effective_number_of_micro_time_channels()
        macro0 = np.asarray(t3.macro_times).astype(np.uint64)
        micro0 = np.asarray(t3.micro_times).astype(np.uint64)
        # Absolute arrival time (in TAC units) is what survives the round trip.
        # Exact (n_sync, dtime) is only recovered when every dtime < n_micro; on
        # real data a few photons have dtime >= n_micro (TAC beyond the sync
        # period), so compare the merged absolute time instead.
        abs0 = macro0 * n_micro + micro0
        t2 = t3.t3_to_t2()
        t3b = t2.t2_to_t3(sync_period=n_micro)
        abs1 = (np.asarray(t3b.macro_times).astype(np.uint64) * n_micro
                + np.asarray(t3b.micro_times).astype(np.uint64))
        np.testing.assert_array_equal(abs1, abs0)


if __name__ == "__main__":
    unittest.main()

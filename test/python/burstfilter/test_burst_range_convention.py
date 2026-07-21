"""Burst index ranges are inclusive on both ends, everywhere.

A burst spanning photons 0..29 is reported as ``[0, 29]`` and contains 30
photons. This was not always consistent: BVA and H2MM indexed their ranges
half-open while every producer emitted inclusive ones, so both silently dropped
each burst's last photon -- a 5% count error on a 20-photon burst, and a biased
one, since the dropped photon is the photon that ended the burst.

These tests pin the convention across producers and consumers together, which is
what the previous per-component tests could not do: each agreed with itself.
"""
import unittest

import numpy as np
import tttrlib

RES = 1e-8  # 10 ns macro-time tick


def make_tttr(groups, channels=None):
    """Build a TTTR of dense photon groups separated by long dark gaps.

    Returns (tttr, bounds) with bounds the inclusive [start, stop] index pairs.
    """
    times, chans, bounds = [], [], []
    t = 0
    for gi, n in enumerate(groups):
        start = len(times)
        for i in range(n):
            t += 100  # 1 us spacing -> dense
            times.append(t)
            chans.append(0 if channels is None else channels[gi][i])
        bounds.append((start, len(times) - 1))  # inclusive
        t += 10_000_000  # 0.1 s dark gap
    n_tot = len(times)
    d = tttrlib.TTTR()
    d.append_events(
        np.asarray(times, dtype=np.uint64),
        np.zeros(n_tot, np.uint16),
        np.asarray(chans, dtype=np.int8),
        np.zeros(n_tot, np.int8))
    d.header.set_macro_time_resolution(RES)
    return d, np.asarray(bounds, dtype=np.int64)


class TestBurstRangeConvention(unittest.TestCase):

    def test_searches_return_inclusive_ranges(self):
        # A 30-photon group must come back as [0, 29], not [0, 30].
        d, bounds = make_tttr([30, 30])
        for name, bursts in [
            ("sliding_window", d.burst_search_sliding_window(5, 5, 1e-5)),
            ("burst_search", d.burst_search(5, 5, 1e-5)),
        ]:
            b = np.asarray(bursts).reshape(-1, 2)
            self.assertEqual(len(b), 2, name)
            np.testing.assert_array_equal(b, bounds, err_msg=name)
            for s, e in b:
                self.assertEqual(e - s + 1, 30, f"{name}: inclusive size")

    def test_burstfilter_reports_inclusive_size(self):
        d, bounds = make_tttr([30, 30])
        bf = tttrlib.BurstFilter(d)
        bf.set_burst_parameters(5, 5, 1e-5)
        bf.find_bursts()
        props = np.asarray(bf.get_all_burst_properties())
        self.assertEqual(len(props), 2)
        for p in props:
            self.assertEqual(int(p[2]), 30)

    def test_get_burst_photons_returns_every_photon(self):
        d, bounds = make_tttr([30, 30])
        bf = tttrlib.BurstFilter(d)
        bf.set_burst_parameters(5, 5, 1e-5)
        bf.find_bursts()
        sub = bf.get_burst_photons(bounds)
        self.assertEqual(len(np.asarray(sub.macro_times)), 60)

    def test_h2mm_keeps_the_last_photon_of_each_burst(self):
        # The regression this file exists for. With half-open indexing H2MM saw
        # 29 of every 30 photons.
        d, bounds = make_tttr([30, 30])
        g = tttrlib.Channel('g')
        g.add_component(0, 0, 65535)
        eng = tttrlib.H2MM()
        eng.set_bursts_from_tttr(d, bounds, [g], 3, 1)
        self.assertEqual(eng.get_n_bursts(), 2)
        self.assertEqual(eng.get_n_photons(), 60)

    def test_bva_counts_every_photon_of_each_burst(self):
        # Every photon is donor except the very last one of each burst, and the
        # whole burst is one slice. The proximity ratio is then 1/30 if the final
        # photon is seen and exactly 0 if it is not, so the off-by-one cannot
        # hide in an average.
        n = 30
        chans = [[1 if i == n - 1 else 0 for i in range(n)] for _ in range(2)]
        d, bounds = make_tttr([n, n], channels=chans)
        bva = tttrlib.BVA(d)
        bva.set_donor([0])
        bva.set_acceptor([1])
        bva.compute(bounds, n, 0.01)
        means = np.asarray(bva.get_proximity_ratio_mean())
        self.assertEqual(len(means), 2)
        np.testing.assert_allclose(means, 1.0 / n, atol=1e-9)


if __name__ == '__main__':
    unittest.main()

"""Bayesian Blocks burst search (include/BurstSearchBayesianBlocks.h).

The behaviour worth pinning is mostly statistical: pure background must yield
(almost) nothing, injected bursts must be found, and the parameters must move
the answer in the documented direction.
"""
import unittest

import numpy as np
import tttrlib

RES = 1e-8  # 10 ns macro-time tick


def tttr_from_times(times_s):
    t = np.sort(np.asarray(np.round(np.asarray(times_s) / RES), dtype=np.uint64))
    n = len(t)
    d = tttrlib.TTTR()
    d.append_events(t, np.zeros(n, np.uint16), np.zeros(n, np.int8),
                    np.zeros(n, np.int8))
    d.header.set_macro_time_resolution(RES)
    return d


def background(duration=5.0, rate=5000.0, seed=0):
    return np.sort(np.random.default_rng(seed).uniform(0, duration, int(duration * rate)))


def inject(bg, specs, seed=1):
    """Add Gaussian-profile transits: specs is a list of (t0, fwhm_s, n_photons)."""
    rng = np.random.default_rng(seed)
    out = list(bg)
    for t0, width, nph in specs:
        out.extend(rng.normal(t0, width / 2.355, nph))
    return np.sort(np.asarray(out))


class TestBayesianBlocksNullCase(unittest.TestCase):
    """Pure Poisson background must not produce bursts."""

    def test_constant_rate_yields_almost_nothing(self):
        d = tttr_from_times(background(duration=5.0, rate=5000.0, seed=3))
        b = np.asarray(d.burst_search_bayesian_blocks(20, 10, 0.05)).reshape(-1, 2)
        # The whole point of a calibrated search: shot noise alone must not look
        # like molecules. Allow a couple over 5 s rather than demanding zero.
        self.assertLessEqual(len(b), 3, f"got {len(b)} false bursts on pure background")

    def test_empty_and_tiny_inputs(self):
        d = tttrlib.TTTR()
        self.assertEqual(len(np.asarray(d.burst_search_bayesian_blocks())), 0)
        for n in (1, 2, 5, 15):
            d = tttr_from_times(np.arange(n) * 1e-5)
            out = np.asarray(d.burst_search_bayesian_blocks(20, 10, 0.05))
            self.assertEqual(len(out) % 2, 0)


class TestBayesianBlocksDetection(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.specs = [(1.0, 1e-3, 25), (2.0, 1e-3, 60), (3.0, 1e-3, 150),
                     (4.0, 2e-3, 40)]
        cls.times = inject(background(duration=5.0, rate=5000.0, seed=3), cls.specs)
        cls.d = tttr_from_times(cls.times)
        cls.t = np.asarray(cls.d.macro_times, dtype=np.float64) * RES

    def _hits(self, bursts):
        b = np.asarray(bursts).reshape(-1, 2)
        return sum(any(self.t[s] <= t0 <= self.t[e] for s, e in b)
                   for (t0, _, _) in self.specs)

    def test_finds_injected_bursts(self):
        b = self.d.burst_search_bayesian_blocks(15, 10, 0.05)
        self.assertEqual(self._hits(b), len(self.specs))

    def test_ranges_are_valid_ordered_and_disjoint(self):
        b = np.asarray(self.d.burst_search_bayesian_blocks(15, 10, 0.05)).reshape(-1, 2)
        n = len(self.t)
        self.assertTrue(np.all(b[:, 0] >= 0) and np.all(b[:, 1] < n))
        self.assertTrue(np.all(b[:, 1] >= b[:, 0]), "stop must not precede start")
        starts = b[:, 0]
        self.assertTrue(np.all(np.diff(starts) > 0), "must be sorted by start")
        self.assertTrue(np.all(b[1:, 0] > b[:-1, 1]), "must not overlap")

    def test_min_photons_is_respected(self):
        for L in (15, 40, 100):
            b = np.asarray(
                self.d.burst_search_bayesian_blocks(L, 10, 0.05)).reshape(-1, 2)
            if len(b):
                self.assertTrue(np.all(b[:, 1] - b[:, 0] + 1 >= L), f"L={L}")

    def test_raising_L_never_adds_bursts(self):
        counts = [len(np.asarray(self.d.burst_search_bayesian_blocks(L, 10, 0.05))) // 2
                  for L in (15, 30, 60, 120)]
        self.assertTrue(all(b <= a for a, b in zip(counts, counts[1:])), counts)

    def test_raising_significance_never_adds_bursts(self):
        counts = [len(np.asarray(self.d.burst_search_bayesian_blocks(
            15, 10, 0.05, 1.5, 64, 4096, sig))) // 2 for sig in (2.0, 4.0, 6.0, 10.0)]
        self.assertTrue(all(b <= a for a, b in zip(counts, counts[1:])), counts)

    def test_significance_modes_all_work(self):
        for mode in (0, 1, 2):  # Gaussian, exact Poisson, Li & Ma
            b = self.d.burst_search_bayesian_blocks(15, 10, 0.05, 1.5, 64, 4096,
                                                    4.0, 0.0, mode)
            self.assertEqual(self._hits(b), len(self.specs), f"mode={mode}")

    def test_false_alarm_rate_overrides_sigma(self):
        # A punishing false-alarm budget must not yield more bursts than a
        # permissive one.
        strict = len(np.asarray(self.d.burst_search_bayesian_blocks(
            15, 10, 0.05, 1.5, 64, 4096, 4.0, 1e-9))) // 2
        loose = len(np.asarray(self.d.burst_search_bayesian_blocks(
            15, 10, 0.05, 1.5, 64, 4096, 4.0, 10.0))) // 2
        self.assertLessEqual(strict, loose)

    def test_trigger_contrast_does_not_break_detection(self):
        # Stage 1 is tuned for completeness; tightening it a little must not
        # start losing clear bursts.
        for tc in (1.2, 1.5, 2.0):
            b = self.d.burst_search_bayesian_blocks(15, 10, 0.05, tc)
            self.assertEqual(self._hits(b), len(self.specs), f"trigger_contrast={tc}")

    def test_small_region_cap_still_finds_bursts(self):
        # Forces the region-splitting path.
        b = self.d.burst_search_bayesian_blocks(15, 10, 0.05, 1.5, 32, 256)
        self.assertGreaterEqual(self._hits(b), len(self.specs) - 1)


class TestBayesianBlocksViaDispatch(unittest.TestCase):

    def test_mode_string_and_registry(self):
        import json
        algos = json.loads(tttrlib.TTTR.burst_search_algorithms_json())
        self.assertIn("bayesian_blocks", algos)
        self.assertEqual(algos["bayesian_blocks"]["method"],
                         "burst_search_bayesian_blocks")

        times = inject(background(duration=3.0, rate=5000.0, seed=5),
                       [(1.0, 1e-3, 80), (2.0, 1e-3, 80)])
        d = tttr_from_times(times)
        b = np.asarray(d.burst_search(15, 10, 0.05, "bayesian_blocks")).reshape(-1, 2)
        self.assertGreaterEqual(len(b), 2)


if __name__ == '__main__':
    unittest.main()

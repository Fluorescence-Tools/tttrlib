# SPDX-License-Identifier: BSD-3-Clause
"""Max-tree burst search and the burst-search registry."""
from __future__ import annotations

import unittest

import numpy as np

import tttrlib

MACRO_TIME_RESOLUTION = 1e-9


def make_tttr(times_s):
    """A TTTR from photon arrival times in seconds, with a 1 ns macro-time tick."""
    ticks = np.round(np.sort(np.asarray(times_s)) / MACRO_TIME_RESOLUTION)
    ticks = ticks.astype(np.uint64)
    n = ticks.size
    tttr = tttrlib.TTTR(
        ticks,
        np.zeros(n, dtype=np.uint16),
        np.zeros(n, dtype=np.int8),
        np.zeros(n, dtype=np.int8),
    )
    tttr.header.set_macro_time_resolution(MACRO_TIME_RESOLUTION)
    return tttr


def synthetic_stream(seed=0, background_cps=2000.0, duration=6.0, bursts=()):
    """Poisson background plus injected bursts; returns ``(tttr, injected)``."""
    rng = np.random.default_rng(seed)
    times = [rng.uniform(0.0, duration, rng.poisson(background_cps * duration))]
    injected = []
    for t0, rate, length in bursts:
        n = rng.poisson(rate * length)
        times.append(rng.uniform(t0, t0 + length, n))
        injected.append((t0, t0 + length, n))
    return make_tttr(np.concatenate(times)), injected


class TestMaxTreeBurstSearch(unittest.TestCase):

    def setUp(self):
        # 40 bright (100 kcps) and 40 dim (25 kcps) bursts of 1 ms, interleaved.
        bursts = []
        for k in range(40):
            bursts.append((0.02 + k * 0.14, 100000.0, 1e-3))
            bursts.append((0.02 + k * 0.14 + 0.07, 25000.0, 1e-3))
        self.tttr, self.injected = synthetic_stream(bursts=bursts)
        self.times_s = (
            np.asarray(self.tttr.macro_times, dtype=np.float64) * MACRO_TIME_RESOLUTION
        )

    def bursts(self, **kwargs):
        result = self.tttr.burst_search_maxtree(**kwargs)
        return np.asarray(result, dtype=np.int64).reshape(-1, 2)

    def test_returns_sorted_disjoint_in_range_intervals(self):
        b = self.bursts(L=20, m=10)
        self.assertGreater(len(b), 0)
        self.assertTrue((b[:, 0] <= b[:, 1]).all(), "inverted interval")
        self.assertGreaterEqual(b.min(), 0)
        self.assertLess(b.max(), self.tttr.size())
        self.assertTrue((np.diff(b[:, 0]) > 0).all(), "not sorted by start")
        self.assertTrue((b[1:, 0] > b[:-1, 1]).all(), "bursts overlap")

    def test_finds_bright_and_dim_bursts_together(self):
        """The point of the method: one pass detects both brightness populations."""
        b = self.bursts(L=20, m=10)
        found_bright = found_dim = 0
        for t0, t1, n_photons in self.injected:
            hit = np.any(
                (self.times_s[b[:, 0]] <= t1) & (self.times_s[b[:, 1]] >= t0)
            )
            if n_photons > 60:
                found_bright += bool(hit)
            else:
                found_dim += bool(hit)
        self.assertGreaterEqual(found_bright, 38)
        self.assertGreaterEqual(found_dim, 35)

    def test_min_photons_is_respected(self):
        b = self.bursts(L=60, m=10)
        self.assertTrue(((b[:, 1] - b[:, 0] + 1) >= 60).all())

    def test_significance_filter_removes_background_detections(self):
        """Raising the significance threshold can only remove bursts."""
        loose = len(self.bursts(L=20, m=10, min_significance=0.0))
        tight = len(self.bursts(L=20, m=10, min_significance=8.0))
        self.assertLess(tight, loose)

    def test_duration_bounds(self):
        b = self.bursts(L=20, m=10, max_duration=5e-4)
        if len(b):
            durations = self.times_s[b[:, 1]] - self.times_s[b[:, 0]]
            self.assertTrue((durations <= 5e-4 + 1e-9).all())

    def test_pure_background_yields_few_detections(self):
        """No injected bursts: a search must not invent many of them."""
        tttr, _ = synthetic_stream(seed=3, bursts=())
        b = np.asarray(
            tttr.burst_search_maxtree(L=20, m=10), dtype=np.int64
        ).reshape(-1, 2)
        self.assertLess(len(b), 20)

    def test_mode_dispatch_matches_the_direct_call(self):
        by_mode = np.asarray(
            self.tttr.burst_search(20, 10, 0.05, "maxtree"), dtype=np.int64
        )
        # The dispatch path forwards the library defaults for everything the
        # three-argument signature cannot express, so the direct call must not
        # pin them either -- otherwise this pins a stale default rather than
        # testing that the two routes agree.
        defaults = tttrlib.TTTR.burst_search_defaults("maxtree")
        direct = np.asarray(
            self.tttr.burst_search_maxtree(
                L=20, m=10, delta=defaults["delta"],
                max_variation=defaults["max_variation"],
                background_window=0.05,
                min_contrast=defaults["min_contrast"],
            ),
            dtype=np.int64,
        )
        np.testing.assert_array_equal(by_mode, direct)

    def test_degenerate_inputs(self):
        for times in ([], [0.0], np.arange(5000) * 1e-6, np.zeros(5000)):
            tttr = make_tttr(np.asarray(times, dtype=np.float64))
            out = np.asarray(
                tttr.burst_search_maxtree(L=20, m=10), dtype=np.int64
            ).reshape(-1, 2)
            if len(out):
                self.assertGreaterEqual(out.min(), 0)
                self.assertLess(out.max(), tttr.size())

    # --- exact detection statistics (include/BurstSignificance.h) -------------

    def test_default_significance_mode_reproduces_gaussian_exactly(self):
        """The default must stay bit-identical to the historical behaviour.

        That is the only reason the (approximate) Gaussian statistic is still
        the default rather than the exact one.
        """
        implicit = np.asarray(self.tttr.burst_search_maxtree(L=20, m=10))
        explicit = np.asarray(
            self.tttr.burst_search_maxtree(L=20, m=10, significance_mode=0))
        np.testing.assert_array_equal(implicit, explicit)

    def test_exact_statistics_keep_finding_the_injected_bursts(self):
        """Switching to exact Poisson or Li & Ma must not cost recall."""
        base = len(self.bursts(L=20, m=10))
        for mode in (1, 2):
            b = self.bursts(L=20, m=10, significance_mode=mode)
            self.assertGreater(len(b), 0.5 * base, f"significance_mode={mode}")

    def test_li_ma_is_not_more_permissive_than_gaussian(self):
        """Li & Ma accounts for the background being *measured* rather than
        known, so at the same nominal sigma it cannot accept more candidates."""
        n_gauss = len(self.bursts(L=20, m=10, significance_mode=0))
        n_lima = len(self.bursts(L=20, m=10, significance_mode=2))
        self.assertLessEqual(n_lima, n_gauss)

    def test_exact_statistics_reject_more_pure_background(self):
        """On background alone the exact statistics must be at least as clean."""
        tttr, _ = synthetic_stream(seed=3, bursts=())
        counts = {}
        for mode in (0, 1, 2):
            counts[mode] = len(np.asarray(
                tttr.burst_search_maxtree(L=20, m=10, significance_mode=mode),
                dtype=np.int64).reshape(-1, 2))
        self.assertLessEqual(counts[2], counts[0])

    def test_false_alarm_rate_threshold_is_monotonic(self):
        strict = len(self.bursts(L=20, m=10, significance_mode=2,
                                 max_false_alarm_rate=1e-9))
        loose = len(self.bursts(L=20, m=10, significance_mode=2,
                                max_false_alarm_rate=10.0))
        self.assertLessEqual(strict, loose)


class TestKalmanBurstSearch(unittest.TestCase):

    def setUp(self):
        bursts = [(0.05 * k, 60000.0, 1e-3) for k in range(40)]
        self.tttr, self.injected = synthetic_stream(seed=11, bursts=bursts)
        self.times_s = (
            np.asarray(self.tttr.macro_times, dtype=np.float64) * MACRO_TIME_RESOLUTION
        )

    def bursts(self, **kwargs):
        result = self.tttr.burst_search_kalman(**kwargs)
        return np.asarray(result, dtype=np.int64).reshape(-1, 2)

    def test_returns_sorted_in_range_intervals(self):
        b = self.bursts()
        self.assertGreater(len(b), 0)
        self.assertTrue((b[:, 0] <= b[:, 1]).all())
        self.assertGreaterEqual(b.min(), 0)
        self.assertLess(b.max(), self.tttr.size())
        self.assertTrue((np.diff(b[:, 0]) > 0).all(), "not sorted by start")

    def test_finds_the_injected_bursts(self):
        b = self.bursts(L=20)
        found = sum(
            bool(np.any((self.times_s[b[:, 0]] <= t1) & (self.times_s[b[:, 1]] >= t0)))
            for t0, t1, _ in self.injected
        )
        self.assertGreaterEqual(found, 35)

    def test_raising_the_threshold_only_removes_bursts(self):
        """Monotonic, not strictly decreasing: well-separated bursts survive a
        wide range of thresholds, so only the direction is guaranteed."""
        counts = [len(self.bursts(z_thresh=z)) for z in (3.0, 12.0, 50.0)]
        self.assertEqual(counts, sorted(counts, reverse=True))
        self.assertEqual(len(self.bursts(z_thresh=1e9)), 0)

    def test_min_photons_is_respected(self):
        b = self.bursts(L=40)
        self.assertTrue(((b[:, 1] - b[:, 0] + 1) >= 40).all())

    def test_more_process_noise_covers_less_of_the_trace(self):
        """Sensitivity is coverage, not burst count.

        Burst *count* is not monotonic in q: too small a q floods every bin, and
        the flooded bins merge into one huge burst, so the count drops while the
        search is in fact at its least selective. Coverage is what actually
        tracks sensitivity, and asserting on it catches that collapse.
        """
        def coverage(**kwargs):
            b = self.bursts(**kwargs)
            if not len(b):
                return 0.0
            span = self.times_s[-1] - self.times_s[0]
            return float((self.times_s[b[:, 1]] - self.times_s[b[:, 0]]).sum() / span)

        self.assertLess(coverage(q=1e5), coverage(q=1.0))

    def test_default_does_not_swallow_the_whole_trace(self):
        """The degenerate mode of this method: one burst covering everything."""
        b = self.bursts()
        span = self.times_s[-1] - self.times_s[0]
        covered = (self.times_s[b[:, 1]] - self.times_s[b[:, 0]]).sum()
        self.assertLess(covered / span, 0.5)
        self.assertGreater(len(b), 10)

    def test_pooled_and_per_channel_agree_on_single_channel_data(self):
        """With one routing channel there is nothing to separate."""
        np.testing.assert_array_equal(
            self.bursts(per_channel=True), self.bursts(per_channel=False)
        )

    def test_multichannel_runs_and_stays_in_range(self):
        n = self.tttr.size()
        rng = np.random.default_rng(5)
        channels = rng.integers(0, 2, n).astype(np.int8)
        tttr = tttrlib.TTTR(
            np.asarray(self.tttr.macro_times, dtype=np.uint64),
            np.zeros(n, dtype=np.uint16), channels, np.zeros(n, dtype=np.int8),
        )
        tttr.header.set_macro_time_resolution(MACRO_TIME_RESOLUTION)
        b = np.asarray(
            tttr.burst_search_kalman(per_channel=True), dtype=np.int64
        ).reshape(-1, 2)
        if len(b):
            self.assertGreaterEqual(b.min(), 0)
            self.assertLess(b.max(), n)

    def test_degenerate_inputs(self):
        for times in ([], [0.0], np.zeros(5000)):
            tttr = make_tttr(np.asarray(times, dtype=np.float64))
            out = np.asarray(tttr.burst_search_kalman(), dtype=np.int64).reshape(-1, 2)
            if len(out):
                self.assertGreaterEqual(out.min(), 0)
                self.assertLess(out.max(), tttr.size())


class TestBurstSearchRegistry(unittest.TestCase):

    def test_registry_describes_every_search(self):
        algorithms = tttrlib.TTTR.burst_search_algorithms()
        self.assertLessEqual(
            {"sliding_window", "cusum_sprt", "maxtree", "kalman",
             "bayesian_blocks"},
            set(algorithms)
        )
        for name, spec in algorithms.items():
            self.assertEqual(spec["name"], name)
            for key in ("label", "summary", "description", "method", "params_schema"):
                self.assertTrue(spec[key], f"{name} has no {key}")
            properties = spec["params_schema"]["properties"]
            self.assertTrue(properties)
            for prop_name, prop in properties.items():
                self.assertIn(prop["type"], ("integer", "number", "boolean"))
                self.assertTrue(prop["title"])
                self.assertTrue(prop["description"])
                self.assertIn("default", prop)
                if prop["type"] != "boolean":
                    self.assertLessEqual(prop["minimum"], prop["default"])
                    self.assertLessEqual(prop["default"], prop["maximum"])

    def test_advertised_methods_and_parameters_exist(self):
        """A generated call must actually reach the implementation."""
        tttr, _ = synthetic_stream(
            seed=1, bursts=[(0.05 * k, 60000.0, 1e-3) for k in range(30)]
        )
        for name, spec in tttrlib.TTTR.burst_search_algorithms().items():
            self.assertTrue(hasattr(tttr, spec["method"]), spec["method"])
            defaults = tttrlib.TTTR.burst_search_defaults(name)
            self.assertEqual(
                set(defaults), set(spec["params_schema"]["properties"])
            )
            getattr(tttr, spec["method"])(**defaults)   # must not raise

    def test_dispatch_by_name(self):
        tttr, _ = synthetic_stream(
            seed=2, bursts=[(0.05 * k, 60000.0, 1e-3) for k in range(30)]
        )
        for name in tttrlib.TTTR.burst_search_algorithms():
            bursts = tttr.burst_search_by_name(name)
            self.assertEqual(bursts.ndim, 2)
            self.assertEqual(bursts.shape[1], 2)
        # Overrides are applied on top of the defaults.
        strict = tttr.burst_search_by_name("maxtree", L=200)
        loose = tttr.burst_search_by_name("maxtree", L=20)
        self.assertLessEqual(len(strict), len(loose))

    def test_registry_defaults_match_the_c_defaults(self):
        """The registry must not drift away from the functions it advertises.

        Every default is written down twice -- once as a C++ default argument and
        once in the registry JSON that drives `burst_search_by_name` and any GUI
        built on it. Nothing in the compiler checks that the two agree, so a
        retuned default silently applied in one place and not the other would give
        two different answers depending on which entry point the caller used.
        """
        import inspect
        for name, spec in tttrlib.TTTR.burst_search_algorithms().items():
            method = getattr(tttrlib.TTTR, spec["method"])
            params = inspect.signature(method).parameters
            for prop, schema in spec["params_schema"]["properties"].items():
                self.assertIn(prop, params, f"{name}: {prop} not an argument of "
                                            f"{spec['method']}")
                cpp = params[prop].default
                self.assertIsNot(cpp, inspect.Parameter.empty,
                                 f"{name}: {prop} has no C++ default")
                self.assertAlmostEqual(
                    float(cpp), float(schema["default"]), places=12,
                    msg=f"{name}.{prop}: registry says {schema['default']}, "
                        f"C++ default is {cpp}")

    def test_rejects_unknown_algorithm_and_parameter(self):
        tttr, _ = synthetic_stream(seed=4, bursts=())
        with self.assertRaises(ValueError):
            tttr.burst_search_by_name("no_such_search")
        with self.assertRaises(ValueError):
            tttr.burst_search_by_name("maxtree", no_such_parameter=1)


if __name__ == "__main__":
    unittest.main()

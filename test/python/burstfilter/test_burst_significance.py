"""Exact low-count detection statistics (include/BurstSignificance.h).

These back the ``significance_mode`` option of the max-tree and Bayesian Blocks
burst searches. The reference values come from scipy where scipy can compute
them; the point of the C++ implementation is that it keeps working in the deep
tail where scipy's float path underflows to zero, so the tests cover both sides.
"""
import math
import unittest

import numpy as np
import tttrlib

try:
    from scipy.stats import norm, poisson
    HAVE_SCIPY = True
except ImportError:
    HAVE_SCIPY = False


class TestPoissonTail(unittest.TestCase):

    @unittest.skipUnless(HAVE_SCIPY, "scipy not available")
    def test_matches_scipy_upper_tail(self):
        for k, mu in [(3, 0.5), (5, 2.0), (10, 2.0), (20, 2.0),
                      (40, 2.0), (100, 10.0), (7, 7.0), (2, 15.0)]:
            got = tttrlib.log_poisson_upper_tail(k, mu)
            want = poisson.logsf(k - 1, mu)
            self.assertAlmostEqual(got, want, places=6, msg=f"k={k} mu={mu}")

    @unittest.skipUnless(HAVE_SCIPY, "scipy not available")
    def test_significance_matches_scipy(self):
        for k, mu in [(10, 2.0), (20, 2.0), (40, 2.0), (100, 10.0)]:
            got = tttrlib.poisson_significance(k, mu)
            want = norm.isf(math.exp(poisson.logsf(k - 1, mu)))
            self.assertAlmostEqual(got, want, places=6, msg=f"k={k} mu={mu}")

    def test_deep_tail_beyond_double_underflow(self):
        # scipy.stats.poisson.sf underflows to 0 here, and norm.isf(0) is inf.
        # The log-space implementation still returns a finite, sensible value --
        # which is the whole reason for computing in logs.
        s = tttrlib.poisson_significance(1000, 100.0)
        self.assertTrue(np.isfinite(s))
        self.assertGreater(s, 40.0)
        self.assertLess(s, 100.0)

    def test_deficit_is_negative(self):
        # Fewer counts than expected is not a detection; it must not come back
        # as a large positive significance.
        self.assertLess(tttrlib.poisson_significance(1, 10.0), 0.0)
        self.assertAlmostEqual(tttrlib.poisson_significance(10, 10.0), 0.0, places=9)

    def test_monotonic_in_counts(self):
        vals = [tttrlib.poisson_significance(k, 5.0) for k in range(5, 60)]
        self.assertTrue(all(b > a for a, b in zip(vals, vals[1:])))

    def test_gaussian_approximation_disagrees_at_low_counts(self):
        # The motivation for this module: at the counts this library operates at
        # the Gaussian z-score is not the Poisson significance. If these ever
        # agree closely, the exact path has silently stopped being exact.
        k, mu = 20, 2.0
        gauss = (k - mu) / math.sqrt(mu)
        exact = tttrlib.poisson_significance(k, mu)
        self.assertGreater(abs(gauss - exact), 1.0)


class TestSigmaConversion(unittest.TestCase):

    @unittest.skipUnless(HAVE_SCIPY, "scipy not available")
    def test_matches_scipy_normal_quantile(self):
        for log_p in [-1.0, -2.0, -5.0, -20.0, -100.0, -300.0, -690.0]:
            got = tttrlib.log_p_to_sigma(log_p)
            want = norm.isf(math.exp(log_p))
            self.assertAlmostEqual(got, want, places=6, msg=f"log_p={log_p}")

    def test_continuous_across_erfc_underflow_handoff(self):
        # Below log_p ~ -700 std::erfc has underflowed and the implementation
        # switches to an asymptotic inversion. A seam here would show up as a
        # jump in threshold for very bright bursts, so pin the continuity.
        # Note the direction: log_p increasing means p increasing, so sigma
        # *decreases* across this sweep.
        xs = np.linspace(-740.0, -660.0, 81)
        ys = np.array([tttrlib.log_p_to_sigma(x) for x in xs])
        steps = np.abs(np.diff(ys))
        self.assertTrue(np.all(np.diff(ys) < 0), "must be monotonically decreasing")
        # No step may be out of line with its neighbours; a seam at the handoff
        # would show up here as one step far larger or smaller than the rest.
        self.assertLess(steps.max() / steps.min(), 1.5)

    def test_monotonic_over_full_range(self):
        xs = [-1e-3, -1.0, -10.0, -100.0, -700.0, -1000.0, -5000.0]
        ys = [tttrlib.log_p_to_sigma(x) for x in xs]
        self.assertTrue(all(b > a for a, b in zip(ys, ys[1:])))

    def test_p_ge_one_is_zero(self):
        self.assertEqual(tttrlib.log_p_to_sigma(0.0), 0.0)
        self.assertEqual(tttrlib.log_p_to_sigma(1.0), 0.0)


class TestLiMa(unittest.TestCase):

    def test_zero_excess_is_zero(self):
        # n_on == alpha * n_off is exactly no excess.
        self.assertAlmostEqual(tttrlib.li_ma_significance(10.0, 100.0, 0.1), 0.0, places=9)

    def test_deficit_is_negative(self):
        self.assertLess(tttrlib.li_ma_significance(5.0, 100.0, 0.1), 0.0)

    def test_more_conservative_than_known_background(self):
        # The point of Li & Ma: with the background *measured* rather than known,
        # the same excess is less significant, because the baseline carries its
        # own Poisson error. A short off region (alpha large) must cost more than
        # a long one.
        short_off = tttrlib.li_ma_significance(50.0, 10.0, 1.0)     # t_off == t_on
        long_off = tttrlib.li_ma_significance(50.0, 1000.0, 0.01)   # t_off = 100 t_on
        self.assertLess(short_off, long_off)

    def test_approaches_poisson_as_background_region_grows(self):
        # As the off region grows the background becomes effectively known, so
        # Li & Ma must converge towards the exact Poisson significance.
        mu = 10.0
        k = 40
        exact = tttrlib.poisson_significance(k, mu)
        prev = -np.inf
        for ratio in (10.0, 100.0, 1000.0, 100000.0):
            s = tttrlib.li_ma_significance(k, mu * ratio, 1.0 / ratio)
            self.assertGreater(s, prev)
            prev = s
        self.assertAlmostEqual(prev, exact, delta=0.5)

    def test_monotonic_in_counts(self):
        vals = [tttrlib.li_ma_significance(float(k), 100.0, 0.1) for k in range(10, 80)]
        self.assertTrue(all(b > a for a, b in zip(vals, vals[1:])))


class TestFalseAlarmRate(unittest.TestCase):

    def test_more_trials_needs_higher_threshold(self):
        a = tttrlib.sigma_for_false_alarm_rate(1e-3, 100.0, 1e3)
        b = tttrlib.sigma_for_false_alarm_rate(1e-3, 100.0, 1e6)
        self.assertLess(a, b)

    def test_looser_rate_lowers_threshold(self):
        strict = tttrlib.sigma_for_false_alarm_rate(1e-6, 100.0, 1e5)
        loose = tttrlib.sigma_for_false_alarm_rate(1e-1, 100.0, 1e5)
        self.assertGreater(strict, loose)

    def test_scales_with_acquisition_length(self):
        # The defining property: the *expected number* of false bursts is
        # far * duration, so a longer acquisition at the same per-second rate
        # tolerates a lower per-trial threshold. This is what makes one setting
        # portable between a 10 s and a 1 h measurement.
        short = tttrlib.sigma_for_false_alarm_rate(1e-3, 10.0, 1e4)
        long = tttrlib.sigma_for_false_alarm_rate(1e-3, 3600.0, 1e4)
        self.assertGreater(short, long)

    def test_trials_estimator(self):
        self.assertAlmostEqual(
            tttrlib.estimate_n_trials(tttrlib.TrialsModel_kIndependentWindows, 1000, 10, 0),
            100.0)
        self.assertAlmostEqual(
            tttrlib.estimate_n_trials(tttrlib.TrialsModel_kTestedComponents, 1000, 10, 42),
            42.0)
        # Never below one trial, whatever the inputs say.
        self.assertGreaterEqual(
            tttrlib.estimate_n_trials(tttrlib.TrialsModel_kTestedComponents, 0, 10, 0), 1.0)


if __name__ == '__main__':
    unittest.main()

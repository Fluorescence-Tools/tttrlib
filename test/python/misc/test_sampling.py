"""Drawing from a discrete distribution: weights, and a tabulated CDF.

Both samplers are three lines of maths, which is exactly why every consumer
ends up writing them again and getting one of the same two details wrong: a
linear scan where a binary search belongs, and normalising the caller's CDF in
place so a second draw from the same table samples a *different* distribution.
The tests below pin the distribution, the tie/edge behaviour, and the absence
of that side effect.
"""

import unittest

import numpy as np

import tttrlib


class TestWeightedChoice(unittest.TestCase):
    """`weighted_choice` returns index i with probability w[i] / sum(w)."""

    def test_matches_the_requested_proportions(self):
        """The empirical fractions converge on the normalised weights."""
        weights = np.array([1.0, 3.0, 6.0])
        draws = tttrlib.weighted_choice(weights, 200000)

        self.assertEqual(draws.dtype, np.uint32)
        self.assertEqual(draws.size, 200000)
        fractions = np.bincount(draws, minlength=3) / draws.size
        np.testing.assert_allclose(fractions, [0.1, 0.3, 0.6], atol=0.01)

    def test_weights_need_not_be_normalised(self):
        """Scaling every weight changes nothing -- the sum is divided out."""
        rng = np.random.default_rng(0)
        weights = rng.uniform(0.1, 2.0, 6)

        one = np.bincount(tttrlib.weighted_choice(weights, 100000), minlength=6)
        two = np.bincount(tttrlib.weighted_choice(weights * 1000.0, 100000), minlength=6)
        np.testing.assert_allclose(one / 1e5, two / 1e5, atol=0.01)

    def test_a_zero_weight_is_never_drawn(self):
        """An index with no weight must not appear at all, not merely rarely."""
        draws = tttrlib.weighted_choice(np.array([1.0, 0.0, 1.0]), 50000)
        self.assertEqual(int(np.count_nonzero(draws == 1)), 0)

    def test_degenerate_weights_stay_in_range(self):
        """All-zero or empty weights yield index 0 rather than something unusable.

        A caller whose weights came out all zero is already in trouble; handing
        back an out-of-range index so the *next* subscript is what fails would
        only move the error away from its cause.
        """
        for weights in (np.zeros(4), np.array([])):
            draws = tttrlib.weighted_choice(weights, 16)
            self.assertEqual(draws.size, 16)
            self.assertTrue(np.all(draws == 0))

    def test_no_draws_is_an_empty_array(self):
        """Asking for nothing returns an empty array, not an error."""
        self.assertEqual(tttrlib.weighted_choice(np.array([1.0, 2.0]), 0).size, 0)


class TestSampleFromCdf(unittest.TestCase):
    """`sample_from_cdf` inverts a tabulated cumulative distribution."""

    def test_matches_the_tabulated_distribution(self):
        """Bin fractions follow the differences of the CDF."""
        axis = np.arange(5, dtype=float)
        cdf = np.array([0.1, 0.3, 0.6, 0.9, 1.0])

        drawn = tttrlib.sample_from_cdf(axis, cdf, 200000, True)
        self.assertEqual(drawn.dtype, np.float64)
        fractions = np.bincount(drawn.astype(int), minlength=5) / drawn.size
        np.testing.assert_allclose(fractions, [0.1, 0.2, 0.3, 0.3, 0.1], atol=0.01)

    def test_the_caller_s_cdf_is_not_modified(self):
        """Normalising must not divide through the array it was handed.

        This is the defect the signature exists to prevent: with an in-place
        normalisation, a second draw from the same table samples a different
        distribution and nothing at the call site suggests it.
        """
        axis = np.arange(4, dtype=float)
        cdf = np.array([2.0, 4.0, 6.0, 8.0])
        before = cdf.copy()

        tttrlib.sample_from_cdf(axis, cdf, 32, True)
        np.testing.assert_array_equal(cdf, before)

    def test_an_unnormalised_table_is_scaled_on_request(self):
        """A CDF that ends at eight samples the same as one that ends at one."""
        axis = np.arange(4, dtype=float)
        scaled = tttrlib.sample_from_cdf(axis, np.array([2.0, 4.0, 6.0, 8.0]), 100000, True)
        unit = tttrlib.sample_from_cdf(axis, np.array([0.25, 0.5, 0.75, 1.0]), 100000, True)

        a = np.bincount(scaled.astype(int), minlength=4) / scaled.size
        b = np.bincount(unit.astype(int), minlength=4) / unit.size
        np.testing.assert_allclose(a, b, atol=0.01)

    def test_a_draw_beyond_the_table_yields_zero(self):
        """Without normalisation a short table leaves draws unmatched.

        The table below only reaches 0.5, so about half the draws find no entry
        at or above them. Those come back as 0.0 -- what an inverse-CDF scan
        that finds no match does -- rather than being clamped to the last axis
        point, which would silently pile weight onto the largest value.
        """
        axis = np.array([10.0, 20.0])
        drawn = tttrlib.sample_from_cdf(axis, np.array([0.25, 0.5]), 20000, False)

        unmatched = float(np.count_nonzero(drawn == 0.0)) / drawn.size
        self.assertAlmostEqual(unmatched, 0.5, delta=0.02)
        self.assertEqual(int(np.count_nonzero(drawn == 10.0)), int(np.count_nonzero(drawn == 10.0)))

    def test_mismatched_lengths_are_rejected(self):
        """An axis and a CDF of different lengths cannot describe a distribution."""
        with self.assertRaises(ValueError):
            tttrlib.sample_from_cdf(np.arange(3, dtype=float), np.array([0.5, 1.0]), 8, True)


if __name__ == "__main__":
    unittest.main()

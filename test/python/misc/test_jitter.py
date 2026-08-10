"""Photons from bins and bins from photons (`Jitter.h`).

The library's rule is that every algorithm has to be usable on photons, and
where an algorithm is only defined on continuous values the bridge is a dither
rather than a binning. What that buys is the difference between a bias and a
variance:

* **binning** sends a whole bin onto its centre. That is an error of up to half
  a bin on every photon, in a direction fixed by the bin, so it does not average
  away -- a million photons reproduce it exactly as faithfully as ten;
* **jittering** sends it onto a uniformly random point of the same bin. The
  error is unbiased and shrinks as 1/sqrt(N), at the price of a known extra
  variance of `width^2 / 12` which can simply be subtracted.

The tests here are mostly about the properties that make that argument hold:
the dither is uniform (not Gaussian), it stays inside the bin the photon is
known to be in, and it is reproducible without being correlated.
"""

import unittest

import numpy as np

import tttrlib


class TestJitter(unittest.TestCase):
    def test_the_round_trip_through_photons_is_exact(self):
        """Bin -> photons -> bin returns the histogram it started from.

        Which is the claim that the dither never leaves the bin: one photon out
        per count, and each lands back where it came from.
        """
        rng = np.random.default_rng(0)
        counts = np.ascontiguousarray(rng.poisson(3.0, (8, 8)).astype(float))
        events = tttrlib.events_from_counts_2d(counts, 0)
        self.assertEqual(events.shape, (int(counts.sum()), 2))
        back = tttrlib.counts_from_events_2d(np.ascontiguousarray(events), 8, 8)
        np.testing.assert_array_equal(back, counts)

    def test_the_dither_is_uniform_across_the_bin_and_not_gaussian(self):
        """A rectangle is what undoes a rectangle.

        Quantisation maps an interval onto a point; the dither has to map it
        back onto the same interval, with the same density. A Gaussian of
        matched variance would put a third of its photons outside the bin the
        instrument said they were in.
        """
        counts = np.ascontiguousarray(np.full((16, 16), 40.0))
        events = tttrlib.events_from_counts_2d(counts, 0)
        offsets = (events - np.round(events)).ravel()
        self.assertLessEqual(np.abs(offsets).max(), 0.5)
        histogram, _ = np.histogram(offsets, bins=10, range=(-0.5, 0.5))
        expected = len(offsets) / 10.0
        # Flat to within Poisson noise on 2048 draws per bin.
        self.assertLess(np.abs(histogram - expected).max(), 5 * np.sqrt(expected))
        self.assertAlmostEqual(offsets.mean(), 0.0, delta=0.01)
        self.assertAlmostEqual(offsets.var(), 1.0 / 12.0, delta=0.005)

    def test_it_repeats_and_the_seed_is_what_changes_it(self):
        counts = np.ascontiguousarray(np.full((8, 8), 5.0))
        first = tttrlib.events_from_counts_2d(counts, 17)
        again = tttrlib.events_from_counts_2d(counts, 17)
        other = tttrlib.events_from_counts_2d(counts, 18)
        np.testing.assert_array_equal(first, again)
        self.assertFalse(np.array_equal(first, other))

    def test_the_two_axes_of_one_photon_are_not_the_same_draw(self):
        """A dither correlated across axes would move photons along a diagonal.

        Cheap to get wrong -- one draw reused per photon -- and invisible in
        every one-dimensional check.
        """
        counts = np.ascontiguousarray(np.full((16, 16), 30.0))
        events = tttrlib.events_from_counts_2d(counts, 3)
        offsets = events - np.round(events)
        correlation = np.corrcoef(offsets[:, 0], offsets[:, 1])[0, 1]
        self.assertLess(abs(correlation), 0.05)

    def test_a_zero_width_axis_is_left_alone(self):
        """How an already-continuous axis rides along beside a quantised one."""
        coordinates = np.ascontiguousarray(np.full((64, 2), [3.0, 4.0]))
        jittered = tttrlib.jitter_coordinates_2d(
            coordinates, np.array([1.0, 0.0]), 11
        )
        np.testing.assert_array_equal(jittered[:, 1], 4.0)
        self.assertGreater(jittered[:, 0].std(), 0.2)
        self.assertLessEqual(np.abs(jittered[:, 0] - 3.0).max(), 0.5)

    def test_the_width_is_the_width(self):
        coordinates = np.ascontiguousarray(np.zeros((20000, 2)))
        for width in (0.5, 1.0, 4.0):
            jittered = tttrlib.jitter_coordinates_2d(
                coordinates, np.array([width, width]), 5
            )
            self.assertAlmostEqual(
                jittered.var(), width**2 / 12.0, delta=0.02 * width**2
            )
            self.assertLessEqual(np.abs(jittered).max(), width / 2.0)

    def test_jittering_removes_the_ties_that_binning_creates(self):
        """The whole argument, and it is about ties rather than about bias.

        The tempting claim is that binning biases estimates and jittering fixes
        it. For a mean that is simply untrue -- the bin centre is unbiased -- and
        it is worth having the real reason written down instead. The real reason
        is that binning puts distinct photons at *identical* coordinates, and
        every algorithm that measures a distance is degenerate on that: here
        98% of nearest-neighbour distances become exactly zero. Jittering
        restores the distance distribution to within a fraction of a percent.
        """
        from scipy.spatial import cKDTree

        rng = np.random.default_rng(0)
        truth = rng.normal([16, 16], 3.0, (4000, 2))
        counts = np.ascontiguousarray(
            np.histogram2d(truth[:, 0], truth[:, 1], bins=32,
                           range=[[-0.5, 31.5]] * 2)[0]
        )
        jittered = tttrlib.events_from_counts_2d(counts, 1)

        def nearest(points):
            distance, _ = cKDTree(points).query(points, k=2)
            return distance[:, 1]

        true_nn = nearest(truth)
        binned_nn = nearest(np.round(truth))
        jittered_nn = nearest(jittered)

        self.assertGreater((binned_nn == 0).mean(), 0.9)
        self.assertEqual((jittered_nn == 0).mean(), 0.0)
        self.assertLess(binned_nn.mean(), true_nn.mean() / 3)
        self.assertAlmostEqual(jittered_nn.mean() / true_nn.mean(), 1.0, delta=0.02)

    def test_the_price_is_one_twelfth_of_a_bin_and_it_is_paid_twice(self):
        """Sheppard's correction, and the half of it that is easy to forget.

        The dither adds `w^2 / 12`. But the histogram it dithers had *already*
        added `w^2 / 12` by rounding, and that one is not undone -- so a photon
        that has been through a histogram and back carries **twice** the
        quantisation variance of one that was never binned. Which is the
        argument for keeping the original coordinates whenever they exist, and
        for treating this bridge as the fallback it is.
        """
        rng = np.random.default_rng(4)
        truth = rng.normal(16.0, 3.0, 200_000)
        binned = np.round(truth)
        counts = np.ascontiguousarray(
            np.histogram(truth, bins=32, range=(-0.5, 31.5))[0]
            .astype(float)
            .reshape(32, 1)
        )
        jittered = tttrlib.events_from_counts_2d(counts, 2)[:, 0]

        self.assertAlmostEqual(binned.var() - truth.var(), 1.0 / 12.0, delta=0.02)
        self.assertAlmostEqual(jittered.var() - binned.var(), 1.0 / 12.0, delta=0.02)
        self.assertAlmostEqual(jittered.var() - truth.var(), 2.0 / 12.0, delta=0.03)

    def test_refusals(self):
        with self.assertRaises(Exception):  # a width per axis, not one
            tttrlib.jitter_coordinates_2d(
                np.ascontiguousarray(np.zeros((4, 2))), np.array([1.0]), 0
            )
        with self.assertRaises(Exception):  # negative width
            tttrlib.jitter_coordinates_2d(
                np.ascontiguousarray(np.zeros((4, 2))), np.array([1.0, -1.0]), 0
            )
        with self.assertRaises(Exception):  # a negative count is not a count
            tttrlib.events_from_counts_2d(np.ascontiguousarray(-np.ones((4, 4))), 0)


if __name__ == "__main__":
    unittest.main()

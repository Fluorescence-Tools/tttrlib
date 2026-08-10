import unittest
import numpy as np
import tttrlib


class TestPCH(unittest.TestCase):

    def test_single_species_normalized(self):
        p = list(tttrlib.pch_single_species(30, 1.0))
        self.assertAlmostEqual(sum(p), 1.0, places=6)
        self.assertTrue(all(v >= 0 for v in p))

    def test_single_species_decreasing(self):
        p = list(tttrlib.pch_single_species(20, 0.5))
        # P(0) should be the largest for small brightness
        self.assertGreater(p[0], p[1])
        self.assertGreater(p[1], p[2])

    def test_open_system_normalized(self):
        p = list(tttrlib.pch_open_system(30, 1.0, 0.5))
        self.assertAlmostEqual(sum(p), 1.0, places=6)

    def test_mixture_normalized(self):
        p = list(tttrlib.pch_mixture(30, [1.0, 2.0], [0.3, 0.2]))
        self.assertAlmostEqual(sum(p), 1.0, places=6)

    def test_zero_brightness(self):
        p = list(tttrlib.pch_single_species(10, 0.0))
        self.assertAlmostEqual(p[0], 1.0)
        self.assertAlmostEqual(sum(p[1:]), 0.0, places=10)

    def test_mixture_rejects_mismatched_species(self):
        """Two vectors of different lengths cannot describe a mixture.

        This used to read past the end of avg_numbers and -- because the heap
        there happened to be zero -- silently fit a three-species argument
        list as one species, with a plausible normalised histogram
        (BUGS 2026-08-10)."""
        with self.assertRaises(ValueError):
            tttrlib.pch_mixture(20, [1.0, 2.0, 3.0, 4.0], [0.5])
        with self.assertRaises(ValueError):
            tttrlib.pch_mixture(20, [1.0], [0.5, 0.3])
        # Equal lengths are untouched.
        p = list(tttrlib.pch_mixture(20, [1.0, 2.0], [0.3, 0.2]))
        self.assertAlmostEqual(sum(p), 1.0, places=6)


class TestFIDA(unittest.TestCase):

    def test_fida_normalized(self):
        p = list(tttrlib.fida_pch(30, [1.0, 0.5], 1, background=0.0))
        self.assertAlmostEqual(sum(p), 1.0, places=6)
        self.assertTrue(all(v >= 0 for v in p))

    def test_fida_decreasing(self):
        p = list(tttrlib.fida_pch(20, [0.5, 0.3], 1))
        self.assertGreater(p[0], p[1])

    def test_fida_background_spreads(self):
        p_no_bg = list(tttrlib.fida_pch(20, [1.0, 0.5], 1, background=0.0))
        p_with_bg = list(tttrlib.fida_pch(20, [1.0, 0.5], 1, background=1.0))
        # background spreads P(k), so P(0) decreases
        self.assertLess(p_with_bg[0], p_no_bg[0])

    def test_dvdx_gaussian(self):
        prof = list(tttrlib.fida_dvdx_gaussian(256, 1e-4))
        self.assertEqual(len(prof), 512)  # 2 * n_bins
        w = prof[256:]
        # weights should be non-negative
        self.assertTrue(all(v >= 0 for v in w))


if __name__ == '__main__':
    unittest.main()

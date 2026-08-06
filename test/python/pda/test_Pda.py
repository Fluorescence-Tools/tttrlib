from __future__ import division

import unittest

import numpy as np

import tttrlib


class Tests(unittest.TestCase):

    def test_getter_setter_pda(self):
        # test getter and setter
        pda = tttrlib.Pda()

        pda.hist2d_valid = True
        self.assertEqual(pda.hist2d_valid, True)
        pda.hist2d_valid = False
        self.assertEqual(pda.hist2d_valid, False)

        pda.hist2d_valid = True
        pda.background_ch1 = 1.7
        self.assertEqual(pda.background_ch1, 1.7)
        self.assertEqual(pda.hist2d_valid, False)

        pda.hist2d_valid = True
        pda.background_ch2 = 0.7
        self.assertEqual(pda.background_ch2, 0.7)
        self.assertEqual(pda.hist2d_valid, False)

        pda.hist2d_valid = True
        pda.hist2d_nmin = 5
        self.assertEqual(pda.hist2d_nmin, 5)
        self.assertEqual(pda.hist2d_valid, False)

        pda.hist2d_valid = True
        pda.hist2d_nmax = 12
        self.assertEqual(pda.hist2d_nmax, 12)
        self.assertEqual(pda.hist2d_valid, False)

    def test_pda_python_extension(self):
        pda = tttrlib.Pda()
        pf = np.arange(0, 10)
        pda.pf = pf
        self.assertEqual(np.all(pf == pda.pf), True)
        self.assertEqual(pda.hist2d_valid, False)

        pda.hist2d_valid = True
        pg = np.array([0.1, 0.7, 0.2, 0.7, 0.7, 0.8])
        pda.spectrum_ch1 = pg
        self.assertEqual(np.all(pda.spectrum_ch1 == pg), True)
        self.assertEqual(pda.hist2d_valid, False)

        pda.hist2d_valid = True
        amplitudes = np.array([0.3, 0.7])
        pda.species_amplitudes = amplitudes
        self.assertEqual(np.all(pda.species_amplitudes == amplitudes), True)
        self.assertEqual(pda.hist2d_valid, False)

    def test_pda_constructor(self):
        kw = {
            "hist2d_nmax": 222,
            "hist2d_nmin": 36,
        }
        pda = tttrlib.Pda(**kw)
        self.assertEqual(pda.hist2d_nmax, kw["hist2d_nmax"])
        self.assertEqual(pda.hist2d_nmin, kw["hist2d_nmin"])

    def test_pda_single_species(self):
        # A single species with no background: the S1S2 matrix is exactly the
        # binomial split of pF, checked against scipy-free arithmetic.
        max_number_of_photons = 5
        pda = tttrlib.Pda(hist2d_nmax=max_number_of_photons)
        pda.background_ch1 = 0.0
        pda.background_ch2 = 0.0

        amplitude, probability_green = 1.0, 0.7
        pda.append(amplitude=amplitude, probability_ch1=probability_green)

        pF = np.zeros(max_number_of_photons + 1)
        pF[max_number_of_photons] = 1.0
        pda.setPF(pF)

        s = pda.s1s2
        self.assertEqual(s.shape, (max_number_of_photons + 1,) * 2)

        from math import comb
        n = max_number_of_photons
        for red in range(n + 1):
            expected = comb(n, red) * probability_green ** (n - red) \
                       * (1 - probability_green) ** red
            self.assertAlmostEqual(s[n - red, red], expected, places=14)
        self.assertAlmostEqual(float(s.sum()), 1.0, places=14)


if __name__ == "__main__":
    unittest.main()

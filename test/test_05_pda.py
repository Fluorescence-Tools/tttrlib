from __future__ import division

import unittest
import tttrlib
import numpy as np

print("Test: ", __file__)


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

    def test_pda_1(self):
        green_background = 1.5
        red_background = 0.6
        max_number_of_photons = 5
        pda = tttrlib.Pda(
            hist2d_nmax=5
        )
        pda.background_ch1 = green_background
        pda.background_ch2 = red_background
        self.assertEqual(
            pda.background_ch1,
            green_background,
        )
        self.assertEqual(
            pda.background_ch2,
            red_background
        )

        amplitude = 0.5
        probability_green = 0.7
        pda.append(
            amplitude=amplitude,
            probability_ch1=probability_green
        )
        pF = np.ones(max_number_of_photons, dtype=np.float)
        pda.setPF(pF)
        ref = np.array(
            [[0.06122821, 0.05510539, 0.0275527, 0.01047002, 0.00347164,
              0.00089271],
             [0.13470207, 0.13408979, 0.07604544, 0.03344897, 0.01122497,
              0.],
             [0.16317319, 0.18414385, 0.12087368, 0.05405563, 0.,
              0.],
             [0.1486621, 0.19416385, 0.13015894, 0., 0.,
              0.],
             [0.1169788, 0.15670619, 0., 0., 0.,
              0.],
             [0.07159453, 0., 0., 0., 0.,
              0.]
             ]
        )
        self.assertEqual(np.allclose(pda.s1s2, ref), True)

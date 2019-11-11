from __future__ import division

import unittest
import tttrlib
import numpy as np


class Tests(unittest.TestCase):

    def test_pda_1(self):
        """
        create a pda object and test its
        setter and getter methods
        :return:
        """
        import tttrlib
        import numpy as np

        pda = tttrlib.Pda()
        green_background = 1.5
        red_background = 0.6
        max_number_of_photons = 20
        pda.set_green_background(green_background)
        pda.set_red_background(red_background)
        # self.assertEqual(
        #     pda.get_green_background(),
        #     green_background
        # )
        # self.assertEqual(
        #     pda.get_red_background(),
        #     red_background
        # )
        pda.set_max_number_of_photons(max_number_of_photons)
        # self.assertEqual(
        #     pda.get_max_number_of_photons(),
        #     max_number_of_photons
        # )

        amplitude = 0.5
        probability_green = 0.7
        pda.append(
            amplitude=amplitude,
            probability_green=probability_green
        )
        pF = np.ones(max_number_of_photons, dtype=np.float)
        pda.setPF(pF)
        pda.evaluate()
        sgsr_matrix = pda.get_SgSr_matrix()

from __future__ import division

import os
import unittest
import tttrlib


class Tests(unittest.TestCase):

    def test_reading(self):
        test_files = [
            ('./data/BH/BH_SPC132.spc', 'SPC-130'),
            ('./data/BH/BH_SPC132.spc', 'SPC-130'),
            ('./data/BH/BH_SPC630_256.spc', 'SPC-600_256'),
            ('./data/HDF/1a_1b_Mix.hdf5', 'PHOTON-HDF5'),
            ('./data/PQ/HT3/PQ_HT3_CLSM.ht3', 'HT3'),
            ('./data/PQ/PTU/PQ_PTU_HH_T2.ptu', 'PTU'),
            ('./data/PQ/PTU/PQ_PTU_HH_T3.ptu', 'PTU')
        ]
        for file_type in test_files:
            data = tttrlib.TTTR(*file_type)

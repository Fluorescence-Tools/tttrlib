from __future__ import division

import os
import unittest
import numpy as np

import tttrlib


class Tests(unittest.TestCase):

    make_references = True

    test_files = [
        ('./data/BH/BH_SPC132.spc', 'SPC-130'),
        ('./data/BH/BH_SPC630_256.spc', 'SPC-600_256'),
        ('./data/HDF/1a_1b_Mix.hdf5', 'PHOTON-HDF5'),
        ('./data/PQ/HT3/PQ_HT3_CLSM.ht3', 'HT3'),
        ('./data/PQ/PTU/PQ_PTU_HH_T2.ptu', 'PTU'),
        ('./data/PQ/PTU/PQ_PTU_HH_T3.ptu', 'PTU')
    ]

    def test_reading(self):
        test_files = self.test_files
        make_references = self.make_references

        for file_type in test_files:
            print("Testing file: %s\nType: %s" % file_type)
            file_root, _ = os.path.splitext(os.path.basename(file_type[0]))
            data = tttrlib.TTTR(*file_type)

            if make_references:
                routing_channels = data.routing_channels
                micro_times = data.micro_times
                macro_times = data.macro_times
                np.savez_compressed(
                    './references/' + file_root + ".npz",
                    routing_channels, micro_times, macro_times
                )

            reference_file = './references/' + file_root + '.npz'
            reference = np.load(reference_file)

            # routing channels
            self.assertEqual(
                np.allclose(
                    reference['arr_0'],
                    data.routing_channels
                ),
                True
            )
            # micro times
            self.assertEqual(
                np.allclose(
                    reference['arr_1'],
                    data.micro_times
                ),
                True
            )
            # macro times
            self.assertEqual(
                np.allclose(
                    reference['arr_2'],
                    data.macro_times
                ),
                True
            )

    # def test_open_non_existing_file(self):
    #     # make sure that opening an non-exisitng file does not crash
    #     d = tttrlib.TTTR('NOFILE', 'PTU')
    #

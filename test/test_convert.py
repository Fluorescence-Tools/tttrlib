from __future__ import division

import tempfile
import unittest
import tttrlib
import numpy as np


test_files = [
    ('./data/BH/BH_SPC132.spc', 'SPC-130'),
    ('./data/BH/BH_SPC132.spc', 'SPC-130'),
    ('./data/BH/BH_SPC630_256.spc', 'SPC-600_256'),
    ('./data/HDF/1a_1b_Mix.hdf5', 'PHOTON-HDF5'),
    ('./data/PQ/HT3/PQ_HT3_CLSM.ht3', 'HT3'),
    ('./data/PQ/PTU/PQ_PTU_HH_T2.ptu', 'PTU'),
    ('./data/PQ/PTU/PQ_PTU_HH_T3.ptu', 'PTU')
]


class Tests(unittest.TestCase):

    @unittest.expectedFailure
    def test_reading(self):
        # The writting does not work very good
        # yet therefore this is expected to fail
        _, fn = tempfile.mkstemp()
        routine = 'SPC-130'
        for file_type in test_files:
            data = tttrlib.TTTR(*file_type)
            data.write_file(
                fn,
                routine
            )
            data_written = tttrlib.TTTR(fn, routine)
            self.assertEqual(
                np.allclose(
                    data.get_macro_time(),
                    data_written.get_macro_time()
                ),
                True
            )

from __future__ import division

import tempfile
import unittest
import tttrlib
import numpy as np

print("Test: ", __file__)

test_files = [
    ('./data/bh/bh_spc132.spc', 'SPC-130'),
    ('./data/bh/bh_spc132.spc', 'SPC-130'),
    ('./data/bh/bh_spc630_256.spc', 'SPC-600_256'),
    ('./data/hdf/1a_1b_Mix.hdf5', 'PHOTON-HDF5'),
    ('./data/imaging/pq/ht3/pq_ht3_clsm.ht3', 'HT3'),
    ('./data/pq/ptu/pq_ptu_hh_t2.ptu', 'PTU'),
    ('./data/pq/ptu/pq_ptu_hh_t3.ptu', 'PTU')
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

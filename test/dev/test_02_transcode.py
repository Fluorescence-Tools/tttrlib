from __future__ import division

import json
import tempfile
import unittest
import tttrlib
import numpy as np

print("Test: ", __file__)

settings = json.load(open(file="./test/settings.json"))
test_files = settings["test_files"]


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

    def test_write_tttr(self):
        _, filename = tempfile.mkstemp(
            suffix='.spc'
        )
        data.write(filename)
        d2 = tttrlib.TTTR(filename, 'SPC-130')
        self.assertEqual(np.allclose(d2.micro_times, data.micro_times), True)
        self.assertEqual(np.allclose(d2.macro_times, data.macro_times), True)
        self.assertEqual(np.allclose(d2.routing_channels, data.routing_channels), True)

    def test_write_tttr_other_header(self):
        _, filename = tempfile.mkstemp(suffix='.ptu')
        # Read header from other TTTR file
        other_header = tttrlib.TTTRHeader(settings["ptu_hh_t3_filename"], 0)
        data.write(filename, other_header)
        d2 = tttrlib.TTTR(filename)
        self.assertEqual(np.allclose(d2.micro_times, data.micro_times), True)
        self.assertEqual(np.allclose(d2.macro_times, data.macro_times), True)
        self.assertEqual(np.allclose(d2.routing_channels, data.routing_channels), True)

    # def test_write_tttr_new_header(self):
    #     _, filename = tempfile.mkstemp(suffix='.ptu')
    #     # Read header from other TTTR file
    #     other_header = tttrlib.TTTRHeader()
    #     data.write(filename, other_header)
    #     d2 = tttrlib.TTTR(filename)
    #     self.assertEqual(np.allclose(d2.micro_times, data.micro_times), True)
    #     self.assertEqual(np.allclose(d2.macro_times, data.macro_times), True)
    #     self.assertEqual(np.allclose(d2.routing_channels, data.routing_channels), True)

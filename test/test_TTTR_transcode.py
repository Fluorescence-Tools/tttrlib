from __future__ import division

import json
import os.path
import tempfile
import unittest
import tttrlib
import numpy as np

print("Test: ", __file__)

settings = json.load(open(file="./test/settings.json"))
test_files = settings["test_files"]
data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')


class Tests(unittest.TestCase):

    def test_read_write(self):
        # Read & write TTTR file
        for input_fn, routine in test_files[:1]: # so far only spc-130 & PTU-T3 supported
            suffix = os.path.splitext(input_fn)[1]
            _, output_fn = tempfile.mkstemp(suffix)

            data = tttrlib.TTTR(output_fn, routine)
            data.write(output_fn)
            data_written = tttrlib.TTTR(output_fn, routine)

            np.testing.assert_array_almost_equal(data.routing_channels, data_written.routing_channels)
            np.testing.assert_array_almost_equal(data.macro_times, data_written.macro_times)
            np.testing.assert_array_almost_equal(data.micro_times, data_written.micro_times)

    def test_write_tttr_other_header(self):
        _, tmp_filename = tempfile.mkstemp(suffix='.ptu')
        fn_data = settings["ptu_hh_t3_filename"]
        container_type = 0
        # Read header from other TTTR file
        other_header = tttrlib.TTTRHeader(fn_data, container_type)
        data.write(tmp_filename, other_header)
        d2 = tttrlib.TTTR(tmp_filename)
        self.assertEqual(np.allclose(d2.micro_times, data.micro_times), True)
        self.assertEqual(np.allclose(d2.macro_times, data.macro_times), True)
        self.assertEqual(np.allclose(d2.routing_channels, data.routing_channels), True)

    def test_write_tttr_new_header(self):
        """
        The container and record type of TTTR container
        is changed to save an SPC file as a PTU file to disk
        """
        _, filename = tempfile.mkstemp(suffix='.ptu')
        data_spc = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')
        header = data_spc.header
        # see: TTTRHeaderTypes.h
        # PQ_PTU_CONTAINER          0
        # PQ_RECORD_TYPE_HHT3v2       4
        header.tttr_container_type = 0
        header.tttr_record_type = 4
        data_spc.write(filename)
        d2 = tttrlib.TTTR(filename)
        self.assertEqual(np.allclose(d2.micro_times, data.micro_times), True)
        self.assertEqual(np.allclose(d2.macro_times, data.macro_times), True)
        self.assertEqual(np.allclose(d2.routing_channels, data.routing_channels), True)

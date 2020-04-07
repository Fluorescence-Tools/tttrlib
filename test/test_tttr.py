from __future__ import division

import os
import unittest
import numpy as np

import tttrlib

data = tttrlib.TTTR('./data/BH/BH_SPC132.spc', 'SPC-130')


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

            # test __rep__
            file_path = os.path.abspath(file_type[0]).replace('\\', '/')
            container_type = file_type[1]
            ref = 'tttrlib.TTTR("%s", "%s")' % (
                file_path,
                container_type.replace('\\', '/')
            )
            self.assertEqual(
                ref,
                data.__repr__()
            )

    def test_header_copy_constructor(self):
        # import tttrlib
        # data = tttrlib.TTTR('./data/BH/BH_SPC132.spc', 'SPC-130')
        p1 = data.header
        p2 = tttrlib.Header(p1)
        self.assertEqual(
            p1.macro_time_resolution,
            p2.macro_time_resolution
        )
        self.assertEqual(
            p1.bytes_per_record,
            p2.bytes_per_record
        )
        self.assertEqual(
            p1.micro_time_resolution,
            p2.micro_time_resolution
        )
        self.assertEqual(
            p1.header_end,
            p2.header_end
        )
        self.assertEqual(
            p1.tttr_record_type,
            p2.tttr_record_type
        )
        self.assertEqual(
            p1.number_of_tac_channels,
            p2.number_of_tac_channels
        )

    def test_tttr_copy_constructor(self):
        # import tttrlib
        # data = tttrlib.TTTR('./data/BH/BH_SPC132.spc', 'SPC-130')
        d2 = tttrlib.TTTR(data)
        self.assertEqual(
            np.allclose(
                data.macro_times,
                d2.macro_times
            ),
            True
        )

    def test_constructor(self):
        # first element is the filename
        # second element is the name of the container
        # third element is the number used by tttrlib to identify containers
        names_types = [
            ('./data/PQ/PTU/PQ_PTU_HH_T3.ptu', 'PTU', 0),
            ('./data/PQ/HT3/PQ_HT3_CLSM.ht3', 'HT3', 1),
            ('./data/BH/BH_SPC132.spc', 'SPC-130', 2),
            ('./data/BH/BH_SPC630_256.spc', 'SPC-600_256', 3),
            ('./data/HDF/1a_1b_Mix.hdf5', 'PHOTON-HDF5', 5)
        ]
        # Two ways of creating a TTTR container - by container type
        # number or name. Both should give the same result
        for fn, container_name, container_type_number in names_types:
            data_1 = tttrlib.TTTR(fn, container_name)
            data_2 = tttrlib.TTTR(fn, container_type_number)
            # only test equality of macro times here
            self.assertEqual(
                np.allclose(
                    data_1.macro_times,
                    data_2.macro_times
                ),
                True
            )

    def test_constructor_with_selection(self):
        # data = tttrlib.TTTR('./data/BH/BH_SPC132.spc', 'SPC-130')
        ch1_indeces = data.get_selection_by_channel(np.array([8]))
        data_ch1 = tttrlib.TTTR(data, ch1_indeces)
        self.assertEqual(
            np.allclose(
                data.macro_times[ch1_indeces],
                data_ch1.macro_times
            ),
            True
        )

    def test_constructor_with_array(self):
        macro_times = data.macro_times
        micro_times = data.micro_times
        routing_channels = data.routing_channels
        event_types = data.event_types
        data2 = tttrlib.TTTR(
            macro_times,
            micro_times,
            routing_channels,
            event_types
        )
        self.assertEqual(
            np.allclose(
                data.macro_times,
                data2.macro_times
            ),
            True
        )

    def test_open_non_existing_file(self):
        # make sure that opening an non-exisitng file does not crash
        d = tttrlib.TTTR('NOFILE', 'PTU')
        self.assertEqual(
            len(d.macro_times), 0
        )

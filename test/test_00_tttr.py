from __future__ import division

import os
import tempfile
import unittest
import numpy as np

import tttrlib


print("Test: ", __file__)
spc132_filename = './data/bh/bh_spc132.spc'
spc630_filename = './data/bh/bh_spc630_256.spc'
photon_hdf_filename = './data/hdf/1a_1b_Mix.hdf5'
ptu_hh_t2_filename = './data/pq/ptu/pq_ptu_hh_t2.ptu'
ptu_hh_t3_filename = './data/pq/ptu/pq_ptu_hh_t3.ptu'
ht3_clsm_filename = './data/imaging/pq/ht3/pq_ht3_clsm.ht3'

data = tttrlib.TTTR(spc132_filename, 'SPC-130')


class Tests(unittest.TestCase):

    make_references = False

    test_files = [
        (spc132_filename, 'SPC-130'),
        (spc630_filename, 'SPC-600_256'),
        (photon_hdf_filename, 'PHOTON-HDF5'),
        (ht3_clsm_filename, 'HT3'),
        (ptu_hh_t2_filename, 'PTU'),
        (ptu_hh_t3_filename, 'PTU')
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

    def test_header(self):
        data = tttrlib.TTTR(ptu_hh_t3_filename, 'PTU')
        header = data.header
        self.assertEqual(
            72, len(header.data.keys())
        )

    def test_slicing(self):
        # single element
        d = data[0]
        self.assertEqual(d.routing_channels, data.routing_channels[0])
        self.assertEqual(d.event_types, data.event_types[0])
        self.assertEqual(d.micro_times, data.micro_times[0])
        self.assertEqual(d.macro_times, data.macro_times[0])

        # steps of two
        d = data[:10:2]
        self.assertEqual(np.allclose(d.routing_channels, data.routing_channels[:10:2]), True)
        self.assertEqual(np.allclose(d.event_types, data.event_types[:10:2]), True)
        self.assertEqual(np.allclose(d.micro_times, data.micro_times[:10:2]), True)
        self.assertEqual(np.allclose(d.macro_times, data.macro_times[:10:2]), True)

        # reverse oder
        d = data[:10:-1]
        self.assertEqual(np.allclose(d.routing_channels, data.routing_channels[:10:-1]), True)
        self.assertEqual(np.allclose(d.event_types, data.event_types[:10:-1]), True)
        self.assertEqual(np.allclose(d.micro_times, data.micro_times[:10:-1]), True)
        self.assertEqual(np.allclose(d.macro_times, data.macro_times[:10:-1]), True)

    def test_join(self):
        d = tttrlib.TTTR(data)
        d.append(data)
        self.assertEqual(
            len(d), 2 * len(data)
        )
        # by default the data macro time data is shifted
        self.assertEqual(
            d.macro_times[len(data)],
            data.macro_times[len(data) - 1] + data.macro_times[0]
        )
        # if shift_macro_time is set to False it is not shifted
        # an optional constant offset is added independently
        d2 = tttrlib.TTTR(data)
        d2.append(data, shift_macro_time=False, macro_time_offset=11)
        self.assertEqual(
            d2.macro_times[len(data)],
            int(data.macro_times[0] + 11)
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
            p1.micro_time_resolution,
            p2.micro_time_resolution
        )
        self.assertEqual(
            p1.tttr_record_type,
            p2.tttr_record_type
        )
        self.assertEqual(
            p1.number_of_micro_time_channels,
            p2.number_of_micro_time_channels
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
            (ptu_hh_t3_filename, 'PTU', 0),
            (ht3_clsm_filename, 'HT3', 1),
            (spc132_filename, 'SPC-130', 2),
            (spc630_filename, 'SPC-600_256', 3),
            (photon_hdf_filename, 'PHOTON-HDF5', 5)
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
        data_subset = data[:1000]
        ch1_indeces = data_subset.get_selection_by_channel([8])
        data_ch1 = tttrlib.TTTR(data_subset, ch1_indeces)
        self.assertEqual(
            np.allclose(
                data.macro_times[ch1_indeces],
                data_ch1.macro_times
            ),
            True
        )
        # selections wrap over
        d2 = tttrlib.TTTR(data, [-1])
        self.assertEqual(
            d2.macro_times[0], data.macro_times[-1]
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

    def test_TTTRRange(self):
        # empty range object
        tttr_range_1 = tttrlib.TTTRRange()
        self.assertEqual(tttr_range_1.start, 0)
        self.assertEqual(tttr_range_1.stop, 0)
        self.assertEqual(tttr_range_1.start_time, 0)
        self.assertEqual(tttr_range_1.stop_time, 0)
        self.assertTupleEqual(tuple(tttr_range_1.start_stop), (0, 0))
        self.assertEqual(len(tttr_range_1.tttr_indices), 0)

        d = {
            "start": 11,
            "stop": 898,
            "start_time": 222,
            "stop_time": 2222
        }
        tttr_range_1 = tttrlib.TTTRRange(*d.values())
        self.assertEqual(tttr_range_1.start, d["start"])
        self.assertEqual(tttr_range_1.stop, d["stop"])
        self.assertEqual(tttr_range_1.start_time, d["start_time"])
        self.assertEqual(tttr_range_1.stop_time, d["stop_time"])
        self.assertTupleEqual(tuple(tttr_range_1.start_stop), (d["start"], d["stop"]))

        tttr_range_2 = tttrlib.TTTRRange(other=tttr_range_1)
        self.assertEqual(tttr_range_2.start, d["start"])
        self.assertEqual(tttr_range_2.stop, d["stop"])
        self.assertEqual(tttr_range_2.start_time, d["start_time"])
        self.assertEqual(tttr_range_2.stop_time, d["stop_time"])
        self.assertTupleEqual(tuple(tttr_range_2.start_stop), (d["start"], d["stop"]))

        # append a index
        tttr_range_1.append(89)
        self.assertEqual(len(tttr_range_1.tttr_indices), 1)
        self.assertTupleEqual(tuple(tttr_range_1.tttr_indices), (89,))
        tttr_range_1.clear()
        self.assertEqual(len(tttr_range_1.tttr_indices), 0)

        tttr_range_2 = tttrlib.TTTRRange(**d)
        tttr_range_3 = tttrlib.TTTRRange(other=tttr_range_1)
        self.assertEqual(
            tttr_range_1, tttr_range_2
        )
        self.assertEqual(
            tttr_range_2, tttr_range_3
        )

    def test_open_non_existing_file(self):
        # make sure that opening an non-exisitng file does not crash
        d = tttrlib.TTTR('NOFILE', 'PTU')
        self.assertEqual(
            len(d.macro_times), 0
        )
        header = d.header
        self.assertEqual(
            header.getTTTRRecordType(), -1
        )

    def test_write_spc(self):
        _, filename = tempfile.mkstemp(
            suffix='.npy'
        )
        data.write(filename, 'SPC-130')
        d2 = tttrlib.TTTR(filename, 'SPC-130')
        self.assertEqual(np.allclose(d2.micro_times, data.micro_times), True)
        self.assertEqual(np.allclose(d2.macro_times, data.macro_times), True)
        self.assertEqual(np.allclose(d2.routing_channels, data.routing_channels), True)

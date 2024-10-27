from __future__ import division

import os
import unittest
import json
import numpy as np
import tempfile

import tttrlib

settings = json.load(open(file="./test/settings.json"))

data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')


class Tests(unittest.TestCase):

    make_references = settings["make_references"]

    test_files = [
        (settings["spc132_filename"], 'SPC-130'),
        (settings["spc630_filename"], 'SPC-600_256'),
        (settings["photon_hdf_filename"], 'PHOTON-HDF5'),
        (settings["ht3_clsm_filename"], 'HT3'),
        (settings["ptu_hh_t2_filename"], 'PTU'),
        (settings["ptu_hh_t3_filename"], 'PTU'),
        (settings["sm_filename"], 'SM')
    ]

    def test_append_event(self):
        data = tttrlib.TTTR()
        data.append_event(10, 11, 3, 0)
        data.append_event(11, 66, 4, 1)
        data.append_event(11, 22, 5, 1, False)
        self.assertListEqual(data.macro_times.tolist(), [10, 21, 11]) # macro times are shifted by default
        self.assertListEqual(data.micro_times.tolist(), [11, 66, 22])
        self.assertListEqual(data.routing_channels.tolist(), [3, 4, 5])
        self.assertListEqual(data.event_types.tolist(), [0, 1, 1])

    def test_tttr_header_ptu(self):
        filename = settings["ptu_hh_t3_filename"]
        data = tttrlib.TTTR(filename)
        json_str = data.header.get_json()
        h2 = tttrlib.TTTRHeader()
        h2.set_json(json_str)
        d1 = json.loads(json_str)
        d2 = json.loads(h2.get_json())
        self.assertDictEqual(d1, d2)

    def test_write_ptu_header(self):
        filename = settings["ptu_hh_t3_filename"]
        file = tempfile.NamedTemporaryFile()
        test_fn = file.name + '.ptu'
        data = tttrlib.TTTR(filename)
        header_original = data.header
        mode = 'wb'  # default is wb
        tttrlib.TTTRHeader.write_ptu_header(test_fn, header_original, mode)
        # test if all info is still there
        data_test = tttrlib.TTTR(test_fn)
        header_test = data_test.header
        d2 = json.loads(header_original.get_json())
        d3 = json.loads(header_test.get_json())
        all_tags = True
        for t3 in d3['tags']:
            if t3 not in d2['tags']:
                all_tags = False
                break
        for t2 in d2['tags']:
            if t2 not in d3['tags']:
                all_tags = False
                break

        self.assertEqual(all_tags, True)

    def test_tttr_header_add_tags(self):
        """
        Tests adding tags to an existing TTTR header.

        This test verifies that when tags from another TTTR header are added to an existing one,
        the number of tags in the original header is not reduced, but rather increased or maintained.

        :return: None

        :raises AssertionError: If any of the assertions in this method fail.
        """
        # Initialize the TTTR library with SPC-130 settings
        data = tttrlib.TTTR(settings["spc132_filename"], "SPC-130")

        # Initialize a new TTTR header for comparison
        # Use PTU file type with container type 0 (new)
        header2 = tttrlib.TTTRHeader(settings["ptu_hh_t3_filename"], 0)  # 0 is the container type

        # Assert that the original data has fewer tags than the new header
        self.assertEqual(len(data.header.tags), len(header2.tags))  # Use '==' instead of '<' for equality

        # Add tags from the new header to the existing one
        data.header.add_tags(header2)

        # Assert that the number of tags in the original header is now greater than or equal to
        self.assertEqual(len(data.header.tags) >= len(header2.tags), True)

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
                    './test/data/reference/' + file_root + ".npz",
                    routing_channels, micro_times, macro_times
                )
            reference_file = './test/data/reference/' + file_root + '.npz'
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
            file_path = file_type[0].replace('\\', '/')
            container_type = file_type[1]
            ref = 'TTTR("%s", "%s")' % (
                file_path,
                container_type.replace('\\', '/')
            )
            self.assertEqual(
                ref,
                data.__repr__()
            )

    def test_microtime_histogram(self):
        data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')
        h, t = data.get_microtime_histogram(32)
        h_ref = np.array(
            [0., 0., 0., 0., 0., 0., 0., 0.,
             0., 0., 0., 0., 0., 0., 74., 394.,
             389., 404., 390., 413., 478., 683., 2855., 9564.,
             17986., 20334., 16233., 11141., 7265., 5364., 4259., 3738.,
             3344., 3001., 2885., 2683., 2456., 2203., 2093., 1990.,
             1953., 1886., 1742., 1700., 1617., 1523., 1422., 1411.,
             1436., 1364., 1261., 1317., 1226., 1175., 1118., 1106.,
             1049., 1033., 935., 942., 935., 951., 922., 870.,
             876., 771., 798., 846., 773., 733., 761., 784.,
             693., 705., 669., 663., 706., 647., 610., 613.,
             614., 630., 572., 562., 589., 611., 543., 536.,
             538., 561., 529., 564., 501., 496., 507., 485.,
             478., 467., 478., 477., 487., 425., 495., 434.,
             432., 485., 488., 483., 511., 549., 561., 714.,
             798., 661., 235., 0., 0., 0., 0., 0.,
             0., 0., 0., 0., 0., 0., 0., 0.
             ]
        )
        t_ref = np.array(
            [0., 0.10546875, 0.2109375, 0.31640625, 0.421875,
             0.52734375, 0.6328125, 0.73828125, 0.84375, 0.94921875,
             1.0546875, 1.16015625, 1.265625, 1.37109375, 1.4765625,
             1.58203125, 1.6875, 1.79296875, 1.8984375, 2.00390625,
             2.109375, 2.21484375, 2.3203125, 2.42578125, 2.53125,
             2.63671875, 2.7421875, 2.84765625, 2.953125, 3.05859375,
             3.1640625, 3.26953125, 3.375, 3.48046875, 3.5859375,
             3.69140625, 3.796875, 3.90234375, 4.0078125, 4.11328125,
             4.21875, 4.32421875, 4.4296875, 4.53515625, 4.640625,
             4.74609375, 4.8515625, 4.95703125, 5.0625, 5.16796875,
             5.2734375, 5.37890625, 5.484375, 5.58984375, 5.6953125,
             5.80078125, 5.90625, 6.01171875, 6.1171875, 6.22265625,
             6.328125, 6.43359375, 6.5390625, 6.64453125, 6.75,
             6.85546875, 6.9609375, 7.06640625, 7.171875, 7.27734375,
             7.3828125, 7.48828125, 7.59375, 7.69921875, 7.8046875,
             7.91015625, 8.015625, 8.12109375, 8.2265625, 8.33203125,
             8.4375, 8.54296875, 8.6484375, 8.75390625, 8.859375,
             8.96484375, 9.0703125, 9.17578125, 9.28125, 9.38671875,
             9.4921875, 9.59765625, 9.703125, 9.80859375, 9.9140625,
             10.01953125, 10.125, 10.23046875, 10.3359375, 10.44140625,
             10.546875, 10.65234375, 10.7578125, 10.86328125, 10.96875,
             11.07421875, 11.1796875, 11.28515625, 11.390625, 11.49609375,
             11.6015625, 11.70703125, 11.8125, 11.91796875, 12.0234375,
             12.12890625, 12.234375, 12.33984375, 12.4453125, 12.55078125,
             12.65625, 12.76171875, 12.8671875, 12.97265625, 13.078125,
             13.18359375, 13.2890625, 13.39453125
             ]
        ) * 1e-9
        self.assertEqual(
            np.allclose(
                h_ref, h
            ), True
        )
        self.assertEqual(
            np.allclose(
                t_ref, t
            ), True
        )

    def test_header(self):
        data = tttrlib.TTTR(settings["ptu_hh_t3_filename"], 'PTU')
        header = data.header
        self.assertEqual(123, len(header.tags))

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
        # merge by __add__
        d3 = d + d
        self.assertEqual(len(d3), len(d) * 2)

    def test_header_copy_constructor(self):
        # import tttrlib
        # data = tttrlib.TTTR('./tttr-data/bh/bh_spc132.spc', 'SPC-130')
        p1 = data.header
        p2 = tttrlib.TTTRHeader(p1)
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
        # data = tttrlib.TTTR('./tttr-data/bh/bh_spc132.spc', 'SPC-130')
        d2 = tttrlib.TTTR(data)
        self.assertEqual(
            np.allclose(
                data.macro_times,
                d2.macro_times
            ),
            True
        )

    def test_getter_as_attributes(self):
        # Test exposure of getters as attributes
        # If an attribute is accesses that does not exist, e.g.,
        # used_routing_channels, TTTR objects fallback to
        # the corresponding getter by calling 'get_accessed_attribute'
        np.testing.assert_array_equal(
            data.used_routing_channels,
            data.get_used_routing_channels()
        )

    def test_constructor(self):
        # first element is the filename
        # second element is the name of the container
        # third element is the number used by tttrlib to identify containers
        names_types = [
            (settings["ptu_hh_t3_filename"], 'PTU', 0),
            (settings["ht3_clsm_filename"], 'HT3', 1),
            (settings["spc132_filename"], 'SPC-130', 2),
            (settings["spc630_filename"], 'SPC-600_256', 3),
            (settings["photon_hdf_filename"], 'PHOTON-HDF5', 5)
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

    def test_tttr_by_selection(self):
        ds = data.get_tttr_by_selection([1, 2])
        self.assertEqual(
            np.allclose(
                ds.macro_times,
                np.array([ 92675, 341107], dtype=np.uint64)
            ),
            True
        )

    def test_tttr_by_channel(self):
        tttr1 = data.get_tttr_by_channel([0])
        tttr2 = data.get_tttr_by_channel([8])
        self.assertEqual(len(tttr1), 56499)
        self.assertEqual(len(tttr2), 79468)

    def test_tttr_by_count_rate(self):
        filter_options = {
            'n_ph_max': 60,
            'time_window': 10.0e-3,  # = 10 ms (macro_time_resolution is in seconds)
            'invert': True  # set invert to True to select TW with more than 60 ph
        }
        tttr_sel = data.get_tttr_by_count_rate(**filter_options)

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
        np.testing.assert_array_equal(data.macro_times, data2.macro_times)

    def test_TTTRRange(self):
        # empty range object
        tttr_range_1 = tttrlib.TTTRRange()
        self.assertEqual(tttr_range_1.start, -1)
        self.assertEqual(tttr_range_1.stop, -1)
        self.assertTupleEqual(tuple(tttr_range_1.start_stop), (-1, -1))
        self.assertEqual(len(tttr_range_1.tttr_indices), 0)
        d = {
            "start": 11,
            "stop": 898
        }
        tttr_range_1 = tttrlib.TTTRRange(*d.values())
        self.assertEqual(tttr_range_1.start, d["start"])
        self.assertEqual(tttr_range_1.stop, d["stop"])
        self.assertTupleEqual(tuple(tttr_range_1.start_stop), (d["start"], d["stop"]))

        tttr_range_2 = tttrlib.TTTRRange(other=tttr_range_1)
        self.assertEqual(tttr_range_2.start, d["start"])
        self.assertEqual(tttr_range_2.stop, d["stop"])
        self.assertTupleEqual(tuple(tttr_range_2.start_stop), (d["start"], d["stop"]))

        # append an index
        tttr_range_1.insert(89)
        self.assertEqual(len(tttr_range_1.tttr_indices), 3)
        self.assertTupleEqual(tuple(tttr_range_1.tttr_indices), (11, 89, 898))
        tttr_range_1.clear()
        self.assertEqual(len(tttr_range_1.tttr_indices), 0)

        tttr_range_3 = tttrlib.TTTRRange(other=tttr_range_2)
        self.assertEqual(tttr_range_2, tttr_range_3)

        # strip/remove indices
        tr = tttrlib.TTTRRange()
        tr.insert(81)
        tr.insert(233)
        tr.insert(232223)
        tr.insert(44444443)
        tr.strip([233, 232223])
        print(list(tr.tttr_indices))
        self.assertTupleEqual(tuple(tr.tttr_indices), (81, 44444443,))
        
    def test_TTTRRange_strip(self):
        tr = tttrlib.TTTRRange()
        tr.insert(22)
        tr.insert(44)
        tr.insert(7)
        tr.insert(11)
        tr.insert(33)
        idx = list(tr.tttr_indices)
        self.assertListEqual(idx, [7, 11, 22, 33, 44])
        
        # offset is last element
        off = tr.strip([11, 22])
        self.assertEqual(off, 2)
        
        idx2 = list(tr.tttr_indices)
        self.assertListEqual(idx2, [7, 33, 44])
        

    def test_mean_microtime(self):
        self.assertAlmostEqual(data.get_mean_microtime(), 4.351471044638555e-09)

    def test_open_non_existing_file(self):
        # make sure that opening a non-exisitng file does not crash
        d = tttrlib.TTTR('NOFILE', 'PTU')
        self.assertEqual(
            len(d.macro_times), 0
        )
        header = d.header
        self.assertEqual(header.tttr_record_type, -1)

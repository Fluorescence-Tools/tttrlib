from __future__ import division

import unittest

import json
import numpy as np
import tttrlib

settings = json.load(open(file="./test/settings.json"))


clsm = {
    'sp5': {
        'filename': './tttr-data/imaging/leica/sp5/LSM_1.ptu',
        'reading_parameter': {
            "reading_routine": 'SP5'
        }
    },
    'sp8': {
        'filename': './tttr-data/imaging/leica/sp8/da/G-28_C-28_S1_6_1.ptu',
        'reading_parameter': {
            "reading_routine": 'SP8'
        }
    },
    'seidel_ht3_sample_1': {
        'filename': './tttr-data/imaging/pq/ht3/pq_ht3_clsm.ht3',
        'reading_parameter': {
            "marker_frame_start": [4],
            "marker_line_start": 1,
            "marker_line_stop": 2,
            "marker_event_type": 1,
            "n_pixel_per_line": 256,
            "reading_routine": 'default',
            "skip_before_first_frame_marker": True
        }
    },
    'seidel_ht3_sample_2': {
        'filename': './tttr-data/imaging/pq/ht3/crn_clv_img.ht3',
        'irf': './tttr-data/imaging/pq/ht3/crn_clv_mirror.ht3',
        'reading_parameter': {
            "marker_frame_start": [4],
            "marker_line_start": 1,
            "marker_line_stop": 2,
            "marker_event_type": 1,
            "n_pixel_per_line": 256,
            "reading_routine": 'default',
            "skip_before_first_frame_marker": True
        }
    },
    'zeiss980_1': {
        'filename': './tttr-data/imaging/zeiss/lsm980_pq/Training_2021-03-04.sptw/Cell_GFP/Cell1_T_0_P_0_Idx_4.ptu',
        'reading_parameter': {}
    }
}

make_reference = settings['make_references']


class TestCLSM(unittest.TestCase):

    def test_zeiss980_1(self):
        filename = clsm['zeiss980_1']['filename']
        reading_parameter = clsm['zeiss980_1']['reading_parameter']
        data = tttrlib.TTTR(filename)
        clsm_img = tttrlib.CLSMImage(data, fill=False, channels=[0], **reading_parameter)
        self.assertTupleEqual(clsm_img.shape, (86, 512, 512))

    def test_leica_sp8_image_2(self):
        filename = clsm['sp8']['filename']
        reading_parameter = clsm['sp8']['reading_parameter']
        data = tttrlib.TTTR(filename, 'PTU')
        clsm_image = tttrlib.CLSMImage(data, **reading_parameter)
        clsm_image.fill(
            tttr_data=data,
            channels=[1]
        )
        self.assertEqual(clsm_image.n_frames, 93)
        self.assertEqual(clsm_image.n_lines, 512)
        self.assertEqual(clsm_image.n_pixel, 512)
        # The string representation of a CLSMImage returns the
        # number of frames, lines, and pixels
        self.assertEqual(clsm_image.__str__(), "tttrlib.CLSMImage(93, 512, 512)")

    def test_leica_sp5_image(self):
        filename = clsm['sp5']['filename']
        reading_parameter = clsm['sp5']['reading_parameter']
        data = tttrlib.TTTR(filename, 'PTU')
        clsm_image = tttrlib.CLSMImage(
            tttr_data=data,
            channels=[0],
            fill=True,
            **reading_parameter
        )
        img = clsm_image.intensity.sum(axis=0)
        fn = './test/data/reference/img_intensity_image_sp5.npy'
        if make_reference:
            np.save(fn, img)
        self.assertEqual(np.allclose(np.load(fn), img), True)
        selection_mask = np.ones(
            (
                clsm_image.n_frames,
                clsm_image.n_lines,
                clsm_image.n_pixel,
            ),
            dtype=np.uint8
        )
        decays = clsm_image.get_decay_of_pixels(
            tttr_data=data,
            mask=selection_mask,
            tac_coarsening=1,
            stack_frames=False
        )
        decay = clsm_image.get_decay_of_pixels(
            tttr_data=data,
            mask=selection_mask,
            tac_coarsening=1,
            stack_frames=True
        )
        self.assertEqual(
            np.allclose(
                decays.sum(axis=0),
                decay
            ),
            True
        )

    def test_leica_sp8_histogram(self):
        filename = clsm['sp8']['filename']
        data = tttrlib.TTTR(filename, 'PTU')
        micro_time = data.micro_times
        mt_sel = micro_time
        counts = np.bincount(mt_sel // 4)
        header = data.get_header()
        dt = header.micro_time_resolution
        x_axis = np.arange(counts.shape[0]) * dt
        decay = np.vstack([x_axis, counts]).T
        fn = './test/data/reference/img_decay_histogram.npy'
        if make_reference:
            np.save(fn, decay)
        self.assertEqual(np.allclose(np.load(fn), decay), True)

    def test_clsm_intensity(self):
        filename = clsm['seidel_ht3_sample_1']['filename']
        reading_parameter = clsm['seidel_ht3_sample_1']['reading_parameter']
        data = tttrlib.TTTR(filename)
        clsm_image_1 = tttrlib.CLSMImage(
            tttr_data=data,
            **reading_parameter
        )
        clsm_image_1.fill(
            tttr_data=data,
            channels=[0]
        )

        fn = './test/data/reference/img_ref_intensity.npy'
        img = clsm_image_1.get_intensity().sum(axis=0)
        if make_reference:
            np.save(fn, img)
        self.assertEqual(np.allclose(img, np.load(fn,)), True)

    def test_clsm_mean_microtime(self):
        filename = clsm['seidel_ht3_sample_1']['filename']
        reading_parameter = clsm['seidel_ht3_sample_1']['reading_parameter']
        data = tttrlib.TTTR(filename)
        clsm_image_1 = tttrlib.CLSMImage(
            tttr_data=data,
            **reading_parameter
        )
        clsm_image_1.fill(
            tttr_data=data,
            channels=[0]
        )
        fn = './test/data/reference/img_ref_mean_tac.npy'
        img = clsm_image_1.get_mean_micro_time(tttr_data=data, minimum_number_of_photons=1)
        img = np.clip(img, 0, np.inf).sum(axis=0)
        if make_reference:
            np.save(fn, img)
        np.testing.assert_array_almost_equal(img, np.load(fn))
        tac_coarsening = 512
        img = clsm_image_1.get_fluorescence_decay(
            tttr_data=data,
            micro_time_coarsening=tac_coarsening,
            stack_frames=True
        ).sum(axis=0)
        fn = './test/data/reference/img_ref_decay_image.npy'
        if make_reference:
            np.save(fn, img)
        np.testing.assert_array_almost_equal(img, np.load(fn))

    def test_mean_tau_stack(self):
        """Mean lifetime images (with IRF correction)
        """
        data = tttrlib.TTTR(clsm['seidel_ht3_sample_2']['filename'], 'HT3')
        irf = tttrlib.TTTR(clsm['seidel_ht3_sample_2']['irf'], 'HT3')
        irf_0 = irf[irf.get_selection_by_channel([0])]
        clsm_image = tttrlib.CLSMImage(
            tttr_data=data,
            **clsm['seidel_ht3_sample_2']['reading_parameter']
        )
        clsm_image.fill(data, channels=[0, 2])
        img = clsm_image.get_mean_lifetime(
            tttr_irf=irf_0,
            tttr_data=data,
            minimum_number_of_photons=4,
            stack_frames=True
        )
        fn = './test/data/reference/img_decay_mean_tau.npy'
        if make_reference:
            np.save(fn, img)
        self.assertEqual(
            np.allclose(img, np.load(fn)),
            True
        )

    def test_mean_tau(self):
        """Mean lifetime images (with IRF correction)
        """
        data = tttrlib.TTTR(clsm['seidel_ht3_sample_2']['filename'], 'HT3')
        irf = tttrlib.TTTR(clsm['seidel_ht3_sample_2']['irf'], 'HT3')
        irf_0 = irf[irf.get_selection_by_channel([0])]
        clsm_image = tttrlib.CLSMImage(
            tttr_data=data,
            **clsm['seidel_ht3_sample_2']['reading_parameter']
        )
        clsm_image.fill(data, channels=[0, 2])
        img = clsm_image.get_mean_lifetime(
            tttr_irf=irf_0,
            tttr_data=data,
            minimum_number_of_photons=4,
            stack_frames=False
        )
        fn = './test/data/reference/img_decay_mean_tau_2.npy'
        if make_reference:
            np.save(fn, img)
        self.assertEqual(
            np.allclose(img, np.load(fn)),
            True
        )

    def strip_tttr_indices(self):
        # Test stripping of photons from clsm image by index
        data = tttrlib.TTTR(clsm['seidel_ht3_sample_2']['filename'], 'HT3')
        clsm_image = tttrlib.CLSMImage(
            tttr_data=data,
            **clsm['seidel_ht3_sample_2']['reading_parameter']
        )
        clsm_image.fill(channels=[0,1])
        idx = list()
        for pixel in clsm_image[0][0]:
            idx += list(pixel.tttr_indices)
        idx = np.array(idx)
        tn1 = data[idx]
        np.testing.assert_array_equal(
            np.array([0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1,
                   0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 0, 1, 1,
                   1, 1, 1, 0], dtype=np.int8),
            tn1.routing_channels
        )

        # Find idxs with routing = 1
        tttr_indices_ch0 = data.get_selection_by_channel([1])
        # strip from clsm
        clsm_image.strip(tttr_indices=tttr_indices_ch0)

        idx = list()
        for pixel in clsm_image[0][0]:
            idx += list(pixel.tttr_indices)
        idx = np.array(idx)
        tn = data[idx]
        np.testing.assert_array_equal(
            np.array([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], dtype=np.int8),
            tn.routing_channels
        )

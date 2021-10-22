from __future__ import division


import unittest

import json
import tttrlib
import numpy as np

settings = json.load(open(file="./test/settings.json"))

sp5_filename = './tttr-data/imaging/leica/sp5/LSM_1.ptu'
sp8_filename = './tttr-data/imaging/leica/sp8/da/G-28_C-28_S1_6_1.ptu'
ht3_filename = './tttr-data/imaging/pq/ht3/pq_ht3_clsm.ht3'
pq_test_files = [
    './tttr-data/imaging/pq/Microtime200_HH400/beads.ptu',
    './tttr-data/imaging/pq/Microtime200_TH260/beads.ptu'
]

sp8_reading_parameter = {
    "reading_routine": 'SP8',
}

ht3_reading_parameter = {
    "marker_frame_start": [4],
    "marker_line_start": 1,
    "marker_line_stop": 2,
    "marker_event_type": 1,
    "n_pixel_per_line": 256,
    "reading_routine": 'default',
    "skip_before_first_frame_marker": True
}

sp5_data = tttrlib.TTTR(sp5_filename, 'PTU')
sp5_reading_parameter = {
    "reading_routine": 'SP5'
}


class TestCLSM(unittest.TestCase):

    # If this is set to True as set of files are written as a
    # reference for future tests
    make_reference = settings['make_references']

    def test_leica_sp8_image_1(self):
        data = tttrlib.TTTR(sp8_filename, 'PTU')
        clsm_image = tttrlib.CLSMImage(
            tttr_data=data,
            **sp8_reading_parameter
        )
        clsm_image.fill_pixels(
            tttr_data=data,
            channels=[1]
        )
        # Test mean TAC image
        mean_tac_image = clsm_image.get_mean_micro_time_image(
            tttr_data=data,
            minimum_number_of_photons=1
        )
        mean_tac_image = np.clip(mean_tac_image, 0, 1280000)
        mean_tac_image = mean_tac_image.sum(axis=0)
        fn = './test/data/reference/img_ref_mean_tac_sp8.npy'
        if self.make_reference:
            np.save(fn, mean_tac_image)
        # Pixel with less than minimum_number_of_photons have negative numbers
        mean_tac_image = np.clip(mean_tac_image, 0, 1280000)
        np.testing.assert_array_almost_equal(np.load(fn), mean_tac_image)

        # Test decay image
        decay_image = clsm_image.get_fluorescence_decay_image(
            tttr_data=data,
            micro_time_coarsening=256,
            stack_frames=True
        )
        fn = './test/data/reference/img_ref_decay_image_sp8.npy'
        if self.make_reference:
            np.save(fn, decay_image)
        np.testing.assert_array_almost_equal(np.load(fn), decay_image)

        # Access the pixel
        frame_nbr = 1
        line_nbr = 200
        pixel_nbr = 200
        frame = clsm_image[frame_nbr]
        line = frame[line_nbr]
        pixel = line[pixel_nbr]

        self.assertEqual(pixel.tttr_indices[0], 40940)
        self.assertEqual(clsm_image[frame_nbr][line_nbr][pixel_nbr].tttr_indices[0], 40940)
        self.assertEqual(
            clsm_image[frame_nbr][line_nbr][pixel_nbr].tttr_indices[0],
            pixel.tttr_indices[0]
        )

    def test_get_frame_edges(self):
        # SP8
        filename = "./tttr-data/imaging/leica/sp8/da/G-28_C-28_S1_6_1.ptu"
        tttr = tttrlib.TTTR(filename)
        frame_marker = tttrlib.CLSMImage.get_frame_edges(tttr, reading_routine="SP8")
        ref = np.array([0, 33529, 66704, 100007, 133448, 166819, 200246,
                  233764, 266964, 300495, 334002, 367244, 400663, 433842,
                  467465, 500897, 534214, 567481, 600787, 633652, 666928,
                  699979, 733392, 766777, 800102, 833586, 866850, 900367,
                  934006, 967652, 1001222, 1034729, 1068121, 1101315, 1134802,
                  1168394, 1201654, 1235243, 1268487, 1301777, 1334909, 1368325,
                  1401602, 1435125, 1468726, 1501850, 1535224, 1568731, 1602119,
                  1635687, 1668901, 1702301, 1735680, 1769383, 1802764, 1836438,
                  1869872, 1903322, 1936681, 1969938, 2003582, 2036774, 2070282,
                  2103539, 2137239, 2170864, 2204452, 2237712, 2270818, 2304016,
                  2337694, 2370946, 2404394, 2437620, 2471361, 2504707, 2538323,
                  2571614, 2604919, 2638158, 2671470, 2704766, 2737823, 2770808,
                  2804447, 2837775, 2871352, 2904614, 2937702, 2971088, 3004543,
                  3037889, 3071241, 3104687, 3104829], dtype=np.uint32)
        self.assertEqual(np.allclose(frame_marker, ref), True)

        sp5_filename = './tttr-data/imaging/leica/sp5/LSM_1.ptu'
        sp5_data = tttrlib.TTTR(sp5_filename, 'PTU')
        frame_marker = tttrlib.CLSMImage.get_frame_edges(tttr, reading_routine="SP5")
        clsm_image = tttrlib.CLSMImage(
            tttr_data=sp5_data,
            channels=[0],
            fill=True,
            reading_routine='SP5'
        )

    def test_copy_constructor(self):
        data = tttrlib.TTTR(ht3_filename, 'HT3')
        reading_parameter = ht3_reading_parameter
        clsm_image_1 = tttrlib.CLSMImage(
            tttr_data=data,
            **reading_parameter
        )
        clsm_image_2 = tttrlib.CLSMImage(source=clsm_image_1, fill=True)
        self.assertAlmostEqual(
            float(np.sum(clsm_image_1.intensity - clsm_image_2.intensity)),
            0.0
        )

    def test_open_clsm_ptu_read_header(self):
        for ptu_file in pq_test_files:
            data = tttrlib.TTTR(ptu_file)
            clsm = tttrlib.CLSMImage(data)

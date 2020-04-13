from __future__ import division


import unittest
import tttrlib
import numpy as np


class TestCLSM(unittest.TestCase):

    # If this is set to True as set of files are wirtten as a
    # reference for future tests
    make_reference = False

    sp8_filename = './data/leica/sp8/da/G-28_C-28_S1_6_1.ptu'
    sp8_reading_parameter = {
        "marker_frame_start": [4, 6],
        "marker_line_start": 1,
        "marker_line_stop": 2,
        "marker_event_type": 15,
        "n_pixel_per_line": 0,
        "reading_routine": 'SP8',
    }

    ht3_filename = './data/PQ/HT3/PQ_HT3_CLSM.ht3'
    ht3_reading_parameter = {
        "marker_frame_start": [4],
        "marker_line_start": 1,
        "marker_line_stop": 2,
        "marker_event_type": 1,
        "n_pixel_per_line": 256,
        "reading_routine": 'default'
    }

    sp5_filename = './data/leica/sp5/LSM_1.ptu'
    sp5_data = tttrlib.TTTR(sp5_filename, 'PTU')
    sp5_reading_parameter = {
        "marker_frame_start": [4, 6],
        "marker_line_start": 1,
        "marker_line_stop": 2,
        "marker_event_type": 1,
        "n_pixel_per_line": 256,
        "reading_routine": 'SP5'
    }

    def test_leica_sp8_image_1(self):
        print("test_leica_sp8_image_1")
        data = tttrlib.TTTR(self.sp8_filename, 'PTU')
        clsm_image = tttrlib.CLSMImage(
            tttr_data=data,
            **self.sp8_reading_parameter
        )
        clsm_image.fill_pixels(
            tttr_data=data,
            channels=[1]
        )

        # Test mean TAC image
        mean_tac_image = clsm_image.get_mean_tac_image(
            tttr_data=data,
            n_ph_min=1
        ).sum(axis=0)
        if self.make_reference:
            np.save('./data/reference/img_ref_mean_tac_sp8.npy', mean_tac_image)
        self.assertEqual(
            np.allclose(
                np.load('./data/reference/img_ref_mean_tac_sp8.npy'), mean_tac_image
            ),
            True
        )

        # Test decay image
        decay_image = clsm_image.get_fluorescence_decay_image(
            tttr_data=data,
            tac_coarsening=256,
            stack_frames=True
        )
        if self.make_reference:
            np.save('./data/reference/img_ref_decay_image_sp8.npy', decay_image)
        self.assertEqual(
            np.allclose(
                np.load('./data/reference/img_ref_decay_image_sp8.npy'),
                decay_image
            ),
            True
        )

        # Access the pixel
        frame_nbr = 0
        frame = clsm_image[frame_nbr]
        line_nbr = 200
        line = frame[line_nbr]
        pixel_nbr = 200
        pixel = line[pixel_nbr]
        self.assertEqual(
            pixel.tttr_indices[0],
            40940
        )
        self.assertEqual(
            clsm_image[0][200][200].tttr_indices[0],
            40940
        )
        self.assertEqual(
            clsm_image[0][200][200].tttr_indices[0],
            pixel.tttr_indices[0]
        )

    def test_copy_constructor(self):
        reading_parameter = {
            "tttr_data": tttrlib.TTTR(self.sp5_filename, 'PTU'),
            "marker_frame_start": [4, 6],
            "marker_line_start": 1,
            "marker_line_stop": 2,
            "marker_event_type": 1,
            "n_pixel_per_line": 256,
            "reading_routine": 'SP5',
            "channels": [0],
            "fill": True
        }
        clsm_image_1 = tttrlib.CLSMImage(**reading_parameter)
        clsm_image_2 = tttrlib.CLSMImage(source=clsm_image_1, fill=True)
        self.assertAlmostEqual(
            float(np.sum(clsm_image_1.intensity - clsm_image_2.intensity)),
            0.0
        )


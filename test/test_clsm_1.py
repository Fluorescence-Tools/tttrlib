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
        # if set to zero the number of pixels will be the set to the number
        # of lines
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

    def test_leica_sp8_image_2(self):
        print("test_leica_sp8_image_2")
        filename = self.sp8_filename
        reading_parameter = self.sp8_reading_parameter

        data = tttrlib.TTTR(filename, 'PTU')
        clsm_image = tttrlib.CLSMImage(
            tttr_data=data, **reading_parameter
        )
        clsm_image.fill_pixels(
            tttr_data=data,
            channels=[1]
        )
        self.assertEqual(clsm_image.n_frames, 92)
        self.assertEqual(clsm_image.n_lines, 512)
        self.assertEqual(clsm_image.n_lines, 512)
        # The string representation of a CLSMImage returns the
        # number of frames, lines, and pixels
        self.assertEqual(
            clsm_image.__str__(),
            "tttrlib.CLSMImage(92, 512, 512)"
        )

    def test_leica_sp5_image(self):
        print("test_leica_sp5_image")
        filename = self.sp5_filename
        reading_parameter = self.sp5_reading_parameter
        data = tttrlib.TTTR(filename, 'PTU')
        clsm_image = tttrlib.CLSMImage(
            tttr_data=data,
            channels=[0],
            fill=True,
            **reading_parameter
        )
        intensity_image_1 = clsm_image.intensity.sum(axis=0)
        reference_filename = './data/reference/img_intensity_image_sp5.npy'
        if self.make_reference:
            np.save(reference_filename, intensity_image_1)

        self.assertEqual(
            np.allclose(
                np.load(reference_filename,),
                intensity_image_1
            ),
            True
        )
        selection = np.ones(
            (
                clsm_image.n_frames,
                clsm_image.n_lines,
                clsm_image.n_pixel,
            ),
            dtype=np.uint8
        )

        decays = clsm_image.get_average_decay_of_pixels(
            tttr_data=data,
            selection=selection,
            tac_coarsening=1,
            stack_frames=False
        )
        decay = clsm_image.get_average_decay_of_pixels(
            tttr_data=data,
            selection=selection,
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
        print("test_leica_sp8_histogram")
        sp8_filename = './data/leica/sp8/da/G-28_C-28_S1_6_1.ptu'
        data = tttrlib.TTTR(sp8_filename, 'PTU')

        micro_time = data.micro_times
        mt_sel = micro_time
        counts = np.bincount(mt_sel // 4)
        header = data.get_header()
        dt = header.micro_time_resolution
        x_axis = np.arange(counts.shape[0]) * dt
        decay = np.vstack([x_axis, counts]).T
        if self.make_reference:
            np.save(
                './data/reference/img_decay_histogram.npy',
                decay
            )
        self.assertEqual(
            np.allclose(
                np.load('./data/reference/img_decay_histogram.npy'),
                decay
            ),
            True
        )

    def test_clsm_intensity(self):
        print("test_clsm_intensity")
        data = tttrlib.TTTR(self.ht3_filename, 'HT3')
        reading_parameter = self.ht3_reading_parameter

        clsm_image_1 = tttrlib.CLSMImage(
            tttr_data=data,
            **reading_parameter
        )
        clsm_image_1.fill_pixels(
            tttr_data=data,
            channels=[0]
        )
        intensity_image_1 = clsm_image_1.get_intensity_image().sum(axis=0)
        self.assertEqual(
            np.allclose(
                intensity_image_1,
                np.load('./data/reference/img_ref_intensity.npy',)
            ),
            True
        )
        mean_tac_image = clsm_image_1.get_mean_micro_time_image(
            tttr_data=data,
            minimum_number_of_photons=1
        ).sum(axis=0)
        self.assertEqual(
            np.allclose(
                mean_tac_image,
                np.load('./data/reference/img_ref_mean_tac.npy',)
            ),
            True
        )
        tac_coarsening = 512
        tac_image = clsm_image_1.get_fluorescence_decay_image(
            tttr_data=data,
            micro_time_coarsening=tac_coarsening,
            stack_frames=True
        ).sum(axis=1)
        self.assertEqual(
            np.allclose(
                tac_image,
                np.load(
                    './data/reference/img_ref_decay_image.npy',
                )
            ),
            True
        )

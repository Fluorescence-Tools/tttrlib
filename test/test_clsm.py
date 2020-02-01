from __future__ import division


import unittest
import tttrlib
import numpy as np


class TestCLSM(unittest.TestCase):

    sp8_filename = './data/leica/sp8/da/G-28_C-28_S1_6_1.ptu'
    sp8_data = tttrlib.TTTR(sp8_filename, 'PTU')

    ht3_filename = './data/PQ/HT3/PQ_HT3_CLSM.ht3'
    ht3_data = tttrlib.TTTR(ht3_filename, 'HT3')

    sp5_filename = './data/leica/sp5/LSM_1.ptu'
    sp5_data = tttrlib.TTTR(sp5_filename, 'PTU')

    def test_leica_sp8_image_1(self):
        data = self.sp8_data
        frame_marker = [4, 6]
        line_start_marker = 1
        line_stop_marker = 2
        event_type_marker = 15
        pixel_per_line = 0
        reading_routine = 'SP8'

        clsm_image = tttrlib.CLSMImage(
            data,
            frame_marker,
            line_start_marker,
            line_stop_marker,
            event_type_marker,
            pixel_per_line,
            reading_routine
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
        np.save('./data/reference/img_ref_mean_tac_sp8.npy', mean_tac_image)
        self.assertEqual(
            np.allclose(
                np.load(
                    './data/reference/img_ref_mean_tac_sp8.npy',
                ),
                mean_tac_image
            ),
            True
        )

        # Test decay image
        decay_image = clsm_image.get_decay_image(
            tttr_data=data,
            tac_coarsening=256,
            stack_frames=True
        )
        self.assertEqual(
            np.allclose(
                np.load(
                    './data/reference/img_ref_decay_image_sp8.npy',
                ),
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
            pixel.get_tttr_indices()[0],
            40940
        )
        self.assertEqual(
            clsm_image[0][200][200].get_tttr_indices()[0],
            40940
        )
        self.assertEqual(
            clsm_image[0][200][200].get_tttr_indices()[0],
            pixel.get_tttr_indices()[0]
        )

    def test_leica_sp8_image_2(self):
        data = self.sp8_data
        frame_marker = [4, 6]
        line_start_marker = 1
        line_stop_marker = 2
        event_type_marker = 15
        pixel_per_line = 512
        reading_routine = 'SP8'

        clsm_image = tttrlib.CLSMImage(
            data,
            frame_marker,
            line_start_marker,
            line_stop_marker,
            event_type_marker,
            pixel_per_line,
            reading_routine
        )
        clsm_image.fill_pixels(
            tttr_data=data,
            channels=[1]
        )

        selection = np.ones(
            (
                clsm_image.n_frames,
                clsm_image.n_lines,
                clsm_image.n_pixel,
            ),
            dtype=np.uint8
        )
        decays = clsm_image.get_decays(
            tttr_data=data,
            selection=selection,
            tac_coarsening=1,
            stack_frames=False
        )

        decay = clsm_image.get_decays(
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

    def test_leica_sp5_image(self):
        data = self.sp5_data
        frame_marker = [4, 6]
        line_start_marker = 1
        line_stop_marker = 2
        event_type_marker = 1
        pixel_per_line = 256
        reading_routine = 'SP5'

        clsm_image = tttrlib.CLSMImage(
            data,
            frame_marker,
            line_start_marker,
            line_stop_marker,
            event_type_marker,
            pixel_per_line,
            reading_routine
        )
        clsm_image.fill_pixels(
            tttr_data=data,
            channels=[0]
        )

        clsm_image_2 = tttrlib.CLSMImage(clsm_image, True)
        intensity_image_1 = clsm_image.get_intensity_image().sum(axis=0)
        intensity_image_2 = clsm_image_2.get_intensity_image().sum(axis=0)
        self.assertEqual(
            np.allclose(intensity_image_1, intensity_image_2),
            True
        )

        self.assertEqual(
            np.allclose(
                np.load(
                    './data/reference/img_intensity_image_sp5.npy',
                ),
                intensity_image_1
            ),
            True
        )

    def test_leica_sp8_histogram(self):
        data = self.sp8_data
        micro_time = data.get_micro_time()
        mt_sel = micro_time
        counts = np.bincount(mt_sel // 4)
        header = data.get_header()
        dt = header.micro_time_resolution
        x_axis = np.arange(counts.shape[0]) * dt
        output_filename = 'test.csv'
        np.savetxt(
            fname=output_filename,
            X=np.vstack([x_axis, counts]).T
        )

    def test_clsm_intensity(self):
        data = self.ht3_data
        frame_marker = [4]
        line_start_marker = 1
        line_stop_marker = 2
        event_type_marker = 1
        pixel_per_line = 0
        clsm_image_1 = tttrlib.CLSMImage(
            data,
            frame_marker,
            line_start_marker,
            line_stop_marker,
            event_type_marker,
            pixel_per_line,
            'default'
        )
        clsm_image_1.fill_pixels(
            tttr_data=data,
            channels=[0]
        )
        intensity_image_1 = clsm_image_1.get_intensity_image().sum(axis=0)
        self.assertEqual(
            np.allclose(
                intensity_image_1,
                np.load(
                    './data/reference/img_ref_intensity.npy',
                )
            ),
            True
        )

        mean_tac_image = clsm_image_1.get_mean_tac_image(
            tttr_data=data,
            n_ph_min=1
        ).sum(axis=0)
        self.assertEqual(
            np.allclose(
                mean_tac_image,
                np.load(
                    './data/reference/img_ref_mean_tac.npy',
                )
            ),
            True
        )

        tac_coarsening = 512
        tac_image = clsm_image_1.get_decay_image(
            tttr_data=data,
            tac_coarsening=tac_coarsening,
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

        clsm_image_2 = tttrlib.CLSMImage(clsm_image_1, False)
        clsm_image_2.fill_pixels(
            tttr_data=data,
            channels=[1]
        )
        intensity_image_2 = clsm_image_1.get_intensity_image().sum(axis=0)
        self.assertEqual(
            np.allclose(
                intensity_image_2,
                intensity_image_1
            ),
            True
        )


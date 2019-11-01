from __future__ import division

import utils
import os
import unittest

TOPDIR = os.path.abspath(
    os.path.join(
        os.path.dirname(__file__),
        '..')
)
utils.set_search_paths(TOPDIR)

import tttrlib
import numpy as np


class TestCLSM(unittest.TestCase):

    filename_data_da = './data/leica/sp8/da/G-28_C-28_S1_6_1.ptu'
    filename_data_d0 = './data/leica/sp8/d0/G-28_S1_1_1.ptu'
    filename_irf = './data/leica/sp8/IRF488_20MHz_25_1.ptu'

    def test_leica_sp8_image(self):
        import tttrlib
        import numpy as np
        import pylab as p

        filename = self.filename_data_da
        data = tttrlib.TTTR(
            filename,
            'PTU'
        )
        frame_marker = 4
        line_start_marker = 1
        line_stop_marker = 2
        event_type_marker = 15
        pixel_per_line = 512
        reading_routine = 1  # sp8-reading routine

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
        intensity_image = clsm_image.get_intensity_image()
        p.imshow(intensity_image.sum(axis=0))

        mean_tac_image = clsm_image.get_mean_tac_image(
            tttr_data=data,
            n_ph_min=1
        )
        p.imshow(mean_tac_image.sum(axis=0))

        decay_image = clsm_image.get_decay_image(
            tttr_data=data,
            tac_coarsening=8
        )
        decay = np.zeros(decay_image.shape[3])
        for frame in decay_image:
            decay += frame.sum(axis=(0, 1))

    def test_leica_sp8_histogram(self):
        import numpy as np
        import tttrlib
        import pylab as p

        filename = self.filename_irf
        data = tttrlib.TTTR(
            filename,
            'PTU'
        )

        micro_time = data.get_micro_time()
        mt_sel = micro_time
        counts = np.bincount(mt_sel // 4)
        p.semilogy(counts)
        p.show()
        header = data.get_header()
        dt = header.micro_time_resolution
        x_axis = np.arange(counts.shape[0]) * dt
        output_filename = 'test_irf.csv'
        np.savetxt(
            fname=output_filename,
            X=np.vstack([x_axis,counts]).T
        )

    def test_clsm_intensity(self):
        import tttrlib
        import numpy as np
        data = tttrlib.TTTR(
            './data/PQ/HT3/PQ_HT3_CLSM.ht3',
            'HT3'
        )
        frame_marker = 4
        line_start_marker = 1
        line_stop_marker = 2
        event_type_marker = 1
        pixel_per_line = 256
        clsm_image = tttrlib.CLSMImage(
            data,
            frame_marker,
            line_start_marker,
            line_stop_marker,
            event_type_marker,
            pixel_per_line
        )
        clsm_image.fill_pixels(
            tttr_data=data,
            channels=[0]
        )
        intensity_image = clsm_image.get_intensity_image()
        intensity_image_reference = np.load(
            './data/reference/img_ref_intensity.npy',
        )
        self.assertEqual(
            np.allclose(
                intensity_image,
                intensity_image_reference
            ),
            True
        )
        # import pylab as p
        # p.imshow(intensity_image.sum(axis=0))
        # mean_tac_image = clsm_image.get_mean_tac_image(
        #     tttr_data=data,
        #     n_ph_min=1
        # )
        # mean_tac_image_reference = np.load(
        #     './data/reference/img_ref_mean_tac.npy',
        # )
        # self.assertEqual(
        #     np.allclose(
        #         mean_tac_image,
        #         mean_tac_image_reference
        #     ),
        #     True
        # )
        #
        #
        # tac_coarsening = 4
        # tac_image = clsm_image.get_decay_image(
        #     tttr_data=data,
        #     tac_coarsening=tac_coarsening
        # )


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

    # This test segfaults
    # @unittest.expectedFailure
    # def test_clsm_intensity(self):
    #     import tttrlib
    #     import numpy as np
    #     data = tttrlib.TTTR(
    #         './data/PQ/HT3/PQ_HT3_CLSM.ht3',
    #         'HT3'
    #     )
    #     frame_marker = 4
    #     line_start_marker = 1
    #     line_stop_marker = 2
    #     event_type_marker = 1
    #     pixel_per_line = 256
    #     clsm_image = tttrlib.CLSMImage(
    #         data,
    #         frame_marker,
    #         line_start_marker,
    #         line_stop_marker,
    #         event_type_marker,
    #         pixel_per_line
    #     )
    #     clsm_image.fill_pixels(
    #         tttr_data=data,
    #         channels=[0]
    #     )
    #     intensity_image = clsm_image.get_intensity_image()
    #     intensity_image_reference = np.load(
    #         './data/reference/img_ref_intensity.npy',
    #     )
    #     self.assertEqual(
    #         np.allclose(
    #             intensity_image,
    #             intensity_image_reference
    #         ),
    #         True
    #     )
    #     mean_tac_image = clsm_image.get_mean_tac_image(
    #         tttr_data=data,
    #         n_ph_min=1
    #     )
    #     mean_tac_image_reference = np.load(
    #         './data/reference/img_ref_mean_tac.npy',
    #     )
    #     self.assertEqual(
    #         np.allclose(
    #             mean_tac_image,
    #             mean_tac_image_reference
    #         ),
    #         True
    #     )

        #
        # tac_coarsening = 4
        # tac_image = clsm_image.get_decay_image(
        #     tttr_data=data,
        #     tac_coarsening=tac_coarsening
        # )


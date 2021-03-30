from __future__ import division

import unittest
import tttrlib
import numpy as np

print("Test: ", __file__)


sp8_filename = '../tttr-data/imaging/leica/sp8/da/G-28_C-28_S1_6_1.ptu'
sp8_reading_parameter = {
    "marker_frame_start": [4, 6],
    "marker_line_start": 1,
    "marker_line_stop": 2,
    "marker_event_type": 15,
    # if zero the number of pixels is the set to the number of lines
    "n_pixel_per_line": 0,
    "reading_routine": 'SP8',
}

ht3_filename = '../tttr-data/imaging/pq/ht3/pq_ht3_clsm.ht3'
ht3_filename_img = '../tttr-data/imaging/pq/ht3/crn_clv_img.ht3'
ht3_filename_irf = '../tttr-data/imaging/pq/ht3/crn_clv_mirror.ht3'
ht3_reading_parameter = {
    "marker_frame_start": [4],
    "marker_line_start": 1,
    "marker_line_stop": 2,
    "marker_event_type": 1,
    "n_pixel_per_line": 256,
    "reading_routine": 'default',
    "skip_before_first_frame_marker": True
}

sp5_filename = '../tttr-data/imaging/leica/sp5/LSM_1.ptu'
sp5_data = tttrlib.TTTR(sp5_filename, 'PTU')
sp5_reading_parameter = {
    "marker_frame_start": [4, 6],
    "marker_line_start": 1,
    "marker_line_stop": 2,
    "marker_event_type": 1,
    "n_pixel_per_line": 256,
    "reading_routine": 'SP5'
}

# If this is set to True as set of files are wirtten as a
# reference for future tests
make_reference = False


class TestCLSM(unittest.TestCase):

    def test_leica_sp8_image_2(self):
        # BROKEN
        filename = sp8_filename
        reading_parameter = sp8_reading_parameter

        data = tttrlib.TTTR(filename, 'PTU')
        clsm_image = tttrlib.CLSMImage(data, **reading_parameter)
        clsm_image.fill_pixels(
            tttr_data=data,
            channels=[1]
        )
        self.assertEqual(clsm_image.n_frames, 93)
        self.assertEqual(clsm_image.n_lines, 512)
        self.assertEqual(clsm_image.n_lines, 512)
        # The string representation of a CLSMImage returns the
        # number of frames, lines, and pixels
        self.assertEqual(
            clsm_image.__str__(),
            "tttrlib.CLSMImage(93, 512, 512)"
        )

    def test_leica_sp5_image(self):
        # BROKEN
        # PROBLEM WITH LINE START/STOP
        filename = sp5_filename
        reading_parameter = sp5_reading_parameter
        data = tttrlib.TTTR(filename, 'PTU')
        clsm_image = tttrlib.CLSMImage(
            tttr_data=data,
            channels=[0],
            fill=True,
            **reading_parameter
        )
        intensity_image_1 = clsm_image.intensity.sum(axis=0)
        reference_filename = './data/reference/img_intensity_image_sp5.npy'
        if make_reference:
            np.save(reference_filename, intensity_image_1)

        self.assertEqual(
            np.allclose(
                np.load(reference_filename,),
                intensity_image_1
            ),
            True
        )
        selection_mask = np.ones(
            (
                clsm_image.n_frames,
                clsm_image.n_lines,
                clsm_image.n_pixel,
            ),
            dtype=np.uint8
        )
        decays = clsm_image.get_average_decay_of_pixels(
            tttr_data=data,
            mask=selection_mask,
            tac_coarsening=1,
            stack_frames=False
        )
        decay = clsm_image.get_average_decay_of_pixels(
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
        data = tttrlib.TTTR(sp8_filename, 'PTU')
        micro_time = data.micro_times
        mt_sel = micro_time
        counts = np.bincount(mt_sel // 4)
        header = data.get_header()
        dt = header.micro_time_resolution
        x_axis = np.arange(counts.shape[0]) * dt
        decay = np.vstack([x_axis, counts]).T
        if make_reference:
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
        """Intensity images
        """
        print("test_clsm_intensity")
        data = tttrlib.TTTR(ht3_filename, 'HT3')
        reading_parameter = ht3_reading_parameter
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
                np.load('./data/reference/img_ref_decay_image.npy')
            ),
            True
        )

    def test_mean_tau(self):
        """Mean lifetime images (with IRF correction)
        """
        data = tttrlib.TTTR(ht3_filename_img, 'HT3')
        irf = tttrlib.TTTR(ht3_filename_irf, 'HT3')
        irf_0 = irf[irf.get_selection_by_channel([0])]
        clsm_image = tttrlib.CLSMImage(
            tttr_data=data,
            **ht3_reading_parameter
        )
        mean_tau = clsm_image.get_mean_lifetime_image(
                    tttr_irf=irf_0,
                    tttr_data=data,
                    minimum_number_of_photons=2
        )
        reference_fn = './data/reference/img_decay_mean_tau.npy'
        if make_reference:
            np.save(reference_fn, mean_tau)
        self.assertEqual(
            np.allclose(mean_tau, np.load(reference_fn)),
            True
        )

from __future__ import division

import unittest

import json
import tttrlib
import numpy as np
import skimage

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
ht3_data = tttrlib.TTTR(ht3_filename)

sp5_data = tttrlib.TTTR(sp5_filename, 'PTU')
sp5_reading_parameter = {
    "reading_routine": 'SP5'
}


class TestCLSMTransform(unittest.TestCase):

    # If this is set to True as set of files are written as a
    # reference for future tests
    make_reference = settings['make_references']

    def test_crop(self):
        # Read contents of file into new CLSMImage
        settings = {
            "channels": [0, 1],
            "fill": True,
        }
        image = tttrlib.CLSMImage(ht3_data, **settings)
        self.assertTupleEqual(image.shape, (40, 256, 256))
        self.assertTupleEqual(image.intensity.shape, (40, 256, 256))
        # Cropping
        image.crop(5, 20, 0, 128, 0, 96)
        self.assertTupleEqual(image.shape, (15, 128, 96))
        self.assertTupleEqual(image.intensity.shape, (15, 128, 96))

    def test_index_transform(self):
        settings = {
            "channels": [0, 1],
            "fill": True,
        }
        image = tttrlib.CLSMImage(ht3_data, **settings)
        n_frames, n_lines, n_pixel = image.shape
        self.assertEqual(
            image.to1D(1, 0, 0),
            n_lines * n_pixel
        )
        self.assertEqual(
            image.to1D(1, 0, 1),
            n_lines * n_pixel + 1
        )
        self.assertEqual(
            image.to1D(1, 2, 1),
            n_lines * n_pixel + n_lines * 2 + 1
        )
        idx = (2, 0, 1)
        self.assertTupleEqual(image.to3D(image.to1D(*idx)), idx)

    def test_clsm_binning(self):
        reading_parameter = dict()
        reading_parameter.update(ht3_reading_parameter)
        reading_parameter.update(
            {
                "channels": [0, 1],
                "fill": True,
            }
        )
        image = tttrlib.CLSMImage(
            tttr_data=ht3_data, # only read the data once per test file
            **reading_parameter
        )
        img_original = image.intensity.sum(axis=0)

        # do a 4x4 binning (uses internally CLSMImage.transform)
        bin_line = 4
        bin_pixel = 4
        n_frames, n_lines, n_pixel = image.shape

        image.rebin(bin_line, bin_pixel)
        img_transformed = image.intensity.sum(axis=0)
        img_transformed = np.copy(img_transformed)
        img_transformed = img_transformed.astype(np.uint16)
        print(img_transformed)

        # compare to reference images
        fn = './test/data/reference/img_ref_clsm_binning.tif'
        if self.make_reference:
            skimage.io.imsave(fn, img_transformed, check_contrast=False, imagej=True)
        np.testing.assert_array_almost_equal(skimage.io.imread(fn), img_transformed)



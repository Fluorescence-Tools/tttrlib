"""Canonical cross-language CLSM reference values.

The SAME assertions are checked in the R (``test/r/test_clsm.R``) and Java
(``test/java/CLSMTest.java``) suites, so CLSM reconstruction is verified to
produce identical images from all three language bindings.

Reference file: ``imaging/pq/ht3/pq_ht3_clsm.ht3``, routing channel 0.
"""
from __future__ import annotations

import unittest

import numpy as np
import tttrlib

from test_settings import settings, DATA_AVAILABLE, get_data_path  # type: ignore

# Canonical values (source of truth for all three language bindings).
REF_N_FRAMES = 40
REF_N_LINES = 256
REF_N_PIXEL = 256
REF_INTENSITY_SUM = 3364714
REF_INTENSITY_MAX = 26
REF_MMT_NONZERO = 610553       # mean-micro-time pixels with a defined value (> 0)
REF_PHASOR_VALID = 412275      # phasor pixels with a defined g coordinate (> -1)


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class CLSMCrossLanguageReference(unittest.TestCase):
    def setUp(self):
        self.tttr = tttrlib.TTTR(get_data_path(settings["ht3_clsm_filename"]))
        self.img = tttrlib.CLSMImage(self.tttr, channels=[0], fill=True)

    def test_dimensions_and_intensity(self):
        self.assertEqual(self.img.n_frames, REF_N_FRAMES)
        self.assertEqual(self.img.n_lines, REF_N_LINES)
        self.assertEqual(self.img.n_pixel, REF_N_PIXEL)
        intensity = np.asarray(self.img.get_intensity())
        self.assertEqual(int(intensity.sum()), REF_INTENSITY_SUM)
        self.assertEqual(int(intensity.max()), REF_INTENSITY_MAX)

    def test_mean_micro_time(self):
        mmt = np.asarray(self.img.get_mean_micro_time(self.tttr))
        self.assertEqual(int((mmt > 0).sum()), REF_MMT_NONZERO)

    def test_phasor(self):
        ph = np.asarray(self.img.get_phasor(self.tttr))
        self.assertEqual(int((ph[..., 0] > -1).sum()), REF_PHASOR_VALID)


if __name__ == "__main__":
    unittest.main()

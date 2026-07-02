# SPDX-License-Identifier: BSD-3-Clause
"""Tests for DecayFitData, the plain container feeding fit23-fit26."""
from __future__ import division

import unittest

import numpy as np
import tttrlib


class TestDecayFitData(unittest.TestCase):

    def test_construction_sizes(self):
        irf = np.arange(8, dtype=np.float64)
        bg = np.zeros(8)
        data = np.arange(8, dtype=np.int32)
        d = tttrlib.DecayFitData(
            dt=0.5,
            corrections=[32.0, 1.0, 0.1, 0.2, 3.0],
            irf=irf, background=bg, data=data
        )
        self.assertEqual(d.n_channels(), 4)
        self.assertEqual(len(d.get_model()), 8)
        self.assertEqual(list(d.get_data()), list(data))
        np.testing.assert_allclose(d.get_irf(), irf)
        np.testing.assert_allclose(d.get_background(), bg)
        np.testing.assert_allclose(d.get_corrections(), [32.0, 1.0, 0.1, 0.2, 3.0])
        self.assertAlmostEqual(d.dt, 0.5)

    def test_padding_to_longest(self):
        # arrays of different lengths are padded to the longest (Jordi 2*n)
        d = tttrlib.DecayFitData(
            dt=1.0,
            corrections=[1.0],
            irf=np.zeros(10),
            background=np.zeros(10),
            data=np.zeros(4, dtype=np.int32)
        )
        self.assertEqual(len(d.get_data()), 10)
        self.assertEqual(len(d.get_model()), 10)

    def test_set_data(self):
        d = tttrlib.DecayFitData(
            dt=1.0, corrections=[1.0],
            irf=np.zeros(6), background=np.zeros(6),
            data=np.zeros(6, dtype=np.int32)
        )
        d.set_data(np.array([1, 2, 3, 4, 5, 6], dtype=np.int32))
        self.assertEqual(list(d.get_data()), [1, 2, 3, 4, 5, 6])
        self.assertGreaterEqual(len(d.get_model()), 6)

    def test_model_filled_by_fit(self):
        # after a fit, the model array holds the fitted decay
        irf = np.zeros(64)
        irf[4] = 1000; irf[36] = 1000
        data = np.random.default_rng(1).poisson(
            np.roll(irf, 3) / irf.sum() * 500).astype(np.int32)
        d = tttrlib.DecayFitData(
            dt=0.5, corrections=[32.0, 1.0, 0.1, 0.1, 31.0],
            irf=irf, background=np.zeros(64), data=data
        )
        x = np.zeros(8)
        x[:4] = [2.0, 0.01, 0.38, 1.2]
        x[4] = -1
        fixed = np.array([0, 1, 1, 1], dtype=np.int16)
        twoIstar = tttrlib.DecayFit23.fit(x, fixed, d)
        self.assertTrue(np.isfinite(twoIstar))
        model = np.asarray(d.get_model())
        self.assertEqual(len(model), 64)
        self.assertGreater(model.sum(), 0)


if __name__ == "__main__":
    unittest.main()

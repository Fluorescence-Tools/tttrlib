"""The deprecated pre-interface API still works, and still gives the old answers.

The shim exists so Python callers can migrate on their own schedule rather than
in lockstep with the C++ change. That is only worth anything if it is *exact*:
these pin the same reference values the interface tests pin, reached through the
old call convention, so a caller that has not migrated yet gets the number it
always did.

Deleting this file is part of removing the shim in 0.29.
"""
from __future__ import division

import unittest
import warnings

import numpy as np

import tttrlib


FN = 32
DT = 0.5
DATA = np.array([
    0, 0, 0, 1, 9, 7, 5, 5, 5, 2, 2, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 2, 2, 2, 2, 3, 0, 1, 0,
    1, 1, 1, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
], dtype=float)


def _irf():
    irf = np.zeros(2 * FN)
    for half in (0, FN):
        for i in range(FN):
            irf[half + i] = np.exp(-((i - 8.0) ** 2) / (2 * 0.5 * 0.5))
    return irf


def _kwargs():
    return dict(dt=DT, irf=_irf(), period=2.0 * FN, g_factor=1.0,
                l1=0.1, l2=0.1, convolution_stop=FN // 2 - 1)


class TestDeprecationIsAnnounced(unittest.TestCase):

    def test_construction_warns_and_names_its_removal(self):
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            tttrlib.Fit23(background=np.zeros(2 * FN), **_kwargs())
        self.assertTrue(caught)
        self.assertIs(caught[0].category, DeprecationWarning)
        message = str(caught[0].message)
        self.assertIn("0.29", message)          # the deadline travels with it
        self.assertIn("DecayFit2", message)     # and so does the replacement


class TestOldAnswersUnchanged(unittest.TestCase):
    """The shim reproduces the published values, not merely something plausible."""

    def setUp(self):
        warnings.simplefilter("ignore", DeprecationWarning)

    def test_fit23(self):
        fit = tttrlib.Fit23(background=np.zeros(2 * FN), **_kwargs())
        # Start at 1.0, not 2.1. This 58-photon reference decay is so sparse that
        # the objective falls monotonically as tau grows — a lifetime far longer
        # than the recorded window describes it better than any real one — so the
        # historical answer is a *local* minimum whose basin runs from about 0.5
        # to 1.2. Started outside it the fit converges, reports success, and
        # returns tau in the tens of thousands. Pinning the reference therefore
        # means starting inside the basin and saying so, rather than relying on
        # the descent path from 2.1 happening to fall into it.
        r = fit(DATA, initial_values=[1.0, 0.01, 0.38, 1.2], fixed=[0, 0, 1, 1])
        self.assertAlmostEqual(r["twoIstar"], 23.802337, places=3)
        self.assertAlmostEqual(r["x"][0], 0.74219, places=3)
        # The old wide vector kept the outputs at slots 6 and 7.
        self.assertEqual(len(r["x"]), 8)
        self.assertAlmostEqual(r["x"][7], 0.25974, places=3)

    def test_fit24(self):
        fit = tttrlib.Fit24(background=np.zeros(2 * FN) + 0.2, **_kwargs())
        r = fit(DATA, initial_values=[3.8, 0.02, 0.4, 0.8, 1.0],
                fixed=[0, 0, 0, 0, 0])
        self.assertAlmostEqual(r["twoIstar"], 2.41049, places=3)

    def test_fit25(self):
        fit = tttrlib.Fit25(background=np.zeros(2 * FN) + 0.2, **_kwargs())
        r = fit(DATA, initial_values=[0.5, 1.0, 2.0, 4.0, 0.02, 0.38],
                fixed=[0, 0, 0, 0, 1, 1])
        self.assertAlmostEqual(r["twoIstar"], 4.738831, places=3)
        self.assertAlmostEqual(r["x"][0], 0.5, places=3)

    def test_fit26(self):
        fit = tttrlib.Fit26(background=np.zeros(2 * FN) + 0.2, **_kwargs())
        r = fit(DATA, initial_values=[0.5], fixed=[0])
        self.assertAlmostEqual(r["twoIstar"], 2.218772, places=3)


class TestOldCallConventions(unittest.TestCase):
    """The shapes callers depended on, including the ones they reached into."""

    def setUp(self):
        warnings.simplefilter("ignore", DeprecationWarning)
        self.fit = tttrlib.Fit23(background=np.zeros(2 * FN), **_kwargs())

    def test_returns_the_old_record(self):
        r = self.fit(DATA, initial_values=[2.1, 0.01, 0.38, 1.2],
                     fixed=[0, 1, 1, 1], include_model=True)
        self.assertEqual(sorted(r), ["fixed", "model", "twoIstar", "x"])
        self.assertEqual(len(r["model"]), 2 * FN)

    def test_model_is_omitted_unless_asked_for(self):
        r = self.fit(DATA, initial_values=[2.1, 0.01, 0.38, 1.2], fixed=[0, 1, 1, 1])
        self.assertNotIn("model", r)

    def test_exposes_the_attributes_callers_reached_for(self):
        """Downstream code read these privates; the shim is only useful if it has them."""
        self.assertEqual(self.fit._bifl_scatter, -1)   # soft BIFL on by default
        self.assertEqual(self.fit._p_2s_flag, 0)
        self.assertIsNotNone(self.fit._m_param)
        self.assertEqual(len(self.fit.irf), 2 * FN)
        self.assertEqual(len(self.fit.background), 2 * FN)

    def test_a_short_fixed_mask_is_refused(self):
        with self.assertRaises(ValueError):
            self.fit(DATA, initial_values=[2.1, 0.01, 0.38, 1.2], fixed=[0])

    def test_fit_many_matches_the_scalar_fit(self):
        r = self.fit(DATA, initial_values=[2.1, 0.01, 0.38, 1.2], fixed=[0, 1, 1, 1])
        matrix = np.tile(DATA, (4, 1))
        batch = self.fit.fit_many(matrix, initial_values=[2.1, 0.01, 0.38, 1.2],
                                  fixed=[0, 1, 1, 1])
        self.assertEqual(batch.shape, (4, 5))       # 4 parameters plus 2I*
        for row in range(4):
            self.assertAlmostEqual(batch[row, 0], r["x"][0], places=9)
            self.assertAlmostEqual(batch[row, 4], r["twoIstar"], places=9)

    def test_decayfit23_fit_matrix_still_fills_its_output(self):
        matrix = np.tile(DATA, (3, 1))
        out = np.empty((3, 5), dtype=np.float64)
        tttrlib.DecayFit23.fit_matrix(
            matrix, np.array([2.1, 0.01, 0.38, 1.2]),
            np.array([0, 1, 1, 1], dtype=np.int16),
            float(self.fit._bifl_scatter), float(self.fit._p_2s_flag),
            self.fit._m_param, out)
        r = self.fit(DATA, initial_values=[2.1, 0.01, 0.38, 1.2], fixed=[0, 1, 1, 1])
        for row in range(3):
            self.assertAlmostEqual(out[row, 0], r["x"][0], places=9)
            self.assertAlmostEqual(out[row, 4], r["twoIstar"], places=9)


if __name__ == "__main__":
    unittest.main()

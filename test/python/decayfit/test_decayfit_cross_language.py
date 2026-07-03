"""Canonical cross-language DecayFit23 reference.

`DecayFit23.modelf` takes plain numeric arrays (param, irf, bg, corrections, and
an output model array), so it is callable identically from Python, R and Java —
no per-language helper is needed. The SAME assertions are checked in
`test/r/test_decayfit.R` and `test/java/DecayFitTest.java`.

The IRF here is built deterministically (a Gaussian at channel 4 in each of the
p/s halves) so every language constructs the exact same input without a helper.
"""
from __future__ import annotations

import math
import unittest

import numpy as np

import tttrlib

N = 16
# out[4] and out[20] of the model function for the fixed inputs below.
REF_OUT_4 = 0.092688
REF_OUT_20 = 0.043233
REF_SUM = 0.99

# full fit loop references (fit23 with bg=0; fit24/25/26 with bg=0.2)
FN = 32
REF_TWO_ISTAR = 23.802337
REF_FIT_TAU = 0.74219
REF_FIT_RS = 0.25974
REF_FIT24_TI = 2.41049
REF_FIT25_TI = 4.738831
REF_FIT25_BEST_TAU = 0.5
REF_FIT26_TI = 2.218772
DATA = [
    0, 0, 0, 1, 9, 7, 5, 5, 5, 2, 2, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 2, 2, 2, 2, 3, 0, 1, 0,
    1, 1, 1, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
]


def _fit_irf():
    irf = np.zeros(2 * FN)
    for half in (0, FN):
        for i in range(FN):
            irf[half + i] = math.exp(-((i - 8.0) ** 2) / (2 * 0.5 * 0.5))
    return irf


def _fit_data(bg_level=0.0):
    bg = np.zeros(2 * FN) + bg_level
    corrections = np.array([2.0 * FN, 1.0, 0.1, 0.1, FN // 2 - 1])
    return tttrlib.DecayFitData(
        irf=_fit_irf(), background=bg, corrections=corrections, dt=0.5, data=DATA)


def _fixed_inputs():
    irf = np.zeros(2 * N)
    for half in (0, N):
        for i in range(N):
            irf[half + i] = math.exp(-((i - 4.0) ** 2) / (2 * 0.5 * 0.5))
    bg = np.zeros(2 * N)
    param = np.array([2.0, 0.01, 0.38, 1.2])            # tau, gamma, r0, rho
    corrections = np.array([2.0 * N, 1.0, 0.1, 0.1, N - 1])  # period, g, l1, l2, conv_stop
    return param, irf, bg, corrections


class DecayFit23CrossLanguageReference(unittest.TestCase):
    def test_modelf(self):
        param, irf, bg, corrections = _fixed_inputs()
        m = np.zeros(2 * N)
        tttrlib.DecayFit23.modelf(param, irf, bg, 0.5, corrections, m)
        self.assertAlmostEqual(float(m[4]), REF_OUT_4, places=5)
        self.assertAlmostEqual(float(m[20]), REF_OUT_20, places=5)
        self.assertAlmostEqual(float(m.sum()), REF_SUM, places=5)

    def test_fit(self):
        irf = _fit_irf()
        bg = np.zeros(2 * FN)
        corrections = np.array([2.0 * FN, 1.0, 0.1, 0.1, FN // 2 - 1])
        fit_data = tttrlib.DecayFitData(
            irf=irf, background=bg, corrections=corrections, dt=0.5, data=DATA)
        x = np.zeros(8)
        x[:6] = [2.1, 0.01, 0.38, 1.2, -1, 0]
        fixed = np.array([0, 0, 1, 1], dtype=np.int16)
        two_istar = tttrlib.DecayFit23.fit(x, fixed, fit_data)   # x fitted in place
        self.assertAlmostEqual(two_istar, REF_TWO_ISTAR, places=3)
        self.assertAlmostEqual(float(x[0]), REF_FIT_TAU, places=3)
        self.assertAlmostEqual(float(x[6]), REF_FIT_RS, places=3)

    def test_fit24(self):
        m = _fit_data(0.2)
        x = np.zeros(8); x[:6] = [3.8, 0.02, 0.4, 0.8, 1.0, -1.0]
        ti = tttrlib.DecayFit24.fit(x, np.array([0, 0, 0, 0, 0], dtype=np.int16), m)
        self.assertAlmostEqual(ti, REF_FIT24_TI, places=3)

    def test_fit25(self):
        m = _fit_data(0.2)
        x = np.zeros(9); x[:6] = [0.5, 1.0, 2.0, 4.0, 0.02, 0.38]
        ti = tttrlib.DecayFit25.fit(x, np.array([0, 0, 0, 0, 1, 1], dtype=np.int16), m)
        self.assertAlmostEqual(ti, REF_FIT25_TI, places=3)
        self.assertAlmostEqual(float(x[0]), REF_FIT25_BEST_TAU, places=3)

    def test_fit26(self):
        m = _fit_data(0.2)
        x = np.zeros(2); x[0] = 0.5
        ti = tttrlib.DecayFit26.fit(x, np.array([0], dtype=np.int16), m)
        self.assertAlmostEqual(ti, REF_FIT26_TI, places=3)


if __name__ == "__main__":
    unittest.main()

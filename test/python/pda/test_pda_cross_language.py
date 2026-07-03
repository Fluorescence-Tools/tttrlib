"""Canonical cross-language PDA (photon distribution analysis) reference.

The SAME assertions are checked in the R (`test/r/test_pda.R`) and Java
(`test/java/PdaTest.java`) suites. The photon-number distribution `pF` is a
Poisson distribution built deterministically (no helper), so every language
constructs identical inputs.

Model: two species with amplitude 0.5 and ch1 probabilities 0.3 / 0.7; the S1S2
probability matrix and the 1-D histogram are the PDA outputs.
"""
from __future__ import annotations

import math
import unittest

import numpy as np

import tttrlib

NMAX = 30
REF_S1S2_SUM = 1.0             # S1S2 is a normalised probability matrix
REF_S1S2_MAX = 0.01800533
REF_HIST1D_SUM = 0.92940452


def _poisson_pF(lam, nmax):
    pF = [0.0] * (nmax + 1)
    pF[0] = math.exp(-lam)
    for i in range(1, nmax + 1):
        pF[i] = pF[i - 1] * lam / i
    s = sum(pF)
    return [v / s for v in pF]


def _make_pda():
    pda = tttrlib.Pda(NMAX, 5, 0.0, 0.0, _poisson_pF(10.0, NMAX), 0)
    pda.append(0.5, 0.3)
    pda.append(0.5, 0.7)
    pda.evaluate()
    return pda


class PdaCrossLanguageReference(unittest.TestCase):
    def test_s1s2_matrix(self):
        s = np.asarray(_make_pda().get_S1S2_matrix())
        self.assertEqual(s.shape, (NMAX + 1, NMAX + 1))
        self.assertAlmostEqual(float(s.sum()), REF_S1S2_SUM, places=5)
        self.assertAlmostEqual(float(s.max()), REF_S1S2_MAX, places=6)

    def test_1d_histogram(self):
        _, hy = _make_pda().get_1dhistogram(x_max=1000.0, x_min=0.01, n_bins=81, log_x=True)
        self.assertAlmostEqual(float(np.asarray(hy).sum()), REF_HIST1D_SUM, places=5)


if __name__ == "__main__":
    unittest.main()

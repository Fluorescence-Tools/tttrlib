#!/usr/bin/env python3
"""`DecayPhasor` argument guards.

The phasor correction divides by ``g_irf**2 + s_irf**2``. Passing ``(0, 0)``
for "no IRF" is therefore a division by zero, and it used to return a silent
``nan`` that propagates through everything downstream. These tests pin the two
halves of the contract:

* **invalid arguments raise** ``ValueError`` (``std::invalid_argument``), because
  they are programmer error;
* **too few photons still returns the sentinel** ``(-1, -1)``, because that is a
  property of the data and always has been.

Keeping both in one file matters: a guard that also swallowed the sparse-data
case would look like it worked while breaking every legitimate caller.
"""
import unittest

import numpy as np

import tttrlib

FREQ = 1.0 / 64
HIST = [100, 50, 20, 5]


def _phasor(hist, frequency=FREQ, minimum=1, g_irf=1.0, s_irf=0.0):
    return tttrlib.DecayPhasor.compute_phasor_bincounts(
        tttrlib.VectorInt32(list(hist)), frequency, minimum, g_irf, s_irf)


class TestDegenerateIrfIsRejected(unittest.TestCase):
    """The trap this file exists for."""

    def test_zero_irf_phasor_raises_instead_of_returning_nan(self):
        with self.assertRaises(ValueError) as cm:
            _phasor(HIST, g_irf=0.0, s_irf=0.0)
        # The message has to say what to do instead, or it just moves the
        # confusion from a nan to an exception.
        self.assertIn("(1, 0)", str(cm.exception))

    def test_the_scalar_helpers_are_guarded_too(self):
        """`g` and `s` are public, so guarding only the callers is not enough."""
        for fn in (tttrlib.DecayPhasor.g, tttrlib.DecayPhasor.s):
            with self.assertRaises(ValueError):
                fn(0.0, 0.0, 0.3, 0.2)

    def test_non_finite_irf_raises(self):
        for bad in (float("nan"), float("inf")):
            with self.assertRaises(ValueError):
                _phasor(HIST, g_irf=bad, s_irf=0.0)
            with self.assertRaises(ValueError):
                _phasor(HIST, g_irf=1.0, s_irf=bad)

    def test_an_irf_too_small_to_invert_raises(self):
        """Not exactly zero, but small enough that 1/|z|**2 overflows."""
        with self.assertRaises(ValueError):
            _phasor(HIST, g_irf=1e-200, s_irf=0.0)


class TestFrequencyIsValidated(unittest.TestCase):
    def test_non_positive_or_non_finite_frequency_raises(self):
        for bad in (0.0, -1.0, float("nan"), float("inf")):
            with self.assertRaises(ValueError):
                _phasor(HIST, frequency=bad)


class TestTheSentinelSurvives(unittest.TestCase):
    """The half that must NOT have changed."""

    def test_too_few_photons_returns_minus_one(self):
        self.assertEqual(list(_phasor([1], minimum=100)), [-1.0, -1.0])

    def test_empty_histogram_returns_minus_one(self):
        self.assertEqual(list(_phasor([])), [-1.0, -1.0])

    def test_a_negative_minimum_cannot_force_an_answer_from_no_data(self):
        """`sum > minimum` alone would pass here and normalise nothing."""
        self.assertEqual(list(_phasor([], minimum=-5)), [-1.0, -1.0])
        self.assertEqual(list(_phasor([0, 0, 0], minimum=-5)), [-1.0, -1.0])

    def test_background_subtracted_bins_are_still_allowed(self):
        """Negative counts are legitimate after background subtraction, so the
        guard must not reject them -- only a non-positive total."""
        g, s = _phasor([100, 50, -3, 20])
        self.assertTrue(np.isfinite(g) and np.isfinite(s))


class TestValidInputIsUnchanged(unittest.TestCase):
    """Hardening must not move any number a correct caller already got."""

    def test_identity_irf_matches_the_raw_moments(self):
        counts = np.asarray(HIST, float)
        mt = np.arange(len(counts))
        factor = 2.0 * np.pi * FREQ
        expect_g = (counts * np.cos(mt * factor)).sum() / counts.sum()
        expect_s = (counts * np.sin(mt * factor)).sum() / counts.sum()
        g, s = _phasor(HIST, g_irf=1.0, s_irf=0.0)
        self.assertAlmostEqual(g, expect_g, places=12)
        self.assertAlmostEqual(s, expect_s, places=12)

    def test_defaults_are_the_identity_irf(self):
        """The new default arguments have to BE the identity, not merely exist."""
        explicit = list(_phasor(HIST, FREQ, 1, 1.0, 0.0))
        implied = list(tttrlib.DecayPhasor.compute_phasor_bincounts(
            tttrlib.VectorInt32(HIST), FREQ))
        self.assertEqual(explicit, implied)

    def test_a_real_irf_phasor_still_rotates(self):
        """A unit-modulus IRF phasor is a pure rotation, which is the whole point."""
        theta = 0.37
        g, s = _phasor(HIST, g_irf=np.cos(theta), s_irf=np.sin(theta))
        g0, s0 = _phasor(HIST)
        self.assertAlmostEqual(np.hypot(g, s), np.hypot(g0, s0), places=12)


if __name__ == "__main__":
    unittest.main()

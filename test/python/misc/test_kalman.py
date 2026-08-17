"""Kalman filter recursion over a count-rate trace (`Kalman.h`).

The kernel is a bit-exact port of the reference Python implementation ChiSurf
runs (``core/fluorescence/burst/kalman.py``, ``_kalman_filter_loop`` plus the
``_inv2x2`` helper it inlines): same trace in, same filtered states,
covariances and Mahalanobis distances out, digit for digit. The Mahalanobis
distance is what ChiSurf's fcs plugin thresholds to build its burst table, so
a one-ulp drift anywhere in the recursion is a different burst. The fixture
below was recorded from that implementation; the rest here is a known-answer
simulation or a structural property, and nothing imports the implementation
being replaced.
"""

import os
import unittest

import numpy as np

import tttrlib


def two_channel_trace(seed=3, n_bins=1200, dt=1e-3):
    """A donor/acceptor FRET trace: rates near constant except a step."""
    rng = np.random.default_rng(seed)
    donor = np.where(np.arange(n_bins) < n_bins // 2, 8e4, 9e4)
    acceptor = np.where(np.arange(n_bins) < n_bins // 2, 5e3, 1.2e5)
    count = lambda rate: rng.poisson(rate * dt).astype(np.float64) / dt
    return np.column_stack([count(donor), count(acceptor)]), donor, acceptor


class TestKnownAnswerSimulation(unittest.TestCase):
    """A constant-rate trace has no innovation, so the filter must leave the
    state alone and the Mahalanobis distance must stay near zero. A step in
    the rate shows up as a spike in D at exactly the step bin."""

    def test_no_step_no_innovation(self):
        y = np.full((50, 2), 1e4, dtype=np.float64)
        x0 = np.array([1e4, 1e4])
        P0 = np.eye(2) * 1e4
        Q = np.eye(2) * 1.0
        xf, _P, D = tttrlib.kalman_filter(y, x0, P0, Q, 1e-3, 1.0)
        # The Poisson floor keeps D from vanishing exactly, but it must stay
        # tiny -- not the tens-of-thousands a burst would produce.
        self.assertLess(float(D.max()), 10.0)
        self.assertLess(np.abs(xf - y).max(), 100.0)

    def test_step_shows_as_innovation_spike(self):
        y, donor, acceptor = two_channel_trace()
        x0 = np.array([8e4, 1e4])
        P0 = np.eye(2) * 1e6
        Q = np.eye(2) * 200.0
        xf, _P, D = tttrlib.kalman_filter(y, x0, P0, Q, 1e-3, 1.0)
        step = y.shape[0] // 2
        # The Mahalanobis distance peaks within a few bins of the step and is
        # well above the steady-state floor just before it. The floor after
        # the step is higher (the acceptor jumps to a higher Poisson rate), so
        # the spike is compared against the quiet side only.
        self.assertAlmostEqual(int(np.argmax(D)), step, delta=8)
        pre = D[step - 30:step - 5]
        self.assertGreater(float(D.max()), 10.0 * float(pre.max()))


class TestShapeAndType(unittest.TestCase):

    def test_output_shapes(self):
        y = np.full((30, 2), 1e4, dtype=np.float64)
        xf, P, D = tttrlib.kalman_filter(y, np.array([1e4, 1e4]),
                                         np.eye(2), np.eye(2), 1e-3, 1.0)
        self.assertEqual(xf.shape, (30, 2))
        self.assertEqual(P.shape, (30, 2, 2))
        self.assertEqual(D.shape, (30,))
        self.assertTrue(np.isfinite(xf).all())
        self.assertTrue(np.isfinite(P).all())

    def test_zero_bins_returns_empty(self):
        y = np.empty((0, 2))
        xf, P, D = tttrlib.kalman_filter(y, np.array([1e4, 1e4]),
                                         np.eye(2), np.eye(2), 1e-3, 1.0)
        self.assertEqual(xf.shape, (0, 2))
        self.assertEqual(P.shape, (0, 2, 2))
        self.assertEqual(D.shape, (0,))

    def test_bad_arguments_are_rejected(self):
        y = np.full((5, 2), 1e4, dtype=np.float64)
        with self.assertRaises(ValueError):
            tttrlib.kalman_filter(y, np.array([1e4, 1e4]),
                                  np.eye(2), np.eye(2), 0.0, 1.0)
        with self.assertRaises(ValueError):
            tttrlib.kalman_filter(y, np.array([1e4, 1e4]),
                                  np.eye(2), np.eye(2), -1e-3, 1.0)


class TestAgainstTheRecordedReference(unittest.TestCase):
    """The committed fixture, recorded from the reference implementation: the
    bit-exactness pin. One ulp anywhere in the recursion fails this."""

    PATH = os.path.join(os.path.dirname(__file__), "..", "..", "data",
                        "reference", "kalman_chisurf_reference.npz")

    @classmethod
    def setUpClass(cls):
        with np.load(cls.PATH) as z:
            cls.y = z["y"]
            cls.x0 = z["x0"]
            cls.P0 = z["P0"]
            cls.Q = z["Q"]
            cls.dt = float(z["dt"])
            cls.r_scale = float(z["r_scale"])
            cls.ref = (z["x_filt"], z["P_filt"], z["D"])

    def test_bit_identical_filtered_states(self):
        xf, _P, _D = tttrlib.kalman_filter(self.y, self.x0, self.P0, self.Q,
                                           self.dt, self.r_scale)
        np.testing.assert_array_equal(xf, self.ref[0])

    def test_bit_identical_covariances(self):
        _xf, P, _D = tttrlib.kalman_filter(self.y, self.x0, self.P0, self.Q,
                                           self.dt, self.r_scale)
        np.testing.assert_array_equal(P, self.ref[1])

    def test_bit_identical_mahalanobis_distances(self):
        _xf, _P, D = tttrlib.kalman_filter(self.y, self.x0, self.P0, self.Q,
                                           self.dt, self.r_scale)
        np.testing.assert_array_equal(D, self.ref[2])


if __name__ == "__main__":
    unittest.main()
import unittest
import numpy as np
import tttrlib


class TestBlindIRF(unittest.TestCase):

    def test_basic_single_channel(self):
        np.random.seed(42)
        n, dt = 256, 0.1
        t = np.arange(n) * dt
        data = np.exp(-t / 4.0) + 0.01
        irf = np.asarray(tttrlib.blind_irf_estimate(
            data.tolist(), n, 1, dt, 50, 3))
        self.assertEqual(irf.shape, (n,))
        self.assertTrue(np.all(irf >= 0))

    def test_multi_channel(self):
        np.random.seed(42)
        n, nc, dt = 128, 3, 0.1
        t = np.arange(n) * dt
        data = np.zeros(n * nc)
        for c in range(nc):
            data[c::nc] = np.exp(-t / (3.0 + c)) + 0.01
        irf = np.asarray(tttrlib.blind_irf_estimate(
            data.tolist(), n, nc, dt, 30, 3))
        self.assertEqual(irf.shape, (n * nc,))

    def test_irf_nonnegative(self):
        np.random.seed(42)
        n, dt = 64, 0.1
        t = np.arange(n) * dt
        data = np.exp(-t / 2.0) + 0.01
        irf = np.asarray(tttrlib.blind_irf_estimate(
            data.tolist(), n, 1, dt, 20, 1))
        self.assertTrue(np.all(irf >= 0))


if __name__ == '__main__':
    unittest.main()

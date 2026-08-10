import unittest
import numpy as np
import tttrlib

try:
    import sys
    sys.path.insert(0, '/Users/tpeulen/dev/chisurf')
    from chisurf.plugins.fluorescence_decay.maxent_decay.core.solver import (
        solve_lifetime_mem, _build_Fi_lifetimes, _run_mem, _quadpr_bound,
    )
    _HAVE_REF = True
except Exception:
    _HAVE_REF = False


def _synth_decay(seed=1):
    np.random.seed(seed)
    n, dt = 256, 0.05
    t = np.arange(n) * dt
    irf = np.exp(-0.5 * ((t - 1.0) / 0.15) ** 2)
    irf /= irf.sum()
    from numpy.fft import fft, ifft
    dec = 0.6 * np.exp(-t / 2.0) + 0.4 * np.exp(-t / 3.5)
    dec /= dec.sum()
    conv = np.real(ifft(fft(np.concatenate([dec, [0] * 100])) *
                        fft(np.concatenate([irf, [0] * 100]))))[:n]
    conv /= conv.sum()
    decay = conv * 20000 + 5.0
    return np.maximum(decay, 0), irf, dt


class TestTcspcShiftLamp(unittest.TestCase):

    def test_shift(self):
        if not _HAVE_REF:
            self.skipTest("ChiSurf reference not available")
        from chisurf.plugins.fluorescence_decay.maxent_decay.core.solver import _shift_lamp
        lamp = np.zeros(20)
        lamp[3] = 1.0
        py = _shift_lamp(lamp, 0.5)
        cpp = np.asarray(tttrlib.tcspc_shift_lamp(lamp.tolist(), 0.5))
        self.assertTrue(np.allclose(py, cpp))


class TestTcspcConvolution(unittest.TestCase):

    def test_fconv_single(self):
        if not _HAVE_REF:
            self.skipTest("ChiSurf reference not available")
        from chisurf.plugins.fluorescence_decay.maxent_decay.core.solver import (
            _fconv_single_shot, _shift_lamp)
        n = 20
        lamp = np.zeros(n)
        lamp[3] = 1.0
        lampsh = _shift_lamp(lamp, 0.5)
        py = _fconv_single_shot(lampsh, 0.1, np.array([1.0]), np.array([2.0]), n - 1)
        cpp = np.asarray(tttrlib.tcspc_fconv_single_shot(
            lampsh.tolist(), 0.1, [1.0], [2.0], n - 1))
        self.assertTrue(np.allclose(py, cpp))


class TestTcspcQuadpr(unittest.TestCase):

    def test_quadpr_matches(self):
        if not _HAVE_REF:
            self.skipTest("ChiSurf reference not available")
        np.random.seed(3)
        n = 30
        A = np.random.randn(n, n)
        C = A @ A.T + 5 * np.eye(n)
        d = np.random.randn(n)
        lb = 1e-4
        py = _quadpr_bound(C, d, lb)
        cpp = np.asarray(tttrlib.tcspc_quadpr_bound(
            C.flatten().tolist(), d.tolist(), lb))
        self.assertTrue(np.allclose(py, cpp, atol=1e-10))


class TestTcspcMemLifetime(unittest.TestCase):

    def test_matches_ref(self):
        if not _HAVE_REF:
            self.skipTest("ChiSurf reference not available")
        decay, irf, dt = _synth_decay()
        tau_grid = np.arange(1.0, 4.01, 0.01)
        res_py = solve_lifetime_mem(
            decay, irf, dt, tau=tau_grid, timeshift=0.0, nu=1e-3, max_iter=100)
        p_py = np.asarray(res_py['p'])
        fs, fe = res_py['fitrange']

        res_cpp = tttrlib.solve_tcspc_mem_lifetime(
            decay.tolist(), irf.tolist(), dt, tau_grid.tolist(),
            0.0, 0.0, 0.0, fs, fe, 0.0, nu=1e-3, max_iter=100)
        p_cpp = np.asarray(res_cpp.p)

        self.assertEqual(res_py['niter'], res_cpp.niter)
        self.assertAlmostEqual(res_py['chisq'], res_cpp.chisq, places=4)
        self.assertAlmostEqual(res_py['Q'], res_cpp.Q, places=4)
        self.assertTrue(np.corrcoef(p_py, p_cpp)[0, 1] > 0.999)
        self.assertLess(np.max(np.abs(p_py - p_cpp)), 1e-8)


class TestTcspcMemFret(unittest.TestCase):
    """The distance-axis sibling: p(R_DA) instead of a lifetime distribution."""

    def test_matches_ref(self):
        if not _HAVE_REF:
            self.skipTest("ChiSurf reference not available")
        from chisurf.plugins.fluorescence_decay.maxent_decay.core.solver import (
            solve_fret_mem)
        decay, irf, dt = _synth_decay()
        R = np.arange(30.0, 70.01, 0.5)
        donly = [1.0, 4.0]
        res_py = solve_fret_mem(
            decay, irf, dt, R=R, tau0=4.0, R0=50.0, donly=donly,
            x_donly=0.1, timeshift=0.0, irf_background=0.0,
            nu=1e-3, max_iter=100)
        p_py = np.asarray(res_py['p'])
        fs, fe = res_py['fitrange']

        res_cpp = tttrlib.solve_tcspc_mem_fret(
            decay.tolist(), irf.tolist(), dt, R.tolist(), 4.0, 50.0,
            donly, 0.1, 0.0, 0.0, 0.0, fs, fe, 0.0,
            irf_background=0.0, nu=1e-3, max_iter=100)
        p_cpp = np.asarray(res_cpp.p)

        self.assertEqual(res_py['niter'], res_cpp.niter)
        self.assertAlmostEqual(res_py['chisq'], res_cpp.chisq, places=4)
        self.assertAlmostEqual(res_py['Q'], res_cpp.Q, places=4)
        self.assertTrue(np.corrcoef(p_py, p_cpp)[0, 1] > 0.999)
        self.assertLess(np.max(np.abs(p_py - p_cpp)), 1e-8)

    def test_recovers_a_known_distance(self):
        """No reference needed: a decay simulated at one distance must come
        back as a distribution concentrated at that distance."""
        np.random.seed(7)
        n, dt = 512, 0.05
        t = np.arange(n) * dt
        irf = np.exp(-0.5 * ((t - 1.0) / 0.15) ** 2)
        irf /= irf.sum()

        tau0, R0, R_true, x_d = 4.0, 50.0, 45.0, 0.1
        kfret = (1.0 / tau0) * (R0 / R_true) ** 6
        tau_da = 1.0 / (1.0 / tau0 + kfret)
        fret = np.asarray(tttrlib.tcspc_fconv_single_shot(
            irf.tolist(), dt, [1.0], [tau_da], n - 1))
        donor = np.asarray(tttrlib.tcspc_fconv_single_shot(
            irf.tolist(), dt, [1.0], [tau0], n - 1))
        model = (1.0 - x_d) * fret + x_d * donor
        decay = np.random.poisson(model / model.sum() * 200000).astype(float)

        # nu must be small against this count scale: entropy at 1e-3 already
        # pulls the answer toward the flat prior (the reference does the same),
        # and the point here is the design matrix, not the regularisation.
        R = np.arange(30.0, 70.01, 0.5)
        res = tttrlib.solve_tcspc_mem_fret(
            decay.tolist(), irf.tolist(), dt, R.tolist(), tau0, R0,
            [1.0, tau0], x_d, 0.0, 0.0, 0.0, 30, n - 1, 0.0,
            irf_background=0.0, nu=1e-6, max_iter=500)
        self.assertTrue(res.success)
        self.assertLess(res.chisq, 2.0)

        p = np.asarray(res.p)
        self.assertGreater(p.sum(), 0.0)
        r_mean = float((p * R).sum() / p.sum())
        self.assertAlmostEqual(r_mean, R_true, delta=2.0)
        # and concentrated, not smeared over the grid
        near = (np.abs(R - R_true) <= 5.0)
        self.assertGreater(p[near].sum() / p.sum(), 0.8)


if __name__ == '__main__':
    unittest.main()

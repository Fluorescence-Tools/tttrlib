"""Maximum-entropy TCSPC: the engine, checked against independent references.

This suite used to import ChiSurf from an absolute path and assert that the two
implementations agreed. That reference has since been deleted -- ChiSurf's
maximum-entropy plugin now calls *these* functions -- so the comparison would
have become a skip that reads like a pass. Every check here is either analytic,
a brute-force sum written independently of the kernel it checks, or a recovery
test on data whose answer is known by construction.
"""
import unittest

import numpy as np
import tttrlib


def _synth_decay(seed=1):
    """A two-exponential decay convolved with a narrow Gaussian IRF."""
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


def _fconv_reference(lampsh, dt, amps, taus, stop):
    """The recursion of ``tcspc_fconv_single_shot``, written out in Python.

    Deliberately a plain loop rather than a vectorised rewrite: it is the
    statement of what the kernel is supposed to compute, and the point of it is
    to be obviously right, not fast.
    """
    n = len(lampsh)
    stop = max(1, min(int(stop), n - 1))
    fit = np.zeros(n)
    half = 0.5 * dt
    for amp, tau in zip(amps, taus):
        if tau <= 0.0 or amp == 0.0:
            continue
        e = np.exp(-dt / tau)
        cur = 0.0
        for i in range(1, stop + 1):
            cur = (cur + half * lampsh[i - 1]) * e + half * lampsh[i]
            fit[i] += cur * amp
    return fit


class TestTcspcShiftLamp(unittest.TestCase):
    """A fractional shift is a linear interpolation, and it does not wrap."""

    def test_half_channel_shift_splits_a_delta(self):
        lamp = np.zeros(20)
        lamp[3] = 1.0
        # ts = 0.5 reads lamp[j] and lamp[j+1] with weights 0.5/0.5, so the
        # delta at 3 appears at channels 2 and 3.
        got = np.asarray(tttrlib.tcspc_shift_lamp(lamp.tolist(), 0.5))
        expected = np.zeros(20)
        expected[2] = 0.5
        expected[3] = 0.5
        np.testing.assert_allclose(got, expected, atol=0.0)

    def test_integer_shift_moves_without_interpolating(self):
        lamp = np.arange(10.0)
        got = np.asarray(tttrlib.tcspc_shift_lamp(lamp.tolist(), 2.0))
        # Channel j takes lamp[j + 2]. The tail is *three* channels wide, not
        # two: the interpolation always reads lamp[idx + 1], so the last
        # channel it can fill is the one whose right-hand neighbour exists.
        expected = np.array([2.0, 3, 4, 5, 6, 7, 8, 0, 0, 0])
        np.testing.assert_allclose(got, expected, atol=0.0)

    def test_a_shift_past_the_array_is_all_zero(self):
        lamp = np.arange(1.0, 11.0)
        for ts in (100.0, -100.0):
            got = np.asarray(tttrlib.tcspc_shift_lamp(lamp.tolist(), ts))
            self.assertEqual(np.count_nonzero(got), 0, f"ts={ts}")


class TestTcspcConvolution(unittest.TestCase):
    """The convolution against an independently written reference."""

    def test_single_shot_matches_the_reference_recursion(self):
        n = 64
        t = np.arange(n, dtype=float)
        lamp = np.exp(-0.5 * ((t - 8.0) / 1.5) ** 2)
        lampsh = np.asarray(tttrlib.tcspc_shift_lamp(lamp.tolist(), 0.5))
        for amps, taus, stop in (
            ([1.0], [2.0], n - 1),
            ([0.7, 0.3], [0.5, 4.0], n - 1),
            ([1.0, 0.0], [2.0, 3.0], 40),   # a zero amplitude contributes nothing
            ([1.0, 0.5], [2.0, -1.0], 40),  # nor does a non-positive lifetime
        ):
            got = np.asarray(tttrlib.tcspc_fconv_single_shot(
                lampsh.tolist(), 0.1, amps, taus, stop))
            ref = _fconv_reference(lampsh, 0.1, amps, taus, stop)
            np.testing.assert_allclose(got, ref, rtol=1e-13, atol=1e-300)

    def test_a_long_period_reduces_to_the_single_shot(self):
        """The periodic form must not change the answer when pulses never overlap."""
        n = 64
        t = np.arange(n, dtype=float)
        lamp = np.exp(-0.5 * ((t - 8.0) / 1.5) ** 2)
        lampsh = np.asarray(tttrlib.tcspc_shift_lamp(lamp.tolist(), 0.0))
        single = np.asarray(tttrlib.tcspc_fconv_single_shot(
            lampsh.tolist(), 0.1, [1.0], [0.5], n - 1))
        periodic = np.asarray(tttrlib.tcspc_fconv_periodic(
            lampsh.tolist(), 0.1, [1.0], [0.5], 10, n - 1, 1e6))
        np.testing.assert_allclose(periodic, single, rtol=1e-9, atol=1e-12)

    def test_a_non_positive_period_is_the_single_shot(self):
        n = 32
        lampsh = np.zeros(n)
        lampsh[4] = 1.0
        single = np.asarray(tttrlib.tcspc_fconv_single_shot(
            lampsh.tolist(), 0.1, [1.0], [2.0], n - 1))
        for period in (0.0, -1.0):
            got = np.asarray(tttrlib.tcspc_fconv_periodic(
                lampsh.tolist(), 0.1, [1.0], [2.0], 5, n - 1, period))
            np.testing.assert_allclose(got, single, atol=0.0)


class TestTcspcQuadpr(unittest.TestCase):
    """The bounded QP is checked by what it guarantees, not against a twin."""

    def test_the_free_block_is_solved_and_the_bound_holds(self):
        """What the active-set sweep guarantees, stated exactly.

        It clamps a violating variable to the bound and re-solves the *free*
        block alone -- ``C_ff x_f = -d_f`` -- so full stationarity
        ``(C x + d)_free = 0`` does **not** hold: the clamped variables' coupling
        ``C_fa lb`` is left out. That is an approximation, not a defect, and it
        is invisible in use because the bound is ``min_prob = 1e-12``; asserting
        the stronger property here would be asserting something the algorithm
        never claimed. Checked with a bound large enough that some variable is
        actually active, so the free/active split is exercised.

        The sweep also never *releases* a clamped variable and never checks the
        sign of its multiplier, so dual feasibility does not hold either and the
        returned point is not certified optimal. That is tolerable because the
        MEM iteration re-solves this program every step with an updated
        diagonal -- but it is why this asserts feasibility and the free-block
        solve rather than the KKT conditions.
        """
        np.random.seed(3)
        n = 30
        A = np.random.randn(n, n)
        C = A @ A.T + 5 * np.eye(n)
        d = np.random.randn(n)
        lb = 1e-4

        x = np.asarray(tttrlib.tcspc_quadpr_bound(
            C.flatten().tolist(), d.tolist(), lb))

        self.assertTrue(np.all(x >= lb - 1e-12), "the bound must hold")
        free = x > lb + 1e-9
        self.assertTrue(np.any(~free), "the bound must bind somewhere")
        resid = C[np.ix_(free, free)] @ x[free] + d[free]
        np.testing.assert_allclose(resid, 0.0, atol=1e-10)

    def test_an_unconstrained_optimum_inside_the_bound_is_returned_exactly(self):
        C = np.array([[4.0, 1.0], [1.0, 3.0]])
        d = np.array([-1.0, -2.0])
        x_star = np.linalg.solve(C, -d)   # both components positive
        got = np.asarray(tttrlib.tcspc_quadpr_bound(
            C.flatten().tolist(), d.tolist(), -10.0))
        np.testing.assert_allclose(got, x_star, rtol=1e-12)


class TestTcspcDesignMatrices(unittest.TestCase):
    """The builders that ChiSurf's plugin calls, checked column by column.

    These are the NumPy bindings: the C++ returns the four arrays through
    reference parameters, which SWIG turned into unsuppliable inputs until
    ``MaxEntTcspc.i`` wrapped them. A test that only called the solver would
    not have noticed the builders were unreachable.
    """

    def test_lifetime_columns_are_the_convolution_over_the_weight(self):
        decay, irf, dt = _synth_decay()
        tau = np.arange(1.0, 4.01, 0.25)
        fitstart, fitstop = 20, 200

        Fi, y, sigma, add = tttrlib.tcspc_build_fi_lifetimes(
            decay, irf, dt, tau, 0.5, 3.0, 0.01, fitstart, fitstop, 0.0)

        M = fitstop - fitstart + 1
        self.assertEqual(Fi.shape, (M, tau.size))
        np.testing.assert_allclose(y, decay[fitstart:fitstop + 1], atol=0.0)
        np.testing.assert_allclose(
            sigma, np.sqrt(y) + (y == 0.0), rtol=1e-15)

        lampsh = np.asarray(tttrlib.tcspc_shift_lamp(irf.tolist(), 0.5))
        np.testing.assert_allclose(
            add, 3.0 + 0.01 * lampsh[fitstart:fitstop + 1], rtol=1e-13)
        for j, tv in enumerate(tau):
            col = np.asarray(tttrlib.tcspc_fconv_single_shot(
                lampsh.tolist(), dt, [1.0], [float(tv)], fitstop))
            np.testing.assert_allclose(
                Fi[:, j], col[fitstart:fitstop + 1] / sigma, rtol=1e-13)

    def test_distance_columns_mix_the_quenched_and_unquenched_donor(self):
        decay, irf, dt = _synth_decay()
        R = np.arange(30.0, 70.01, 5.0)
        tau0, R0, x_d, irf_bg = 4.0, 50.0, 0.25, 0.0
        donly = np.array([1.0, tau0])
        fitstart, fitstop = 20, 200

        Fi, y, sigma, add = tttrlib.tcspc_build_fi_distances(
            decay, irf, dt, R, tau0, R0, donly, x_d,
            0.0, 0.0, 0.0, fitstart, fitstop, 0.0, irf_bg)

        self.assertEqual(Fi.shape, (fitstop - fitstart + 1, R.size))
        lampsh = np.asarray(tttrlib.tcspc_shift_lamp(irf.tolist(), 0.0))
        donor = np.asarray(tttrlib.tcspc_fconv_single_shot(
            lampsh.tolist(), dt, [1.0], [tau0], fitstop))
        for j, Rj in enumerate(R):
            kfret = (1.0 / tau0) * (R0 / Rj) ** 6
            tau_da = 1.0 / (1.0 / tau0 + 1.0 / (1.0 / kfret))
            fret = np.asarray(tttrlib.tcspc_fconv_single_shot(
                lampsh.tolist(), dt, [1.0], [tau_da], fitstop))
            expected = ((1.0 - x_d) * fret + x_d * donor)[fitstart:fitstop + 1] / sigma
            np.testing.assert_allclose(Fi[:, j], expected, rtol=1e-13)

    def test_the_donor_only_reference_may_be_multi_exponential(self):
        """Two donor components produce a different matrix, not a crash."""
        decay, irf, dt = _synth_decay()
        R = np.arange(30.0, 70.01, 10.0)
        one = tttrlib.tcspc_build_fi_distances(
            decay, irf, dt, R, 4.0, 50.0, np.array([1.0, 4.0]), 0.1,
            0.0, 0.0, 0.0, 20, 200, 0.0, 0.0)[0]
        two = tttrlib.tcspc_build_fi_distances(
            decay, irf, dt, R, 4.0, 50.0, np.array([0.7, 4.0, 0.3, 1.2]), 0.1,
            0.0, 0.0, 0.0, 20, 200, 0.0, 0.0)[0]
        self.assertEqual(one.shape, two.shape)
        self.assertGreater(np.max(np.abs(one - two)), 0.0)

    def test_a_non_positive_distance_is_rejected(self):
        decay, irf, dt = _synth_decay()
        with self.assertRaises(ValueError):
            tttrlib.tcspc_build_fi_distances(
                decay, irf, dt, np.array([30.0, 0.0]), 4.0, 50.0,
                np.array([1.0, 4.0]), 0.1, 0.0, 0.0, 0.0, 20, 200, 0.0, 0.0)


class TestTcspcMemLifetime(unittest.TestCase):

    def test_recovers_a_known_lifetime(self):
        """A single-exponential decay comes back as a peak at its lifetime."""
        np.random.seed(11)
        n, dt = 512, 0.05
        t = np.arange(n) * dt
        irf = np.exp(-0.5 * ((t - 1.0) / 0.15) ** 2)
        irf /= irf.sum()

        tau_true = 2.5
        model = np.asarray(tttrlib.tcspc_fconv_single_shot(
            irf.tolist(), dt, [1.0], [tau_true], n - 1))
        decay = np.random.poisson(model / model.sum() * 200000).astype(float)

        tau = np.arange(1.0, 4.51, 0.05)
        res = tttrlib.solve_tcspc_mem_lifetime(
            decay.tolist(), irf.tolist(), dt, tau.tolist(),
            0.0, 0.0, 0.0, 30, n - 1, 0.0, nu=1e-6, max_iter=500)

        self.assertTrue(res.success)
        p = np.asarray(res.p)
        self.assertGreater(p.sum(), 0.0)
        self.assertAlmostEqual(float((p * tau).sum() / p.sum()), tau_true, delta=0.2)
        near = np.abs(tau - tau_true) <= 0.3
        self.assertGreater(p[near].sum() / p.sum(), 0.8)

    def test_the_solver_uses_the_same_design_matrix_as_the_builder(self):
        """The high-level solve is the builder plus the MEM iteration.

        Not a tautology: the two go through different code (``solve_`` assembles
        H and g0 itself), and a divergence here is exactly how the fast path and
        the step-by-step path would drift apart.
        """
        decay, irf, dt = _synth_decay()
        tau = np.arange(1.0, 4.01, 0.1)
        fitstart, fitstop = 20, 200

        Fi, y, sigma, add = tttrlib.tcspc_build_fi_lifetimes(
            decay, irf, dt, tau, 0.0, 0.0, 0.0, fitstart, fitstop, 0.0)
        y_w = (y - add) / sigma
        M = float(y.size)
        H = (2.0 / M) * (Fi.T @ Fi)
        g0 = (2.0 / M) * (y_w @ Fi)
        const = float(np.sum(y_w * y_w) / M)
        m = np.full(tau.size, 1.0 / tau.size)

        stepwise = tttrlib.tcspc_run_mem(
            H.flatten().tolist(), g0.tolist(), m.tolist(), const, 1e-4,
            200, 1e-4, 1e-12)
        whole = tttrlib.solve_tcspc_mem_lifetime(
            decay.tolist(), irf.tolist(), dt, tau.tolist(),
            0.0, 0.0, 0.0, fitstart, fitstop, 0.0, nu=1e-4,
            max_iter=200, tol=1e-4, min_prob=1e-12)

        self.assertEqual(stepwise.niter, whole.niter)
        self.assertAlmostEqual(stepwise.chisq, whole.chisq, places=8)
        np.testing.assert_allclose(
            np.asarray(stepwise.p), np.asarray(whole.p), rtol=1e-8, atol=1e-12)


class TestTcspcMemFret(unittest.TestCase):
    """The distance-axis sibling: p(R_DA) instead of a lifetime distribution."""

    def test_recovers_a_known_distance(self):
        """A decay simulated at one distance must come back as a distribution
        concentrated at that distance."""
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
        # pulls the answer toward the flat prior, and the point here is the
        # design matrix, not the regularisation.
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

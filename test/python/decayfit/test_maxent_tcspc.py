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
import pytest
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

    @pytest.mark.slow
    def test_target_chisq_converges_on_a_steep_fret_case(self):
        """The regression test for why the search is a joint controller.

        A Gaussian distance distribution simulated at 1e6 photons over a
        100-point grid makes the chisq(nu) transition steep, and the FIRST
        design of this search -- an outer bisection cold-starting run_mem
        once per nu probe -- failed exactly here: 500 cold MEM solves,
        ~143 s, stuck at chisq 0.98 against a target of 1.0, converged=False
        (replicated over two runs). The joint (p, nu) controller converges to
        chisq 1.0000 in 157 warm-started QP steps, ~4 s, all in C++. This
        test exists so a regression back to an outer root-find (or anything
        that stalls the controller) fails loudly instead of quietly returning
        a best-effort fit.
        """
        import json as _json
        n_bins, period = 1024, 25.6
        dt = period / n_bins
        t = np.arange(n_bins) * dt
        irf = np.exp(-0.5 * ((t - 2.0) / 0.3) ** 2)
        irf /= irf.sum()

        tau0, R0, r_mean_true, r_sd_true = 4.0, 50.0, 45.0, 4.0
        R_sim = np.linspace(28.0, 72.0, 45)
        w_sim = np.exp(-0.5 * ((R_sim - r_mean_true) / r_sd_true) ** 2)
        w_sim /= w_sim.sum()
        tau_sim = 1.0 / (1.0 / tau0 + (1.0 / tau0) * (R0 / R_sim) ** 6)

        cfg = _json.loads(tttrlib.SimEngine.default_json())
        cfg["settings"].update(
            n_ph_max=1000000, max_windows=10 ** 8,
            n_microtime_channels=n_bins, microtime_resolution=dt,
            laser_period=period, seed_diffusion=7, seed_emission=8)
        cfg["background"] = [0.0, 0.0]
        cfg["species"][0]["decay"] = {
            "lifetimes": tau_sim.tolist(), "amplitudes": w_sim.tolist(),
            "dt": dt, "n_bins": n_bins, "irf": irf.tolist()}
        eng = tttrlib.SimEngine.from_dict(cfg)
        eng.run()
        hist = np.bincount(np.asarray(eng.photons()["micro_time"]),
                           minlength=n_bins).astype(float)[:n_bins]

        R = np.arange(25.0, 75.01, 0.5)
        res = tttrlib.solve_tcspc_mem_fret(
            hist.tolist(), irf.tolist(), dt, R.tolist(), tau0, R0,
            [1.0, tau0], 0.0, 0.0, 0.0, 0.0, 5, n_bins - 1, 0.0,
            irf_background=0.0, nu=1e-5, max_iter=200, target_chisq=1.0)

        self.assertTrue(res.success)
        self.assertTrue(res.target_chisq_converged)
        self.assertAlmostEqual(res.chisq, 1.0, delta=0.01)
        self.assertGreater(res.nu_used, 0.0)

        p = np.asarray(res.p)
        p = p / p.sum()
        r_mean = float((p * R).sum())
        r_sd = float(np.sqrt((p * (R - r_mean) ** 2).sum()))
        # measured on this deterministic fixture (fixed seeds): mean 44.15,
        # sd 5.05. Spiky collapse lands near sd~1, over-regularised smearing
        # near sd~8+, so these bands catch both classic MEM failure modes.
        self.assertAlmostEqual(r_mean, r_mean_true, delta=2.0)
        self.assertAlmostEqual(r_sd, r_sd_true, delta=2.0)


# ---------------------------------------------------------------------------
# Historic MaxEnt: find nu such that chisq(nu) lands at a target.
#
# The reference implementation below is the statement of what the C++
# `run_mem_target_chisq` is supposed to compute -- same role as
# `_fconv_reference` above for the convolution kernel. It is a JOINT (p, nu)
# controller, Gull-Skilling style: one bound-QP Newton step on the amplitudes
# at the current nu, then a secant move of log(nu) in (log nu, log chisq)
# space toward the target, warm-started throughout. The first design here was
# an outer bisection that cold-started run_mem once per nu probe, and it
# FAILED on real problems -- on a 1e6-photon simulated FRET decay over a
# 100-point distance grid it burned 500 cold MEM solves (~143 s, replicated)
# stuck at chisq 0.98 against a target of 1.0, while the joint controller
# lands at chisq 1.0000 in 157 warm-started QP steps (~4 s, all in C++).
# Two measured facts the design encodes (found prototyping, worth not
# rediscovering):
#
#  * There is NO nu=0 floor precheck. run_mem at exactly nu=0 lands ABOVE the
#    truly reachable chi-square floor (measured: 1.679 vs 0.919 at nu=1e-8 on
#    the fixture below), because the unregularised QP on the near-singular
#    lifetime-grid H is ill-conditioned, while a tiny nu>0 acts as an
#    interior-point regulariser. The floor is discovered by the controller
#    driving nu down to its clamp, never asserted analytically.
#  * The nu->infinity ceiling IS analytic: p -> m, so the ceiling is the
#    quadratic form evaluated at the prior, no solve needed.
# ---------------------------------------------------------------------------

def _mem_quadratic(H, g0, const_chi2, p):
    """chisq = 1/2 p^T H p - g0^T p + const -- run_mem's own objective."""
    p = np.asarray(p, dtype=float)
    n = p.size
    return 0.5 * p @ np.asarray(H, float).reshape(n, n) @ p \
        - np.asarray(g0, float) @ p + const_chi2


def _log_clip(v):
    return np.where(v > 1e-300, np.log(np.maximum(v, 1e-300)), -1e300)


def _mem_dgrad(H, g0, p, m, min_prob):
    """The Skilling-Bryan TEST quantity, as mem_dgrad in MaxEntQp.cpp computes
    it: 0.5 * the norm of the difference of the normalised chi^2 / entropy
    gradients, over coordinates not clamped to the floor."""
    Hm = np.asarray(H, float).reshape(p.size, p.size)
    p = np.asarray(p, float)
    grad_chi2 = Hm @ p - np.asarray(g0, float)
    grad_S = -_log_clip(p / np.asarray(m, float))
    mask = p > -1.1 * min_prob
    gc = np.where(mask, grad_chi2, 0.0)
    gs = np.where(mask, grad_S, 0.0)
    nc, ns = np.linalg.norm(gc), np.linalg.norm(gs)
    if nc == 0.0 or ns == 0.0:
        return 0.0
    return 0.5 * float(np.linalg.norm(gc / nc - gs / ns))


def _mem_target_chisq_reference(H, g0, m, const_chi2, target,
                                nu0=1e-5, max_iter=1000, chisq_tol=1e-2,
                                dgrad_tol=1e-4, min_prob=1e-12):
    """Reference joint (p, nu) controller with best-observe tracking.

    Returns (result, nu, converged, niter). `result` is the MemTcspcResult of
    the returned iterate; on the nu->inf endpoint it is None and the caller
    reads the ceiling from `_mem_quadratic(..., m)`.
    """
    import math
    H = np.asarray(H, float)
    Hm = H.reshape(m.size, m.size)
    Hm = 0.5 * (Hm + Hm.T)
    H = Hm.ravel()
    m = np.asarray(m, dtype=float)
    n = m.size
    tol_abs = chisq_tol * max(target, 1.0)

    ceiling = _mem_quadratic(H, g0, const_chi2, m)
    if target >= ceiling:
        return None, math.inf, abs(ceiling - target) <= tol_abs, 0

    p = m.copy()
    nu = nu0
    l1 = c1 = l2 = c2 = None
    best = {"r": None, "nu": 0.0, "err": math.inf}
    niter_out = 0
    for it in range(1, max_iter + 1):
        niter_out = it
        Delta = 0.5 / np.maximum(p, min_prob)
        C_eff = Hm + np.diag(nu * Delta)
        d_eff = -np.asarray(g0, float) + 0.5 * nu * (_log_clip(p / m) - 1.0)
        p = np.asarray(tttrlib.tcspc_quadpr_bound(
            C_eff.ravel(), d_eff, min_prob))
        chisq = _mem_quadratic(H, g0, const_chi2, p)
        dgrad = _mem_dgrad(H, g0, p, m, min_prob)
        err = abs(chisq - target)

        if err < best["err"]:
            best.update(r=p, nu=nu, err=err, chisq=chisq, it=it)
        if err <= tol_abs and dgrad <= dgrad_tol:
            from types import SimpleNamespace
            return (SimpleNamespace(p=p, chisq=chisq, niter=it),
                    nu, True, it)

        l1, c1, l2, c2 = l2, c2, math.log(nu), chisq
        new_log_nu = None
        if (c1 is not None and c1 > 0.0 and c2 > 0.0
                and abs(l2 - l1) > 1e-14
                and abs(math.log(c2) - math.log(c1)) > 1e-12):
            slope = (math.log(c2) - math.log(c1)) / (l2 - l1)
            if slope > 1e-6:
                new_log_nu = l2 + (math.log(target) - math.log(c2)) / slope
        if new_log_nu is None:
            new_log_nu = math.log(nu) + 0.7 * math.log(
                max(target, 1e-300) / max(chisq, 1e-300))
        step = min(max(new_log_nu - math.log(nu), -math.log(30.0)),
                   math.log(30.0))
        nu_before = nu
        nu = min(max(math.exp(math.log(nu) + step), 1e-30), 1e30)

        # floor clamp: nu pinned AND amplitudes stationary -> nothing changes
        if nu == nu_before and dgrad <= dgrad_tol:
            break

    b = best
    from types import SimpleNamespace
    return (SimpleNamespace(p=(b["r"] if b["r"] is not None else m),
                            chisq=b.get("chisq", ceiling),
                            niter=b.get("it", 0)),
            b["nu"], b["err"] <= tol_abs, niter_out)


def _assemble_mean_chi2(decay, irf, dt, tau_grid, fitstart=5):
    """H, g0, const in run_mem_from_design's mean-chi^2 convention.

    `tcspc_build_fi_lifetimes` returns Fi ALREADY divided by sigma -- do not
    weight it again (doing so was measured to shift the floor from 0.92 to 43).
    """
    n = len(decay)
    Fi, y, sigma, add = tttrlib.tcspc_build_fi_lifetimes(
        np.asarray(decay, float), np.asarray(irf, float), dt,
        np.asarray(tau_grid, float), 0.0, 0.0, 0.0, fitstart, n - 1, 0.0)
    M = y.size
    y_w = (y - add) / sigma
    H = (2.0 / M) * (Fi.T @ Fi)
    g0 = (2.0 / M) * (Fi.T @ y_w)
    const_chi2 = float(y_w @ y_w) / M
    return H, g0, const_chi2


class TestMemTargetChisq(unittest.TestCase):
    """The target-chi^2 search: monotonicity it rests on, hit, and both misses."""

    @classmethod
    def setUpClass(cls):
        np.random.seed(1)
        n, dt = 512, 0.05
        t = np.arange(n) * dt
        irf = np.exp(-0.5 * ((t - 1.0) / 0.15) ** 2)
        irf /= irf.sum()
        model = np.asarray(tttrlib.tcspc_fconv_single_shot(
            irf.tolist(), dt, [1.0], [2.5], n - 1))
        decay = np.random.poisson(model / model.sum() * 200000).astype(float)
        cls.tau_grid = np.linspace(1.0, 4.0, 60)
        cls.H, cls.g0, cls.const_chi2 = _assemble_mean_chi2(
            decay, irf, dt, cls.tau_grid)
        cls.m = np.full(cls.tau_grid.size, 1.0 / cls.tau_grid.size)

    def test_chisq_is_monotone_in_nu(self):
        """The property the nu search rests on, checked, not assumed."""
        chis = []
        for nu in np.geomspace(1e-8, 1e3, 15):
            r = tttrlib.tcspc_run_mem(self.H.ravel(), self.g0, self.m,
                                      self.const_chi2, nu, 200, 1e-4, 1e-12)
            chis.append(r.chisq)
        self.assertTrue(np.all(np.diff(chis) >= -1e-9))

    def test_the_ceiling_is_the_quadratic_at_the_prior(self):
        """chisq(nu) saturates at exactly _mem_quadratic(m) -- the analytic
        limit the unreachable-high branch relies on."""
        ceiling = _mem_quadratic(self.H, self.g0, self.const_chi2, self.m)
        r = tttrlib.tcspc_run_mem(self.H.ravel(), self.g0, self.m,
                                  self.const_chi2, 1e3, 200, 1e-4, 1e-12)
        self.assertAlmostEqual(r.chisq, ceiling, delta=1e-3 * ceiling)

    def test_hits_a_reachable_target(self):
        r, nu, converged, _ = _mem_target_chisq_reference(
            self.H, self.g0, self.m, self.const_chi2, 1.0)
        self.assertTrue(converged)
        self.assertGreater(nu, 0.0)
        self.assertAlmostEqual(r.chisq, 1.0, delta=0.01)
        # the target-chisq solution must still be the right answer
        p = np.asarray(r.p)
        tau_mean = float((p * self.tau_grid).sum() / p.sum())
        self.assertAlmostEqual(tau_mean, 2.5, delta=0.2)

    def test_an_unreachably_low_target_degrades_gracefully(self):
        r, nu, converged, _ = _mem_target_chisq_reference(
            self.H, self.g0, self.m, self.const_chi2, 1e-12)
        self.assertFalse(converged)
        self.assertIsNotNone(r)          # best-observed floor result, no crash
        self.assertGreater(r.chisq, 0.0)

    def test_an_unreachably_high_target_degrades_gracefully(self):
        r, nu, converged, _ = _mem_target_chisq_reference(
            self.H, self.g0, self.m, self.const_chi2, 1e12)
        self.assertFalse(converged)
        self.assertEqual(nu, float("inf"))


class TestSolveLifetimeTargetChisq(unittest.TestCase):
    """The C++ auto-nu path (`solve_tcspc_mem_lifetime(target_chisq=...)`)
    against the Python reference above, on the same decay. The caller's `nu`
    seeds the joint controller on both sides (1e-5 here)."""

    @classmethod
    def setUpClass(cls):
        np.random.seed(1)
        n, dt = 512, 0.05
        t = np.arange(n) * dt
        cls.irf = np.exp(-0.5 * ((t - 1.0) / 0.15) ** 2)
        cls.irf /= cls.irf.sum()
        model = np.asarray(tttrlib.tcspc_fconv_single_shot(
            cls.irf.tolist(), dt, [1.0], [2.5], n - 1))
        cls.decay = np.random.poisson(
            model / model.sum() * 200000).astype(float)
        cls.n, cls.dt = n, dt
        cls.tau_grid = np.linspace(1.0, 4.0, 60)

    def _solve_cpp(self, target):
        return tttrlib.solve_tcspc_mem_lifetime(
            self.decay.tolist(), self.irf.tolist(), self.dt,
            self.tau_grid.tolist(), 0.0, 0.0, 0.0, 5, self.n - 1, 0.0,
            nu=1e-5, max_iter=200, target_chisq=target)

    @pytest.mark.slow
    def test_agrees_with_the_reference_implementation(self):
        res = self._solve_cpp(1.0)
        self.assertTrue(res.success)
        self.assertTrue(res.target_chisq_converged)
        self.assertAlmostEqual(res.chisq, 1.0, delta=0.01)

        H, g0, c = _assemble_mean_chi2(self.decay, self.irf, self.dt,
                                       self.tau_grid)
        m = np.full(self.tau_grid.size, 1.0 / self.tau_grid.size)
        ref, ref_nu, ref_conv, _ = _mem_target_chisq_reference(H, g0, m, c, 1.0)
        self.assertTrue(ref_conv)
        # Same algorithm on the same problem: the found nu and chi-square must
        # agree to well below the search tolerance.
        self.assertAlmostEqual(res.chisq, ref.chisq, delta=1e-6)
        self.assertAlmostEqual(res.nu_used / ref_nu, 1.0, delta=1e-6)
        np.testing.assert_allclose(np.asarray(res.p), np.asarray(ref.p),
                                   atol=1e-9)

    def test_the_recovered_lifetime_is_still_right(self):
        res = self._solve_cpp(1.0)
        p = np.asarray(res.p)
        tau_mean = float((p * self.tau_grid).sum() / p.sum())
        self.assertAlmostEqual(tau_mean, 2.5, delta=0.2)

    def test_disabled_by_default_and_backward_compatible(self):
        """target_chisq <= 0 must be the untouched fixed-nu path."""
        res_default = tttrlib.solve_tcspc_mem_lifetime(
            self.decay.tolist(), self.irf.tolist(), self.dt,
            self.tau_grid.tolist(), 0.0, 0.0, 0.0, 5, self.n - 1, 0.0,
            nu=1e-5, max_iter=200)
        res_disabled = self._solve_cpp(-1.0)
        self.assertEqual(res_default.chisq, res_disabled.chisq)
        np.testing.assert_array_equal(np.asarray(res_default.p),
                                      np.asarray(res_disabled.p))
        self.assertEqual(res_disabled.nu_used, 1e-5)
        self.assertTrue(res_disabled.target_chisq_converged)

    def test_an_impossible_target_reports_not_converged(self):
        res = self._solve_cpp(1e6)
        self.assertTrue(res.success)
        self.assertFalse(res.target_chisq_converged)


def _simulate_decay_histogram(lifetimes, amplitudes, n_ph, n_bins=1024,
                               seed=7):
    """A micro-time histogram from tttrlib's own photon simulator.

    Ground truth is the config, not an analytic curve. Two constraints are
    load-bearing:
      * `background` is zeroed -- the simulator's unconfigured background is
        a delta spike at micro-time 0, not a flat floor, and would bias any
        lifetime recovery (okf: sim-background-microtime-zero).
      * the IRF pattern's dt must equal laser_period / n_microtime_channels
        exactly, or the pattern is mis-binned (okf/design/sim-irf-convolution).
    A 0.3 ns IRF at 1024 bins keeps the simulator's discrete pattern sampling
    and the fitter's trapezoidal fconv recursion consistent to below the
    Poisson noise at 2e5 photons (measured: floor mean-chi^2 0.92; a 0.1 ns
    IRF at 512 bins leaves a visible model mismatch, floor 1.38).
    """
    import json as _json
    cfg = _json.loads(tttrlib.SimEngine.default_json())
    period = 25.6
    dt = period / n_bins
    t = np.arange(n_bins) * dt
    irf = np.exp(-0.5 * ((t - 2.0) / 0.3) ** 2)
    irf /= irf.sum()
    cfg["settings"].update(
        n_ph_max=n_ph, max_windows=10 ** 7,
        n_microtime_channels=n_bins, microtime_resolution=dt,
        laser_period=period, seed_diffusion=seed, seed_emission=seed + 1)
    cfg["background"] = [0.0, 0.0]
    cfg["species"][0]["decay"] = {
        "lifetimes": list(lifetimes), "amplitudes": list(amplitudes),
        "dt": dt, "n_bins": n_bins, "irf": irf.tolist()}
    eng = tttrlib.SimEngine.from_dict(cfg)
    eng.run()
    micro = np.asarray(eng.photons()["micro_time"])
    hist = np.bincount(micro, minlength=n_bins).astype(float)[:n_bins]
    return hist, irf, dt


class TestTcspcMemOnSimulatedPhotons(unittest.TestCase):
    """The solver against data the photon simulator generated.

    Every other recovery test in this file draws Poisson noise on an analytic
    convolution -- the same forward model the fitter uses, so a shared error
    would cancel. The simulator samples photons by a separate mechanism
    (pattern-based micro-time draws per emission event), so agreement here is
    evidence about the solver, not about one formula agreeing with itself.
    """

    @pytest.mark.slow
    def test_recovers_a_simulated_lifetime_at_fixed_nu(self):
        hist, irf, dt = _simulate_decay_histogram([2.5], [1.0], 200000)
        tau_grid = np.linspace(0.5, 6.0, 56)
        H, g0, c = _assemble_mean_chi2(hist, irf, dt, tau_grid)
        m = np.full(tau_grid.size, 1.0 / tau_grid.size)
        r = tttrlib.tcspc_run_mem(H.ravel(), g0, m, c, 1e-5, 200, 1e-4, 1e-12)
        self.assertTrue(r.success)
        p = np.asarray(r.p)
        tau_mean = float((p * tau_grid).sum() / p.sum())
        self.assertAlmostEqual(tau_mean, 2.5, delta=0.25)

    @pytest.mark.slow
    def test_target_chisq_search_recovers_the_simulated_lifetime(self):
        hist, irf, dt = _simulate_decay_histogram([2.5], [1.0], 200000)
        tau_grid = np.linspace(0.5, 6.0, 56)
        n = hist.size

        res = tttrlib.solve_tcspc_mem_lifetime(
            hist.tolist(), irf.tolist(), dt, tau_grid.tolist(),
            0.0, 0.0, 0.0, 5, n - 1, 0.0,
            nu=1e-5, max_iter=200, target_chisq=1.0)
        self.assertTrue(res.success)
        self.assertTrue(res.target_chisq_converged)
        self.assertGreater(res.nu_used, 0.0)
        self.assertAlmostEqual(res.chisq, 1.0, delta=0.01)
        p = np.asarray(res.p)
        p = p / p.sum()
        tau_mean = float((p * tau_grid).sum())
        self.assertAlmostEqual(tau_mean, 2.5, delta=0.25)
        near = (tau_grid > 2.0) & (tau_grid < 3.0)
        self.assertGreater(p[near].sum(), 0.7)

        # and the C++ search agrees with the Python reference on sim data too
        H, g0, c = _assemble_mean_chi2(hist, irf, dt, tau_grid)
        m = np.full(tau_grid.size, 1.0 / tau_grid.size)
        ref, ref_nu, ref_conv, _ = _mem_target_chisq_reference(H, g0, m, c, 1.0)
        self.assertTrue(ref_conv)
        self.assertAlmostEqual(res.chisq, ref.chisq, delta=1e-6)
        self.assertAlmostEqual(res.nu_used / ref_nu, 1.0, delta=1e-6)

    @pytest.mark.slow
    def test_two_simulated_lifetimes_come_out_as_two_groups(self):
        hist, irf, dt = _simulate_decay_histogram([1.0, 4.0], [0.5, 0.5],
                                                   200000, seed=11)
        tau_grid = np.linspace(0.5, 6.0, 56)
        H, g0, c = _assemble_mean_chi2(hist, irf, dt, tau_grid)
        m = np.full(tau_grid.size, 1.0 / tau_grid.size)

        # Probe the floor first: the systematic sim-vs-fconv discretisation
        # residual scales with photon count, so a fixed target of exactly 1.0
        # can sit just below the floor. Targeting floor*1.2 tests the search
        # mechanism itself without betting on the mismatch's size.
        floor = tttrlib.tcspc_run_mem(H.ravel(), g0, m, c, 1e-10,
                                      200, 1e-4, 1e-12).chisq
        target = max(1.0, floor * 1.2)
        res = tttrlib.solve_tcspc_mem_lifetime(
            hist.tolist(), irf.tolist(), dt, tau_grid.tolist(),
            0.0, 0.0, 0.0, 5, hist.size - 1, 0.0,
            nu=1e-5, max_iter=200, target_chisq=target)
        self.assertTrue(res.target_chisq_converged)
        p = np.asarray(res.p)
        p = p / p.sum()
        mass_short = float(p[(tau_grid > 0.5) & (tau_grid < 2.0)].sum())
        mass_long = float(p[(tau_grid > 3.0) & (tau_grid < 5.5)].sum())
        self.assertGreater(mass_short, 0.15)
        self.assertGreater(mass_long, 0.3)
        self.assertGreater(mass_short + mass_long, 0.6)


if __name__ == '__main__':
    unittest.main()

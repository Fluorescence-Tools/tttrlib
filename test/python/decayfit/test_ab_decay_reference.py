"""A/B of the decay kernels against independent references (2026-08-17).

Every check here compares a `modules/spectroscopy/decay` kernel with something
that was NOT derived from it:

* the convolution family (`fconv`, `fconv_per`, `fconv_per_cs`, `sconv`,
  `fconv_ref`, the `*_time_axis` wrappers) against NumPy transcriptions of the
  trapezoid-rule convolution integral written as an explicit sum / `np.convolve`
  (no recursion), and the periodic variants against a brute-force periodic
  sum -- the IRF repeated over many periods and folded back;
* `shift_lamp` against `np.interp`, the `rescale*` family against their closed
  formulas, `add_pile_up_to_model` against a transcription of Coates (1968);
* the Fit23/24/25/26 objectives against a NumPy Poisson likelihood built on the
  same reference convolution, and their optima against `scipy.optimize` on that
  likelihood; Fit23's anisotropy outputs against the closed formulas;
* `FitNExp` against a `scipy.optimize` maximum-likelihood fit of the same
  mixture model, plus a known-answer simulation;
* the DFA spectral/recursive convolution against an `np.fft` circular
  convolution of the closed-form periodic decay;
* `blind_irf_estimate` against a known-answer simulation (peak recovered; the
  shape is NOT -- pinned as an expected failure, see the class docstring).

`MaxEntTcspc` (`test_maxent_tcspc.py`, `test_math_ab_probabilistic.py`) and
`decay_pattern_fit` (`test_decay_pattern_fit.py`, vs `scipy.optimize.nnls`) are
already A/B-tested elsewhere and are not repeated here.
"""
import os
import unittest

import numpy as np
import scipy.optimize
import tttrlib


# ---------------------------------------------------------------------------
# NumPy references (no recursion; written from the integral, not from the code)
# ---------------------------------------------------------------------------

def trap_conv(lamp, a, tau, dt, n):
    """Trapezoid-rule convolution of the IRF with a*exp(-t/tau).

    c[i] = a*dt*sum_{j<=i} w_j lamp[j] exp(-(i-j) dt/tau), w = 1/2 at the two
    endpoints j = 0 and j = i, 1 inside; c[0] = a*dt/2*lamp[0].
    """
    lamp = np.asarray(lamp, dtype=float)[:n]
    e = np.exp(-np.arange(n) * dt / tau)
    full = np.convolve(lamp, e)[:n]
    c = dt * full - 0.5 * dt * (lamp + lamp[0] * e)
    c[0] = 0.5 * dt * lamp[0]
    return a * c


def trap_conv_matrix(lamp, a, tau, dt, n):
    """The same integral as an explicit weighted sum -- a second, slower form."""
    lamp = np.asarray(lamp, dtype=float)[:n]
    i = np.arange(n)[:, None]
    j = np.arange(n)[None, :]
    lag = np.clip(i - j, 0, None)
    K = np.where(j <= i, np.exp(-lag * dt / tau), 0.0)
    W = np.ones((n, n))
    W[:, 0] = 0.5
    W[np.arange(n), np.arange(n)] = 0.5
    return a * dt * (K * W * lamp[None, :]).sum(axis=1)


def spectrum_conv(irf, x, dt, n):
    return sum(trap_conv(irf, x[2 * k], x[2 * k + 1], dt, n) for k in range(len(x) // 2))


def periodic_conv(irf, x, dt, n, conv_stop=None, n_periods=40):
    """Brute-force periodic model: the IRF (truncated after conv_stop) repeated
    over many periods of n bins, trapezoid-convolved, folded back into one period.
    Valid when period == n*dt."""
    if conv_stop is None:
        conv_stop = n - 1
    nn = n * (n_periods + 1)
    long_irf = np.zeros(nn)
    long_irf[:conv_stop + 1] = np.asarray(irf, dtype=float)[:conv_stop + 1]
    return spectrum_conv(long_irf, x, dt, nn).reshape(n_periods + 1, n).sum(axis=0)


def log_m_ext(m):
    """DecayStatistics.h: log(m) above 1e-12, its C1 linear continuation below."""
    floor = 1e-12
    return np.where(m > floor, np.log(np.maximum(m, floor)), np.log(floor) + (m - floor) / floor)


def gaussian_irf(n, dt, position, width):
    t = np.arange(n) * dt
    return np.exp(-0.5 * ((t - position) / width) ** 2)


# ---------------------------------------------------------------------------
# Convolutions
# ---------------------------------------------------------------------------

class TestFconvAgainstTrapezoidSum(unittest.TestCase):

    def setUp(self):
        self.n, self.period = 64, 13.0
        self.dt = self.period / self.n
        self.irf = gaussian_irf(self.n, self.dt, 2.0, 0.15)
        # scalar path (1 lifetime) and SIMD path (>= 2 lifetimes)
        self.spectra = [np.array([1.0, 4.1]),
                        np.array([0.4, 4.1, 0.3, 1.7, 0.2, 0.6, 0.1, 9.0])]

    def test_fconv_is_the_trapezoid_rule(self):
        for x in self.spectra:
            with self.subTest(numexp=len(x) // 2):
                ref = spectrum_conv(self.irf, x, self.dt, self.n)
                ref2 = sum(trap_conv_matrix(self.irf, x[2 * k], x[2 * k + 1], self.dt, self.n)
                           for k in range(len(x) // 2))
                np.testing.assert_allclose(ref, ref2, rtol=0, atol=1e-13)  # the two references agree
                got = np.zeros(self.n)
                tttrlib.fconv(fit=got, irf=self.irf, x=x, dt=self.dt)
                np.testing.assert_allclose(got, ref, rtol=0, atol=1e-13)

    def test_fconv_cs_time_axis_is_the_trapezoid_rule_on_a_uniform_axis(self):
        """fconv_cs_time_axis (uneven-axis capable) on a uniform axis equals the
        same trapezoid convolution as fconv. Until 2026-08-17 the wrapper was
        unreachable from Python (its IRF parameter name was missing from
        DecayConvolution.i's %apply list, SWIG exposed a bare double*); found by
        this A/B and fixed the same day."""
        for x in self.spectra:
            with self.subTest(n_exp=len(x) // 2):
                got = np.zeros(self.n)
                tttrlib.fconv_cs_time_axis(got, np.arange(self.n) * self.dt, self.irf, x,
                                           0, self.n)
                ref = spectrum_conv(self.irf, x, self.dt, self.n)
                # the uneven-axis kernel carries the same bin-0 start-term
                # convention as fconv_per_cs (see the pinned test below)
                np.testing.assert_allclose(got[1:], ref[1:], rtol=1e-12, atol=1e-13)

    def _fconv_ref_reference(self, x, tauref, dt):
        """fconv_ref: fit = sum_a * irf + sum_k a_k (1/tau_ref - 1/tau_k) conv_k, bins >= 1."""
        ref = np.zeros(self.n)
        for k in range(len(x) // 2):
            a, tau = x[2 * k], x[2 * k + 1]
            ref += trap_conv(self.irf, a * (1.0 / tauref - 1.0 / tau), tau, dt, self.n)
        ref += self.irf * x[0::2].sum()
        ref[0] = 0.0
        return ref

    def test_fconv_ref_is_a_reference_corrected_trapezoid_sum(self):
        """The kernel agrees with the transcription at the C++ default bin
        width when the wrapper is asked for exactly that."""
        x = self.spectra[1]
        got = np.zeros(self.n)
        tttrlib.fconv_ref(got, self.irf, x, 2.3, 0, self.n, 0.05)
        np.testing.assert_allclose(got, self._fconv_ref_reference(x, 2.3, 0.05), rtol=0, atol=1e-13)

    def test_fconv_ref_honours_the_dt_argument(self):
        """DecayConvolution.i's `my_fconv_ref` used to forward
        (fit, x, irf, n_x/2, start, stop, tauref) and drop its `dt`, so the
        C++ default 0.05 was used whatever the caller gave (found by this
        A/B 2026-08-17, fixed the same day)."""
        x = self.spectra[1]
        got = np.zeros(self.n)
        tttrlib.fconv_ref(got, self.irf, x, 2.3, 0, self.n, self.dt)
        np.testing.assert_allclose(got, self._fconv_ref_reference(x, 2.3, self.dt), rtol=0, atol=1e-13)

    def test_sconv_is_the_trapezoid_discrete_convolution(self):
        """sconv: fit[i] = 1/2 irf[0] p[i] + sum_{0<j<i} irf[j] p[i-j] + 1/2 irf[i] p[0]; fit[0]=0."""
        p = np.exp(-np.arange(self.n) * self.dt / 3.3)
        got = np.zeros(self.n)
        tttrlib.sconv(got, self.irf, p, 0, self.n)
        full = np.convolve(self.irf, p)[:self.n]
        ref = full - 0.5 * self.irf[0] * p - 0.5 * self.irf * p[0]
        ref[0] = 0.0
        np.testing.assert_allclose(got, ref, rtol=0, atol=1e-13)


class TestPeriodicConvolutionAgainstBruteForce(unittest.TestCase):
    """fconv_per / fconv_per_cs vs the IRF tiled over 40 periods and folded back."""

    def setUp(self):
        self.n, self.period = 64, 13.0
        self.dt = self.period / self.n              # period == n*dt, period_n == n
        self.irf = gaussian_irf(self.n, self.dt, 2.0, 0.15)
        self.spectra = [np.array([1.0, 4.1]),
                        np.array([0.4, 4.1, 0.3, 1.7, 0.2, 0.6, 0.1, 9.0])]

    def test_fconv_per(self):
        for x in self.spectra:
            with self.subTest(numexp=len(x) // 2):
                ref = periodic_conv(self.irf, x, self.dt, self.n)
                got = np.zeros(self.n)
                tttrlib.fconv_per(fit=got, irf=self.irf, x=x, period=self.period,
                                  start=0, stop=-1, dt=self.dt)
                np.testing.assert_allclose(got, ref, rtol=0, atol=1e-13)

    def test_fconv_per_cs_with_and_without_a_convolution_stop(self):
        for x in self.spectra:
            for conv_stop in (self.n - 1, 20):
                with self.subTest(numexp=len(x) // 2, conv_stop=conv_stop):
                    ref = periodic_conv(self.irf, x, self.dt, self.n, conv_stop)
                    got = np.zeros(self.n)
                    tttrlib.fconv_per_cs(fit=got, irf=self.irf, x=x, period=self.period,
                                         stop=-1, conv_stop=conv_stop, dt=self.dt)
                    # bin 0 carries a documented extra factor (see the next test)
                    np.testing.assert_allclose(got[1:], ref[1:], rtol=0, atol=1e-13)
                    self.assertLess(abs(got[0] - ref[0]), 1e-13 + 0.5 * self.dt * self.irf[0] * x[0::2].sum())

    def test_fconv_per_cs_time_axis_is_fconv_per(self):
        x = self.spectra[1]
        got = np.zeros(self.n)
        tttrlib.fconv_per_cs_time_axis(got, np.arange(self.n) * self.dt, self.irf, x,
                                       0, self.n, self.period)
        np.testing.assert_allclose(got, periodic_conv(self.irf, x, self.dt, self.n),
                                   rtol=0, atol=1e-13)

    def test_bin_zero_of_fconv_per_cs_differs_from_fconv_per_when_the_irf_starts_nonzero(self):
        """Documented divergence, pinned so a change is noticed.

        Both kernels put the trapezoid start term into bin 0, but with a
        different weight: fconv_per uses dt/2*irf[0]*a (the trapezoid rule
        exactly, and what the brute-force reference gives), fconv_per_cs uses
        dt/2*irf[0]*(1 + exp(-dt/tau))*a. Invisible whenever irf[0] == 0, which
        is every real IRF; bins >= 1 are identical.
        """
        irf = self.irf.copy()
        irf[0] = 0.7
        x = np.array([1.0, 4.1])
        ref = periodic_conv(irf, x, self.dt, self.n)
        per = np.zeros(self.n)
        tttrlib.fconv_per(fit=per, irf=irf, x=x, period=self.period, start=0, stop=-1, dt=self.dt)
        cs = np.zeros(self.n)
        tttrlib.fconv_per_cs(fit=cs, irf=irf, x=x, period=self.period, stop=-1,
                             conv_stop=self.n - 1, dt=self.dt)
        np.testing.assert_allclose(per, ref, rtol=0, atol=1e-13)
        np.testing.assert_allclose(cs[1:], ref[1:], rtol=0, atol=1e-13)
        expected_bin0 = ref[0] + 0.5 * self.dt * irf[0] * np.exp(-self.dt / 4.1) * x[0]
        self.assertAlmostEqual(cs[0], expected_bin0, places=12)


class TestShiftLampAgainstInterp(unittest.TestCase):

    def test_matches_linear_interpolation_and_zero_fills_outside(self):
        lamp = gaussian_irf(40, 1.0, 12.0, 2.5)
        for ts in (0.3, 2.7, -1.6, 4.0, -0.25):
            with self.subTest(ts=ts):
                got = np.zeros_like(lamp)
                tttrlib.shift_lamp(lamp, got, ts, 0.0)
                ref = np.interp(np.arange(40) + ts, np.arange(40), lamp, left=np.nan, right=np.nan)
                inside = np.isfinite(ref)
                np.testing.assert_allclose(got[inside], ref[inside], rtol=0, atol=1e-13)
                # every bin the kernel zero-fills is one np.interp cannot define, or the
                # last bin needed for the right-hand interpolation partner
                self.assertTrue(np.all(got[~inside] == 0.0))


class TestRescaleAgainstFormulas(unittest.TestCase):

    def setUp(self):
        rng = np.random.default_rng(1)
        self.n = 50
        self.decay = rng.poisson(200 * np.exp(-np.arange(self.n) / 15.0)).astype(float)
        self.fit = np.exp(-np.arange(self.n) / 12.0)
        self.w_sq = np.maximum(self.decay, 1.0)
        self.start, self.stop = 3, 40

    def test_rescale_by_area(self):
        fit = self.fit.copy()
        scale = tttrlib.rescale(fit, self.decay, self.start, self.stop)
        s = self.decay[self.start:self.stop].sum() / self.fit[self.start:self.stop].sum()
        self.assertAlmostEqual(scale, s, places=12)
        np.testing.assert_allclose(fit[self.start:self.stop], self.fit[self.start:self.stop] * s, rtol=1e-13)

    def test_rescale_w_weighted_least_squares_scale(self):
        fit = self.fit.copy()
        scale = tttrlib.rescale_w(fit, self.decay, self.w_sq, self.start, self.stop)
        sl = slice(self.start, self.stop)
        m = self.decay[sl] != 0
        s = np.sum((self.fit[sl] * self.decay[sl] / self.w_sq[sl])[m]) / np.sum((self.fit[sl] ** 2 / self.w_sq[sl])[m])
        self.assertAlmostEqual(scale, s, places=12)

    def test_rescale_w_bg_weighted_scale_with_background(self):
        """Note the kernel treats its third argument as inverse *errors* e:
        weight = e*e + 1e-12 (the header's 1/w^2 wording notwithstanding)."""
        fit = self.fit.copy()
        e = 1.0 / np.sqrt(self.w_sq)
        bg = 3.0
        scale = tttrlib.rescale_w_bg(fit, self.decay, e, bg, self.start, self.stop)
        sl = slice(self.start, self.stop)
        m = self.decay[sl] > 0
        iw = e[sl] ** 2 + 1e-12
        s = np.sum((self.fit[sl] * (self.decay[sl] - bg) * iw)[m]) / np.sum((self.fit[sl] ** 2 * iw)[m])
        self.assertAlmostEqual(scale, s, places=12)


class TestPileUpAgainstCoates(unittest.TestCase):
    """Coates (1968) eq. 2/4 transcribed independently.

    The kernel reproduces the transcription exactly when the pulses already
    'used up' by channel i include channel i itself (inclusive cumulative sum).
    Coates' own denominator is N - sum_{j<i} n_j (exclusive); the difference is
    one channel's counts in the denominator, 1.4e-4 relative here at 5e6
    photons. Both are pinned so the convention is on record.
    """

    def _coates(self, model, data, rep_MHz, dead_ns, meas_s, inclusive):
        rep, dead = rep_MHz * 1e6, dead_ns * 1e-9
        cs = np.cumsum(data)
        n_det = int(cs[-1])
        live = meas_s - n_det * dead
        n_pulses = max(live * rep, n_det)
        used = cs if inclusive else np.concatenate([[0.0], cs[:-1]])
        r = -np.log(1.0 - data / (n_pulses - used))
        r = np.where(r == 0, 1.0, r)
        sf = data / r
        return model * (sf / sf.sum() * len(data))

    def test_matches_the_inclusive_transcription_exactly(self):
        rng = np.random.default_rng(5)
        n = 256
        model = np.exp(-np.arange(n) * 0.05 / 2.0)
        data = rng.poisson(model / model.sum() * 5e6).astype(float)
        got = model.copy()
        tttrlib.add_pile_up_to_model(got, data, 80.0, 100.0, 10.0, "coates", 0, -1)
        np.testing.assert_allclose(got, self._coates(model, data, 80.0, 100.0, 10.0, True), rtol=1e-12)
        excl = self._coates(model, data, 80.0, 100.0, 10.0, False)
        rel = np.abs(got - excl).max() / np.abs(got).max()
        self.assertGreater(rel, 1e-6)   # the two conventions are distinguishable ...
        self.assertLess(rel, 1e-3)      # ... and differ by one channel's counts


# ---------------------------------------------------------------------------
# The Fit2x objectives and optima
# ---------------------------------------------------------------------------

class _PolarisedProblem:
    """A polarisation-resolved decay problem, plus the NumPy model behind it."""

    def __init__(self, seed, n=64, period=32.0, g=1.05, l1=0.05, l2=0.04):
        self.n, self.period, self.g, self.l1, self.l2 = n, period, g, l1, l2
        self.dt = period / n
        self.cs = n // 2
        self.irf1 = gaussian_irf(n, self.dt, 3.0, 0.3)
        self.irf = np.concatenate([self.irf1, self.irf1])
        self.bg = np.ones(2 * n) / (2 * n)
        self.rng = np.random.default_rng(seed)

    def problem(self, data):
        p = tttrlib.DecayFitProblem(2, self.n, self.dt)
        p.irf = tttrlib.VectorDouble(self.irf.tolist())
        p.background = tttrlib.VectorDouble(self.bg.tolist())
        p.data = tttrlib.VectorDouble([float(v) for v in data])
        return p

    def fit(self, name):
        setup = tttrlib.setup_vector(
            name, dt=self.dt, period=self.period, g_factor=self.g, l1=self.l1, l2=self.l2,
            convolution_stop=self.cs, soft_bifl_scatter_flag=False)
        return tttrlib.DecayFit2(name, setup, self.irf.tolist())

    # -- fit23 -------------------------------------------------------------
    def model23(self, tau, gamma, r0, rho):
        taurho = 1.0 / (1.0 / tau + 1.0 / rho)
        xvv = np.array([1.0, tau, r0 * (2.0 - 3.0 * self.l1), taurho])
        xvh = np.array([1.0 / self.g, tau, (1.0 / self.g) * r0 * (-1.0 + 3.0 * self.l2), taurho])
        m = np.concatenate([periodic_conv(self.irf1, xvv, self.dt, self.n, self.cs),
                            periodic_conv(self.irf1, xvh, self.dt, self.n, self.cs)])
        m = m / m.sum()
        return m * (1.0 - gamma) + self.bg * gamma

    def objective23(self, data, tau, gamma, r0, rho):
        m = data.sum() * self.model23(tau, gamma, r0, rho)
        return -np.sum(data * log_m_ext(m)) / self.n

    def anisotropies(self, data, gamma):
        """r_experimental (from signal minus gamma-weighted background) and
        r_scatter (from raw signals): the closed formulas of DecayFit.h."""
        Sp, Ss = data[:self.n].sum(), data[self.n:].sum()
        Bp, Bs = self.bg[:self.n].sum(), self.bg[self.n:].sum()
        B = max(1.0, Bp + Bs)
        Bp *= (Sp + Ss) / B
        Bs *= (Sp + Ss) / B
        Fp = (Sp - gamma * Bp) / (1.0 - gamma)
        Fs = (Ss - gamma * Bs) / (1.0 - gamma)
        g, l1, l2 = self.g, self.l1, self.l2
        r = (Fp - g * Fs) / (Fp * (1.0 - 3.0 * l2) + (2.0 - 3.0 * l1) * g * Fs)
        rs = (Sp - g * Ss) / (Sp * (1.0 - 3.0 * l2) + (2.0 - 3.0 * l1) * g * Ss)
        return r, rs

    # -- fit24 -------------------------------------------------------------
    def model24(self, tau1, gamma, tau2, A2, offset):
        x = np.array([1.0 - A2, tau1, A2, tau2])
        m = np.concatenate([periodic_conv(self.irf1, x, self.dt, self.n, self.cs)] * 2)
        return m * (1.0 - gamma) / m.sum() + self.bg * gamma / self.bg.sum() + offset / self.n

    def objective24(self, data, x):
        m = self.model24(*x).copy()
        m[:self.n] *= data[:self.n].sum() / m[:self.n].sum()
        m[self.n:] *= data[self.n:].sum() / m[self.n:].sum()
        return -np.sum(data * log_m_ext(m)) / self.n


class TestFit23AgainstNumpyAndScipy(unittest.TestCase):

    def setUp(self):
        self.P = _PolarisedProblem(seed=1)
        self.r0, self.rho = 0.38, 1.5
        truth = self.P.model23(2.7, 0.15, self.r0, self.rho)
        self.data = self.P.rng.poisson(truth * 20000).astype(float)
        self.fit = self.P.fit("fit23")
        self.problem = self.P.problem(self.data)

    def test_the_objective_is_the_poisson_likelihood_of_the_reference_model(self):
        for tau, gamma in ((2.0, 0.1), (3.5, 0.3), (2.7, 0.15), (0.6, 0.0)):
            with self.subTest(tau=tau, gamma=gamma):
                got = self.fit.evaluate([tau, gamma, self.r0, self.rho], self.problem)
                ref = self.P.objective23(self.data, tau, gamma, self.r0, self.rho)
                self.assertAlmostEqual(got, ref, delta=1e-9 * abs(ref))

    def test_the_optimum_is_scipys_optimum(self):
        constraints = tttrlib.DecayFitConstraints(tttrlib.VectorInt32([0, 0, -1, -1]))
        out = self.fit.fit([2.0, 0.1, self.r0, self.rho], constraints, self.problem)
        res = scipy.optimize.minimize(
            lambda x: self.P.objective23(self.data, x[0], x[1], self.r0, self.rho),
            [2.0, 0.1], method="Nelder-Mead", options=dict(xatol=1e-9, fatol=1e-13, maxiter=5000))
        np.testing.assert_allclose(list(out.parameters)[:2], res.x, rtol=1e-5, atol=1e-6)
        # neither side found a lower value than the other
        ours = self.P.objective23(self.data, out.parameters[0], out.parameters[1], self.r0, self.rho)
        self.assertAlmostEqual(ours, res.fun, delta=1e-9 * abs(res.fun))
        # the recovered lifetime is the planted one (20k photons)
        self.assertAlmostEqual(out.parameters[0], 2.7, delta=0.05)

    def test_the_anisotropy_outputs_are_the_closed_formulas(self):
        constraints = tttrlib.DecayFitConstraints(tttrlib.VectorInt32([0, 0, -1, -1]))
        out = self.fit.fit([2.0, 0.1, self.r0, self.rho], constraints, self.problem)
        named = tttrlib.results_as_dict("fit23", list(out.results))
        r, rs = self.P.anisotropies(self.data, out.parameters[1])
        # Naming on record: the slot called `r_scatter` carries r() -- the
        # gamma-weighted, background-corrected anisotropy of DecayFit.h -- and
        # `r_experimental` carries rs(), the anisotropy of the raw signals.
        self.assertAlmostEqual(named["r_scatter"], r, places=10)
        self.assertAlmostEqual(named["r_experimental"], rs, places=10)


class TestFit24AgainstNumpyAndScipy(unittest.TestCase):

    def setUp(self):
        self.P = _PolarisedProblem(seed=2, g=1.0, l1=0.0, l2=0.0)
        truth = self.P.model24(0.8, 0.1, 4.0, 0.4, 0.0)
        self.data = self.P.rng.poisson(truth * 30000).astype(float)
        self.fit = self.P.fit("fit24")
        self.problem = self.P.problem(self.data)

    def test_the_objective_is_the_per_channel_normalised_poisson_likelihood(self):
        for x in ([1.0, 0.05, 3.0, 0.5, 0.0], [0.8, 0.1, 4.0, 0.4, 0.0], [2.0, 0.2, 2.5, 0.1, 0.5]):
            with self.subTest(x=x):
                got = self.fit.evaluate(x, self.problem)
                ref = self.P.objective24(self.data, x)
                self.assertAlmostEqual(got, ref, delta=1e-9 * abs(ref))

    def test_the_optimum_is_scipys_optimum(self):
        constraints = tttrlib.DecayFitConstraints(tttrlib.VectorInt32([0, 0, 0, 0, -1]))
        out = self.fit.fit([1.0, 0.05, 3.0, 0.5, 0.0], constraints, self.problem)
        res = scipy.optimize.minimize(
            lambda y: self.P.objective24(self.data, [y[0], y[1], y[2], y[3], 0.0]),
            [1.0, 0.05, 3.0, 0.5], method="Nelder-Mead",
            options=dict(xatol=1e-9, fatol=1e-13, maxiter=20000))
        np.testing.assert_allclose(list(out.parameters)[:4], res.x, rtol=1e-4, atol=1e-5)
        ours = self.P.objective24(self.data, list(out.parameters))
        self.assertAlmostEqual(ours, res.fun, delta=1e-9 * abs(res.fun))
        # planted: tau1=0.8, tau2=4.0, A2=0.4
        self.assertAlmostEqual(out.parameters[0], 0.8, delta=0.1)
        self.assertAlmostEqual(out.parameters[2], 4.0, delta=0.15)


class TestFit25SelectsTheLikelihoodWinner(unittest.TestCase):

    def test_selected_index_is_the_argmin_of_the_reference_likelihood(self):
        P = _PolarisedProblem(seed=7, g=1.0, l1=0.0, l2=0.0)
        gamma, r0, rho = 0.1, 0.38, 1.5
        for tau_true in (0.5, 2.0, 4.0):
            with self.subTest(tau_true=tau_true):
                data = P.rng.poisson((P.model23(tau_true, gamma, r0, rho)) * 20000).astype(float)
                cands = [0.5, 1.0, 2.0, 4.0]
                out = P.fit("fit25").fit(
                    cands + [gamma, r0],
                    tttrlib.DecayFitConstraints(tttrlib.VectorInt32([0, 0, 0, 0, -1, -1])),
                    P.problem(data))
                named = tttrlib.results_as_dict("fit25", list(out.results))
                ref = np.argmin([P.objective23(data, c, gamma, r0, rho) for c in cands])
                self.assertEqual(int(named["selected_index"]), int(ref))
                self.assertEqual(out.parameters[0], cands[ref])


class TestFit26AgainstNumpyAndScipy(unittest.TestCase):

    def setUp(self):
        n = 64
        self.n = n
        t = np.arange(n) * 0.5
        self.pat1 = np.concatenate([np.exp(-t / 1.0)] * 2)
        self.pat1 /= self.pat1.sum()
        self.pat2 = np.concatenate([np.exp(-t / 5.0)] * 2)
        self.pat2 /= self.pat2.sum()
        rng = np.random.default_rng(3)
        self.data = rng.poisson((0.3 * self.pat1 + 0.7 * self.pat2) * 20000).astype(float)
        setup = tttrlib.setup_vector("fit26", dt=0.5, period=32.0, g_factor=1.0, l1=0.0, l2=0.0,
                                     convolution_stop=n // 2, soft_bifl_scatter_flag=False)
        self.fit = tttrlib.DecayFit2("fit26", setup, self.pat1.tolist())
        p = tttrlib.DecayFitProblem(2, n, 0.5)
        p.irf = tttrlib.VectorDouble(self.pat1.tolist())
        p.background = tttrlib.VectorDouble(self.pat2.tolist())
        p.data = tttrlib.VectorDouble(self.data.tolist())
        self.problem = p

    def _objective(self, f):
        m = (f * self.pat1 + (1.0 - f) * self.pat2) * self.data.sum()
        return -np.sum(self.data * log_m_ext(m)) / (2 * self.n)

    def test_objective_and_optimum(self):
        for f in (0.2, 0.5, 0.9):
            self.assertAlmostEqual(self.fit.evaluate([f], self.problem), self._objective(f),
                                   delta=1e-10 * abs(self._objective(f)))
        out = self.fit.fit([0.5], tttrlib.DecayFitConstraints(tttrlib.VectorInt32([0])), self.problem)
        res = scipy.optimize.minimize_scalar(self._objective, bounds=(0.0, 1.0), method="bounded",
                                             options=dict(xatol=1e-10))
        self.assertAlmostEqual(out.parameters[0], res.x, places=6)
        self.assertAlmostEqual(out.parameters[0], 0.3, delta=0.02)


class TestFit2xLeastSquaresObjectives(unittest.TestCase):
    """``objective = neyman_lsq`` / ``gehrels_lsq`` in the fit23 setup: the
    registry advertised both since the objective category existed, but every
    kernel scored the Poisson likelihood (only p2s_mle was honoured) until
    2026-08-17. Now the evaluated statistic is the NumPy chi-square on the same
    model -- Neyman weight max(1, C), Gehrels weight (1 + sqrt(C + 0.75))^2.
    ``evaluate`` returns chi2 / n like the kernels' w / Nchannels, the fit
    outcome reports the reduced chi2 / (2n) -- and the optimum is scipy's
    optimum of that statistic; the three objectives give three different optima."""

    def setUp(self):
        self.P = _PolarisedProblem(seed=3)
        self.r0, self.rho = 0.38, 1.5
        truth = self.P.model23(2.7, 0.15, self.r0, self.rho)
        self.data = self.P.rng.poisson(truth * 3000).astype(float)      # sparse enough to separate them
        self.problem = self.P.problem(self.data)

    def _fit(self, objective):
        setup = tttrlib.setup_vector(
            "fit23", dt=self.P.dt, period=self.P.period, g_factor=self.P.g, l1=self.P.l1, l2=self.P.l2,
            convolution_stop=self.P.cs, soft_bifl_scatter_flag=False, objective=objective)
        return tttrlib.DecayFit2("fit23", setup, self.P.irf.tolist())

    def _chi2(self, objective, tau, gamma):
        m = self.data.sum() * self.P.model23(tau, gamma, self.r0, self.rho)
        if objective == "neyman_lsq":
            w = np.maximum(1.0, self.data)
        else:
            w = (1.0 + np.sqrt(self.data + 0.75)) ** 2
        return np.sum((m - self.data) ** 2 / w)

    def test_evaluate_is_the_reduced_chi_square_of_the_reference_model(self):
        for objective in ("neyman_lsq", "gehrels_lsq"):
            fit = self._fit(objective)
            for tau, gamma in ((2.0, 0.1), (3.5, 0.3), (2.7, 0.15)):
                with self.subTest(objective=objective, tau=tau, gamma=gamma):
                    got = fit.evaluate([tau, gamma, self.r0, self.rho], self.problem)
                    ref = self._chi2(objective, tau, gamma) / self.P.n
                    self.assertAlmostEqual(got, ref, delta=1e-9 * abs(ref))

    def test_the_optimum_is_scipys_optimum_of_the_same_statistic(self):
        constraints = tttrlib.DecayFitConstraints(tttrlib.VectorInt32([0, 0, -1, -1]))
        optima = {}
        for objective in ("neyman_lsq", "gehrels_lsq"):
            with self.subTest(objective=objective):
                out = self._fit(objective).fit([2.0, 0.1, self.r0, self.rho], constraints, self.problem)
                res = scipy.optimize.minimize(
                    lambda x: self._chi2(objective, x[0], x[1]), [2.0, 0.1],
                    method="Nelder-Mead", options=dict(xatol=1e-9, fatol=1e-13, maxiter=5000))
                np.testing.assert_allclose(list(out.parameters)[:2], res.x, rtol=1e-4, atol=1e-5)
                self.assertAlmostEqual(out.objective, res.fun / (2 * self.P.n), delta=1e-6 * abs(res.fun))
                optima[objective] = out.parameters[0]
        poisson = self._fit("poisson_mle").fit([2.0, 0.1, self.r0, self.rho], constraints, self.problem).parameters[0]
        self.assertNotAlmostEqual(optima["neyman_lsq"], poisson, delta=1e-4)
        self.assertNotAlmostEqual(optima["gehrels_lsq"], poisson, delta=1e-4)
        self.assertNotAlmostEqual(optima["neyman_lsq"], optima["gehrels_lsq"], delta=1e-4)


class TestFitNExpAgainstScipyMle(unittest.TestCase):
    """FitNExp profiles amplitudes by EM and searches lifetimes by Brent; scipy
    minimises the same Poisson NLL over (tau1, tau2, w) directly."""

    def setUp(self):
        self.n, self.period = 128, 25.0
        self.dt = self.period / self.n
        self.cs = self.n // 2
        self.irf = gaussian_irf(self.n, self.dt, 2.0, 0.2)
        rng = np.random.default_rng(3)
        truth = 0.6 * self._component(0.7) + 0.4 * self._component(3.5)
        self.data = rng.poisson(truth * 200000).astype(float)

    def _component(self, tau):
        c = periodic_conv(self.irf, np.array([1.0, tau]), self.dt, self.n, self.cs)
        return c / c.sum()

    def _nll(self, x):
        tau1, tau2, w = x
        if not (0.0 <= w <= 1.0) or tau1 <= 0 or tau2 <= 0:
            return 1e30
        p = w * self._component(tau1) + (1.0 - w) * self._component(tau2)
        return -np.sum(self.data * np.log(np.maximum(p, 1e-300)))

    def test_lifetimes_amplitudes_and_likelihood_agree_with_scipy(self):
        fitter = tttrlib.FitNExp(dt=self.dt, irf=self.irf, period=self.period, convolution_stop=self.cs)
        r = fitter(self.data, [1.0, 3.0], include_model=True)
        self.assertTrue(r["converged"])
        w = r["amplitudes"][0] / sum(r["amplitudes"])
        res = scipy.optimize.minimize(self._nll, [1.0, 3.0, 0.5], method="Nelder-Mead",
                                      options=dict(xatol=1e-8, fatol=1e-10, maxiter=5000))
        np.testing.assert_allclose(sorted(r["lifetimes"]), sorted(res.x[:2]), rtol=1e-5)
        self.assertAlmostEqual(w, res.x[2], places=5)
        # the reported NLL is the reference NLL of the reported point, and no worse than scipy's
        self.assertAlmostEqual(r["negative_log_likelihood"], self._nll([r["lifetimes"][0], r["lifetimes"][1], w]),
                               delta=1e-6)
        self.assertLessEqual(r["negative_log_likelihood"], res.fun + 1e-6)
        # the returned model is the mixture of the reference components
        model = np.asarray(r["model"])
        ref = w * self._component(r["lifetimes"][0]) + (1 - w) * self._component(r["lifetimes"][1])
        np.testing.assert_allclose(model / model.sum(), ref, rtol=0, atol=1e-13)
        # known answer: 0.7 / 3.5 ns, 60/40
        np.testing.assert_allclose(sorted(r["lifetimes"]), [0.7, 3.5], rtol=0.02)
        self.assertAlmostEqual(w, 0.6, delta=0.02)


# ---------------------------------------------------------------------------
# DFA spectral machinery
# ---------------------------------------------------------------------------

class TestDfaConvolveAgainstNumpyFft(unittest.TestCase):
    """The recursion's kernel is exp(-kL) for L>0 and 1/2 at L=0 (trapezoid);
    periodically that is K(L) = e^{-kL}/(1-e^{-kn}) - delta(L)/2, and the
    convolution with the (unit-area) response is circular. np.fft does that
    with no closed-form spectrum and no recursion."""

    def _reference(self, k, irf, n, shift=0.0):
        i = np.arange(n)
        w = np.fft.fftfreq(n) * 2 * np.pi
        irf = irf / irf.sum()
        irf_s = np.real(np.fft.ifft(np.fft.fft(irf) * np.exp(-1j * w * shift)))
        K = np.exp(-k * i) / (1.0 - np.exp(-k * n))
        K[0] -= 0.5
        return np.real(np.fft.ifft(np.fft.fft(irf_s) * np.fft.fft(K)))

    def test_both_backends_and_a_fractional_shift(self):
        n = 512
        irf = gaussian_irf(n, 1.0, 50.0, 8.0)
        for k in (1 / 5.0, 1 / 20.0, 1 / 200.0):
            ref = self._reference(k, irf, n)
            for method, tol in ((1, 1e-13), (0, 1e-9)):   # spectral exact; recursive = discretisation
                got = np.asarray(tttrlib.dfa_convolve([k], [1.0], irf.tolist(), n, 0.0, method))
                with self.subTest(k=k, method=method):
                    self.assertLess(np.abs(got - ref).max() / np.abs(ref).max(), tol)
        got = np.asarray(tttrlib.dfa_convolve([1 / 20.0], [1.0], irf.tolist(), n, 0.37, 1))
        ref = self._reference(1 / 20.0, irf, n, 0.37)
        self.assertLess(np.abs(got - ref).max() / np.abs(ref).max(), 1e-9)

    def test_vv_vh_convolved_is_the_projection_of_the_convolved_rates(self):
        n = 256
        irf = gaussian_irf(n, 1.0, 20.0, 4.0)
        kd, kf, ka, r0, g = 1 / 40.0, 1 / 60.0, 1 / 30.0, 0.38, 1.1
        out = np.asarray(tttrlib.dfa_vv_vh_convolved([kd], [1.0], [kf], [1.0], [ka], [1.0],
                                                     r0, g, irf.tolist(), n, 0.0, 1))
        vv, vh = out[:n], out[n:]
        f = self._reference(kd + kf, irf, n)
        fr = self._reference(kd + kf + ka, irf, n)
        np.testing.assert_allclose(vv, f + 2 * r0 * fr, rtol=0, atol=1e-12 * np.abs(f).max())
        np.testing.assert_allclose(vh, g * (f - r0 * fr), rtol=0, atol=1e-12 * np.abs(f).max())


# ---------------------------------------------------------------------------
# Blind IRF: a known answer
# ---------------------------------------------------------------------------

class TestBlindIrfKnownAnswer(unittest.TestCase):
    """A mono-exponential decay (tau = 2.5 ns) through a Gaussian IRF
    (0.15 ns sigma at 2.0 ns), 1e6 photons: the estimate must recover the
    IRF's position AND shape. On 2026-08-17 this A/B found it recovering the
    position only (~25% of the mass within +-0.5 ns of the peak, corr 0.3-0.5).
    Checked against the reference implementation -- VicidominiLab's **birfi**
    (github.com/VicidominiLab/birfi, Gomez-Sanchez et al. 2024; see
    TestBlindIrfAgainstBirfi) and ChiSurf's port of it as a second check
    (TestBlindIrfAgainstChiSurf) -- the tttrlib port's own defects were: the
    Savitzky-Golay derivative mixed a dt-scaled abscissa with an unscaled
    weight vector (ChiSurf uses scipy's savgol_filter; the port's minimum sat
    on the rising edge, so the tail window was a few bins and tau came out
    0.2 ns), and the lifetime was the centroid ChiSurf uses only as the
    initial guess of its L-BFGS-B fit (now a Poisson-weighted log-linear fit
    of the tail, k = 0.400/ns here vs ChiSurf's 0.3988). The periodic
    (circular) forward model is the reference's and is kept, made exact for
    any n; the back-projection is the exact adjoint (the reference's
    time-reversed-kernel convolution is that shifted by one bin)."""

    def setUp(self):
        rng = np.random.default_rng(11)
        self.n, self.dt = 256, 0.05
        self.t = np.arange(self.n) * self.dt
        self.irf = np.exp(-0.5 * ((self.t - 2.0) / 0.15) ** 2)
        self.irf /= self.irf.sum()
        d = np.convolve(self.irf, np.exp(-self.t / 2.5))[:self.n]
        self.data = rng.poisson(d / d.sum() * 1e6 + 20).astype(float)

    def _estimate(self):
        e = np.asarray(tttrlib.blind_irf_estimate(self.data.tolist(), self.n, 1, self.dt, 500, 3, 11, 3))
        return e / e.sum()

    def test_the_peak_position_is_recovered(self):
        e = self._estimate()
        self.assertAlmostEqual(self.t[np.argmax(e)], 2.0, delta=0.15)

    def test_the_shape_is_recovered(self):
        e = self._estimate()
        mass_near_peak = e[(self.t > 1.5) & (self.t < 2.5)].sum()
        self.assertGreater(mass_near_peak, 0.95)
        self.assertGreater(np.corrcoef(e, self.irf)[0, 1], 0.99)

    def test_the_array_binding_equals_the_flat_form(self):
        """`blind_irf_estimate_array((n_bins, n_channels))` is the same kernel as
        the flat-list form, bit for bit, and returns the same shape."""
        data = np.stack([self.data, 0.5 * self.data], axis=1)
        a = np.asarray(tttrlib.blind_irf_estimate_array(data, self.dt, 500, 3, 11, 3))
        b = np.asarray(tttrlib.blind_irf_estimate(data.ravel().tolist(), self.n, 2, self.dt, 500, 3, 11, 3)).reshape(self.n, 2)
        self.assertEqual(a.shape, (self.n, 2))
        np.testing.assert_array_equal(a, b)

    def test_two_channels_with_different_irfs_share_the_lifetime(self):
        """A second channel with a wider, later IRF and fewer photons; the
        shared decay rate is fit across channels and each channel's IRF comes
        back in place and in shape."""
        rng = np.random.default_rng(12)
        irf2 = np.exp(-0.5 * ((self.t - 3.0) / 0.4) ** 2)
        irf2 /= irf2.sum()
        d2 = np.convolve(irf2, np.exp(-self.t / 2.5))[:self.n]
        y2 = rng.poisson(d2 / d2.sum() * 2e5 + 5).astype(float)
        data = np.stack([self.data, y2], axis=1).ravel()
        e = np.asarray(tttrlib.blind_irf_estimate(data.tolist(), self.n, 2, self.dt, 500, 3, 11, 3)).reshape(self.n, 2)
        for c, (irf, peak, corr_min) in enumerate([(self.irf, 2.0, 0.99), (irf2, 3.0, 0.97)]):
            ec = e[:, c] / e[:, c].sum()
            self.assertAlmostEqual(self.t[np.argmax(ec)], peak, delta=0.15)
            self.assertGreater(np.corrcoef(ec, irf)[0, 1], corr_min)


_CHISURF_IRF_EST = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "..",
                                "chisurf", "chisurf", "core", "fluorescence", "tcspc", "irf_estimation.py")


def _load_chisurf_irf_estimator():
    """ChiSurf's IRFEstimator from its source file (its `run` delegates to
    tttrlib when importable, so the reference steps are called one by one)."""
    import importlib.util
    if not os.path.exists(_CHISURF_IRF_EST):
        return None
    try:
        spec = importlib.util.spec_from_file_location("chisurf_irf_estimation", _CHISURF_IRF_EST)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        return mod
    except Exception:   # scipy/numba/etc. missing in this env
        return None


_BIRFI_SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "..",
                          "chisurf", "junk", "birfi", "src")
_BIRFI_RUNNER = r"""
import sys, numpy as np, torch
sys.path.insert(0, sys.argv[1])
from birfi.birfi import Birfi
d = np.load(sys.argv[2]); iters = int(sys.argv[3])
torch.manual_seed(0)
b = Birfi(torch.tensor(d['y'], dtype=torch.float32), dt=float(d['dt']), device=torch.device('cpu'))
out = b.run(rl_iterations=iters).cpu().numpy()
np.savez(sys.argv[4], irf=out, t0=b.t0.cpu().numpy(), t1=b.t1.cpu().numpy(), k=b.params['k'])
"""


def _birfi_available():
    if not os.path.isdir(_BIRFI_SRC):
        return False
    try:
        import torch  # noqa: F401
        return True
    except ImportError:
        return False


@unittest.skipUnless(_birfi_available(), "VicidominiLab birfi (../chisurf/junk/birfi) or torch not available")
class TestBlindIrfAgainstBirfi(unittest.TestCase):
    """tttrlib.blind_irf_estimate vs **birfi** (VicidominiLab,
    github.com/VicidominiLab/birfi -- Gomez-Sanchez et al. 2024, the method
    this kernel implements), run from the junk checkout in a subprocess (torch
    and tttrlib each ship an OpenMP runtime; KMP_DUPLICATE_LIB_OK lets the
    reference process start). Two conventions of the reference are undone or
    allowed for, and pinned: its `partial_convolution` applies an ifftshift
    after every FFT product, so its IRF comes out circularly rolled by n/2 --
    the A/B asserts that the best alignment IS n/2; and its lifetime is an
    Adam MSE fit (1000 steps, lr 1e-2) that does not converge (k 5-38 % off
    the truth on these cases) while the RL step forgives it. Compared: the
    aligned IRF estimates (correlation > 0.97, peaks within 0.15 ns) at both
    birfi's default 30 and tttrlib's default 500 RL iterations, and that
    tttrlib is never worse than birfi against the truth by more than 0.02 in
    correlation. Same simulated configurations as the ChiSurf check below."""

    CASES = [dict(seed=11, tau=2.5, pos=2.0, sig=0.15, nph=1e6),
             dict(seed=2, tau=4.0, pos=3.0, sig=0.4, nph=2e5),
             dict(seed=3, tau=1.2, pos=1.5, sig=0.1, nph=5e5),
             dict(seed=4, tau=6.0, pos=2.5, sig=0.25, nph=1e6, extra=[(3.0, 0.4, 2e5)])]

    @staticmethod
    def _sim(seed, tau, pos, sig, nph, n=256, dt=0.05, bg=20, extra=None):
        rng = np.random.default_rng(seed)
        t = np.arange(n) * dt
        cols, irfs = [], []
        for (p, s_, ph) in [(pos, sig, nph)] + (extra or []):
            irf = np.exp(-0.5 * ((t - p) / s_) ** 2)
            irf /= irf.sum()
            irfs.append(irf)
            d = np.convolve(irf, np.exp(-t / tau))[:n]
            cols.append(rng.poisson(d / d.sum() * ph + bg).astype(float))
        return t, np.stack(cols, axis=1), irfs

    def _run_birfi(self, y, dt, iters):
        import subprocess, sys, tempfile
        tmp = tempfile.mkdtemp(prefix="birfi_ab_")
        fin, fout, frun = [os.path.join(tmp, f) for f in ("in.npz", "out.npz", "run.py")]
        np.savez(fin, y=y, dt=dt)
        with open(frun, "w") as fh:
            fh.write(_BIRFI_RUNNER)
        env = dict(os.environ, KMP_DUPLICATE_LIB_OK="TRUE")
        r = subprocess.run([sys.executable, frun, _BIRFI_SRC, fin, str(iters), fout],
                           capture_output=True, text=True, env=env)
        if r.returncode != 0:
            raise unittest.SkipTest("birfi did not run: " + r.stderr[-600:])
        o = np.load(fout)
        return o["irf"], float(o["k"])

    def test_aligned_irfs_agree_with_birfi(self):
        for cfg in self.CASES:
            t, y, irfs = self._sim(**cfg)
            n, nch = y.shape
            for iters in (30, 500):
                ref_irf, k_ref = self._run_birfi(y, 0.05, iters)
                ours = np.asarray(tttrlib.blind_irf_estimate(y.ravel().tolist(), n, nch, 0.05, iters, 3, 11, 3)).reshape(n, nch)
                for ch in range(nch):
                    with self.subTest(tau=cfg["tau"], iterations=iters, channel=ch):
                        ref = ref_irf[:, ch] / ref_irf[:, ch].sum()
                        got = ours[:, ch] / ours[:, ch].sum()
                        # birfi's ifftshift convention: rolled by n/2 -- give or
                        # take the one bin its reversed-kernel back-projection
                        # (correlation shifted by one) can move a peak
                        roll = max(range(n), key=lambda r_: np.corrcoef(np.roll(ref, r_), got)[0, 1])
                        self.assertIn(roll, (n // 2 - 1, n // 2, n // 2 + 1))
                        ref = np.roll(ref, roll)
                        self.assertGreater(np.corrcoef(ref, got)[0, 1], 0.97)
                        self.assertLessEqual(abs(t[np.argmax(ref)] - t[np.argmax(got)]), 0.15)
                        c_ref = np.corrcoef(ref, irfs[ch])[0, 1]
                        c_got = np.corrcoef(got, irfs[ch])[0, 1]
                        self.assertGreater(c_got, c_ref - 0.02)


# The blind-IRF estimate is validated against VicidominiLab's **birfi**
# (the implementation it reproduces) and against the known answer above.
# ChiSurf used to be a second check here and is not a reference: a moving
# target this library is the upstream of.

# ---------------------------------------------------------------------------
# Priors
# ---------------------------------------------------------------------------

class TestPriorsAgainstScipyStats(unittest.TestCase):
    """DecayFitPrior.lnpdf vs scipy.stats logpdf, inside and outside support."""

    def test_log_densities(self):
        from scipy import stats
        xs = np.array([-2.0, -0.5, 0.0, 1e-3, 0.3, 0.999, 1.0, 1.7, 4.0, 12.0])
        pairs = [
            (tttrlib.UniformPrior(0.2, 3.0), stats.uniform(0.2, 2.8)),
            (tttrlib.NormalPrior(1.5, 0.7), stats.norm(1.5, 0.7)),
            (tttrlib.HalfNormalPrior(0.8, 0.5), stats.halfnorm(0.5, 0.8)),
            (tttrlib.LogNormalPrior(0.4, 0.6), stats.lognorm(s=0.6, scale=np.exp(0.4))),
            (tttrlib.ExponentialPrior(2.0, 0.3), stats.expon(0.3, 2.0)),
            (tttrlib.GammaPrior(2.5, 1.7, 0.2), stats.gamma(2.5, loc=0.2, scale=1.0 / 1.7)),
            (tttrlib.BetaPrior(2.0, 3.5), stats.beta(2.0, 3.5)),
        ]
        for prior, ref in pairs:
            with self.subTest(prior=prior.kind()):
                for x in xs:
                    got = prior.lnpdf(float(x))
                    want = ref.logpdf(x)
                    if np.isfinite(want) and ref.pdf(x) > 0:
                        self.assertAlmostEqual(got, want, places=10, msg=f"x={x}")
                    else:
                        self.assertFalse(np.isfinite(got), msg=f"x={x}: got {got}")

    def test_truncated_normal_is_the_normal_up_to_the_truncation_constant(self):
        """The truncated normal is not renormalised (constant offset vs
        scipy.stats.truncnorm), which a MAP fit cannot see; pinned as such."""
        from scipy import stats
        mu, sigma, lb, ub = 1.5, 0.7, 0.5, 2.5
        prior = tttrlib.TruncatedNormalPrior(mu, sigma, lb, ub)
        ref = stats.truncnorm((lb - mu) / sigma, (ub - mu) / sigma, loc=mu, scale=sigma)
        xs = np.linspace(0.6, 2.4, 7)
        diffs = np.array([prior.lnpdf(float(x)) - ref.logpdf(x) for x in xs])
        np.testing.assert_allclose(diffs, diffs[0], atol=1e-10)
        self.assertAlmostEqual(diffs[0], np.log(ref.cdf(ub) - ref.cdf(lb)) if False else
                               np.log(stats.norm(mu, sigma).cdf(ub) - stats.norm(mu, sigma).cdf(lb)), places=10)
        self.assertFalse(np.isfinite(prior.lnpdf(0.4)))
        self.assertFalse(np.isfinite(prior.lnpdf(2.6)))

    def test_product_prior_is_the_sum_of_its_factors(self):
        """ProductPrior.lnpdf = sum of the factors' lnpdf (independent priors);
        each factor is itself pinned to scipy.stats above. Constructible from a
        Python list since 2026-08-17 (the shared_ptr vector had no typemap; found
        by this A/B, fixed the same day)."""
        a, b = tttrlib.NormalPrior(1.0, 0.5), tttrlib.ExponentialPrior(2.0, 0.0)
        p = tttrlib.ProductPrior([a, b])
        for x in (0.05, 0.3, 1.0, 2.5, 7.0):
            self.assertAlmostEqual(p.lnpdf(x), a.lnpdf(x) + b.lnpdf(x), places=12)
        self.assertAlmostEqual(tttrlib.ProductPrior(tttrlib.VectorDecayFitPrior([a, b])).lnpdf(1.0),
                               a.lnpdf(1.0) + b.lnpdf(1.0), places=12)

if __name__ == "__main__":
    unittest.main()

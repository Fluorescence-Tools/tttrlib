"""A/B and known-answer validation of ``modules/simulation`` against independent references.

What is compared, and against what:

* RNGs -- ``SimRandom`` (MT19937) bit-exact against numpy's legacy MT19937 seeding and
  the mt19937ar ``init_by_array`` test vector; ``SimXoshiroRandom`` bit-exact against a
  transcription of Blackman & Vigna's xoshiro256++ reference C (the transcription
  itself is checked on the published state-{1,2,3,4} vector); ``SimCounterRandom``
  bit-exact against Philox4x32-10 (Salmon et al. 2011, the Random123 known-answer
  vectors were pinned in round 1); the ziggurat ``sim_randn`` tables and stream
  bit-exact against Marsaglia & Tsang 2000's ``zigset``/``RNOR`` driven by the same
  MT stream, plus KS/moments against N(0,1).
* Boundary-flux samplers (``SimInjection.h``) -- ``qnorm`` vs ``scipy.stats.norm.sf``,
  ``influx_weight`` vs its closed form and a numerical integral, ``random_erfc`` and
  ``random_entry_depth`` KS-tested against their numerically integrated CDFs.
* ``SimDecay`` -- pattern builders and ``convolve`` exact against numpy; alias sampling
  chi-square against the stored pdf.
* Kinetics (``SimKinetics.h``, engine state log) -- 3-state occupancy vs the null
  space of the generator; ``sim_state_at_times`` marginals vs ``scipy.linalg.expm``;
  dwell times KS vs exponential.
* Diffusion -- MSD linear in lag, 6 D t, across dt (the one-lag check is
  ``test_engine.py::test_diffusion_msd_recovers_D``).
* Emission -- window counts Poisson (dispersion), background rate recovered, decay
  histogram vs IRF (x) exp and a truncated-exponential MLE recovering tau; anisotropy
  r(0) = r0 and the Perrin initial slope from micro-time-resolved par/perp histograms
  (the steady-state check is ``test_anisotropy.py``); the full exponential
  r0 exp(-t/rho) is pinned as an expected failure -- see the test docstring.
* FCS -- G(tau) of the simulated stream vs PyBroMo's stream (recorded fixture,
  ``gen_pybromo_reference.py``) through the same correlator, and both vs the analytic
  3-D diffusion curve (the analytic-only checks are in ``test_flow.py``, heavy tier).
* Records -- SimEngine -> PTU on disk -> read back with tttrlib *and* independently
  with ``ptufile`` (benchmarks/.venvs/read), timestamps/channels/micro-times identical
  (three PTU *header* defects of the writer are worked around in-file and documented
  on the test class); SPC-132 export via ``to_tttr`` keeps every photon.

The header-only C++ samplers are reached through ``test/cpp/ab_simulation_harness.cpp``,
compiled once per session (skipped without a compiler).
"""
import json
import math
import os
import shutil
import subprocess
import unittest

import numpy as np
import pytest

tttrlib = pytest.importorskip("tttrlib")
if not hasattr(tttrlib, "SimEngine"):
    pytest.skip("tttrlib built without the photon simulator", allow_module_level=True)

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.abspath(os.path.join(_HERE, "..", "..", ".."))
_HARNESS_SRC = os.path.join(_ROOT, "test", "cpp", "ab_simulation_harness.cpp")
_SIMRANDOM_SRC = os.path.join(_ROOT, "modules", "simulation", "src", "SimRandom.cpp")
_INCLUDES = [os.path.join(_ROOT, "modules", d, "include") for d in ("simulation", "math", "util")]
_REF = os.path.join(_ROOT, "test", "data", "reference")
_M32 = 0xFFFFFFFF
_M64 = 0xFFFFFFFFFFFFFFFF

_harness_path = None
_harness_error = None


def _harness():
    global _harness_path, _harness_error
    if _harness_path is not None:
        return _harness_path
    if _harness_error is not None:
        raise unittest.SkipTest(_harness_error)
    cxx = os.environ.get("CXX") or shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
    if cxx is None:
        _harness_error = "no C++ compiler on PATH"
        raise unittest.SkipTest(_harness_error)
    import tempfile
    exe = os.path.join(tempfile.mkdtemp(prefix="tttrlib_ab_sim_"), "ab_simulation_harness")
    cmd = [cxx, "-std=c++17", "-O2"] + [f"-I{p}" for p in _INCLUDES] + \
          [_SIMRANDOM_SRC, _HARNESS_SRC, "-o", exe]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        _harness_error = "harness failed to compile:\n" + proc.stderr[-2000:]
        raise unittest.SkipTest(_harness_error)
    _harness_path = exe
    return exe


def _run(cmd, values):
    exe = _harness()
    text = "\n".join(str(v) for v in values) + "\n"
    proc = subprocess.run([exe, cmd], input=text, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"harness {cmd} failed: {proc.stderr}")
    return proc.stdout.split()


def _ints(tokens):
    return [int(t) for t in tokens]


def _floats(tokens):
    return np.array([float(t) for t in tokens], dtype=float)


def _vd(x):
    return tttrlib.VectorDouble([float(v) for v in x])


# ---------------------------------------------------------------------------
# Reference generators, transcribed from the published algorithms
# ---------------------------------------------------------------------------
def _rotl64(x, k):
    return ((x << k) | (x >> (64 - k))) & _M64


def _splitmix64_next(z):
    """(new_state, output) of Vigna's splitmix64."""
    z = (z + 0x9E3779B97F4A7C15) & _M64
    x = z
    x = ((x ^ (x >> 30)) * 0xBF58476D1CE4E5B9) & _M64
    x = ((x ^ (x >> 27)) * 0x94D049BB133111EB) & _M64
    return z, x ^ (x >> 31)


class _Xoshiro256pp:
    """xoshiro256++ ``next()`` from Blackman & Vigna's reference xoshiro256plusplus.c."""

    def __init__(self, s):
        self.s = list(s)

    def next(self):
        s = self.s
        result = (_rotl64((s[0] + s[3]) & _M64, 23) + s[0]) & _M64
        t = (s[1] << 17) & _M64
        s[2] ^= s[0]
        s[3] ^= s[1]
        s[1] ^= s[2]
        s[0] ^= s[3]
        s[2] ^= t
        s[3] = _rotl64(s[3], 45)
        return result


def _sim_xoshiro_state(base, mid, counter):
    """SimXoshiroRandom::reset -- the documented seed hash expanded by splitmix64."""
    z = ((base << 32) ^ ((mid * 0x9E3779B97F4A7C15) & _M64)
         ^ ((counter * 0xD1B54A32D192ED03) & _M64) ^ 0x1) & _M64
    s = []
    for _ in range(4):
        z, v = _splitmix64_next(z)
        s.append(v)
    return s


def _philox4x32_10(ctr, key):
    """Philox4x32-10 exactly as in Salmon et al. 2011 / Random123."""
    c0, c1, c2, c3 = ctr
    k0, k1 = key
    for _ in range(10):
        p0 = 0xD2511F53 * c0
        p1 = 0xCD9E8D57 * c2
        hi0, lo0 = p0 >> 32, p0 & _M32
        hi1, lo1 = p1 >> 32, p1 & _M32
        c0, c1, c2, c3 = hi1 ^ c1 ^ k0, lo1, hi0 ^ c3 ^ k1, lo0
        k0 = (k0 + 0x9E3779B9) & _M32
        k1 = (k1 + 0xBB67AE85) & _M32
    return [c0, c1, c2, c3]


def _philox_stream(seed, stream, start, n):
    out = []
    block = start // 4
    while len(out) < (start % 4) + n:
        out += _philox4x32_10((block & _M32, block >> 32, 0, 0), (seed, stream))
        block += 1
    return out[start % 4:start % 4 + n]


def _mt_raw(seed, n):
    bg = np.random.MT19937()
    bg._legacy_seeding(int(seed))
    return bg.random_raw(int(n)).astype(np.uint64)


def _zigset():
    """Marsaglia & Tsang 2000 ``zigset`` (the RNOR tables), 128 layers, 32-bit m1."""
    m1 = 2147483648.0
    dn = 3.442619855899
    tn = dn
    vn = 9.91256303526217e-3
    kn = [0] * 128
    wn = [0.0] * 128
    fn = [0.0] * 128
    q = vn / math.exp(-0.5 * dn * dn)
    kn[0] = int((dn / q) * m1)
    kn[1] = 0
    wn[0] = q / m1
    wn[127] = dn / m1
    fn[0] = 1.0
    fn[127] = math.exp(-0.5 * dn * dn)
    for i in range(126, 0, -1):
        dn = math.sqrt(-2.0 * math.log(vn / dn + math.exp(-0.5 * dn * dn)))
        kn[i + 1] = int((dn / tn) * m1)
        tn = dn
        fn[i] = math.exp(-0.5 * dn * dn)
        wn[i] = dn / m1
    return kn, wn, fn


class _RnorRef:
    """Marsaglia & Tsang's RNOR/nfix over a caller-supplied uint32 stream, with the
    simulator's uniform mapping ``u = raw * 2^-32``."""
    KF1 = 2.3283064365387E-010

    def __init__(self, raw):
        self.raw = iter(int(v) for v in raw)
        self.kn, self.wn, self.fn = _zigset()

    def _u32(self):
        return next(self.raw)

    def _uni(self):
        return self._u32() * self.KF1

    def next(self):
        kn, wn, fn = self.kn, self.wn, self.fn
        r = 3.442620
        while True:
            hz = self._u32()
            if hz >= 2 ** 31:
                hz -= 2 ** 32
            iz = hz & 127
            if abs(hz) < kn[iz]:
                return hz * wn[iz]
            x = hz * wn[iz]
            if iz == 0:
                while True:
                    xx = -math.log(self._uni()) / r
                    yy = -math.log(self._uni())
                    if yy + yy >= xx * xx:
                        break
                return r + xx if hz > 0 else -r - xx
            if fn[iz] + self._uni() * (fn[iz - 1] - fn[iz]) < math.exp(-0.5 * x * x):
                return x


# ---------------------------------------------------------------------------
# RNGs
# ---------------------------------------------------------------------------
class TestSimRandomAgainstNumpyMT19937(unittest.TestCase):
    """``SimRandom`` is MT19937 (Cokus); numpy's legacy seeding is ``init_genrand``."""

    def test_raw_stream_is_bit_exact(self):
        for seed in (5489, 0, 1, 12345, 0xFFFFFFFF):
            r = tttrlib.SimRandom()
            r.seed(seed)
            ours = [r.randomUInt() for _ in range(2000)]
            self.assertEqual(ours, _mt_raw(seed, 2000).tolist(), f"seed {seed}")

    def test_init_by_array_is_reachable_from_python_and_matches_the_vector(self):
        """The array binding landed 2026-08-17 (the raw uint32_t* form was not
        callable from Python); same mt19937ar test vector as below."""
        r = tttrlib.SimRandom(1)
        r.init_by_array(np.array([0x123, 0x234, 0x345, 0x456], np.uint32))
        self.assertEqual([r.randomUInt() for _ in range(5)],
                         [1067595299, 955945823, 477289528, 4107218783, 4228976476])

    def test_init_by_array_matches_the_mt19937ar_test_vector(self):
        """mt19937ar.c's own test: init_by_array({0x123,0x234,0x345,0x456}) -> the
        first outputs 1067595299 955945823 477289528 4107218783 4228976476."""
        got = _ints(_run("mt_init_by_array", [4, 0x123, 0x234, 0x345, 0x456, 5]))
        self.assertEqual(got, [1067595299, 955945823, 477289528, 4107218783, 4228976476])

    def test_res53_is_numpys_random_sample(self):
        r = tttrlib.SimRandom()
        r.seed(2024)
        ours = np.array([r.random_res53() for _ in range(5000)])
        ref = np.random.RandomState(2024).random_sample(5000)   # genrand_res53
        np.testing.assert_array_equal(ours, ref)

    def test_uniform_mappings_are_the_documented_scalings(self):
        raw = _mt_raw(77, 3000).astype(float)
        r = tttrlib.SimRandom()
        r.seed(77)
        got = np.array([r.random0i1e() for _ in range(3000)])
        np.testing.assert_array_equal(got, raw * 2.3283064365387E-010)
        r.seed(77)
        got = np.array([r.random0e1e() for _ in range(3000)])
        np.testing.assert_array_equal(got, (raw + 0.5) * 2.3283064365387E-010)
        r.seed(77)
        got = np.array([r.random0i1i() for _ in range(3000)])
        np.testing.assert_array_equal(got, raw * 2.3283064370808E-010)
        r.seed(77)
        got = np.array([r.random4nrm() for _ in range(3000)])
        signed = raw.copy()
        signed[signed >= 2 ** 31] -= 2 ** 32
        np.testing.assert_array_equal(got, signed * 3.994274348768903E-010)

    def test_state_snapshot_round_trip_reproduces_the_stream(self):
        r = tttrlib.SimRandom()
        r.seed(9)
        for _ in range(1000):
            r.randomUInt()
        st = r.getState()
        a = [r.randomUInt() for _ in range(700)]     # crosses a twist boundary
        r2 = tttrlib.SimRandom()
        r2.setState(st)
        b = [r2.randomUInt() for _ in range(700)]
        self.assertEqual(a, b)


class TestSimXoshiroAgainstTheReference(unittest.TestCase):
    def test_transcription_reproduces_the_published_vector(self):
        """xoshiro256++ from state {1,2,3,4} (the vector every port checks against)."""
        g = _Xoshiro256pp([1, 2, 3, 4])
        self.assertEqual([g.next() for _ in range(6)],
                         [41943041, 58720359, 3588806011781223, 3591011842654386,
                          9228616714210784205, 9973669472204895162])

    def test_stream_is_bit_exact_including_the_seed_hash(self):
        for base, mid, ctr in [(0, 0, 0), (1, 0, 0), (7, 3, 99), (0xFFFFFFFF, 12345, 2 ** 40 + 5),
                               (20260731, 41, 4096)]:
            with self.subTest(base=base, id=mid, ctr=ctr):
                got = _ints(_run("xoshiro", [base, mid, ctr, 512]))
                g = _Xoshiro256pp(_sim_xoshiro_state(base, mid, ctr))
                self.assertEqual(got, [g.next() for _ in range(512)])

    def test_randomnorm_is_standard_normal(self):
        from scipy import stats
        x = _floats(_run("xoshiro_norm", [3, 1, 0, 200000]))
        self.assertAlmostEqual(x.mean(), 0.0, delta=0.01)
        self.assertAlmostEqual(x.var(), 1.0, delta=0.02)
        self.assertGreater(stats.kstest(x, "norm").pvalue, 1e-3)


class TestSimCounterRandomAgainstPhilox(unittest.TestCase):
    def test_stream_is_philox4x32_10_bit_exact(self):
        """key = (base, molecule id), counter = draw index; ``reset`` seeks, not burns."""
        for base, mid, start in [(0, 0, 0), (1, 2, 0), (42, 7, 3), (42, 7, 4), (99, 1, 4097),
                                 (0xDEADBEEF, 5, 2 ** 33 + 1)]:
            with self.subTest(base=base, id=mid, start=start):
                got = _ints(_run("counter", [base, mid, start, 64]))
                self.assertEqual(got, _philox_stream(base, mid, start, 64))


class TestZigguratAgainstMarsagliaTsang(unittest.TestCase):
    def test_tables_are_zigset(self):
        toks = _run("zigg_tables", [])
        kn = _ints(toks[:128])
        wn = _floats(toks[128:256])
        fn = _floats(toks[256:384])
        rkn, rwn, rfn = _zigset()
        self.assertEqual(kn, rkn)
        np.testing.assert_array_equal(wn, np.array(rwn))
        np.testing.assert_array_equal(fn, np.array(rfn))

    def test_stream_is_rnor_bit_exact(self):
        """sim_randn over MT19937(seed) equals RNOR/nfix over numpy's identical MT stream."""
        for seed in (1, 5489, 31415):
            with self.subTest(seed=seed):
                got = _floats(_run("zigg", [seed, 20000]))
                ref = _RnorRef(_mt_raw(seed, 60000))
                exp = np.array([ref.next() for _ in range(20000)])
                np.testing.assert_array_equal(got, exp)

    def test_is_standard_normal(self):
        from scipy import stats
        x = _floats(_run("zigg", [2718, 300000]))
        self.assertAlmostEqual(x.mean(), 0.0, delta=0.01)
        self.assertAlmostEqual(x.var(), 1.0, delta=0.02)
        self.assertAlmostEqual(stats.skew(x), 0.0, delta=0.02)
        self.assertAlmostEqual(stats.kurtosis(x), 0.0, delta=0.05)
        self.assertGreater(stats.kstest(x, "norm").pvalue, 1e-3)
        # both tails populated (the iz == 0 branch)
        self.assertGreater((np.abs(x) > 3.5).sum(), 50)


class TestSimdNormalsAgainstScipy(unittest.TestCase):
    """``SimRandomV`` -- the SIMD normal generator -- against N(0,1). Complements
    test_simd.py, which checks moments, tails and lane independence."""

    def test_ks_against_the_normal_cdf(self):
        from scipy import stats
        v = tttrlib.SimRandomV()
        v.seed(123)
        x = np.asarray(v.normals(200000), dtype=float)
        self.assertEqual(x.size, 200000)
        self.assertGreater(stats.kstest(x, "norm").pvalue, 1e-3)


# ---------------------------------------------------------------------------
# Boundary-flux samplers (SimInjection.h)
# ---------------------------------------------------------------------------
class TestInjectionSamplersAgainstScipy(unittest.TestCase):
    def test_qnorm_is_the_normal_survival_function(self):
        from scipy import stats
        xs = np.concatenate([np.linspace(-6, 6, 241), [2.99, 3.0, 3.01, 8.0, -8.0, 0.0]])
        got = _floats(_run("qnorm", [len(xs)] + [repr(float(v)) for v in xs]))
        ref = stats.norm.sf(xs)
        np.testing.assert_allclose(got, ref, rtol=1e-9, atol=1e-16)

    def test_influx_weight_is_the_expected_positive_part(self):
        from scipy import integrate, stats
        cases = [(0.0, 1.0), (0.5, 1.0), (-0.5, 1.0), (2.0, 0.3), (-3.0, 0.5), (0.0, 0.01), (1.0, 0.0)]
        flat = []
        for mu, s in cases:
            flat += [repr(mu), repr(s)]
        got = _floats(_run("influx", [len(cases)] + flat))
        for (mu, s), g in zip(cases, got):
            if s > 0:
                closed = mu * stats.norm.cdf(mu / s) + s * stats.norm.pdf(mu / s)
                numeric = integrate.quad(lambda x: x * stats.norm.pdf(x, mu, s), 0, mu + 12 * s)[0]
                self.assertAlmostEqual(g, closed, delta=1e-12 * max(1.0, abs(closed)))
                self.assertAlmostEqual(g, numeric, delta=1e-8)
            else:
                self.assertEqual(g, max(mu, 0.0))
        # mu = 0 is the diffusion-only formula sigma / sqrt(2 pi)
        self.assertAlmostEqual(got[0], 1.0 / math.sqrt(2 * math.pi), places=12)

    def test_random_erfc_has_density_erfc_x_over_sqrt2(self):
        from scipy import special, stats
        x = _floats(_run("erfc", [11, 100000]))
        self.assertTrue(np.all(x >= 0))
        # CDF of p(x) = erfc(x/sqrt2)/sqrt(2/pi):  F(x) = [x erfc(x/sqrt2) + sqrt(2/pi)(1-exp(-x^2/2))]/sqrt(2/pi)
        c = math.sqrt(2 / math.pi)

        def cdf(t):
            t = np.asarray(t, float)
            return (t * special.erfc(t / math.sqrt(2)) + c * (1 - np.exp(-t * t / 2))) / c

        self.assertGreater(stats.kstest(x, cdf).pvalue, 1e-3)
        self.assertAlmostEqual(x.mean(), 1.0 / c / 2, delta=0.01)   # E[x] = sqrt(pi/2)/2 ~ 0.6267

    def test_random_entry_depth_has_density_q_of_h_minus_mu_over_sigma(self):
        from scipy import stats
        for mu, s in [(0.0, 1.0), (0.7, 0.4), (-0.6, 0.5)]:
            with self.subTest(mu=mu, sigma=s):
                h = _floats(_run("entry", [5, repr(mu), repr(s), 60000]))
                self.assertTrue(np.all(h >= 0))
                grid = np.linspace(0, max(mu, 0) + 8 * s, 4001)
                dens = stats.norm.sf((grid - mu) / s)
                cdf_grid = np.concatenate([[0.0], np.cumsum(0.5 * (dens[1:] + dens[:-1]) * np.diff(grid))])
                cdf_grid /= cdf_grid[-1]
                self.assertGreater(stats.kstest(h, lambda t: np.interp(t, grid, cdf_grid)).pvalue, 1e-3)


# ---------------------------------------------------------------------------
# SimDecay
# ---------------------------------------------------------------------------
class TestSimDecayAgainstNumpy(unittest.TestCase):
    def test_pattern_builders_and_convolve_match_numpy(self):
        n, dt, t0 = 512, 0.05, 0.0
        amps, taus = [1.0, 0.4], [2.5, 0.6]
        t = np.arange(n) * dt + t0
        ref = sum(a * np.exp(-t / tau) for a, tau in zip(amps, taus))
        got = np.asarray(tttrlib.SimDecay.multi_exponential_pattern(_vd(amps), _vd(taus), n, dt, t0))
        np.testing.assert_allclose(got, ref, rtol=1e-15, atol=0)
        irf = np.exp(-0.5 * ((t - 1.0) / 0.1) ** 2)
        conv = np.asarray(tttrlib.SimDecay.convolve(_vd(ref), _vd(irf)))
        full = np.convolve(ref, irf)[:n]
        np.testing.assert_allclose(conv, full, rtol=1e-12, atol=1e-12 * full.max())
        d = tttrlib.SimDecay.multi_exponential_with_irf(_vd(amps), _vd(taus), n, dt, _vd(irf), t0)
        pdf = np.array([d.pdf(i) for i in range(n)])
        np.testing.assert_allclose(pdf, full / full.sum(), rtol=1e-12)
        self.assertAlmostEqual(pdf.sum(), 1.0, places=12)
        self.assertEqual(d.n_bins(), n)

    def test_alias_sampling_reproduces_the_pdf(self):
        from scipy import stats
        n, dt = 64, 0.1
        w = np.exp(-np.arange(n) * dt / 1.5)
        w[:3] = 0.0
        w[20] *= 6.0                                          # a spike keeps the alias table honest
        toks = _run("decay_sample", [17, repr(dt), "0.0", n] + [repr(float(v)) for v in w] + [200000])
        s = _floats(toks)
        self.assertTrue(np.all((s >= 0) & (s < n * dt)))
        bins = np.floor(s / dt).astype(int)
        counts = np.bincount(bins, minlength=n)
        p = w / w.sum()
        self.assertTrue(np.all(counts[p == 0] == 0))
        keep = p > 0
        chi2 = np.sum((counts[keep] - s.size * p[keep]) ** 2 / (s.size * p[keep]))
        self.assertGreater(stats.chi2.sf(chi2, keep.sum() - 1), 1e-3)
        # sub-bin jitter is uniform within a bin
        frac = s / dt - bins
        self.assertGreater(stats.kstest(frac, "uniform").pvalue, 1e-3)


# ---------------------------------------------------------------------------
# Kinetics
# ---------------------------------------------------------------------------
_K3 = np.array([[0.0, 30.0, 5.0],
                [12.0, 0.0, 20.0],
                [8.0, 15.0, 0.0]])          # per macro-time unit, source -> target


def _generator(k):
    q = np.array(k, float).copy()
    np.fill_diagonal(q, 0.0)
    np.fill_diagonal(q, -q.sum(axis=1))
    return q


def _equilibrium(k):
    q = _generator(k)
    w, v = np.linalg.eig(q.T)
    p = np.real(v[:, np.argmin(np.abs(w))])
    return p / p.sum()


class TestKineticsAgainstTheCtmc(unittest.TestCase):
    def test_occupation_fractions_average_to_the_null_space(self):
        n = 3
        frac = np.asarray(tttrlib.sim_occupation_fractions(_K3.ravel().tolist(), n, 5.0, 20000, [], 3)).reshape(-1, n)
        np.testing.assert_allclose(frac.sum(axis=1), 1.0, atol=1e-12)
        pi = _equilibrium(_K3)
        # a 5-unit window is ~100+ transitions, so each row is close to pi; the mean is closer
        np.testing.assert_allclose(frac.mean(axis=0), pi, atol=0.01)

    def test_state_at_times_marginals_follow_expm(self):
        from scipy import stats
        from scipy.linalg import expm
        n = 3
        q = _generator(_K3)
        p0 = np.array([1.0, 0.0, 0.0])
        times = np.array([0.005, 0.02, 0.05, 0.2, 1.0])
        n_rep = 4000
        counts = np.zeros((times.size, n))
        for seed in range(n_rep):
            st = np.asarray(tttrlib.sim_state_at_times(_K3.ravel().tolist(), n, times.tolist(), p0.tolist(), seed))
            counts[np.arange(times.size), st] += 1
        for i, t in enumerate(times):
            with self.subTest(t=t):
                expect = p0 @ expm(q * t)
                chi2 = np.sum((counts[i] - n_rep * expect) ** 2 / (n_rep * expect))
                self.assertGreater(stats.chi2.sf(chi2, n - 1), 1e-3, f"t={t}: {counts[i] / n_rep} vs {expect}")

    def test_engine_dwell_times_are_exponential_ks(self):
        """The photon engine's own state log, three states, KS against exp(k_exit)."""
        from scipy import stats
        s = tttrlib.SimSystem()
        for _ in range(3):
            sp = tttrlib.SimSpecies()
            sp.D = 0.0
            sp.q = _vd([0.0])
            s.add_species(sp)
        s.set_rate_matrices(_vd([0.0] * 9), _vd(_K3.ravel().tolist()))
        s.set_background(_vd([0.0]))
        s.set_box(50.0, 50.0)
        for _ in range(4):
            s.add_fluorophore(0.0, 0.0, 0.0, 0, False)
        st = tttrlib.SimIntegrator()
        st.dt = 0.01
        st.n_channels = 1
        st.n_ph_max = 10 ** 12
        st.max_windows = 60000
        st.seed_diffusion = 5
        st.seed_emission = 6
        eng = tttrlib.SimEngine(s, tttrlib.SimGrid.gaussian3d(0.3, 2.0, 4.0, 8.0, 0.2, 1.0),
                                tttrlib.VectorSimGrid([]), st)
        eng.set_state_log(True)
        eng.run()
        tr = eng.state_trajectory()
        occupancy = np.zeros(3)
        for mol in np.unique(tr["molecule"]):
            m = tr["molecule"] == mol
            t, to = tr["macro_time"][m], tr["to"][m]
            ok = to >= 0
            dwell, state = np.diff(t)[ok[:-1]], to[:-1][ok[:-1]]
            for i in range(3):
                d = dwell[state == i]
                occupancy[i] += d.sum()
                if d.size > 200:
                    k_exit = _K3[i].sum()
                    self.assertGreater(stats.kstest(d, "expon", args=(0, 1.0 / k_exit)).pvalue, 1e-3,
                                       f"state {i}, mol {mol}")
        np.testing.assert_allclose(occupancy / occupancy.sum(), _equilibrium(_K3), atol=0.02)


# ---------------------------------------------------------------------------
# Diffusion
# ---------------------------------------------------------------------------
class TestDiffusionKnownAnswers(unittest.TestCase):
    def test_msd_is_linear_in_lag_at_6_d_t_across_dt(self):
        D = 2.0
        for dt in (0.002, 0.02):
            with self.subTest(dt=dt):
                s = tttrlib.SimSystem()
                sp = tttrlib.SimSpecies()
                sp.D = D
                sp.q = _vd([0.0])
                s.add_species(sp)
                s.set_rate_matrices(_vd([0.0]), _vd([0.0]))
                s.set_background(_vd([0.0]))
                s.set_box(1e6, 1e6)
                for _ in range(8):
                    s.add_fluorophore(0.0, 0.0, 0.0, 0, True)
                st = tttrlib.SimIntegrator()
                st.dt = dt
                st.n_channels = 1
                st.n_ph_max = 10 ** 9
                st.max_windows = 20000
                st.seed_diffusion = 11
                eng = tttrlib.SimEngine(s, tttrlib.SimGrid.uniform(0.0, 1.0, 2.0, 0.5),
                                        tttrlib.VectorSimGrid([]), st)
                eng.set_trajectory_reporter(1)
                eng.run()
                ids = np.asarray(eng.trajectory_id())
                p = np.c_[np.asarray(eng.trajectory_x()), np.asarray(eng.trajectory_y()),
                          np.asarray(eng.trajectory_z())]
                lags = [1, 2, 4, 8]
                msd = []
                for lag in lags:
                    acc, cnt = 0.0, 0
                    for mol in np.unique(ids):
                        pm = p[ids == mol]
                        d = pm[lag:] - pm[:-lag]
                        acc += np.sum(d ** 2)
                        cnt += d.shape[0]
                    msd.append(acc / cnt)
                msd = np.array(msd)
                np.testing.assert_allclose(msd, 6 * D * dt * np.array(lags), rtol=0.05)
                # per-axis isotropy at lag 1
                d1 = np.concatenate([np.diff(p[ids == m], axis=0) for m in np.unique(ids)])
                np.testing.assert_allclose(d1.var(axis=0), 2 * D * dt, rtol=0.06)


# ---------------------------------------------------------------------------
# Emission statistics, decay and anisotropy
# ---------------------------------------------------------------------------
class TestEmissionKnownAnswers(unittest.TestCase):
    def _immobile(self, q, n_ph, dt=0.01, decay=None, r0=0.0, D_rot=0.0, n_mol=1,
                  micro=None, bg=None, seed=1):
        s = tttrlib.SimSystem()
        sp = tttrlib.SimSpecies()
        sp.D = 0.0
        sp.q = _vd(q)
        if decay is not None:
            sp.decay = decay
        sp.r0 = r0
        sp.D_rot = D_rot
        s.add_species(sp)
        s.set_rate_matrices(_vd([0.0]), _vd([0.0]))
        s.set_background(_vd(bg if bg is not None else [0.0] * len(q)))
        for _ in range(n_mol):
            s.add_fluorophore(0.0, 0.0, 0.0, 0, False)
        st = tttrlib.SimIntegrator()
        st.dt = dt
        st.n_channels = len(q)
        st.n_ph_max = n_ph
        st.max_windows = 10 ** 9
        st.seed_emission = seed
        st.seed_diffusion = seed + 1
        if micro is not None:
            n, mdt = micro
            st.n_microtime_channels = n
            st.microtime_resolution = mdt
            st.laser_period = n * mdt
        eng = tttrlib.SimEngine(s, tttrlib.SimGrid.gaussian3d(0.5, 1.0, 1.0, 2.0, 0.05, 1.0),
                                tttrlib.VectorSimGrid([]), st)
        eng.run()
        return eng

    def test_window_counts_are_poisson_with_the_set_brightness(self):
        from scipy import stats
        q, dt = 120.0, 0.01                     # 1.2 photons / window at the focus
        eng = self._immobile([q], 150000, dt=dt)
        et = np.asarray(eng.event_type())
        w = np.asarray(eng.macro_window())[et == 0]
        counts = np.bincount(w.astype(np.int64), minlength=int(eng.current_window()))[:int(eng.current_window()) - 1]
        mean, var = counts.mean(), counts.var()
        self.assertAlmostEqual(mean, q * dt, delta=0.02)
        self.assertAlmostEqual(var / mean, 1.0, delta=0.03)      # Poisson dispersion
        # full pmf against scipy
        kmax = counts.max()
        obs = np.bincount(counts, minlength=kmax + 1).astype(float)
        exp = stats.poisson.pmf(np.arange(kmax + 1), q * dt) * counts.size
        merge = exp >= 5
        o = np.append(obs[merge], obs[~merge].sum())
        e = np.append(exp[merge], exp[~merge].sum())
        chi2 = np.sum((o - e) ** 2 / e)
        self.assertGreater(stats.chi2.sf(chi2, o.size - 1), 1e-3)
        # inter-photon times exponential
        t = np.asarray(eng.macro_window())[et == 0] * dt + np.asarray(eng.arrival_time())[et == 0]
        gaps = np.diff(t)
        self.assertGreater(stats.kstest(gaps, "expon", args=(0, 1.0 / q)).pvalue, 1e-3)

    def test_background_rate_is_recovered(self):
        bg, dt = 40.0, 0.01
        eng = self._immobile([0.0], 60000, dt=dt, bg=[bg])
        et = np.asarray(eng.event_type())
        n_windows = eng.current_window()
        rate = (et == 0).sum() / n_windows / dt
        self.assertAlmostEqual(rate / bg, 1.0, delta=0.02)

    def test_micro_time_histogram_is_irf_convolved_exponential_and_tau_is_recovered(self):
        from scipy import optimize, stats
        n, mdt, tau = 1024, 0.032, 2.4
        t = np.arange(n) * mdt
        irf = np.exp(-0.5 * ((t - 1.0) / 0.12) ** 2)
        model = np.convolve(np.exp(-t / tau), irf)[:n]
        dec = tttrlib.SimDecay.multi_exponential_with_irf(_vd([1.0]), _vd([tau]), n, mdt, _vd(irf), 0.0)
        eng = self._immobile([500.0], 300000, decay=dec, micro=(n, mdt))
        micro = np.asarray(eng.micro_time())[np.asarray(eng.event_type()) == 0]
        hist = np.bincount(micro, minlength=n)[:n].astype(float)
        p = model / model.sum()
        keep = p * hist.sum() >= 5
        chi2 = np.sum((hist[keep] - hist.sum() * p[keep]) ** 2 / (hist.sum() * p[keep]))
        self.assertGreater(stats.chi2.sf(chi2, keep.sum() - 1), 1e-3)
        # MLE of tau for a truncated exponential on the tail (t > 2 ns after the IRF)
        tt = micro * mdt
        sel = tt > 2.0
        x = tt[sel] - 2.0
        T = n * mdt - 2.0

        def score(th):
            return x.mean() - th + T / math.expm1(T / th)

        tau_hat = optimize.brentq(score, 0.5, 10.0)
        self.assertAlmostEqual(tau_hat, tau, delta=0.05)

    def _anisotropy_decay(self, r0=0.4, rho=2.0, tau=4.0, n=512, mdt=0.032):
        dec = tttrlib.SimDecay.multi_exponential(_vd([1.0]), _vd([tau]), n, mdt, 0.0)
        eng = self._immobile([100.0, 100.0], 800000, decay=dec, micro=(n, mdt), r0=r0,
                             D_rot=1.0 / (6.0 * rho), n_mol=400)
        et = np.asarray(eng.event_type()) == 0
        micro = np.asarray(eng.micro_time())[et]
        ch = np.asarray(eng.channel())[et]
        par = np.bincount(micro[ch == 0], minlength=n)[:n].astype(float)
        perp = np.bincount(micro[ch == 1], minlength=n)[:n].astype(float)
        t = np.arange(n) * mdt
        return t, (par - perp) / (par + 2 * perp), par + perp

    def test_time_resolved_anisotropy_starts_at_r0_with_the_perrin_slope(self):
        """r(0) = r0 and dr/dt|0 = -r0/rho: the regime (t << rho) in which the engine's
        single-Gaussian-kick rotation is exact to first order."""
        r0, rho = 0.4, 2.0
        t, r, w = self._anisotropy_decay(r0=r0, rho=rho)
        sel = t < 0.6 * rho
        # weighted linear fit of log r on the early part
        a, b = np.polyfit(t[sel], np.log(r[sel]), 1, w=np.sqrt(w[sel]))
        self.assertAlmostEqual(math.exp(b), r0, delta=0.03)
        self.assertAlmostEqual(-1.0 / a, rho, delta=0.35 * rho)

    def test_time_resolved_anisotropy_is_exponential_out_to_two_rotational_times(self):
        """r(t) = r0 exp(-t/rho) out to t ~ 2 rho. This A/B found the engine rotating
        the emission dipole by ONE Gaussian kick of variance 2 D_rot * delay per axis
        (exact to first order only; r(2 rho) came out 0.10-0.12 vs 0.054) on
        2026-08-17; the rotation is now sub-stepped so each kick stays small, and
        this test keeps it that way."""
        r0, rho = 0.4, 2.0
        t, r, w = self._anisotropy_decay(r0=r0, rho=rho)
        sel = (t > 1.5 * rho) & (t < 2.5 * rho)
        expect = r0 * np.exp(-t[sel] / rho)
        self.assertLess(np.abs(np.average(r[sel] - expect, weights=w[sel])), 0.02)


# ---------------------------------------------------------------------------
# FCS against PyBroMo and the analytic curve
# ---------------------------------------------------------------------------
def _correlate(macro, dt_macro, tau_max):
    corr = tttrlib.Correlator()
    corr.n_bins = 8
    corr.n_casc = 22
    macro = np.ascontiguousarray(macro, dtype=np.uint64)
    w = np.ones(macro.size)
    corr.set_macrotimes(macro, macro)
    corr.set_weights(w, w)
    corr.run()
    tau = np.asarray(corr.get_x_axis(), float) * dt_macro
    g = np.asarray(corr.get_corr_normalized(), float) - 1.0
    keep = (tau > 0) & (tau <= tau_max) & np.isfinite(g)
    return tau[keep], g[keep]


@pytest.mark.heavy  # ~37 s: a 150k-photon open-volume simulation
class TestFcsAgainstPyBroMo(unittest.TestCase):
    FIX = os.path.join(_REF, "sim_fcs_pybromo_reference.npz")

    def test_g_tau_agrees_with_pybromo_and_the_analytic_curve(self):
        if not os.path.exists(self.FIX):
            raise unittest.SkipTest("PyBroMo fixture missing; run gen_pybromo_reference.py")
        from scipy.optimize import curve_fit
        f = np.load(self.FIX)
        D, w0, wz = float(f["D_um2_ms"]), float(f["w0_um"]), float(f["wz_um"])
        peak = float(f["peak_kcps"])
        c = float(f["n_particles"]) / (8 * float(f["box_half_xy_um"]) ** 2 * float(f["box_half_z_um"]))
        # tttrlib: same D, PSF, brightness; ellipsoid 4/3 pi a^2 b at the same concentration
        a, b = 2.0, 4.0
        pop = c * 4.0 / 3.0 * math.pi * a * a * b
        dt = 0.001                                                # ms
        cfg = {
            "settings": {"dt": dt, "n_ph_max": 150000, "max_windows": 40000000,
                         "seed_diffusion": 3, "seed_emission": 60, "n_channels": 1,
                         "per_molecule_skip": False},
            "box": {"xy": a, "z": b},
            "species": [{"D": D, "q": [peak]}],
            "k_rad": [0.0], "k_nrad": [0.0], "background": [0.0],
            "population": [pop],
            "excitation": {"type": "analytic_gaussian3d", "w0": w0, "z0": wz, "amplitude": 1.0},
        }
        e = tttrlib.SimEngine.from_json(json.dumps(cfg))
        e.run()
        ph = e.photons()
        macro = ph["macro_window"][ph["event_type"] == 0]
        tau_a, g_a = _correlate(macro, dt, 2.0)
        # PyBroMo timestamps: rebin to the same 1 us clock so the correlator sees the same lags
        ts = np.asarray(f["timestamps"], dtype=np.int64)
        unit_ms = float(f["timestamps_unit_s"]) * 1e3
        macro_p = np.floor(ts * unit_ms / dt).astype(np.uint64)
        tau_p, g_p = _correlate(macro_p, dt, 2.0)
        self.assertGreater(tau_a.size, 20)
        self.assertGreater(tau_p.size, 20)

        def model(t, g0, d):
            return g0 / ((1 + 4 * d * t / w0 ** 2) * np.sqrt(1 + 4 * d * t / wz ** 2))

        (g0_a, d_a), _ = curve_fit(model, tau_a, g_a, p0=[g_a[0], D], bounds=([0, 1e-3], [100, 100]))
        (g0_p, d_p), _ = curve_fit(model, tau_p, g_p, p0=[g_p[0], D], bounds=([0, 1e-3], [100, 100]))
        n_eff = c * math.pi ** 1.5 * w0 * w0 * wz
        # both recover the set D and G(0) = 1/(c V_eff)
        self.assertAlmostEqual(d_a / D, 1.0, delta=0.25, msg=f"tttrlib D={d_a}")
        self.assertAlmostEqual(d_p / D, 1.0, delta=0.25, msg=f"PyBroMo D={d_p}")
        self.assertAlmostEqual(g0_a * n_eff, 1.0, delta=0.3, msg=f"tttrlib G0 N={g0_a * n_eff}")
        self.assertAlmostEqual(g0_p * n_eff, 1.0, delta=0.3, msg=f"PyBroMo G0 N={g0_p * n_eff}")
        # and each other, curve against curve, on the common lag grid
        common = np.intersect1d(np.round(tau_a, 9), np.round(tau_p, 9))
        ga = np.interp(common, tau_a, g_a)
        gp = np.interp(common, tau_p, g_p)
        sel = common < 0.5
        self.assertLess(np.sqrt(np.mean((ga[sel] - gp[sel]) ** 2)) / g0_a, 0.15)


# ---------------------------------------------------------------------------
# Record encoding: SimEngine -> PTU on disk -> tttrlib and ptufile agree
# ---------------------------------------------------------------------------
class TestRecordsRoundTripThroughPtufile(unittest.TestCase):
    """The simulated photon stream, encoded as HHT3v2 records in a PTU file, decodes
    to the same (macro, micro, channel) triples in tttrlib and in ``ptufile`` -- an
    independent PicoQuant reader.

    Three PTU *header* defects of ``TTTR::write`` for a from-scratch header were
    found on the way (all in modules/io/pq, none in the record encoding) and
    fixed the same day (2026-08-17); ``_check_header`` below pins the fixed
    header bytes: (1) the auto-added ``Header_End`` tag had an uninitialised
    type/ident tail (ptufile: "invalid tag type ... typecode=0") -- it is now a
    zero-filled tyEmpty8 tag; (2) a caller-supplied ``Header_End`` was written
    before the mandatory tags the writer appends -- it is now always the last
    tag; (3) ``TTResult_NumberOfRecords`` was the number of *events*, not
    records (overflow records uncounted), so a strict reader truncated the
    stream -- it is now patched to the record count after the stream is written.
    """
    _READ_PY = os.path.join(_ROOT, "benchmarks", ".venvs", "read", "bin", "python")
    _TY_F8, _TY_I8, _TY_EMPTY8 = 0x20000008, 0x10000008, 0xFFFF0008

    def _simulate(self):
        n_micro, mdt = 4096, 0.008
        dec = tttrlib.SimDecay.multi_exponential(_vd([1.0]), _vd([3.0]), n_micro, mdt, 0.0)
        s = tttrlib.SimSystem()
        sp = tttrlib.SimSpecies()
        sp.D = 0.0
        sp.q = _vd([60.0, 40.0])
        sp.decay = dec
        s.add_species(sp)
        s.set_rate_matrices(_vd([0.0]), _vd([0.0]))
        s.set_background(_vd([0.0, 0.0]))
        s.add_fluorophore(0.0, 0.0, 0.0, 0, False)
        st = tttrlib.SimIntegrator()
        st.dt = 0.01
        st.n_channels = 2
        st.n_ph_max = 30000
        st.max_windows = 10 ** 9
        st.n_microtime_channels = n_micro
        st.microtime_resolution = mdt
        st.laser_period = n_micro * mdt
        eng = tttrlib.SimEngine(s, tttrlib.SimGrid.gaussian3d(0.5, 1.0, 1.0, 2.0, 0.05, 1.0),
                                tttrlib.VectorSimGrid([]), st)
        eng.run()
        ph = eng.photons()
        photon = ph["event_type"] == 0
        macro_res = st.dt / 1024
        macro = np.floor((ph["macro_window"][photon] * st.dt + ph["arrival_time"][photon]) / macro_res).astype(np.uint64)
        order = np.argsort(macro, kind="stable")
        return (macro[order], ph["micro_time"][photon][order].astype(np.uint16),
                ph["channel"][photon][order].astype(np.int8), macro_res, mdt)

    def _check_header(self, path):
        """The header bytes a strict reader needs: Header_End is the LAST tag,
        typed tyEmpty8 with a zero-filled ident tail, and
        TTResult_NumberOfRecords equals the number of 4-byte records that
        follow the header."""
        import struct
        b = open(path, "rb").read()
        i = b.rfind(b"Header_End")
        self.assertEqual(i, b.find(b"Header_End"))               # exactly one
        self.assertEqual(b[i + 10:i + 32], b"\0" * 22)            # zero-filled ident
        self.assertEqual(struct.unpack("<I", b[i + 36:i + 40])[0], self._TY_EMPTY8)
        j = b.find(b"TTResult_NumberOfRecords")
        self.assertGreater(j, 0)
        self.assertLess(j, i)                                     # before Header_End
        n_rec = struct.unpack("<q", b[j + 40:j + 48])[0]
        self.assertEqual(n_rec, (len(b) - (i + 48)) // 4)
        return n_rec

    def test_ptu_written_from_a_simulation_decodes_identically_in_ptufile(self):
        import tempfile
        macro, micro, rc, macro_res, mdt = self._simulate()
        d = tttrlib.TTTR()
        d.append_events(macro, micro, rc, np.zeros(macro.size, np.int8))
        d.header.tttr_container_type = 0                       # PTU
        d.header.tttr_record_type = 4                          # HHT3v2
        d.header.set_tag("MeasDesc_GlobalResolution", macro_res * 1e-3, self._TY_F8)   # ms -> s
        d.header.set_tag("MeasDesc_Resolution", mdt * 1e-9, self._TY_F8)
        d.header.set_tag("Measurement_Mode", 3, self._TY_I8)
        d.header.set_tag("Measurement_SubMode", 0, self._TY_I8)
        d.header.set_tag("MeasDesc_BinningFactor", 1, self._TY_I8)
        d.header.set_tag("TTResult_SyncRate", int(round(1.0 / (macro_res * 1e-3))), self._TY_I8)
        fd, path = tempfile.mkstemp(suffix=".ptu")
        os.close(fd)
        try:
            self.assertTrue(d.write(path))
            back = tttrlib.TTTR(path, "PTU")                   # tttrlib reads its own output as is
            np.testing.assert_array_equal(np.asarray(back.macro_times), macro)
            np.testing.assert_array_equal(np.asarray(back.micro_times), micro)
            np.testing.assert_array_equal(np.asarray(back.routing_channels), rc)
            if not os.path.exists(self._READ_PY):
                raise unittest.SkipTest("ptufile venv (benchmarks/.venvs/read) not built")
            n_rec = self._check_header(path)
            self.assertGreater(n_rec, macro.size)              # overflow records are counted
            script = (
                "import sys, numpy as np, ptufile\n"
                "f = ptufile.PtuFile(sys.argv[1])\n"
                "r = f.decode_records()\n"
                "ph = r[r['channel'] >= 0]\n"
                "np.savez(sys.argv[2], time=ph['time'].astype(np.int64), dtime=ph['dtime'].astype(np.int64),"
                " channel=ph['channel'].astype(np.int64), n=len(r))\n"
            )
            out = path + ".npz"
            proc = subprocess.run([self._READ_PY, "-c", script, path, out], capture_output=True, text=True)
            if proc.returncode != 0:
                self.fail("ptufile could not decode the written PTU: " + proc.stderr[-800:])
            ref = np.load(out)
            self.assertEqual(int(ref["n"]), n_rec)               # ptufile sees every record
            self.assertEqual(ref["time"].size, macro.size)
            np.testing.assert_array_equal(ref["time"], macro.astype(np.int64))
            np.testing.assert_array_equal(ref["dtime"], micro.astype(np.int64))
            np.testing.assert_array_equal(ref["channel"], rc.astype(np.int64))
        finally:
            for p in (path, path + ".npz"):
                if os.path.exists(p):
                    os.remove(p)

    def test_a_caller_supplied_header_end_is_still_written_last(self):
        """A header carrying its own Header_End (as one read from a real PTU
        does) is written with Header_End as the last tag, after the mandatory
        tags the writer appends, and reads back."""
        import tempfile
        macro, micro, rc, macro_res, mdt = self._simulate()
        d = tttrlib.TTTR()
        d.append_events(macro[:5000], micro[:5000], rc[:5000], np.zeros(5000, np.int8))
        d.header.tttr_container_type = 0
        d.header.tttr_record_type = 4
        d.header.set_tag("MeasDesc_GlobalResolution", macro_res * 1e-3, self._TY_F8)
        d.header.set_tag("MeasDesc_Resolution", mdt * 1e-9, self._TY_F8)
        d.header.set_tag("Header_End", 0, self._TY_EMPTY8)     # supplied EARLY
        d.header.set_tag("Measurement_Mode", 3, self._TY_I8)
        fd, path = tempfile.mkstemp(suffix=".ptu")
        os.close(fd)
        try:
            self.assertTrue(d.write(path))
            self._check_header(path)
            back = tttrlib.TTTR(path, "PTU")
            np.testing.assert_array_equal(np.asarray(back.macro_times), macro[:5000])
        finally:
            if os.path.exists(path):
                os.remove(path)

    def test_spc_export_via_to_tttr_preserves_the_photons(self):
        """``SimEngine.to_tttr`` (SPC-132 encoder) keeps every photon's channel and
        micro-time; complements test_engine.py::test_spc132_encoder_roundtrip."""
        n_micro, mdt = 4096, 0.008
        dec = tttrlib.SimDecay.multi_exponential(_vd([1.0]), _vd([3.0]), n_micro, mdt, 0.0)
        s = tttrlib.SimSystem()
        sp = tttrlib.SimSpecies()
        sp.D = 0.0
        sp.q = _vd([60.0, 40.0])
        sp.decay = dec
        s.add_species(sp)
        s.set_rate_matrices(_vd([0.0]), _vd([0.0]))
        s.set_background(_vd([0.0, 0.0]))
        s.add_fluorophore(0.0, 0.0, 0.0, 0, False)
        st = tttrlib.SimIntegrator()
        st.dt = 0.01
        st.n_channels = 2
        st.n_ph_max = 20000
        st.max_windows = 10 ** 9
        st.n_microtime_channels = n_micro
        st.microtime_resolution = mdt
        st.laser_period = n_micro * mdt
        eng = tttrlib.SimEngine(s, tttrlib.SimGrid.gaussian3d(0.5, 1.0, 1.0, 2.0, 0.05, 1.0),
                                tttrlib.VectorSimGrid([]), st)
        eng.run()
        t = eng.to_tttr(st.dt, 2)
        et = np.asarray(eng.event_type()) == 0
        self.assertEqual(t.get_n_valid_events(), int(et.sum()))
        ch = np.asarray(eng.channel())[et]
        micro = np.asarray(eng.micro_time())[et]
        # per-channel counts and the micro-time histogram survive the SPC encoding
        self.assertEqual(np.bincount(np.asarray(t.routing_channels), minlength=2).tolist(),
                         np.bincount(ch, minlength=2).tolist())
        h_sim = np.bincount(micro, minlength=n_micro)[:n_micro]
        h_back = np.bincount(np.asarray(t.micro_times), minlength=n_micro)[:n_micro]
        # reverse_tac=True mirrors the axis; compare as multisets and as mirrored histograms
        self.assertEqual(sorted(h_sim.tolist()), sorted(h_back.tolist()))


if __name__ == "__main__":
    unittest.main()

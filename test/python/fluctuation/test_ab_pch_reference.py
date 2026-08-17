"""A/B of the photon-counting-histogram kernels (PCH and FIDA) against
independent references.

The model behind both is the same compound-Poisson process: a Poisson number of
molecules, each contributing Poisson counts with a brightness set by where it
sits in the detection profile. Nothing here compares tttrlib against tttrlib;
the references are

* ``scipy.integrate.quad`` on the single-molecule integral
  ``p1(k) = int x^2 Poi(k; eps e^{-2x^2}) dx`` (Chen et al. 1999, Biophys. J.
  77, 553 -- the 3-D Gaussian PCH in the radial variable);
* the probability generating function of a compound Poisson process, inverted
  by NumPy's FFT (Kask et al. 1999, PNAS 96, 13756 -- FIDA), written here
  independently for the PCH profile ``x^2 dx`` and for the FIDA brightness
  profile ``w(x) dx``;
* the analytic factorial moments of that process: ``E[k] = N q <x> + bg``,
  ``Var[k] - E[k] = N q^2 <x^2>``, which for the 3-D Gaussian give the Mandel
  Q ``= eps / (2 sqrt 2)`` -- the textbook gamma factor of a 3DG volume;
* ``pch_mixture`` against ``numpy.convolve`` of its species, ``fida_pch`` with
  no species against ``scipy.stats.poisson`` (background only), and against
  ChiSurf's pure-NumPy ``fida_pch`` (the implementation this was ported from;
  second-tier check, skipped when ChiSurf is absent).

What this A/B found (2026-08-17): ``fida_pch``'s **N is grid-relative**. The
default brightness profile (256 linear bins on [1e-4, 1]) is a poor quadrature
of ``w(x) ~ sqrt(-ln x)/x``: the first bin holds most of the unit-integral
mass as a lump of essentially dark molecules, so for the same physical system
the default-profile ``N`` is ~6.8x the ``N`` of a converged (65536-bin)
profile. The *shape* of P(k) at equal mean agrees to ~3e-3, so fitted
brightnesses are fine and ``N`` absorbs the factor -- but ``N`` is not "the
mean number in the reference volume" of any convention unless the profile is
converged. Pinned below (``TestFidaEqualsPchUnderTheConventionMap``).
"""
import importlib
import os
import sys
import unittest

import numpy as np
from scipy import integrate, special, stats

import tttrlib

_CHISURF_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", "..", "chisurf"))


def _fida_profile(n_bins, x_min):
    flat = np.array(tttrlib.fida_dvdx_gaussian(n_bins, x_min))
    return flat[:n_bins], flat[n_bins:]


def _pgf_pmf(k_max, exponent_fn, oversample=8):
    """P(k) from a PGF exponent E(xi) on the unit circle: G(xi) = exp(E(xi))."""
    m = oversample * (k_max + 1)
    xi = np.exp(-2j * np.pi * np.arange(m) / m)
    g = np.exp(exponent_fn(xi))
    return np.real(np.fft.ifft(g))[: k_max + 1]


class TestPchSingleSpeciesAgainstQuad(unittest.TestCase):
    """The single-molecule P(k) is the radial integral, term by term."""

    def test_matches_adaptive_quadrature(self):
        k_max = 40
        for eps in (0.05, 0.8, 3.0):
            with self.subTest(brightness=eps):
                got = np.array(tttrlib.pch_single_species(k_max, eps))
                ref = np.zeros(k_max + 1)
                for k in range(1, k_max + 1):
                    f = lambda x, k=k: x * x * np.exp(k * np.log(eps * np.exp(-2 * x * x))
                                                        - special.gammaln(k + 1) - eps * np.exp(-2 * x * x))
                    ref[k] = integrate.quad(f, 0.0, 5.0, limit=200)[0]
                ref[0] = 1.0 - ref[1:].sum()
                # the C++ is a 1000-point Riemann sum: 3e-4 relative on the
                # entries that carry mass, absolute on the tail
                np.testing.assert_allclose(got, ref, rtol=3e-4, atol=1e-12)
                self.assertAlmostEqual(got.sum(), 1.0, places=12)


class TestPchOpenSystemAgainstTheCompoundPoissonModel(unittest.TestCase):

    @staticmethod
    def _pgf_reference(k_max, eps, avg_n):
        x = np.linspace(0.0, 5.0, 20001)
        dx = x[1] - x[0]
        lam = eps * np.exp(-2.0 * x * x)

        def E(xi):
            return avg_n * np.trapz((np.exp((xi[:, None] - 1.0) * lam[None, :]) - 1.0) * (x * x)[None, :], dx=dx, axis=1)
        return _pgf_pmf(k_max, E)

    def test_matches_an_independent_pgf_inversion(self):
        k_max = 40
        for eps, n in [(0.8, 1.5), (0.2, 4.0), (2.5, 0.3)]:
            with self.subTest(brightness=eps, avg_n=n):
                got = np.array(tttrlib.pch_open_system(k_max, eps, n))
                ref = self._pgf_reference(k_max, eps, n)
                np.testing.assert_allclose(got, ref, rtol=1e-4, atol=1e-10)

    def test_analytic_moments_and_the_3dg_gamma_factor(self):
        """E[k] = N eps gamma_1 with gamma_1 = int x^2 e^{-2x^2} dx = sqrt(2 pi)/16;
        Mandel Q = (Var - E)/E = eps gamma_2/gamma_1 = eps / (2 sqrt 2)."""
        k_max = 60
        gamma_1 = np.sqrt(2.0 * np.pi) / 16.0
        for eps, n in [(0.8, 1.5), (0.3, 3.0), (1.5, 0.5)]:
            with self.subTest(brightness=eps, avg_n=n):
                p = np.array(tttrlib.pch_open_system(k_max, eps, n))
                k = np.arange(k_max + 1)
                mean = (p * k).sum()
                var = (p * k * k).sum() - mean ** 2
                self.assertAlmostEqual(mean, n * eps * gamma_1, places=9)
                self.assertAlmostEqual((var - mean) / mean, eps / (2.0 * np.sqrt(2.0)), places=9)
                self.assertAlmostEqual(p.sum(), 1.0, places=9)

    def test_mixture_is_the_convolution_of_its_species(self):
        k_max = 40
        b = [0.5, 1.5]
        n = [1.0, 0.4]
        got = np.array(tttrlib.pch_mixture(k_max, b, n))
        ref = np.array(tttrlib.pch_open_system(k_max, b[0], n[0]))
        ref = np.convolve(ref, np.array(tttrlib.pch_open_system(k_max, b[1], n[1])))[: k_max + 1]
        np.testing.assert_allclose(got, ref, rtol=1e-12, atol=1e-15)
        # and equals the PGF of the summed exponents (independent route)
        pg = self._pgf_reference(k_max, b[0], n[0])
        pg = np.convolve(pg, self._pgf_reference(k_max, b[1], n[1]))[: k_max + 1]
        np.testing.assert_allclose(got, pg, rtol=1e-4, atol=1e-10)


class TestFidaAgainstTheGeneratingFunction(unittest.TestCase):

    def test_profile_is_the_3dg_differential_volume(self):
        x, w = _fida_profile(256, 1e-4)
        dx = x[1] - x[0]
        xa = np.linspace(1e-4, 1.0, 256)
        wa = np.sqrt(-np.log(xa)) / xa
        wa /= wa.sum() * dx
        np.testing.assert_allclose(x, xa, rtol=0, atol=1e-15)
        np.testing.assert_allclose(w, wa, rtol=1e-12)
        self.assertAlmostEqual(w.sum() * dx, 1.0, places=12)

    def test_matches_an_independent_pgf_inversion(self):
        k_max = 40
        x, w = _fida_profile(256, 1e-4)
        dx = x[1] - x[0]
        for species, bg in [([(0.5, 1.2), (2.0, 0.3)], 0.2), ([(1.0, 3.0)], 0.0), ([(0.1, 10.0), (0.7, 1.0), (3.0, 0.05)], 0.5)]:
            with self.subTest(species=species, background=bg):
                flat = [v for s in species for v in s]
                got = np.array(tttrlib.fida_pch(k_max, flat, len(species), bg))

                def E(xi):
                    e = np.zeros(len(xi), dtype=complex)
                    for q, n in species:
                        e += n * ((np.exp((xi[:, None] - 1.0) * (q * x[None, :])) - 1.0) * w[None, :]).sum(axis=1) * dx
                    return e + (xi - 1.0) * bg
                ref = _pgf_pmf(k_max, E)
                ref = np.clip(ref, 0.0, None)
                ref /= ref.sum()
                np.testing.assert_allclose(got, ref, rtol=1e-9, atol=1e-13)

    def test_analytic_moments(self):
        k_max = 80
        x, w = _fida_profile(256, 1e-4)
        dx = x[1] - x[0]
        species = [(0.5, 1.2), (2.0, 0.3)]
        bg = 0.2
        p = np.array(tttrlib.fida_pch(k_max, [v for s in species for v in s], 2, bg))
        k = np.arange(k_max + 1)
        mean = (p * k).sum()
        var = (p * k * k).sum() - mean ** 2
        m1 = sum(n * q * (x * w).sum() * dx for q, n in species) + bg
        m2 = sum(n * q * q * (x * x * w).sum() * dx for q, n in species)
        self.assertAlmostEqual(mean, m1, places=10)
        self.assertAlmostEqual(var - mean, m2, places=10)

    def test_background_only_is_poisson(self):
        k_max = 30
        for bg in (0.3, 2.0):
            with self.subTest(background=bg):
                got = np.array(tttrlib.fida_pch(k_max, [], 0, bg))
                ref = stats.poisson.pmf(np.arange(k_max + 1), bg)
                np.testing.assert_allclose(got, ref / ref.sum(), rtol=1e-9, atol=1e-14)

    def test_matches_chisurf_numpy_fallback(self):
        if not os.path.isdir(_CHISURF_ROOT):
            self.skipTest("ChiSurf checkout not found")
        sys.path.insert(0, _CHISURF_ROOT)
        try:
            fida = importlib.import_module("chisurf.core.models.pch.fida")
        except Exception as e:  # pragma: no cover
            self.skipTest(f"chisurf.core.models.pch.fida not importable: {e}")
        finally:
            sys.path.remove(_CHISURF_ROOT)
        fida._HAVE_TTTRLIB = False    # force the NumPy path
        k_max = 40
        species = [(0.5, 1.2), (2.0, 0.3)]
        ref = fida.fida_pch(k_max, species, background=0.2)
        got = np.array(tttrlib.fida_pch(k_max, [v for s in species for v in s], 2, 0.2))
        np.testing.assert_allclose(got, ref, rtol=1e-12, atol=1e-15)


class TestFidaEqualsPchUnderTheConventionMap(unittest.TestCase):
    """The same physical system through both routes. PCH counts molecules per
    ``x^2 dx`` of the radial coordinate; FIDA per ``w(b) db`` of brightness
    ``b = e^{-2x^2}``. Changing variables, ``x^2 dx = sqrt(-ln b) / (4 sqrt2 b) db``,
    so with a unit-integral profile ``w = C sqrt(-ln b)/b`` the FIDA number is
    ``N = avg_n / (4 sqrt2 C)``. That holds when the profile is a converged
    quadrature; the default 256-bin profile is not (see the module docstring)."""

    def test_converged_profile_reproduces_pch(self):
        k_max = 40
        eps, avg_n = 0.7, 1.5
        n_bins = 65536
        x, w = _fida_profile(n_bins, 1e-4)
        dx = x[1] - x[0]
        c_num = 1.0 / ((np.sqrt(-np.log(x)) / x).sum() * dx)   # the profile's own normalization
        n_f = avg_n / (4.0 * np.sqrt(2.0) * c_num)
        p_fida = np.array(tttrlib.fida_pch(k_max, [eps, n_f], 1, 0.0, list(np.concatenate([x, w])), n_bins))
        p_pch = np.array(tttrlib.pch_open_system(k_max, eps, avg_n))
        # brightness below x_min = 1e-4 is cut from the FIDA side; that tail
        # carries ~1e-3 of the counts, so the two agree at that level
        np.testing.assert_allclose(p_fida, p_pch, rtol=5e-3, atol=2e-4)

    def test_default_profile_only_rescales_n(self):
        """Grid dependence of N: at equal mean the shapes agree, and the
        default-profile N is ~6.8x the converged one. Pinned so a change of
        the default profile is noticed."""
        k_max = 30
        q, n_conv = 1.0, 2.0
        n_bins = 65536
        x, w = _fida_profile(n_bins, 1e-4)
        p_conv = np.array(tttrlib.fida_pch(k_max, [q, n_conv], 1, 0.0, list(np.concatenate([x, w])), n_bins))
        p_def = np.array(tttrlib.fida_pch(k_max, [q, n_conv], 1, 0.0))
        k = np.arange(k_max + 1)
        m_conv, m_def = (p_conv * k).sum(), (p_def * k).sum()
        ratio = m_conv / m_def
        self.assertGreater(ratio, 5.0)
        self.assertLess(ratio, 9.0)
        p_def_rescaled = np.array(tttrlib.fida_pch(k_max, [q, n_conv * ratio], 1, 0.0))
        np.testing.assert_allclose(p_def_rescaled, p_conv, rtol=1e-2, atol=1e-5)


class TestPchAgainstPysimfcs(unittest.TestCase):
    """An implementation that is not ours: Jay Unruh's ``pysimfcs``
    (``analysis_utils.p3DG`` / ``singlespecies``, the NumPy port of his ImageJ
    Jay_Plugins PCH), which evaluates Chen et al. 1999 eq. 16 with the
    incomplete gamma function and references the particle number to the PSF
    volume V_PSF = (pi/2)^{3/2} w0^2 z0 (Chen's N_PSF).

    tttrlib's radial x^2 form has the same k >= 1 shape (ratio constant to
    1e-5) but references N to V0 = 4 pi w0^3, so tttrlib's ``avg_n`` is
    N_PSF * 16/sqrt(2 pi) = 6.383 N_PSF; with that conversion the open-system
    histograms agree to 5e-5 relative (pysimfcs' own dx = 0.01 sum sets the
    floor). Nothing about the shape or the brightness depends on the
    convention -- see the header. Skips when junk/pysimfcs is absent."""

    PYSIMFCS = os.path.join(_CHISURF_ROOT, "junk", "pysimfcs")
    F = np.sqrt(2.0 * np.pi) / 16.0            # N_PSF / N_tttrlib

    def _au(self):
        if not os.path.isdir(self.PYSIMFCS):
            self.skipTest("junk/pysimfcs not present")
        sys.path.insert(0, self.PYSIMFCS)
        try:
            return importlib.import_module("analysis_utils")
        finally:
            sys.path.pop(0)

    def test_single_particle_shape_and_reference_volume(self):
        au = self._au()
        for eps in (0.3, 0.8, 2.5):
            with self.subTest(eps=eps):
                p1 = np.asarray(tttrlib.pch_single_species(10, eps))
                ref = np.array([au.p3DG(k, eps) for k in range(1, 11)])
                ratio = p1[1:9] / ref[:8]
                np.testing.assert_allclose(ratio, self.F, rtol=2e-5)

    def test_open_system_with_n_converted_to_the_psf_volume(self):
        au = self._au()
        for eps, n_psf in ((0.3, 0.5), (0.8, 2.0), (2.5, 0.5)):
            with self.subTest(eps=eps, n_psf=n_psf):
                ref = au.singlespecies(eps, n_psf, nlength=60, klength=14)
                ours = np.asarray(tttrlib.pch_open_system(14, eps, n_psf / self.F, 60))
                self.assertLess(float(np.abs(ref - ours).max()), 1e-5)
                sel = ref > 1e-6
                self.assertLess(float(np.max(np.abs(ref - ours)[sel] / ref[sel])), 1e-4)


if __name__ == "__main__":
    unittest.main()

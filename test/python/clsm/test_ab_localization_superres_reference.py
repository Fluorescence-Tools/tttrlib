"""A/B of the localization fit and the array-detector super-resolution helpers
against scipy / skimage / numpy references.

* `localization.fit2DGaussian` minimises the Poisson deviance
  ``sum(model - data*log(model)) / n`` of ``A*exp(-(x-x0)^2/2s^2 -(y-y0)^2/2(s*eps)^2)
  + bg`` with the library's own L-BFGS. The reference is the *same* objective
  handed to `scipy.optimize.minimize` (L-BFGS-B, analytic-free) from the same
  start: two different minimisers of one convex-enough bowl must meet at the
  same optimum, and both must sit at the true emitter on noise-free data.
* `CLSMSuperRes.airy_psf` is compared to ``(2 J1(v)/v)^2`` with
  ``v = 2*pi*NA*r/lambda`` from `scipy.special.j1` -- the whole array, not
  just the first zero (that is `test_ism_psf_model.py`'s check).
* `CLSMSuperRes.apr_reconstruction` = shift_vectors (already pinned to
  `skimage.registration.phase_cross_correlation` in
  `test_clsm_superres_ism_arrays.py`) + Fourier registration + sum. The
  registration step is BrightEyes-ISM `Reassignment(mode='fourier')` on a
  half-width zero-padded canvas with a non-negativity clamp; the reference here
  is exactly that with `scipy.ndimage.fourier_shift`, fed the library's own
  shift vectors so the two steps are pinned separately.
* `CLSMSuperRes.temporal_combine` AVG / VAR / TAC2 / INT against numpy's mean,
  population variance, lag-1 second-order autocumulant
  ``E[X_t X_{t+1}] - E[X]^2`` and (INT) the mean again.

Pre-existing A/Bs (not repeated): eSRRF `rgc_map`/`temporal_combine` vs the
NanoJ-eSRRF transcription (`test_clsm_superres_nanoj_ab.py`, dev-only oracle),
`shift_vectors` and FRC vs skimage / BrightEyes (`test_clsm_superres_ism_arrays.py`),
s2ISM (`test_clsm_superres_s2ism.py`), SOFISM (`test_clsm_superres_sofism.py`),
`detector_grid` lattices and the vectorial PSF's scalar limit
(`test_ism_psf_model.py`).
"""
import unittest

import numpy as np
import pytest
import tttrlib

scipy_optimize = pytest.importorskip("scipy.optimize")
scipy_special = pytest.importorskip("scipy.special")
ndimage = pytest.importorskip("scipy.ndimage")


# ---------------------------------------------------------------------------
# 2-D Gaussian localization vs scipy on the same Poisson deviance
# ---------------------------------------------------------------------------

def _model(theta, xlen, ylen):
    x0, y0, A, sigma, eps, bg = theta
    y, x = np.mgrid[0:ylen, 0:xlen]
    tx = 0.5 / (sigma * sigma)
    ty = 0.5 / (sigma * sigma * eps * eps)
    return A * np.exp(-((x - x0) ** 2) * tx - ((y - y0) ** 2) * ty) + bg


def _deviance(theta, data):
    """localization::W2DG -- terms independent of the model dropped."""
    m = _model(theta, data.shape[1], data.shape[0])
    ok = (data > 1e-12) & (m > 1e-12)
    w = np.where(ok, m - data * np.log(np.where(m > 0, m, 1.0)), m)
    return w.sum() / data.size


def _fit_lib(img, start, fit_bg=True, fix_eps=True):
    v = [0.0] * 18
    v[0], v[1], v[2], v[3], v[4], v[5] = start
    v[14] = 0 if fit_bg else 1
    v[15] = 1 if fix_eps else 0
    v[16] = 0
    out = tttrlib.VectorDouble(list(map(float, v)))
    arr = np.ascontiguousarray(np.asarray(img, dtype=float))
    rc = tttrlib.localization.fit2DGaussian_numpy(out, arr)
    return rc, np.array(list(out))[:6]


def _fit_scipy(img, start, fit_bg=True, fix_eps=True):
    start = np.asarray(start, float)
    free = [0, 1, 2, 3] + ([4] if not fix_eps else []) + ([5] if fit_bg else [])
    theta0 = start.copy()

    def f(p):
        th = theta0.copy()
        th[free] = p
        return _deviance(th, img)

    bounds = []
    for k in free:
        if k in (0,):
            bounds.append((0.0, img.shape[1] - 1.0))
        elif k in (1,):
            bounds.append((0.0, img.shape[0] - 1.0))
        elif k == 3:
            bounds.append((0.3, None))
        elif k == 4:
            bounds.append((0.2, 5.0))
        else:
            bounds.append((0.0, None))
    r = scipy_optimize.minimize(f, start[free], method="L-BFGS-B",
                                bounds=bounds,
                                options=dict(ftol=1e-15, gtol=1e-10, maxiter=5000))
    th = theta0.copy()
    th[free] = r.x
    return th


class TestGaussianLocalizationAgainstScipy(unittest.TestCase):

    def _case(self, x0, y0, A, sigma, bg, eps=1.0, seed=None, n=15):
        y, x = np.mgrid[0:n, 0:n]
        img = _model((x0, y0, A, sigma, eps, bg), n, n)
        if seed is not None:
            img = np.random.default_rng(seed).poisson(img).astype(float)
        return img

    def test_noise_free_optimum_is_the_emitter_for_both_minimisers(self):
        for x0, y0 in [(7.0, 7.0), (7.4, 6.6), (5.2, 9.3), (9.9, 4.1)]:
            with self.subTest(x0=x0, y0=y0):
                img = self._case(x0, y0, 200.0, 2.0, 5.0)
                start = (7.0, 7.0, 150.0, 1.5, 1.0, 5.0)
                rc, ours = _fit_lib(img, start)
                theirs = _fit_scipy(img, start)
                self.assertEqual(rc, 1)
                truth = np.array([x0, y0, 200.0, 2.0, 1.0, 5.0])
                np.testing.assert_allclose(ours[[0, 1, 3]], truth[[0, 1, 3]], atol=2e-3)
                np.testing.assert_allclose(theirs[[0, 1, 3]], truth[[0, 1, 3]], atol=2e-3)
                np.testing.assert_allclose(ours[[2, 5]], truth[[2, 5]], rtol=2e-3)

    def test_poisson_noise_both_minimisers_reach_the_same_deviance_minimum(self):
        """On noisy data the optimum is not the truth; what must agree is the
        minimum found. The two minimisers stop with different tolerances, so
        the pin is on the objective value (to 1e-9 of the deviance) and on the
        position to 1e-2 px, which is well inside a photon-limited CRLB here."""
        for seed in range(6):
            with self.subTest(seed=seed):
                img = self._case(7.3, 6.8, 300.0, 1.8, 8.0, seed=seed)
                start = (7.0, 7.0, 200.0, 1.5, 1.0, 5.0)
                rc, ours = _fit_lib(img, start)
                theirs = _fit_scipy(img, start)
                self.assertEqual(rc, 1)
                d_ours = _deviance(ours, img)
                d_theirs = _deviance(theirs, img)
                self.assertLess(abs(d_ours - d_theirs), 1e-9 * abs(d_theirs) + 1e-12,
                                f"deviance {d_ours} vs {d_theirs}")
                np.testing.assert_allclose(ours[:2], theirs[:2], atol=1e-2)
                np.testing.assert_allclose(ours[3], theirs[3], atol=1e-2)

    def test_elliptical_fit_agrees_with_scipy(self):
        img = self._case(7.2, 7.6, 250.0, 1.6, 4.0, eps=1.5)
        start = (7.0, 7.0, 200.0, 1.4, 1.2, 4.0)
        rc, ours = _fit_lib(img, start, fix_eps=False)
        theirs = _fit_scipy(img, start, fix_eps=False)
        self.assertEqual(rc, 1)
        np.testing.assert_allclose(ours[[0, 1, 3, 4]], [7.2, 7.6, 1.6, 1.5], atol=5e-3)
        np.testing.assert_allclose(theirs[[0, 1, 3, 4]], [7.2, 7.6, 1.6, 1.5], atol=5e-3)


# ---------------------------------------------------------------------------
# Airy PSF vs scipy.special.j1
# ---------------------------------------------------------------------------

class TestAiryPsfAgainstScipy(unittest.TestCase):

    def test_whole_array_matches_the_analytic_pattern(self):
        for (ny, nx), na, wl, px in [((41, 41), 1.4, 520.0, 20.0),
                                     ((33, 47), 1.0, 640.0, 35.0),
                                     ((64, 64), 1.2, 488.0, 15.0)]:
            with self.subTest(shape=(ny, nx), na=na):
                got = np.asarray(tttrlib.CLSMSuperRes.airy_psf((ny, nx), na, wl, px))
                yy, xx = np.mgrid[0:ny, 0:nx]
                cy, cx = (ny - 1) / 2.0, (nx - 1) / 2.0
                r = np.hypot((yy - cy) * px, (xx - cx) * px)
                v = 2 * np.pi * na * r / wl
                with np.errstate(invalid="ignore", divide="ignore"):
                    ref = np.where(v > 0, (2 * scipy_special.j1(v) / np.where(v > 0, v, 1)) ** 2, 1.0)
                # the library may centre on a pixel or on the array centre;
                # accept whichever centre it uses by re-deriving it from argmax
                if got.shape == ref.shape and not np.allclose(got, ref, atol=1e-9):
                    iy, ix = np.unravel_index(np.argmax(got), got.shape)
                    r = np.hypot((yy - iy) * px, (xx - ix) * px)
                    v = 2 * np.pi * na * r / wl
                    with np.errstate(invalid="ignore", divide="ignore"):
                        ref = np.where(v > 0, (2 * scipy_special.j1(v) / np.where(v > 0, v, 1)) ** 2, 1.0)
                np.testing.assert_allclose(got, ref, atol=1e-9)


# ---------------------------------------------------------------------------
# APR: registration + sum vs scipy fourier_shift on the padded canvas
# ---------------------------------------------------------------------------

def _ism_cube(side=3, pitch=2.0, sigma=2.5, n=40, seed=0):
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:n, 0:n]
    obj = np.zeros((n, n))
    for _ in range(6):
        cy, cx = rng.uniform(8, n - 8, 2)
        obj += rng.uniform(50, 200) * np.exp(-((yy - cy) ** 2 + (xx - cx) ** 2) / (2 * 1.2 ** 2))
    offs = np.array([(i - side // 2, j - side // 2) for i in range(side) for j in range(side)], float) * pitch
    cube = np.zeros((side * side, n, n))
    for k, (dy, dx) in enumerate(offs):
        sh = ndimage.shift(obj, (dy / 2, dx / 2), order=1, mode="constant")
        cube[k] = ndimage.gaussian_filter(sh, sigma) + 1.0
    return cube


class TestAprAgainstScipy(unittest.TestCase):

    def test_registration_and_sum_reproduce_the_reference_from_the_same_shifts(self):
        cube = _ism_cube()
        n_det, ny, nx = cube.shape
        shifts = np.asarray(tttrlib.CLSMSuperRes.shift_vectors(cube, usf=10, ref_idx=-1))
        got = np.asarray(tttrlib.CLSMSuperRes.apr_reconstruction(cube, usf=10, ref_idx=-1))[0]
        # APR_lib.Reassignment(mode='fourier'): scipy fourier_shift on the
        # unpadded transform (circular), negatives clamped, summed. (Until
        # 2026-08-17 tttrlib shifted on a canvas zero-padded to twice the frame:
        # 4x the FFT work and a different answer wherever content reached an
        # edge; the reference's estimator is now reproduced exactly.)
        ref = np.zeros((ny, nx))
        for k in range(n_det):
            dy, dx = shifts[k]
            img = cube[k]
            if abs(dx) > 1e-9 or abs(dy) > 1e-9:
                img = np.real(np.fft.ifftn(ndimage.fourier_shift(np.fft.fftn(img), (dy, dx))))
            ref += np.clip(img, 0.0, None)
        np.testing.assert_allclose(got, ref, rtol=1e-9, atol=1e-9 * ref.max())
        # circular: flux conserved to the clamp
        self.assertLess(abs(got.sum() - cube.sum()) / cube.sum(), 5e-3)


# ---------------------------------------------------------------------------
# temporal_combine vs numpy
# ---------------------------------------------------------------------------

class TestTemporalCombineAgainstNumpy(unittest.TestCase):

    def test_modes(self):
        rng = np.random.default_rng(1)
        stack = rng.gamma(2.0, 3.0, size=(12, 9, 11))
        got = {m: np.asarray(tttrlib.CLSMSuperRes.temporal_combine(stack, m))
               for m in ("AVG", "VAR", "TAC2", "INT")}
        np.testing.assert_allclose(got["AVG"], stack.mean(0), rtol=1e-12)
        # INT is the frame mean too (NanoJ's "interpolated intensity")
        np.testing.assert_allclose(got["INT"], stack.mean(0), rtol=1e-12)
        d = stack - stack.mean(0)
        np.testing.assert_allclose(got["VAR"], (d ** 2).mean(0), rtol=1e-12)
        # TAC2 = E[X(t) X(t+1)] - E[X]^2, the lag-1 product averaged over the
        # n-1 pairs (NanoJ's second-order auto-cumulant)
        tac2 = (stack[:-1] * stack[1:]).mean(0) - stack.mean(0) ** 2
        np.testing.assert_allclose(got["TAC2"], tac2, rtol=1e-11, atol=1e-11)


if __name__ == "__main__":
    unittest.main()

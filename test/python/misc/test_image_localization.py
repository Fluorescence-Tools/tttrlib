"""Tests for the 2D-Gaussian localization fit.

These were written alongside the conversion to smooth bounds + exact gradients,
and pin the properties that conversion had to preserve: that a known emitter is
recovered, that the fit is stable when a parameter starts on or outside a bound
(the case the old midpoint-teleport handled incorrectly), and that the fitted
parameters stay physical.
"""
import numpy as np
import pytest

import tttrlib


def _render(x0, y0, amplitude, sigma, bg, xlen=15, ylen=15, eps=1.0, seed=None):
    """Render a 2D Gaussian on a pixel grid, optionally with Poisson noise."""
    y, x = np.mgrid[0:ylen, 0:xlen]
    tx = 0.5 / (sigma * sigma)
    ty = 0.5 / (sigma * sigma * eps * eps)
    img = amplitude * np.exp(-((x - x0) ** 2) * tx - ((y - y0) ** 2) * ty) + bg
    if seed is not None:
        img = np.random.default_rng(seed).poisson(img).astype(float)
    return img


def _make_vars(x0, y0, amplitude, sigma, bg, eps=1.0, n_gauss=0,
               fit_bg=0, fix_eps=1):
    """Build the 18-element parameter vector fit2DGaussian expects."""
    v = [0.0] * 18
    v[0], v[1], v[2], v[3], v[4], v[5] = x0, y0, amplitude, sigma, eps, bg
    v[14] = fit_bg     # 1 -> background fixed
    v[15] = fix_eps    # 1 -> ellipticity fixed
    v[16] = n_gauss    # 0/1/2 -> one/two/three Gaussians
    return v


def _fit(img, v):
    """Run the fit; ``vars`` is an in/out vector so it must be a VectorDouble."""
    out = tttrlib.VectorDouble(list(map(float, v)))
    arr = np.ascontiguousarray(np.asarray(img, dtype=float))
    rc = tttrlib.localization.fit2DGaussian_numpy(out, arr)
    return rc, list(out)


# ---------------------------------------------------------------------------
# Recovery
# ---------------------------------------------------------------------------


@pytest.mark.parametrize("x0,y0", [(7.0, 7.0), (7.4, 6.6), (5.2, 9.3)])
def test_recovers_noise_free_emitter(x0, y0):
    """A clean Gaussian must be localized to well below a pixel."""
    img = _render(x0, y0, 200.0, 2.0, 5.0)
    rc, out = _fit(img, _make_vars(7.0, 7.0, 150.0, 1.5, 5.0))

    assert rc == 1
    assert abs(out[0] - x0) < 0.15, f"x0: got {out[0]}, want {x0}"
    assert abs(out[1] - y0) < 0.15, f"y0: got {out[1]}, want {y0}"
    assert abs(out[3] - 2.0) < 0.3, f"sigma: got {out[3]}"


def test_recovers_emitter_under_poisson_noise():
    img = _render(7.3, 6.9, 400.0, 2.0, 8.0, seed=42)
    rc, out = _fit(img, _make_vars(7.0, 7.0, 300.0, 1.8, 8.0))

    assert rc == 1
    assert abs(out[0] - 7.3) < 0.4
    assert abs(out[1] - 6.9) < 0.4


def test_fitted_parameters_stay_physical():
    """sigma, ellipticity and background are mapped through exp(), so the fit
    can never return a non-positive value for them."""
    img = _render(7.0, 7.0, 200.0, 2.0, 5.0, seed=1)
    rc, out = _fit(img, _make_vars(7.0, 7.0, 150.0, 1.5, 5.0, fit_bg=0))

    assert rc == 1
    assert out[3] > 0.0, "sigma must stay positive"
    assert out[4] > 0.0, "ellipticity must stay positive"
    assert out[5] > 0.0, "background must stay positive"


def test_position_stays_inside_the_image():
    """Positions are mapped through a logistic, so they cannot leave the grid."""
    img = _render(7.0, 7.0, 200.0, 2.0, 5.0)
    rc, out = _fit(img, _make_vars(7.0, 7.0, 150.0, 1.5, 5.0))
    assert rc == 1
    assert 0.0 <= out[0] <= 15.0
    assert 0.0 <= out[1] <= 15.0


# ---------------------------------------------------------------------------
# The case the old midpoint-teleport bound handling got wrong
# ---------------------------------------------------------------------------


@pytest.mark.parametrize("start_x", [0.0, 15.0, -3.0, 18.0])
def test_start_on_or_outside_a_bound_still_converges(start_x):
    """Starting at or beyond the edge must not throw the fit to the midpoint.

    The previous bound handling reset an out-of-range parameter to the middle of
    the range, which is discontinuous: a central-difference step straddling the
    bound compared f(15.0) with f(7.5). Positions are now mapped through a
    logistic, so the start is merely a large coordinate in the free space.
    """
    true_x, true_y = 4.5, 7.0
    img = _render(true_x, true_y, 300.0, 2.0, 5.0)
    rc, out = _fit(img, _make_vars(start_x, 7.0, 250.0, 2.0, 5.0))

    assert rc == 1
    assert 0.0 <= out[0] <= 15.0
    # it must not have parked at the midpoint of the range
    assert abs(out[0] - 7.5) > 1e-6 or abs(true_x - 7.5) < 0.5


def test_repeated_fits_are_deterministic():
    img = _render(7.2, 6.8, 250.0, 2.0, 5.0, seed=3)
    _, a = _fit(img, _make_vars(7.0, 7.0, 200.0, 1.8, 5.0))
    _, b = _fit(img, _make_vars(7.0, 7.0, 200.0, 1.8, 5.0))
    np.testing.assert_allclose(a[:12], b[:12], rtol=0, atol=0)


# ---------------------------------------------------------------------------
# Multi-emitter models
# ---------------------------------------------------------------------------


def test_two_gaussian_model_runs_and_separates_emitters():
    img = _render(4.5, 7.0, 250.0, 1.6, 5.0) + _render(10.5, 7.0, 250.0, 1.6, 0.0)
    v = _make_vars(4.0, 7.0, 200.0, 1.6, 5.0, n_gauss=1)
    v[6], v[7], v[8] = 11.0, 7.0, 200.0
    rc, out = _fit(img, v)

    assert rc == 1
    got = sorted([out[0], out[6]])
    assert abs(got[0] - 4.5) < 0.6, f"first emitter: {got}"
    assert abs(got[1] - 10.5) < 0.6, f"second emitter: {got}"


def test_three_gaussian_model_runs():
    img = (_render(4.0, 4.0, 200.0, 1.5, 5.0)
           + _render(11.0, 4.0, 200.0, 1.5, 0.0)
           + _render(7.5, 11.0, 200.0, 1.5, 0.0))
    v = _make_vars(4.0, 4.0, 180.0, 1.5, 5.0, n_gauss=2)
    v[6], v[7], v[8] = 11.0, 4.0, 180.0
    v[9], v[10], v[11] = 7.5, 11.0, 180.0
    rc, out = _fit(img, v)

    assert rc == 1
    for i in (0, 1, 6, 7, 9, 10):
        assert np.isfinite(out[i])
    assert 0.0 <= out[0] <= 15.0 and 0.0 <= out[9] <= 15.0


def test_rejects_ragged_input():
    rc = tttrlib.localization.fit2DGaussian(
        tttrlib.VectorDouble([0.0] * 18),
        tttrlib.VectorDouble_2D([tttrlib.VectorDouble([1.0, 2.0, 3.0]),
                                 tttrlib.VectorDouble([1.0, 2.0])]))
    assert rc == -1

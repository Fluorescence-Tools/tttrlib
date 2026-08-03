"""
The simulated ISM PSF the array-detector examples and notebooks rest on.

`prototype/esrrf/simulate` is a *scalar* model. The vectorial calculation in
BrightEyes-ISM `PSF_sim` needs torch, psf_generator and zernikepy, so it cannot
be run here; what can be checked is that the detector geometry reproduces the
reference's lattices exactly, and that the optical model agrees with the
analytic scalar diffraction result it claims to approximate.

That second check is the point: every ISM result validated against this
simulator inherits its errors, so the size of the Gaussian approximation's
error is worth knowing rather than assuming.
"""

import sys
from pathlib import Path

import numpy as np
import pytest

PROTOTYPE = Path(__file__).parents[3] / "prototype" / "esrrf"
if str(PROTOTYPE) not in sys.path:
    sys.path.insert(0, str(PROTOTYPE))

from simulate import airy_psf, detector_grid, generate_ism_psf  # noqa: E402


def test_detector_grid_matches_the_reference_lattices():
    """
    Ported from BrightEyes-ISM detector.rect_grid / hex_grid. A 5-per-side
    hexagonal grid must give 23 elements -- the SPAD23G array the CW-SOFISM
    work uses -- and 7-per-side must give 45, which is the length the
    reference's hex_to_airy index table assumes.
    """
    assert len(detector_grid(5, "rect")) == 25
    assert len(detector_grid(5, "hex")) == 23
    assert len(detector_grid(7, "rect")) == 49
    assert len(detector_grid(7, "hex")) == 45

    rect = detector_grid(5, "rect")
    assert np.allclose(rect.mean(axis=0), 0.0)          # centred
    assert np.allclose(np.sort(np.unique(rect[:, 0])), [-2, -1, 0, 1, 2])

    # hexagonal rows are offset by half an element and spaced by sqrt(3)/2
    hexg = detector_grid(5, "hex")
    columns = np.unique(np.round(hexg[:, 0], 9))
    assert np.allclose(np.diff(columns), 0.5 * np.sqrt(3))

    with pytest.raises(ValueError):
        detector_grid(5, "triangular")


def test_airy_psf_reproduces_the_analytic_diffraction_limit():
    """The first zero sits at 0.61 lambda / NA and the FWHM at 0.51 lambda / NA."""
    na, wavelength, px = 1.4, 520.0, 2.0
    n = 401
    psf = airy_psf((n, n), na, wavelength, px)

    assert psf[n // 2, n // 2] == pytest.approx(1.0)

    profile = psf[n // 2, n // 2:]
    radius_nm = np.arange(len(profile)) * px

    # search well past the expected zero -- a window that stops short of it
    # just returns its own edge
    window = radius_nm < 2.0 * 0.61 * wavelength / na
    first_zero = radius_nm[window][np.argmin(profile[window])]
    assert first_zero == pytest.approx(0.61 * wavelength / na, rel=0.02)

    half = np.argmax(profile < 0.5)
    fwhm = 2 * radius_nm[half]
    assert fwhm == pytest.approx(0.51 * wavelength / na, rel=0.05)


def test_gaussian_approximation_error_against_airy():
    """
    How good is the Gaussian? Matched at the centre it tracks the core closely
    but has no wings at all, which is exactly where the out-of-focus background
    and the crosstalk between neighbouring elements live. This pins the size of
    that error so a result relying on the Gaussian can be judged.
    """
    na, wavelength, px, n = 1.4, 520.0, 4.0, 129
    airy = airy_psf((n, n), na, wavelength, px)

    sigma_px = ((0.5 * wavelength / na) / 2.35482) / px
    y, x = np.mgrid[0:n, 0:n]
    r2 = (x - (n - 1) / 2) ** 2 + (y - (n - 1) / 2) ** 2
    gauss = np.exp(-r2 / (2 * sigma_px ** 2))

    core = np.sqrt(r2) * px < 0.3 * wavelength / na
    assert np.abs(airy - gauss)[core].max() < 0.12, "the cores should agree closely"

    # Beyond the first zero the Gaussian has essentially nothing left while the
    # Airy still carries its rings: measured, the Airy holds ~43x more energy
    # out there. That factor is the error budget for anything judged on a
    # Gaussian-simulated background or on element-to-element crosstalk.
    wings = np.sqrt(r2) * px > 0.8 * wavelength / na
    assert airy[wings].sum() > 20 * gauss[wings].sum()


@pytest.mark.parametrize("geometry,expected", [("rect", 25), ("hex", 23)])
@pytest.mark.parametrize("model", ["gaussian", "airy"])
def test_generate_ism_psf_geometry_and_model(geometry, expected, model):
    psf = generate_ism_psf(n_det=5, geometry=geometry, model=model,
                           nx=48, ny=48, pixel_size_nm=20.0)
    assert psf["channel_psfs"].shape == (expected, 48, 48)
    assert psf["detector_offsets"].shape == (expected, 2)
    assert np.isfinite(psf["sum_psf"]).all()
    # each element's PSF is normalized, and the array is centred
    assert np.allclose(psf["channel_psfs"].sum(axis=(1, 2)), 1.0)
    assert np.allclose(psf["detector_offsets"].mean(axis=0), 0.0, atol=1e-9)


def test_reassignment_sharpens_the_simulated_psf():
    """
    The property every ISM reconstruction in the suite depends on: shifting each
    element's PSF back by half its offset must produce a narrower effective PSF
    than the plain sum.
    """
    psf = generate_ism_psf(n_det=5, nx=64, ny=64, pixel_size_nm=15.0)

    def width(img):
        yy, xx = np.mgrid[0:img.shape[0], 0:img.shape[1]]
        tot = img.sum()
        my, mx = (img * yy).sum() / tot, (img * xx).sum() / tot
        return np.sqrt((img * ((xx - mx) ** 2 + (yy - my) ** 2)).sum() / tot / 2)

    assert width(psf["reassigned_psf"]) < width(psf["sum_psf"])


def test_vectorial_psf_converges_to_the_scalar_limit():
    """
    Richards-Wolf must reduce to the Airy pattern as the aperture angle shrinks.
    That is the check that the integral, the apodization factor and the
    normalization are all right, since at low NA the answer is known.
    """
    from simulate import vectorial_psf

    for na, tol in ((0.1, 1e-3), (0.3, 5e-3)):
        vec = vectorial_psf((129, 129), na, 520.0, 40.0, n_immersion=1.518)
        scalar = airy_psf((129, 129), na, 520.0, 40.0)
        assert np.abs(vec - scalar).max() < tol, f"NA {na}"


def test_vectorial_psf_elongates_along_the_polarization_axis():
    """
    The reason this exists. At NA 1.4 the longitudinal field is not negligible,
    and with linear illumination the focal spot is measurably longer along the
    polarization axis -- an asymmetry no scalar or Gaussian model can produce.
    Circular illumination must stay radially symmetric.
    """
    from simulate import vectorial_psf

    n, px = 161, 8.0

    def fwhm(img, axis):
        profile = img[n // 2, :] if axis == "x" else img[:, n // 2]
        radius = np.arange(len(profile)) * px
        below = np.where(profile < 0.5)[0]
        return 2 * abs(radius[below[below > n // 2][0]] - radius[n // 2])

    linear = vectorial_psf((n, n), 1.4, 520.0, px, polarization="x")
    circular = vectorial_psf((n, n), 1.4, 520.0, px, polarization="circular")

    # elongated along x by about a third
    assert fwhm(linear, "x") / fwhm(linear, "y") > 1.2
    # and 'y' polarization elongates the other way
    linear_y = vectorial_psf((n, n), 1.4, 520.0, px, polarization="y")
    assert fwhm(linear_y, "y") / fwhm(linear_y, "x") > 1.2
    # circular is symmetric
    assert fwhm(circular, "x") == pytest.approx(fwhm(circular, "y"))

    # and the scalar model is optimistic even about the symmetric case
    assert fwhm(circular, "x") > 1.1 * fwhm(airy_psf((n, n), 1.4, 520.0, px), "x")


def test_vectorial_psf_rejects_impossible_optics():
    from simulate import vectorial_psf

    with pytest.raises(ValueError):
        vectorial_psf((33, 33), 1.6, 520.0, 20.0, n_immersion=1.518)
    with pytest.raises(ValueError):
        vectorial_psf((33, 33), 1.2, 520.0, 20.0, polarization="radial")


@pytest.mark.parametrize("geometry,expected", [("rect", 25), ("hex", 23)])
def test_generate_ism_psf_vectorial(geometry, expected):
    psf = generate_ism_psf(n_det=5, geometry=geometry, model="vectorial",
                           nx=40, ny=40, pixel_size_nm=25.0, polarization="x")
    assert psf["channel_psfs"].shape == (expected, 40, 40)
    assert np.allclose(psf["channel_psfs"].sum(axis=(1, 2)), 1.0)

"""
Array-detector (ISM) reconstructions in CLSMSuperRes, against known answers.

The reference implementation is BrightEyes-ISM (APR_lib, FocusISM_lib). Each
test here builds a cube whose answer is known analytically -- the detector shift
vectors, the photon flux, the in-focus fraction -- rather than asserting on the
shape of a reconstruction of random noise.
"""

import tempfile

import numpy as np
import pytest

import tttrlib

DET_SIDE = 5
N_DET = DET_SIDE * DET_SIDE
CENTRE = N_DET // 2
# The shift estimate apodizes with a Hann window, which biases a spot displaced
# towards the edge of a small field. 64 px keeps that below a twentieth of a pixel.
N = 64


def _ism_cube(pitch=1.6, sigma=2.0, n=N, side=DET_SIDE):
    """
    A point object seen by a square detector array: element k sees the object
    through a PSF centred half-way to its own offset, which is the pixel
    reassignment ISM exists to undo.

    Returns the (n_det, n, n) cube and the (n_det, 2) true (dy, dx) offsets of
    the per-element PSF centres.
    """
    yy, xx = np.mgrid[0:n, 0:n]
    cube = np.zeros((side * side, n, n))
    offsets = np.zeros((side * side, 2))
    for row in range(side):
        for col in range(side):
            k = row * side + col
            dx = (col - (side - 1) / 2) * pitch
            dy = (row - (side - 1) / 2) * pitch
            offsets[k] = (dy / 2, dx / 2)
            cube[k] = np.exp(
                -(((xx - (n / 2 + dx / 2)) ** 2 + (yy - (n / 2 + dy / 2)) ** 2)
                  / (2 * sigma ** 2))
            )
    return cube, offsets


def _spot_width(img):
    yy, xx = np.mgrid[0:img.shape[0], 0:img.shape[1]]
    tot = img.sum()
    my = (img * yy).sum() / tot
    mx = (img * xx).sum() / tot
    var = (img * ((xx - mx) ** 2 + (yy - my) ** 2)).sum() / tot / 2.0
    return 2.3548 * np.sqrt(var)


def test_shift_vectors_recover_the_known_detector_offsets():
    """
    Each element's PSF sits at half its detector offset, so the shift that
    registers it onto the central element is minus that displacement.
    """
    cube, offsets = _ism_cube()
    expected = -(offsets - offsets[CENTRE])

    shifts = tttrlib.CLSMSuperRes.shift_vectors(cube, usf=20, ref_idx=CENTRE)
    assert shifts.shape == (N_DET, 2)
    assert np.abs(shifts - expected).max() < 0.1, (
        f"worst shift error {np.abs(shifts - expected).max():.3f} px"
    )
    # the reference element is its own reference
    assert np.allclose(shifts[CENTRE], 0.0)


@pytest.mark.parametrize("filter_sigma", [0.0, 1.0])
def test_shift_vectors_match_the_brighteyes_reference(filter_sigma):
    """
    The shift estimator is a port of BrightEyes-ISM ShiftVectors: Hann
    apodization, optional Gaussian denoising, then skimage's
    phase_cross_correlation with normalization=None. Compare against exactly
    that, so a change in any of the three steps is caught.
    """
    registration = pytest.importorskip("skimage.registration")
    filters = pytest.importorskip("skimage.filters")

    cube, _ = _ism_cube(pitch=2.3, sigma=2.5, n=48, side=3)
    n_det, ny, nx = cube.shape
    ref = n_det // 2

    # APR_lib.hann2d on a channels-last dataset
    iy = np.arange(ny)[:, None]
    ix = np.arange(nx)[None, :]
    window = (0.5 * (1 - np.cos(2 * np.pi * iy / (ny - 1)))
              * 0.5 * (1 - np.cos(2 * np.pi * ix / (nx - 1))))
    dset = np.moveaxis(cube, 0, -1) * window[:, :, None]
    if filter_sigma > 0:
        dset = filters.gaussian(dset, sigma=filter_sigma, channel_axis=-1)

    expected = np.array([
        registration.phase_cross_correlation(
            dset[:, :, ref], dset[:, :, i], upsample_factor=10, normalization=None
        )[0]
        for i in range(n_det)
    ])

    got = tttrlib.CLSMSuperRes.shift_vectors(
        cube, usf=10, ref_idx=ref, filter_sigma=filter_sigma
    )
    assert np.abs(got - expected).max() < 1e-9


def test_shift_vectors_subpixel_beats_integer():
    """usf is the subpixel precision knob; at usf=1 the estimate must be integral."""
    cube, offsets = _ism_cube(pitch=1.5)
    expected = -(offsets - offsets[CENTRE])

    integer = tttrlib.CLSMSuperRes.shift_vectors(cube, usf=1, ref_idx=CENTRE)
    fine = tttrlib.CLSMSuperRes.shift_vectors(cube, usf=20, ref_idx=CENTRE)

    assert np.allclose(integer, np.round(integer))
    assert np.abs(fine - expected).max() < np.abs(integer - expected).max()


def test_apr_conserves_flux_and_sharpens():
    """
    APR only moves photons between pixels: the total is preserved, and the
    reassigned image is narrower than the plain detector sum. Compare against
    shifting each channel by its known offset, which is the best APR can do.
    """
    cube, offsets = _ism_cube()
    plain_sum = cube.sum(axis=0)

    apr = tttrlib.CLSMSuperRes.apr_reconstruction(cube, usf=20, ref_idx=CENTRE)
    assert apr.shape == (1, N, N)
    apr = apr[0]

    assert apr.sum() == pytest.approx(plain_sum.sum(), rel=1e-6)
    assert (apr >= 0).all()

    nd_shift = pytest.importorskip("scipy.ndimage").shift
    ideal = sum(nd_shift(cube[k], -(offsets[k] - offsets[CENTRE])) for k in range(N_DET))

    w_sum, w_apr, w_ideal = (_spot_width(i) for i in (plain_sum, apr, ideal))
    assert w_apr < w_sum, f"APR ({w_apr:.3f}) no sharper than the sum ({w_sum:.3f})"
    assert w_apr == pytest.approx(w_ideal, abs=0.05), (
        f"APR width {w_apr:.3f} vs the ideal reassignment {w_ideal:.3f}"
    )


def test_apr_channels_last_matches_channels_first():
    """The (ny, nx, n_det) layout BrightEyes-ISM uses gives the same answer."""
    cube, _ = _ism_cube()
    a = tttrlib.CLSMSuperRes.apr_reconstruction(cube, usf=10, ref_idx=CENTRE)
    b = tttrlib.CLSMSuperRes.apr_reconstruction(
        np.ascontiguousarray(np.moveaxis(cube, 0, -1)), usf=10, ref_idx=CENTRE,
        channels_last=True,
    )
    assert np.allclose(a, b)


def _focus_cube(sigma_a=0.9, sigma_b=3.0, n=48, side=DET_SIDE, photons=500.0):
    """
    A cube whose micro-image at every pixel is a known mixture of a narrow
    in-focus fingerprint and a wide out-of-focus one, so the background fraction
    focus-ISM has to recover is known everywhere.
    """
    x = np.linspace(-(side // 2), side // 2, side)
    xx_d, yy_d = np.meshgrid(x, x)
    r2 = (xx_d ** 2 + yy_d ** 2).ravel()

    def fingerprint(sigma):
        v = np.exp(-r2 / (2 * sigma ** 2))
        return v / v.sum()

    b_true = np.full((n, n), 0.60)
    b_true[:12, :12] = 0.20
    c0, c1 = (n - 12) // 2, (n + 12) // 2
    b_true[c0:c1, c0:c1] = 0.0          # the calibration patch is pure in-focus

    total = np.full((n, n), photons)
    ga, gb = fingerprint(sigma_a), fingerprint(sigma_b)
    cube = np.stack([total * ((1 - b_true) * ga[k] + b_true * gb[k])
                     for k in range(side * side)])
    return cube, b_true, (c0, c1)


def test_focus_ism_recovers_the_background_fraction():
    """
    Focus-ISM splits every pixel into signal and background. With the in-focus
    width calibrated on a patch of pure signal, the recovered background
    fraction has to track the fraction that was put in.
    """
    cube, b_true, (c0, c1) = _focus_cube()

    out = tttrlib.CLSMSuperRes.focus_reconstruction(
        cube, sigma_bound=2.0, threshold=0.0, calibration_size=12
    )
    assert out.shape == (3, cube.shape[1], cube.shape[2])
    focus, background, ism = out

    assert (focus >= 0).all() and (background >= 0).all()
    # The split is a partition of the *reassigned* photons, which is the third
    # plane. It is not the input total: registration moves some photons off the
    # edge of the frame, and those are gone rather than wrapped around.
    assert (focus + background).sum() == pytest.approx(ism.sum(), rel=1e-6)
    assert ism.sum() < cube.sum()

    # The APR pass shifts photons by up to a pixel, so the fit smears across a
    # region boundary; judge each region on its interior.
    b_hat = background / (focus + background)
    for name, region, expected in (
        ("calibration", (slice(c0 + 3, c1 - 3), slice(c0 + 3, c1 - 3)), 0.00),
        ("corner", (slice(2, 10), slice(2, 10)), 0.20),
        ("bulk", (slice(-10, -2), slice(-10, -2)), 0.60),
    ):
        got = b_hat[region].mean()
        assert abs(got - expected) < 0.05, (
            f"{name}: background fraction {got:.3f}, expected {expected:.2f}"
        )

    # and the regions stay ordered
    assert (b_hat[c0 + 3:c1 - 3, c0 + 3:c1 - 3].mean()
            < b_hat[2:10, 2:10].mean()
            < b_hat[-10:-2, -10:-2].mean())


def test_focus_ism_threshold_assigns_dim_pixels_to_background():
    """Below the photon threshold a pixel is background, as in pixel_fit_2."""
    cube, _, _ = _focus_cube(n=24, photons=50.0)
    focus, background, ism = tttrlib.CLSMSuperRes.focus_reconstruction(
        cube, threshold=1e9, calibration_size=8
    )
    assert np.allclose(focus, 0.0)
    assert background.sum() == pytest.approx(ism.sum(), rel=1e-6)


def test_focus_ism_needs_a_square_array_without_coordinates():
    """A non-square channel count has no implied lattice, and must say so."""
    cube = np.random.default_rng(0).poisson(10, size=(7, 12, 12)).astype(float)
    with pytest.raises(Exception):
        tttrlib.CLSMSuperRes.focus_reconstruction(cube)
    # ... unless the caller supplies the geometry
    coords = np.column_stack([np.arange(7) - 3.0, np.zeros(7)])
    out = tttrlib.CLSMSuperRes.focus_reconstruction(cube, detector_coords=coords)
    assert out.shape == (3, 12, 12)


def _brighteyes_frc_lib():
    """Load the reference FRC_lib directly; its package __init__ pulls in a
    reader we do not have.

    The reference is somebody else's checkout, so everything about it is
    optional: where it lives (BRIGHTEYES_ISM_SRC names the `src` directory) and
    what it imports -- FRC_lib itself needs matplotlib and statsmodels, which
    this suite does not depend on. Any of that missing is a skip: these three
    tests compare against an external implementation, and not having it says
    nothing about our own.
    """
    import importlib.util
    import os

    candidates = []
    env = os.environ.get("BRIGHTEYES_ISM_SRC")
    if env:
        candidates.append(os.path.join(env, "brighteyes_ism/analysis/FRC_lib.py"))
    candidates.append("/Users/tpeulen/dev/chisurf/junk/brighteyes-ism/src/"
                      "brighteyes_ism/analysis/FRC_lib.py")
    path = next((p for p in candidates if os.path.exists(p)), None)
    if path is None:
        pytest.skip("BrightEyes-ISM reference not available")

    spec = importlib.util.spec_from_file_location("FRC_lib", path)
    mod = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(mod)
    except ImportError as e:
        pytest.skip("BrightEyes-ISM reference needs %s" % e.name)
    return mod


def _frc_pair(seed=0, n=96):
    rng = np.random.default_rng(seed)
    gy, gx = np.mgrid[0:n, 0:n]
    obj = np.exp(-(((gx - 48) ** 2 + (gy - 40) ** 2) / (2 * 7.0 ** 2))) * 300
    obj += np.exp(-(((gx - 30) ** 2 + (gy - 60) ** 2) / (2 * 4.0 ** 2))) * 200
    return (rng.poisson(obj + 5).astype(float),
            rng.poisson(obj + 5).astype(float))


def test_frc_curve_matches_the_brighteyes_reference():
    """
    The raw curve is a port of FRC_lib.FRC, apodization and radial binning
    included, and has to reproduce it to round-off.
    """
    ref = _brighteyes_frc_lib()
    a, b = _frc_pair()
    assert np.abs(ref.FRC(a, b) - tttrlib.CLSMSuperRes.frc_curve(a, b)).max() < 1e-12


@pytest.mark.parametrize("method", ["fixed", "3sigma"])
def test_frc_resolution_matches_the_brighteyes_reference(method):
    """
    Including the LOWESS smoothing, which is reimplemented here rather than
    pulled in from a statistics package -- so it has to agree with statsmodels'
    window choice, not merely look similar.
    """
    ref = _brighteyes_frc_lib()
    a, b = _frc_pair()
    expected = ref.FRC_resolution(a, b, px=0.05, method=method)
    got = tttrlib.CLSMSuperRes.frc_resolution(a, b, pixel_size=0.05, method=method)

    assert got[0] == pytest.approx(expected[0], rel=1e-9)      # resolution
    assert np.abs(got[1] - expected[1]).max() < 1e-12          # k axis
    assert np.abs(got[4] - expected[4]).max() < 1e-9           # smoothed curve


def test_frc_curve_limits():
    """An image against itself correlates perfectly; independent noise does not."""
    rng = np.random.default_rng(3)
    gy, gx = np.mgrid[0:64, 0:64]
    smooth = np.exp(-(((gx - 32) ** 2 + (gy - 32) ** 2) / (2 * 6.0 ** 2)))

    same = tttrlib.CLSMSuperRes.frc_curve(smooth, smooth)
    assert np.allclose(same[np.isfinite(same)], 1.0, atol=1e-9)

    cross = tttrlib.CLSMSuperRes.frc_curve(rng.normal(size=(64, 64)),
                                           rng.normal(size=(64, 64)))
    assert np.abs(cross[5:]).mean() < 0.3


def test_frc_resolution_rejects_bad_arguments():
    a, b = _frc_pair(n=64)
    with pytest.raises(ValueError):
        tttrlib.CLSMSuperRes.frc_resolution(a, b, method="nope")
    with pytest.raises(ValueError):
        tttrlib.CLSMSuperRes.frc_resolution(a, b, smoothing="nope")


def test_detector_cube_accepts_a_tiff_path():
    """A TIFF stack on disk is a valid detector cube."""
    tifffile = pytest.importorskip("tifffile")
    cube, _ = _ism_cube(n=20, side=3)

    with tempfile.NamedTemporaryFile(suffix='.tif', delete=False) as tmp:
        tmp_path = tmp.name
    try:
        tifffile.imwrite(tmp_path, cube)
        assert tttrlib.CLSMSuperRes.read_tiff(tmp_path).shape == cube.shape
        from_path = tttrlib.CLSMSuperRes.apr_reconstruction(tmp_path, usf=4)
        from_array = tttrlib.CLSMSuperRes.apr_reconstruction(cube, usf=4)
        assert np.allclose(from_path, from_array)
    finally:
        import os
        if os.path.exists(tmp_path):
            os.remove(tmp_path)


def test_detector_cube_rejects_non_3d():
    with pytest.raises(ValueError):
        tttrlib.CLSMSuperRes.apr_reconstruction(np.zeros((8, 8)))


# ---------------------------------------------------------------------------
# The real reference, live: BrightEyes-ISM APR_lib / FocusISM_lib
# ---------------------------------------------------------------------------

def _brighteyes_analysis(name):
    """Import `brighteyes_ism.analysis.<name>` from the reference checkout with
    the package __init__ bypassed (it pulls in a reader we do not have), so the
    intra-package relative imports (FocusISM_lib -> APR_lib) still resolve.
    Optional like _brighteyes_frc_lib: missing checkout or dependency = skip."""
    import importlib
    import os
    import sys
    import types

    candidates = []
    env = os.environ.get("BRIGHTEYES_ISM_SRC")
    if env:
        candidates.append(os.path.join(env, "brighteyes_ism"))
    candidates.append("/Users/tpeulen/dev/chisurf/junk/brighteyes-ism/src/brighteyes_ism")
    root = next((p for p in candidates if os.path.isdir(p)), None)
    if root is None:
        pytest.skip("BrightEyes-ISM reference not available")
    if "brighteyes_ism" not in sys.modules or getattr(sys.modules["brighteyes_ism"], "__path__", None) != [root]:
        pkg = types.ModuleType("brighteyes_ism")
        pkg.__path__ = [root]
        sub = types.ModuleType("brighteyes_ism.analysis")
        sub.__path__ = [os.path.join(root, "analysis")]
        sys.modules["brighteyes_ism"] = pkg
        sys.modules["brighteyes_ism.analysis"] = sub
    try:
        return importlib.import_module("brighteyes_ism.analysis." + name)
    except ImportError as e:
        pytest.skip("BrightEyes-ISM reference needs %s" % e.name)


@pytest.mark.parametrize("filter_sigma", [0.0, 1.0])
def test_shift_vectors_equal_brighteyes_ShiftVectors_live(filter_sigma):
    """The reference function itself (not the transcription above): identical
    shift vectors, both filter settings."""
    APR = _brighteyes_analysis("APR_lib")
    cube, _ = _ism_cube(pitch=2.3, sigma=2.5, n=48, side=3)
    n_det = cube.shape[0]
    ref = n_det // 2
    expected, _ = APR.ShiftVectors(np.moveaxis(cube, 0, -1), 10, ref, apodize=True, filter_sigma=filter_sigma)
    got = tttrlib.CLSMSuperRes.shift_vectors(cube, usf=10, ref_idx=ref, filter_sigma=filter_sigma)
    assert np.abs(got - expected).max() == 0.0


@pytest.mark.parametrize("filter_sigma", [0.0, 1.0])
def test_apr_equals_brighteyes_APR_fourier_mode_live(filter_sigma):
    """apr_reconstruction == APR_lib.APR(mode='fourier') summed over elements
    (to 1e-9 relative). BrightEyes' default mode is 'interp' (a cubic-spline
    scipy.ndimage.shift), which tttrlib does not offer; on this cube it differs
    from the Fourier registration by 7e-4 relative, recorded here."""
    APR = _brighteyes_analysis("APR_lib")
    cube, _ = _ism_cube()
    n_det = cube.shape[0]
    ref = n_det // 2
    dset = np.moveaxis(cube, 0, -1)
    _, res = APR.APR(dset, 10, ref, apodize=True, filter_sigma=filter_sigma, mode="fourier")
    expected = res.sum(-1)
    got = tttrlib.CLSMSuperRes.apr_reconstruction(cube, usf=10, ref_idx=ref, filter_sigma=filter_sigma)[0]
    assert np.abs(got - expected).max() / expected.max() < 1e-9
    _, res_i = APR.APR(dset, 10, ref, apodize=True, filter_sigma=filter_sigma, mode="interp")
    assert np.abs(got - res_i.sum(-1)).max() / expected.max() < 5e-3


def test_focus_ism_agrees_with_brighteyes_focusISM_live():
    """focus_reconstruction vs FocusISM_lib.focusISM on the known-mixture cube,
    the reference calibrated on the same central pure-signal patch. Agreement is
    at tolerance, not round-off: the reference reassigns with its default
    'interp' mode before fitting and fits every micro-image with scipy
    curve_fit; tttrlib registers by Fourier shift and fits natively. Pinned:
    per-pixel focus/background correlate > 0.98, the recovered background
    fractions of the two agree within 0.02 in each region and both are within
    0.05 of the truth."""
    import contextlib
    import io
    F = _brighteyes_analysis("FocusISM_lib")
    cube, b_true, (c0, c1) = _focus_cube()
    dset = np.moveaxis(cube, 0, -1)
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        sig, bkg, ism = F.focusISM(dset, sigma_B_bound=2.0, threshold=0, apr=True,
                                   calibration=dset[c0:c1, c0:c1, :], sum_results=True, parallelize=False)
    focus, background, ism_t = tttrlib.CLSMSuperRes.focus_reconstruction(
        cube, sigma_bound=2.0, threshold=0.0, calibration_size=c1 - c0)
    for got, ref in ((focus, sig), (background, bkg), (ism_t, ism)):
        assert np.corrcoef(got.ravel(), ref.ravel())[0, 1] > 0.98
    b_ref = bkg / (sig + bkg)
    b_got = background / (focus + background)
    for region, truth in (((slice(2, 10), slice(2, 10)), 0.20),
                          ((slice(-10, -2), slice(-10, -2)), 0.60)):
        assert abs(b_got[region].mean() - b_ref[region].mean()) < 0.02
        assert abs(b_got[region].mean() - truth) < 0.05
        assert abs(b_ref[region].mean() - truth) < 0.05

"""
SOFISM: super-resolution optical fluctuation image scanning microscopy.

Reference: Sroda et al., Optica 7, 1308 (2020); the formulation followed is the
one restated by Beck et al., arXiv:2606.16508 (Eqs. 1-3).

SOFISM stacks two independent resolution mechanisms, and the tests here check
each separately and then their product:

* the SOFI part -- a second-order cumulant of independently blinking emitters
  has an effective PSF equal to the square of the optical one, narrower by
  sqrt(2) for a Gaussian;
* the ISM part -- each detector pair is a virtual detector midway between the
  two elements and is reassigned by the mean of their shifts.

The contrast depends on the emitters *fluctuating independently*, so a static
sample must yield no SOFI signal at all. That is the sharpest test of the
implementation, and it is checked explicitly.
"""

import numpy as np
import pytest

import tttrlib

SIDE = 5
N_DET = SIDE * SIDE
CENTRE = N_DET // 2


def _channel_psfs(n=40, side=SIDE, pitch=1.8, sigma=2.2):
    """
    Per-element PSFs of a square array detector: element k sees an object
    through a PSF centred half-way to its own offset.
    """
    yy, xx = np.mgrid[0:n, 0:n]
    psfs = np.zeros((side * side, n, n))
    for row in range(side):
        for col in range(side):
            k = row * side + col
            dx = (col - (side - 1) / 2) * pitch
            dy = (row - (side - 1) / 2) * pitch
            psfs[k] = np.exp(-(((xx - (n / 2 + dx / 2)) ** 2
                                + (yy - (n / 2 + dy / 2)) ** 2) / (2 * sigma ** 2)))
    return psfs


def _blinking_acquisition(emitters, n_time=64, n=40, p_on=0.35, brightness=3000.0,
                          seed=0, blink=True):
    """
    A SOFISM acquisition: (n_time, n_det, ny, nx).

    Every emitter switches on and off independently, so the fluctuation
    cross-correlation between two detector elements retains a per-emitter term.
    With ``blink=False`` all emitters stay on and only shot noise fluctuates --
    the negative control.
    """
    rng = np.random.default_rng(seed)
    psfs = _channel_psfs(n=n)
    n_det = psfs.shape[0]

    data = np.zeros((n_time, n_det, n, n))
    for e, (ey, ex) in enumerate(emitters):
        obj = np.zeros((n, n))
        obj[int(round(ey)), int(round(ex))] = 1.0
        # the emitter seen by each element, at full brightness
        from scipy.signal import fftconvolve
        per_det = np.stack([fftconvolve(obj, psfs[k], mode="same")
                            for k in range(n_det)])
        on = (rng.random(n_time) < p_on) if blink else np.ones(n_time, bool)
        for t in np.nonzero(on)[0]:
            data[t] += per_det

    return rng.poisson(np.clip(data * brightness, 0, None)).astype(np.float64)


def _fwhm(img):
    im = np.clip(img, 0, None)
    yy, xx = np.mgrid[0:im.shape[0], 0:im.shape[1]]
    tot = im.sum()
    my = (im * yy).sum() / tot
    mx = (im * xx).sum() / tot
    return 2.3548 * np.sqrt((im * ((xx - mx) ** 2 + (yy - my) ** 2)).sum() / tot / 2)


def test_sofism_beats_ism_beats_confocal():
    """
    The headline claim of the method: SOFI and ISM multiply. The paper reports
    a factor of about two over confocal before any Fourier reweighting, which
    is sqrt(2) from the cumulant times sqrt(2) from pixel reassignment.
    """
    pytest.importorskip("scipy")
    n = 40
    data = _blinking_acquisition([(n / 2, n / 2)], n=n, seed=1)

    time_averaged = data.mean(axis=0)
    confocal = time_averaged.sum(axis=0)
    apr = tttrlib.CLSMSuperRes.apr_reconstruction(time_averaged, usf=10)[0]
    sofism = tttrlib.CLSMSuperRes.sofism_reconstruction(data, lag=0, usf=10)

    assert sofism.shape == (n, n)

    w_conf, w_apr, w_sofism = _fwhm(confocal), _fwhm(apr), _fwhm(sofism)
    assert w_apr < w_conf, f"APR {w_apr:.2f} not sharper than confocal {w_conf:.2f}"
    assert w_sofism < w_apr, f"SOFISM {w_sofism:.2f} not sharper than APR {w_apr:.2f}"
    # both mechanisms together: close to the factor of two the paper reports
    assert 1.6 < w_conf / w_sofism < 2.6, (
        f"resolution gain {w_conf / w_sofism:.2f} outside the expected range"
    )


def test_sofism_needs_independent_fluctuations():
    """
    The negative control. Without blinking there is no SOFI contrast: the
    remaining fluctuation is shot noise, which is independent between detector
    elements and so cancels in the cross-correlation. The reconstruction must
    collapse relative to the blinking case, not merely change a little.
    """
    pytest.importorskip("scipy")
    n = 40
    kwargs = dict(n=n, n_time=64, seed=2)
    blinking = _blinking_acquisition([(n / 2, n / 2)], blink=True, **kwargs)
    static = _blinking_acquisition([(n / 2, n / 2)], blink=False, **kwargs)

    # match the mean photon flux, so only the fluctuations differ
    static = static * (blinking.mean() / static.mean())

    sig_blink = tttrlib.CLSMSuperRes.sofism_reconstruction(blinking, usf=10).max()
    sig_static = tttrlib.CLSMSuperRes.sofism_reconstruction(static, usf=10).max()

    assert sig_blink > 20 * sig_static, (
        f"blinking {sig_blink:.4g} vs static {sig_static:.4g}: the reconstruction "
        "does not depend on the emitters actually fluctuating"
    )


def test_sofism_resolves_a_pair_the_confocal_cannot():
    """Two emitters closer than the confocal resolution."""
    pytest.importorskip("scipy")
    n = 44
    sep = 3.2
    data = _blinking_acquisition(
        [(n / 2, n / 2 - sep / 2), (n / 2, n / 2 + sep / 2)], n=n, seed=3
    )

    confocal = data.mean(axis=0).sum(axis=0)
    sofism = tttrlib.CLSMSuperRes.sofism_reconstruction(data, usf=10)

    def modulation(img):
        p = np.clip(img, 0, None)[n // 2]
        p = (p - p.min()) / (p.max() - p.min() + 1e-12)
        i1 = int(round(n / 2 - sep / 2))
        i2 = int(round(n / 2 + sep / 2))
        dip = p[i1:i2 + 1].min()
        return (p[i1] + p[i2] - 2 * dip) / (p[i1] + p[i2] + 1e-12)

    assert modulation(sofism) > modulation(confocal)


def test_sofism_excludes_autocorrelation_by_default():
    """
    The i == j terms carry uncancelled shot noise. Including them must change
    the result, and the default must be to leave them out.
    """
    pytest.importorskip("scipy")
    n = 32
    data = _blinking_acquisition([(n / 2, n / 2)], n=n, n_time=32, seed=4)

    without = tttrlib.CLSMSuperRes.sofism_reconstruction(data, usf=4)
    with_auto = tttrlib.CLSMSuperRes.sofism_reconstruction(
        data, usf=4, include_auto=True
    )
    assert not np.allclose(without, with_auto)
    # the autocorrelation adds a positive shot-noise pedestal
    assert with_auto.sum() > without.sum()


def test_sofism_rejects_bad_input():
    with pytest.raises(ValueError):
        tttrlib.CLSMSuperRes.sofism_reconstruction(np.zeros((4, 8, 8)))
    with pytest.raises(Exception):
        # a single time bin cannot carry a fluctuation
        tttrlib.CLSMSuperRes.sofism_reconstruction(np.zeros((1, 4, 8, 8)))
    with pytest.raises(Exception):
        tttrlib.CLSMSuperRes.sofism_reconstruction(np.zeros((4, 4, 8, 8)), lag=4)


def test_fourier_reweight_sharpens():
    """
    The Wiener-type reweighting of Eq. 3 boosts the frequencies the ISM OTF
    attenuates, so it must narrow a blurred spot and leave the total finite.
    """
    n = 48
    yy, xx = np.mgrid[0:n, 0:n]
    blurred = np.exp(-(((xx - n / 2) ** 2 + (yy - n / 2) ** 2) / (2 * 3.0 ** 2)))

    # OTF of a Gaussian PSF of the same width, on the fftshifted grid
    ky = (yy - n / 2) / n
    kx = (xx - n / 2) / n
    otf = np.exp(-2 * (np.pi * 3.0) ** 2 * (kx ** 2 + ky ** 2))

    sharpened = tttrlib.CLSMSuperRes.fourier_reweight(blurred, otf, epsilon=1e-2)
    assert np.isfinite(sharpened).all()

    def half_max_width(img):
        """
        Width of the central peak at half its height. A second-moment width is
        useless here: the Wiener filter boosts high frequencies, so the
        deconvolved image rings, and the sidelobes dominate the second moment
        however narrow the peak itself becomes.
        """
        p = img[img.shape[0] // 2]
        peak = p.max()
        above = np.nonzero(p >= 0.5 * peak)[0]
        return above[-1] - above[0] + 1

    assert half_max_width(sharpened) < half_max_width(blurred)

    with pytest.raises(ValueError):
        tttrlib.CLSMSuperRes.fourier_reweight(blurred, otf[:-1])

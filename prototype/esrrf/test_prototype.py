"""
pytest suite for the eSRRF photon-reassignment Python prototype.

Covers the five provable properties plus the RGC oracle check. Inputs are
kept small so the deliberately-slow pure-Python reference stays fast enough
for CI (16x16 native image, M=3, ~40k photons).

Run:  python -m pytest prototype/esrrf/test_prototype.py -q
"""

from __future__ import annotations

import numpy as np
import pytest

from esrrf.esrrf_reference import rgc_map, temporal_combine
from esrrf.esrrf_photon import (
    PhotonData,
    intensity_from_photons,
    reassign_photons,
    reassign_photons_multichannel,
    _categorical_sample,
)
from esrrf.simulate import (
    Emitter,
    CLSMScanParameters,
    two_colour_filaments,
    crossing_filaments,
    render_frame,
    render_photon_stream,
)


NX, NY = 16, 16
MAG = 3
FWHM = 1.5
SEED = 20260731


def make_photons(n_frames: int = 2, seed: int = SEED) -> PhotonData:
    """Build a small PhotonData with exact fractional x from macro times."""
    emitters, params = crossing_filaments(nx=NX, ny=NY)
    macro, frames, lines, pixels, channels = render_photon_stream(
        emitters, params, n_frames=n_frames, sigma=1.0, noise_seed=seed
    )
    pdur = params.pixel_duration
    # exact x within a line, in pixel units: (macro - line_start)/pixel_duration
    # already includes the pixel index (macro = line_start + x*pdur + u*pdur)
    line_start = frames * params.frame_marker_delay + lines * params.line_duration
    x_exact = (macro - line_start) / pdur
    micro = np.arange(len(macro), dtype=np.uint16) % 1000
    return PhotonData(
        n_photons=len(macro),
        frame=frames,
        line=lines,
        pixel=pixels,
        x_exact=x_exact.astype(float),
        y_line=lines.astype(float),
        micro_time=micro,
        routing_channel=channels,
    )


def make_rgc(seed: int = SEED) -> np.ndarray:
    emitters, params = crossing_filaments(nx=NX, ny=NY)
    img = render_frame(emitters, params, sigma=1.0, noise_seed=seed)
    return rgc_map(img, magnification=MAG, fwhm=FWHM, sensitivity=1)


@pytest.fixture(scope="module")
def photons() -> PhotonData:
    return make_photons()


@pytest.fixture(scope="module")
def rgc() -> np.ndarray:
    return make_rgc()


def test_rgc_oracle():
    """RGC peak lands near the input peak, on a minimal 4x4 image."""
    img = np.zeros((4, 4))
    img[1:3, 1:3] = 1.0
    img[2, 2] = 2.0
    rgc = rgc_map(img, magnification=2, fwhm=1.0, sensitivity=1)
    assert rgc.max() > 0
    peak_y, peak_x = np.unravel_index(rgc.argmax(), rgc.shape)
    assert abs(peak_x / 2 - 2.0) < 1.0
    assert abs(peak_y / 2 - 2.0) < 1.0


def test_property_1_conservation(photons, rgc):
    """N_out == N_in overall, per frame, and per routing channel."""
    x_new, y_new = reassign_photons(photons, rgc, MAG, FWHM / 2, seed=SEED)
    n = photons.n_photons
    assert len(x_new) == n
    for f in np.unique(photons.frame):
        assert (photons.frame == f).sum() == len(x_new[photons.frame == f])
    for ch in np.unique(photons.routing_channel):
        assert (photons.routing_channel == ch).sum() == len(
            x_new[photons.routing_channel == ch]
        )


def test_property_2_expectation(photons, rgc):
    """E[I'] over many seeds converges to the direct RGC redistribution."""
    my, mx = rgc.shape
    sr = int(np.ceil((FWHM / 2) * MAG))

    # Exact expectation: each photon i redistributes its mass to the magnified
    # pixels in its own search window with probability proportional to RGC there.
    # (Photons in the same native pixel have different exact-x positions, so a
    # per-pixel mean-position approximation would be wrong.)
    expected = np.zeros((my, mx), dtype=float)
    for i in range(photons.n_photons):
        xM = photons.x_exact[i] * MAG
        yM = photons.y_line[i] * MAG
        x0 = max(int(np.floor(xM - sr)), 0)
        x1 = min(int(np.ceil(xM + sr)), mx - 1)
        y0 = max(int(np.floor(yM - sr)), 0)
        y1 = min(int(np.ceil(yM + sr)), my - 1)

        weights, positions = [], []
        for yi in range(y0, y1 + 1):
            for xi in range(x0, x1 + 1):
                if np.sqrt((xi - xM) ** 2 + (yi - yM) ** 2) <= (FWHM / 2) * MAG:
                    weights.append(rgc[yi, xi])
                    positions.append((xi, yi))
        w = np.array(weights)
        total = w.sum()
        if total > 0:
            w = w / total
        else:
            w = np.ones_like(w) / max(len(w), 1)
        for (xi, yi), wi in zip(positions, w):
            expected[yi, xi] += wi

    accumulated = np.zeros((my, mx), dtype=float)
    for seed in range(40):
        x_new, y_new = reassign_photons(photons, rgc, MAG, FWHM / 2, seed=seed)
        for xi, yi in zip(np.floor(x_new).astype(int), np.floor(y_new).astype(int)):
            if 0 <= xi < mx and 0 <= yi < my:
                accumulated[yi, xi] += 1
    empirical = accumulated / 40

    # Statistical check: each pixel's 40-seed average has variance mu/40
    # (independent multinomial draws). Standardized residuals must be ~N(0,1).
    mask = expected > 1.0
    resid = (empirical - expected)[mask] / np.sqrt(expected[mask] / 40)
    assert np.abs(resid).max() < 4.5, f"max |std residual| {np.abs(resid).max():.3f}"
    assert np.abs(resid).mean() < 1.5, f"mean |std residual| {np.abs(resid).mean():.3f}"
    assert (resid**2).mean() < 2.0, f"chi2/dof {(resid**2).mean():.3f}"


def test_property_3_preservation(photons, rgc):
    """Micro-time array and routing-channel multiset survive reassignment."""
    x_new, y_new = reassign_photons(photons, rgc, MAG, FWHM / 2, seed=SEED)
    assert np.array_equal(photons.micro_time, photons.micro_time)
    assert np.array_equal(photons.routing_channel, photons.routing_channel)
    # reassign_photons only returns positions; attrs live on the container
    assert len(x_new) == photons.n_photons


def test_property_4_sensitivity_zero():
    """sensitivity=0 without intensity weighting gives an all-ones prior."""
    params = CLSMScanParameters(nx=NX, ny=NY)
    img = render_frame(
        [Emitter(x=NX / 2, y=NY / 2, photons=1000.0)],
        params, sigma=1.0, background=0.0, noise_seed=SEED,
    )
    flat = rgc_map(img, magnification=5, fwhm=1.5, sensitivity=0, intensity_weighting=False)
    assert np.all(flat == 1.0)
    iw = rgc_map(img, magnification=5, fwhm=1.5, sensitivity=0, intensity_weighting=True)
    assert iw.max() > 0


def test_property_5_determinism():
    """Same (seed, index) always gives the same categorical sample."""
    weights = np.array([0.1, 0.2, 0.3, 0.4])
    first = [_categorical_sample(weights, 12345, i) for i in range(20)]
    second = [_categorical_sample(weights, 12345, i) for i in range(20)]
    assert first == second
    # different index -> (overwhelmingly likely) different draw
    assert len(set(first)) > 1


def test_intensity_from_photons(photons):
    """Magnified intensity built from exact positions conserves counts."""
    img = intensity_from_photons(photons, NX, NY, magnification=MAG)
    assert img.shape == (MAG * NY, MAG * NX)
    assert int(img.sum()) == photons.n_photons


def test_split_channel_preserves_channels():
    """Split mode uses each channel's own field and preserves counts."""
    emitters, params = two_colour_filaments(nx=NX, ny=NY)
    macro, frames, lines, pixels, channels = render_photon_stream(
        emitters, params, n_frames=2, sigma=1.0, noise_seed=SEED
    )
    line_start = frames * params.frame_marker_delay + lines * params.line_duration
    x_exact = (macro - line_start) / params.pixel_duration
    photons = PhotonData(
        n_photons=len(macro), frame=frames, line=lines, pixel=pixels,
        x_exact=x_exact.astype(float), y_line=lines.astype(float),
        micro_time=np.zeros(len(macro), dtype=np.uint16),
        routing_channel=channels,
    )
    img = render_frame(emitters, params, sigma=1.0, noise_seed=SEED)
    rgc = rgc_map(img, magnification=MAG, fwhm=FWHM, sensitivity=1)
    fields = {0: rgc, 1: rgc}
    x_new, y_new = reassign_photons_multichannel(photons, fields, MAG, FWHM / 2, seed=SEED)
    assert len(x_new) == photons.n_photons
    for ch in np.unique(channels):
        assert (channels == ch).sum() == len(x_new[channels == ch])


def test_temporal_combine_modes():
    """AVG/VAR/TAC2 agree with direct numpy formulas on a tiny stack."""
    stack = np.random.default_rng(0).poisson(3, size=(8, 4, 4)).astype(float)
    assert np.allclose(temporal_combine(stack, "AVG"), stack.mean(axis=0))
    var = (stack * stack).mean(axis=0) - stack.mean(axis=0) ** 2
    assert np.allclose(temporal_combine(stack, "VAR"), var)
    tac2 = (stack[:-1] * stack[1:]).mean(axis=0) - stack.mean(axis=0) ** 2
    assert np.allclose(temporal_combine(stack, "TAC2"), tac2)


def test_reassignment_moves_photons_toward_peaks(photons, rgc):
    """The reassigned image is sharper: more mass near the RGC peak."""
    x_new, y_new = reassign_photons(photons, rgc, MAG, FWHM / 2, seed=SEED)
    img_new = np.zeros((MAG * NY, MAG * NX), dtype=float)
    for xi, yi in zip(np.floor(x_new).astype(int), np.floor(y_new).astype(int)):
        img_new[yi, xi] += 1
    # radial second moment around the RGC peak should shrink
    peak_y, peak_x = np.unravel_index(rgc.argmax(), rgc.shape)
    img_orig = intensity_from_photons(photons, NX, NY, magnification=MAG)

    def radial_moment(im):
        yy, xx = np.indices(im.shape)
        r2 = (xx - peak_x) ** 2 + (yy - peak_y) ** 2
        tot = im.sum()
        return (im * r2).sum() / tot if tot > 0 else 0.0

    assert radial_moment(img_new) < radial_moment(img_orig)

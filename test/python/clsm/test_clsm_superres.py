"""
Photon-level super-resolution (eSRRF) on CLSM data.

The RGC field and the temporal combination are checked against closed-form
expectations; the photon reassignment is checked for photon conservation, for
the raster geometry it writes, and for actually concentrating photons.
"""

import os
import tempfile

import numpy as np
import pytest

import tttrlib

# Centralized test settings — resolves to an absolute path under the data root. A bare
# relative path here made `os.path.exists` depend on pytest's working directory, so these
# tests silently skipped whenever the suite was not invoked from the repository root.
from test_settings import settings  # type: ignore

PTU_FILE = settings["microtime_th260_beads_filename"]


def _gaussian_spot(ny, nx, cy, cx, sigma, amplitude=1.0):
    y, x = np.mgrid[0:ny, 0:nx]
    return amplitude * np.exp(-(((x - cx) ** 2 + (y - cy) ** 2) / (2 * sigma ** 2)))


def test_rgc_map_shape_and_range():
    """The RGC field lives on the magnified grid and is non-negative."""
    img = np.zeros((8, 8), dtype=float)
    img[3:5, 3:5] = 10.0

    mag = 2
    rgc = tttrlib.CLSMSuperRes.rgc_map(img, magnification=mag, fwhm=1.5, sensitivity=1)
    assert rgc.shape == (mag * 8, mag * 8)
    assert (rgc >= 0.0).all()
    assert rgc.max() > 0.0

    rgc_iw = tttrlib.CLSMSuperRes.rgc_map(
        img, magnification=mag, fwhm=1.5, sensitivity=1, intensity_weighting=True
    )
    assert rgc_iw.shape == (mag * 8, mag * 8)
    assert rgc_iw.max() > 0.0


def test_rgc_map_rejects_non_2d():
    with pytest.raises(ValueError):
        tttrlib.CLSMSuperRes.rgc_map(np.zeros((2, 4, 4)))


def test_rgc_map_localizes_a_point_source():
    """
    The point of the RGC transform: on an isolated diffraction-limited spot the
    convergence field must peak on the emitter, not merely somewhere nearby, and
    it must be narrower than the spot that produced it.
    """
    ny = nx = 24
    fwhm = 3.0
    sigma = fwhm / 2.354
    cy, cx = 11.5, 13.5  # sits on a magnified sub-pixel, not on a native centre
    img = _gaussian_spot(ny, nx, cy, cx, sigma, amplitude=100.0)

    mag = 5
    rgc = tttrlib.CLSMSuperRes.rgc_map(
        img, magnification=mag, fwhm=fwhm, sensitivity=2, intensity_weighting=True
    )

    peak = np.unravel_index(np.argmax(rgc), rgc.shape)
    # magnified pixel centres sample native coordinate (i + 0.5) / mag
    peak_y = (peak[0] + 0.5) / mag - 0.5
    peak_x = (peak[1] + 0.5) / mag - 0.5
    assert abs(peak_y - cy) < 0.6, f"RGC peak y={peak_y} away from the emitter at {cy}"
    assert abs(peak_x - cx) < 0.6, f"RGC peak x={peak_x} away from the emitter at {cx}"

    def spot_width(field, upsample):
        yy, xx = np.mgrid[0:field.shape[0], 0:field.shape[1]]
        tot = field.sum()
        my = (field * yy).sum() / tot
        mx = (field * xx).sum() / tot
        var = (field * ((xx - mx) ** 2 + (yy - my) ** 2)).sum() / tot / 2.0
        return 2.3548 * np.sqrt(var) / upsample

    assert spot_width(rgc, mag) < 0.8 * spot_width(img, 1)


def test_temporal_combine_modes():
    """AVG / VAR / TAC2 / INT against their closed forms."""
    rng = np.random.default_rng(42)
    stack = rng.poisson(5, size=(10, 8, 8)).astype(float)

    avg = tttrlib.CLSMSuperRes.temporal_combine(stack, mode="AVG")
    assert avg.shape == (8, 8)
    assert np.allclose(avg, stack.mean(axis=0))

    var = tttrlib.CLSMSuperRes.temporal_combine(stack, mode="VAR")
    assert np.allclose(var, (stack ** 2).mean(axis=0) - stack.mean(axis=0) ** 2)

    tac2 = tttrlib.CLSMSuperRes.temporal_combine(stack, mode="TAC2")
    assert np.allclose(
        tac2, (stack[:-1] * stack[1:]).mean(axis=0) - stack.mean(axis=0) ** 2
    )

    assert np.allclose(tttrlib.CLSMSuperRes.temporal_combine(stack, mode="INT"), avg)


def test_temporal_combine_rejects_bad_input():
    # a lag-1 cumulant is undefined for a single frame
    with pytest.raises(Exception):
        tttrlib.CLSMSuperRes.temporal_combine(np.zeros((1, 4, 4)), mode="TAC2")
    with pytest.raises(Exception):
        tttrlib.CLSMSuperRes.temporal_combine(np.zeros((4, 4)), mode="AVG")
    with pytest.raises(Exception):
        tttrlib.CLSMSuperRes.temporal_combine(np.zeros((2, 4, 4)), mode="NOPE")


@pytest.mark.skipif(not os.path.exists(PTU_FILE), reason="test data not available")
def test_get_photon_positions_reproduces_the_intensity_image():
    """
    The exact-x photon stream is the seam reassignment builds on, so binning it
    back to native pixels has to reproduce get_intensity() photon for photon.
    """
    t = tttrlib.TTTR(PTU_FILE)
    clsm = tttrlib.CLSMImage(PTU_FILE, build_pixels=True, fill=True)

    frames, lines, x_exact, y_line, events = tttrlib.CLSMSuperRes.get_photon_positions(clsm, t)

    n = len(frames)
    assert n == len(lines) == len(x_exact) == len(y_line) == len(events)
    assert n > 0
    assert (frames >= 0).all() and (frames < clsm.n_frames).all()
    assert (lines >= 0).all() and (lines < clsm.n_lines).all()
    assert (x_exact >= 0).all() and (x_exact < clsm.n_pixel).all()
    assert np.array_equal(y_line, lines.astype(float))

    intensity = np.asarray(clsm.get_intensity(), dtype=np.int64)
    assert n == intensity.sum()

    binned = np.zeros_like(intensity)
    np.add.at(binned, (frames, lines, np.clip(x_exact.astype(int), 0, clsm.n_pixel - 1)), 1)
    # A handful of photons land within one ULP of a pixel edge and can bin to
    # the neighbour; anything more means the position arithmetic is off.
    assert np.abs(binned - intensity).max() <= 1
    assert (binned != intensity).sum() < 0.001 * intensity.size


@pytest.mark.slow
@pytest.mark.skipif(not os.path.exists(PTU_FILE), reason="test data not available")
def test_reassign_photons_conserves_photons_and_concentrates_them():
    """
    Reassignment moves photons, it does not create or destroy them, and an RGC
    prior must concentrate them more than a flat one does.
    """
    t_src = tttrlib.TTTR(PTU_FILE)
    clsm = tttrlib.CLSMImage(PTU_FILE, build_pixels=True, fill=True)
    n_src = int(np.asarray(clsm.get_intensity()).sum())

    mag = 2
    esrrf = tttrlib.CLSMSuperRes.reassign_photons(
        clsm, t_src, magnification=mag, fwhm=1.0, sensitivity=2,
        search_radius=1.0, seed=42, method="esrrf",
    )
    uniform = tttrlib.CLSMSuperRes.reassign_photons(
        clsm, t_src, magnification=mag, fwhm=1.0, sensitivity=2,
        search_radius=1.0, seed=42, method="uniform",
    )
    assert esrrf.n_valid_events == n_src
    assert uniform.n_valid_events == n_src

    my, mx = mag * clsm.n_lines, mag * clsm.n_pixel

    def spread(tttr):
        flat = (np.asarray(tttr.macro_times) % (my * mx)).astype(np.intp)
        counts = np.bincount(flat, minlength=my * mx).astype(float)
        p = counts / counts.sum()
        nz = p[p > 0]
        return -(nz * np.log(nz)).sum()  # entropy: lower = more concentrated

    assert spread(esrrf) < spread(uniform)


@pytest.mark.heavy  # 6s
@pytest.mark.skipif(not os.path.exists(PTU_FILE), reason="test data not available")
def test_reassign_photons_is_reproducible():
    """The reassignment RNG is seeded, so the same seed gives the same stream."""
    t_src = tttrlib.TTTR(PTU_FILE)
    clsm = tttrlib.CLSMImage(PTU_FILE, build_pixels=True, fill=True)
    kwargs = dict(magnification=2, fwhm=1.0, sensitivity=1, search_radius=1.0)

    a = tttrlib.CLSMSuperRes.reassign_photons(clsm, t_src, seed=7, **kwargs)
    b = tttrlib.CLSMSuperRes.reassign_photons(clsm, t_src, seed=7, **kwargs)
    c = tttrlib.CLSMSuperRes.reassign_photons(clsm, t_src, seed=8, **kwargs)

    assert np.array_equal(np.asarray(a.macro_times), np.asarray(b.macro_times))
    assert not np.array_equal(np.asarray(a.macro_times), np.asarray(c.macro_times))


@pytest.mark.slow
@pytest.mark.skipif(not os.path.exists(PTU_FILE), reason="test data not available")
def test_reassign_photons_and_write_all_formats():
    """Every supported container round-trips back into a magnified CLSMImage."""
    t_src = tttrlib.TTTR(PTU_FILE)
    clsm = tttrlib.CLSMImage(PTU_FILE, build_pixels=True, fill=True)

    nx, ny = clsm.n_pixel, clsm.n_lines
    mag = 2

    res = tttrlib.CLSMSuperRes.reassign_photons(
        clsm, t_src, magnification=mag, fwhm=1.0, sensitivity=1,
        search_radius=1.5, channel_mode="merged", seed=42, method="esrrf",
    )
    assert res.n_valid_events > 0

    with tempfile.TemporaryDirectory() as tmpdir:
        # No "spc": a CLSM image needs line and frame markers, and the plain
        # SPC-130 record has nowhere to put them -- the file is written but
        # cannot be identified or rebuilt into an image, which is a property of
        # the format rather than a defect in this path. The formats that can
        # carry the markers are the ones checked here.
        for ext in ["ptu", "ht3", "photons"]:
            out_fn = os.path.join(tmpdir, f"test_sr.{ext}")
            ok = tttrlib.CLSMSuperRes.write(res, nx, ny, mag, out_fn)
            assert ok, f"Writing format {ext} failed"
            assert os.path.exists(out_fn), f"File {out_fn} was not created"

            if ext == "photons":
                t_read = tttrlib.TTTR(out_fn)
                # Position-marker stream: an X marker, a Y marker and the photon
                assert t_read.n_valid_events == 3 * res.n_valid_events
            else:
                clsm_read = tttrlib.CLSMImage(out_fn)
                I = clsm_read.get_intensity()
                assert I.shape == (clsm.n_frames, mag * ny, mag * nx)
                assert I.sum() == res.n_valid_events


def test_write_rejects_unsupported_container():
    """A container with no raster representation is reported, not silently 'written'."""
    t = tttrlib.TTTR()
    with tempfile.TemporaryDirectory() as tmpdir:
        assert not tttrlib.CLSMSuperRes.write(
            t, 8, 8, 2, os.path.join(tmpdir, "nope.unknown-extension")
        )

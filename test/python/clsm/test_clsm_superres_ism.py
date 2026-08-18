"""
Photon-level ISM reassignment: the "ism" and "esrrf+ism" folds of
CLSMSuperRes.reassign_photons.

Unlike the array-detector reconstructions, this path moves individual TTTR
photons rather than whole channel images, so the check is on where the photons
end up in the magnified raster.
"""

import os

import numpy as np
import pytest

import tttrlib

PTU_FILE = "tttr-data/imaging/pq/Microtime200_TH260/beads.ptu"

pytestmark = pytest.mark.skipif(
    not os.path.exists(PTU_FILE), reason=f"Test data file {PTU_FILE} not found"
)


def _magnified_positions(tttr, my, mx):
    """Decode (y, x) in the magnified raster from the reassigned macro times."""
    # signed, so that differences between two reassignments do not wrap
    flat = np.asarray(tttr.macro_times).astype(np.int64) % (my * mx)
    return flat // mx, flat % mx


@pytest.mark.slow
def test_ism_applies_the_detector_shift():
    """
    With explicit detector offsets, pure ISM reassignment (sensitivity <= 0) is
    a rigid translation by ism_shift_factor times the offset of the photon's
    routing channel, in native pixels.
    """
    t_src = tttrlib.TTTR(PTU_FILE)
    clsm = tttrlib.CLSMImage(PTU_FILE, build_pixels=True, fill=True)
    mag = 2
    my, mx = mag * clsm.n_lines, mag * clsm.n_pixel

    # The same offset for every routing channel the file might use, so the
    # expected translation does not depend on which channel the photons carry.
    offsets = np.tile(np.array([2.0, 1.0]), (8, 1))
    shift_factor = 0.5

    plain = tttrlib.CLSMSuperRes.reassign_photons(
        clsm, t_src, magnification=mag, fwhm=1.0, sensitivity=0,
        search_radius=1.0, seed=1, method="uniform",
    )
    shifted = tttrlib.CLSMSuperRes.reassign_photons(
        clsm, t_src, magnification=mag, fwhm=1.0, sensitivity=0,
        search_radius=1.0, seed=1, method="ism",
        detector_offsets=offsets, ism_shift_factor=shift_factor,
    )
    assert shifted.n_valid_events == plain.n_valid_events

    y0, x0 = _magnified_positions(plain, my, mx)
    y1, x1 = _magnified_positions(shifted, my, mx)

    # Photons that ran into the raster edge are clamped, so compare the bulk
    interior = (x0 > 8) & (x0 < mx - 8) & (y0 > 8) & (y0 < my - 8)
    dx = (x1[interior] - x0[interior]).mean()
    dy = (y1[interior] - y0[interior]).mean()

    assert dx == pytest.approx(shift_factor * offsets[0, 0] * mag, abs=0.6)
    assert dy == pytest.approx(shift_factor * offsets[0, 1] * mag, abs=0.6)


@pytest.mark.heavy  # 5s
def test_ism_and_esrrf_ism_conserve_photons():
    """Both folds move photons without creating or dropping any."""
    t_src = tttrlib.TTTR(PTU_FILE)
    clsm = tttrlib.CLSMImage(PTU_FILE, build_pixels=True, fill=True)
    n_src = int(np.asarray(clsm.get_intensity()).sum())

    for method, sensitivity in (("ism", 0), ("ism", 1), ("esrrf+ism", 2)):
        out = tttrlib.CLSMSuperRes.reassign_photons(
            clsm, t_src, magnification=2, fwhm=1.0, sensitivity=sensitivity,
            search_radius=1.5, channel_mode="merged", seed=42, method=method,
        )
        assert out.n_valid_events == n_src, f"{method} (sensitivity={sensitivity})"


def test_unimplemented_method_is_reported():
    """SOFI is reserved but not implemented, and says so rather than silently
    falling back to the default prior."""
    t_src = tttrlib.TTTR(PTU_FILE)
    clsm = tttrlib.CLSMImage(PTU_FILE, build_pixels=True, fill=True)
    with pytest.raises(Exception):
        tttrlib.CLSMSuperRes.reassign_photons(clsm, t_src, method="sofi")


def test_an_unknown_reassignment_method_is_refused():
    """A misspelt method used to fall through to eSRRF silently; 'sofi' parsed
    and then threw a RuntimeError. Both are ValueErrors that name the methods."""
    t_src = tttrlib.TTTR(PTU_FILE)
    clsm = tttrlib.CLSMImage(PTU_FILE, build_pixels=True, fill=True)
    for bad in ("unifrom", "sofi"):
        with pytest.raises(ValueError, match="esrrf, uniform, ism"):
            tttrlib.CLSMSuperRes.reassign_photons(
                clsm, t_src, magnification=2, fwhm=1.0, sensitivity=0,
                search_radius=1.0, seed=1, method=bad)

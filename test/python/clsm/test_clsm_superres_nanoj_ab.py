"""
A/B test of the C++ eSRRF kernels against the NanoJ-eSRRF reference math.

`prototype/esrrf/esrrf_reference.py` is a line-by-line numpy transcription of
liveSRRF.cl / RadialGradientConvergence.cl / LiveSRRF_CL.java and is the oracle
the C++ has to reproduce -- everywhere, including the borders. The border is
where a port goes wrong quietly: NanoJ switches from bicubic interpolation to
bilinear extrapolation outside the 4x4 support, drops gradient samples that fall
outside the frame, and reads Gx and Gy from grids shifted against each other.
None of that changes the image centre, so a test that only looks at the median
passes while the edges are wrong.
"""

import sys
from pathlib import Path

import numpy as np
import pytest

import tttrlib

PROTOTYPE_DIR = Path(__file__).parents[3] / "prototype" / "esrrf"
if str(PROTOTYPE_DIR) not in sys.path:
    sys.path.insert(0, str(PROTOTYPE_DIR))

from esrrf_reference import rgc_map as nanoj_rgc_map  # noqa: E402
from esrrf_reference import temporal_combine as nanoj_temporal_combine  # noqa: E402


@pytest.mark.parametrize("intensity_weighting", [False, True])
@pytest.mark.parametrize("magnification,fwhm,sensitivity", [(3, 1.5, 1), (2, 2.5, 2)])
def test_ab_rgc_map_vs_nanoj_reference(
    intensity_weighting, magnification, fwhm, sensitivity
):
    """Every magnified pixel must agree with the reference to double precision."""
    rng = np.random.default_rng(42)
    img = rng.poisson(10, size=(16, 16)).astype(float)

    rgc_nanoj = nanoj_rgc_map(
        img,
        magnification=magnification,
        fwhm=fwhm,
        sensitivity=sensitivity,
        intensity_weighting=intensity_weighting,
    )
    rgc_cpp = tttrlib.CLSMSuperRes.rgc_map(
        img,
        magnification=magnification,
        fwhm=fwhm,
        sensitivity=sensitivity,
        intensity_weighting=intensity_weighting,
    )

    assert rgc_cpp.shape == rgc_nanoj.shape
    scale = max(1.0, np.abs(rgc_nanoj).max())
    worst = np.abs(rgc_nanoj - rgc_cpp).max() / scale
    assert worst < 1e-12, f"worst relative deviation from the reference: {worst:.3g}"


def test_ab_rgc_map_border_is_not_special_cased():
    """
    Guard against the failure the old tolerances hid: a mismatch confined to the
    last few rows and columns.
    """
    rng = np.random.default_rng(7)
    img = rng.poisson(20, size=(12, 14)).astype(float)

    ref = nanoj_rgc_map(img, magnification=3, fwhm=2.0, sensitivity=1,
                        intensity_weighting=True)
    got = tttrlib.CLSMSuperRes.rgc_map(img, magnification=3, fwhm=2.0, sensitivity=1,
                                       intensity_weighting=True)

    scale = max(1.0, np.abs(ref).max())
    deviation = np.abs(ref - got) / scale
    border = np.zeros_like(deviation, dtype=bool)
    border[:6, :] = True
    border[-6:, :] = True
    border[:, :6] = True
    border[:, -6:] = True
    assert deviation[border].max() < 1e-12
    assert deviation[~border].max() < 1e-12


def test_ab_temporal_combine_vs_nanoj_reference():
    """AVG, VAR, TAC2 and INT against the reference accumulation."""
    rng = np.random.default_rng(123)
    stack = rng.poisson(5, size=(8, 12, 12)).astype(float)

    for mode in ["AVG", "VAR", "TAC2", "INT"]:
        comb_nanoj = nanoj_temporal_combine(stack, mode=mode)
        comb_cpp = tttrlib.CLSMSuperRes.temporal_combine(stack, mode=mode)
        assert np.allclose(comb_nanoj, comb_cpp, rtol=1e-12, atol=1e-12), (
            f"Mode {mode} does not match the NanoJ reference"
        )

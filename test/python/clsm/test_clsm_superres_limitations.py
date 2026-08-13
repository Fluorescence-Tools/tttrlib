"""
Qualitative tests illustrating algorithmic tendencies of eSRRF photon reassignment
on simplified synthetic patterns.

NOTE: These tests use simplified 2D toy simulations and are UNTESTED for physical
experimental CLSM setups.
"""

import sys
from pathlib import Path
import numpy as np
import pytest
import tttrlib

SIM_DIR = Path(__file__).parents[3] / "examples" / "simulation"
if str(SIM_DIR) not in sys.path:
    sys.path.insert(0, str(SIM_DIR))

from simulate import Emitter, CLSMScanParameters, render_frame, render_photon_stream


def generate_stripes(spacing: float, n_stripes: int = 3, nx: int = 64, ny: int = 64, photons_per_point: float = 100.0):
    """Generate vertical stripe pattern with fixed inter-stripe spacing in native pixels."""
    emitters = []
    cx = nx / 2.0
    offset_x = cx - (n_stripes - 1) * spacing / 2.0
    for s in range(n_stripes):
        x = offset_x + s * spacing
        for y in np.linspace(10, ny - 10, 120):
            emitters.append(Emitter(x=x, y=y, photons=photons_per_point))
    params = CLSMScanParameters(nx=nx, ny=ny)
    return emitters, params


def test_limitation_1_resolution_limit_and_merging():
    """
    Test 1: When stripe spacing d < 0.35 * FWHM, eSRRF fails to resolve individual stripes
    and merges them into a central artifact peak.
    """
    fwhm = 2.0
    mag = 4
    
    # Case A: Resolvable spacing (d = 1.5 * FWHM = 3.0 px)
    emitters_res, params = generate_stripes(spacing=3.0, n_stripes=2, nx=64, ny=64)
    img_res = render_frame(emitters_res, params, sigma=fwhm / 2.354, background=2.0, noise_seed=42)
    rgc_res = tttrlib.CLSMSuperRes.rgc_map(img_res, magnification=mag, fwhm=fwhm, sensitivity=1, intensity_weighting=True)
    
    # Take horizontal profile across middle
    mid_y = (mag * 64) // 2
    profile_res = rgc_res[mid_y, :]
    # Should have 2 distinct local maxima for 2 stripes
    from scipy.signal import find_peaks
    peaks_res, _ = find_peaks(profile_res, height=0.1 * profile_res.max(), distance=mag)
    assert len(peaks_res) == 2, f"Expected 2 resolved peaks for d=3.0px, got {len(peaks_res)}"
    
    # Case B: Unresolvable spacing (d = 0.3 * FWHM = 0.6 px)
    emitters_unres, params = generate_stripes(spacing=0.6, n_stripes=2, nx=64, ny=64)
    img_unres = render_frame(emitters_unres, params, sigma=fwhm / 2.354, background=2.0, noise_seed=42)
    rgc_unres = tttrlib.CLSMSuperRes.rgc_map(img_unres, magnification=mag, fwhm=fwhm, sensitivity=1, intensity_weighting=True)
    
    profile_unres = rgc_unres[mid_y, :]
    peaks_unres, _ = find_peaks(profile_unres, height=0.1 * profile_unres.max(), distance=mag)
    # Merges into a single central peak artifact
    assert len(peaks_unres) == 1, f"Expected 1 merged artifact peak for d=0.6px, got {len(peaks_unres)}"


def test_limitation_2_high_sensitivity_beading_artifacts():
    """
    Test 2: High sensitivity (S = 4) on continuous lines causes 'beading' artifacts,
    breaking continuous structures into false puncta.
    """
    fwhm = 2.0
    mag = 4
    emitters, params = generate_stripes(spacing=10.0, n_stripes=1, nx=64, ny=64, photons_per_point=50.0)
    img = render_frame(emitters, params, sigma=fwhm / 2.354, background=5.0, noise_seed=42)
    
    # Low sensitivity S=1 (smooth continuous stripe)
    rgc_s1 = tttrlib.CLSMSuperRes.rgc_map(img, magnification=mag, fwhm=fwhm, sensitivity=1, intensity_weighting=True)
    # High sensitivity S=4 (fragmented line / beading)
    rgc_s4 = tttrlib.CLSMSuperRes.rgc_map(img, magnification=mag, fwhm=fwhm, sensitivity=4, intensity_weighting=True)
    
    # Compute variation along vertical direction of the stripe (x = 32 * mag)
    stripe_x = (mag * 64) // 2
    v_s1 = rgc_s1[20:100, stripe_x]
    v_s4 = rgc_s4[20:100, stripe_x]
    
    cv_s1 = np.std(v_s1) / np.mean(v_s1)
    cv_s4 = np.std(v_s4) / (np.mean(v_s4) + 1e-6)
    
    # High sensitivity S=4 has higher coefficient of variation along continuous line
    assert cv_s4 > 1.2 * cv_s1, f"Expected high sensitivity S=4 to increase variation (CV S4={cv_s4:.2f} vs S1={cv_s1:.2f})"


def test_limitation_3_unweighted_rgc_intensity_independence():
    """
    Test 3: Unweighted RGC (intensity_weighting=False) measures pure spatial convergence geometry,
    making it independent of input emitter brightness.
    """
    fwhm = 1.5
    mag = 2
    # Two stripes: Bright (1000 photons) vs Dim (100 photons), ratio = 10:1
    emitters = []
    for y in np.linspace(10, 54, 80):
        emitters.append(Emitter(x=20.0, y=y, photons=1000.0))  # Bright
        emitters.append(Emitter(x=44.0, y=y, photons=100.0))   # Dim
    params = CLSMScanParameters(nx=64, ny=64)
    img = render_frame(emitters, params, sigma=fwhm / 2.354, background=0.0, noise_seed=42)
    
    # Without intensity weighting: RGC normalizes gradient convergence to ~1.0 for both bright and dim
    rgc_unweighted = tttrlib.CLSMSuperRes.rgc_map(img, magnification=mag, fwhm=fwhm, sensitivity=1, intensity_weighting=False)
    prof_unweighted = rgc_unweighted[64, :]
    peak_bright_unw = prof_unweighted[35:45].max()
    peak_dim_unw = prof_unweighted[83:93].max()
    
    ratio_unweighted = peak_bright_unw / (peak_dim_unw + 1e-12)
    assert abs(ratio_unweighted - 1.0) < 0.2, f"Unweighted RGC ratio {ratio_unweighted:.2f} should be ~1.0 (intensity independent)"
    
    # With intensity weighting: RGC restores linear brightness scaling
    rgc_weighted = tttrlib.CLSMSuperRes.rgc_map(img, magnification=mag, fwhm=fwhm, sensitivity=1, intensity_weighting=True)
    prof_weighted = rgc_weighted[64, :]
    peak_bright_w = prof_weighted[35:45].max()
    peak_dim_w = prof_weighted[83:93].max()
    
    ratio_weighted = peak_bright_w / (peak_dim_w + 1e-12)
    assert ratio_weighted > 5.0, f"Intensity-weighted RGC ratio {ratio_weighted:.1f} preserves brightness difference"

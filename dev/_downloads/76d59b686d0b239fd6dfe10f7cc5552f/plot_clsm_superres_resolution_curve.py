"""
CLSM Super-Resolution (eSRRF) Resolution Curve Demonstration (Unverified Toy Model)
====================================================================================

.. warning::
    DISCLAIMER: The simulated nanometer values, optical parameters, and contrast
    curves in this script are based on a simplified 2D toy model. They are UNTESTED
    and UNVERIFIED for actual physical CLSM instruments.
"""

import sys
from pathlib import Path
import numpy as np
import matplotlib.pyplot as plt
import tttrlib

PROTOTYPE_DIR = Path(__file__).parents[2] / "prototype" / "esrrf"
if str(PROTOTYPE_DIR) not in sys.path:
    sys.path.insert(0, str(PROTOTYPE_DIR))

from simulate import Emitter, CLSMScanParameters, render_frame, render_blinking_frame_stack

# Optical parameters
na = 1.30
wavelength_nm = 520.0
fwhm_nm = wavelength_nm / (2.0 * na)  # 200.0 nm
nyquist_limit_nm = fwhm_nm / 2.35482  # 84.93 nm

pixel_size_nm = 25.0
fwhm_px = fwhm_nm / pixel_size_nm     # 8.0 px
mag = 3
nx, ny = 32, 32

separations_nm = np.linspace(40.0, 360.0, 14)
separations_px = separations_nm / pixel_size_nm

contrast_clsm = []
contrast_static_s1 = []
contrast_blinking_tac2 = []

for sep_px in separations_px:
    emitters = []
    cx = nx / 2.0
    for x_pos in [cx - sep_px / 2.0, cx + sep_px / 2.0]:
        for y in np.linspace(6, ny - 6, 50):
            emitters.append(Emitter(x=x_pos, y=y, photons=250.0))
    params = CLSMScanParameters(nx=nx, ny=ny)
    
    # 1. Diffraction-limited CLSM frame
    img_orig = render_frame(emitters, params, sigma=fwhm_px / 2.35482, background=2.0, noise_seed=42).squeeze()
    rgc_static = tttrlib.CLSMSuperRes.rgc_map(img_orig, magnification=mag, fwhm=fwhm_px, sensitivity=1, intensity_weighting=True)
    
    # 2. Fluorophore blinking stack (10 frames, p_on = 20%)
    stack = render_blinking_frame_stack(emitters, params, n_frames=10, p_on=0.20, sigma=fwhm_px / 2.35482, background=2.0, seed=42)
    rgc_stack = np.zeros((10, mag * ny, mag * nx))
    for f in range(10):
        rgc_stack[f] = tttrlib.CLSMSuperRes.rgc_map(stack[f], magnification=mag, fwhm=fwhm_px, sensitivity=1, intensity_weighting=True)
    rgc_tac2 = tttrlib.CLSMSuperRes.temporal_combine(rgc_stack, mode='TAC2')
    
    mid_y_orig = ny // 2
    mid_y_sr = (mag * ny) // 2
    
    p_orig = img_orig[mid_y_orig, :]
    p_stat = rgc_static[mid_y_sr, :]
    p_tac2 = rgc_tac2[mid_y_sr, :]
    
    def calc_contrast(profile, center_idx):
        p_max = profile.max()
        if p_max <= 0:
            return 0.0
        p_dip = profile[center_idx]
        return max(0.0, (p_max - p_dip) / p_max)
    
    c_orig = calc_contrast(p_orig, nx // 2)
    c_stat = calc_contrast(p_stat, (mag * nx) // 2)
    c_tac2 = calc_contrast(p_tac2, (mag * nx) // 2)
    
    contrast_clsm.append(c_orig)
    contrast_static_s1.append(c_stat)
    contrast_blinking_tac2.append(c_tac2)


# Render Figure
fig, ax1 = plt.subplots(figsize=(10, 6.5))

ax1.plot(separations_nm, contrast_clsm, 'k--', linewidth=2, label='Confocal CLSM (Diffraction-Limited)', marker='o')
ax1.plot(separations_nm, contrast_static_s1, 'g-', linewidth=2, label='eSRRF Static / Continuous (AVG)', marker='s')
ax1.plot(separations_nm, contrast_blinking_tac2, 'r-', linewidth=2.5, label='eSRRF Blinking Kinetics ($p_{on}=20\\%$, TAC2)', marker='^')

# Annotations & Threshold Lines
ax1.axvline(x=fwhm_nm, color='black', linestyle=':', linewidth=1.5, label=f'Abbe Limit ({fwhm_nm:.0f} nm)')
ax1.axvline(x=nyquist_limit_nm, color='red', linestyle='--', linewidth=1.5, label=f'Nyquist-Shannon Limit ({nyquist_limit_nm:.1f} nm)')
ax1.axhline(y=0.265, color='gray', linestyle=':', linewidth=1.2, label='Rayleigh Criterion (Contrast = 26.5%)')

ax1.fill_between([40.0, nyquist_limit_nm], 0, 1.05, color='red', alpha=0.1, label='Regime 1: Unresolvable / Merged (< 85 nm)')
ax1.fill_between([nyquist_limit_nm, fwhm_nm], 0, 1.05, color='green', alpha=0.12, label='Regime 2: eSRRF Super-Resolution Gain (85 - 200 nm)')
ax1.fill_between([fwhm_nm, 360.0], 0, 1.05, color='blue', alpha=0.05, label='Regime 3: Diffraction-Limited Regimes (> 200 nm)')

ax1.set_title('eSRRF Physical Resolution Curve: Blinking Kinetics vs Nyquist Limit\n(60x Oil, NA = 1.30, $\\lambda = 520$ nm)', fontsize=13, fontweight='bold')
ax1.set_xlabel('Stripe Separation $d$ (nanometers)', fontsize=12, fontweight='bold')
ax1.set_ylabel('Rayleigh Dip Contrast $\\frac{I_{max} - I_{dip}}{I_{max}}$', fontsize=12, fontweight='bold')
ax1.set_ylim(-0.02, 1.05)
ax1.set_xlim(40.0, 360.0)
ax1.grid(True, linestyle='--', alpha=0.5)

# Top axis in FWHM units
ax2 = ax1.twiny()
ax2.set_xlim(ax1.get_xlim())
ax2.set_xticks(np.arange(50, 361, 50))
ax2.set_xticklabels([f'{x/fwhm_nm:.2f}×' for x in np.arange(50, 361, 50)])
ax2.set_xlabel('Separation in FWHM Units ($d / \\text{FWHM}$)', fontsize=11, fontweight='bold')

ax1.legend(loc='lower right', fontsize=9)

plt.tight_layout()

plt.show()

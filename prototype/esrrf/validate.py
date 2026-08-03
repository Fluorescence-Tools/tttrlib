"""
Visual validation of the eSRRF photon reassignment prototype.

The automated checks -- the RGC oracle and the five provable properties of the
reassignment -- live in `test_prototype.py`; `run_all()` below delegates to
them. This module only adds the eyeball check on a couple of tiny phantoms,
which no assertion covers.

Run this before writing any C++: if the prototype is wrong, the C++ port
inherits the error.
"""

from __future__ import annotations

import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path

# Import the prototype modules
from .esrrf_reference import rgc_map
from .simulate import (
    crossing_filaments,
    ring_phantom,
    render_frame,
)


def visual_check():
    """
    Visual validation on crossing filaments and ring phantom.

    Kept deliberately small: the pure-Python RGC reference is O(M*ny * M*nx)
    with a nested gradient sub-lattice, so large inputs are impractically slow.
    The automated property checks live in test_prototype.py; this is a
    convenience for eyeballing the RGC field on a tiny phantom.
    """
    print("\n=== Visual Validation ===")

    # Create phantoms
    emitters_cross, params_cross = crossing_filaments(nx=16, ny=16)
    emitters_ring, params_ring = ring_phantom(radius=6.0, nx=16, ny=16)

    img_cross = render_frame(emitters_cross, params_cross, sigma=1.0, noise_seed=42)
    img_ring = render_frame(emitters_ring, params_ring, sigma=1.0, noise_seed=42)

    # Compute RGC
    magnification = 3
    rgc_cross = rgc_map(img_cross, magnification=magnification, fwhm=1.5, sensitivity=1)
    rgc_ring = rgc_map(img_ring, magnification=magnification, fwhm=1.5, sensitivity=1)

    # Plot
    fig, axes = plt.subplots(2, 3, figsize=(12, 8))

    # Crossing filaments
    axes[0, 0].imshow(img_cross, cmap="viridis")
    axes[0, 0].set_title("Crossing Filaments - Original")

    axes[0, 1].imshow(rgc_cross, cmap="viridis")
    axes[0, 1].set_title("Crossing Filaments - RGC")

    axes[0, 2].imshow(rgc_cross[:24, :24], cmap="viridis")  # Zoom in
    axes[0, 2].set_title("Crossing Filaments - RGC (zoom)")

    # Ring
    axes[1, 0].imshow(img_ring, cmap="viridis")
    axes[1, 0].set_title("Ring - Original")

    axes[1, 1].imshow(rgc_ring, cmap="viridis")
    axes[1, 1].set_title("Ring - RGC")

    axes[1, 2].imshow(rgc_ring[:24, :24], cmap="viridis")  # Zoom in
    axes[1, 2].set_title("Ring - RGC (zoom)")

    plt.tight_layout()

    output_dir = Path(__file__).resolve().parent
    fig.savefig(output_dir / "validation_visual.png", dpi=150)
    print(f"✓ Visual validation saved to {output_dir / 'validation_visual.png'}")

    plt.close(fig)


def run_all():
    """Run all validation checks by delegating to the pytest suite."""
    import os
    import subprocess
    import sys

    print("=" * 60)
    print("eSRRF Prototype Validation")
    print("=" * 60)

    env = dict(os.environ)
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    test_file = Path(__file__).parent / "test_prototype.py"
    result = subprocess.run(
        [sys.executable, "-m", "pytest", str(test_file), "-q", "--no-header"],
        env=env,
    )
    sys.exit(result.returncode)


if __name__ == "__main__":
    run_all()

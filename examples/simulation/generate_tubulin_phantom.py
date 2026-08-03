"""
Generate 2D Microtubule Network / Tubulin Phantom Samples in Python.

This example generates synthetic 2D tubulin network phantoms for simulating
Image Scanning Microscopy (ISM) detector arrays and super-resolution methods.
"""

import numpy as np
import matplotlib.pyplot as plt


def generate_tubulin_phantom(
    n_filaments: int = 15,
    size_px: int = 256,
    pixel_size_nm: float = 10.0,
    filament_radius_nm: float = 12.5,
    seed: int = 42
) -> np.ndarray:
    """
    Generate a 2D synthetic microtubule/tubulin network phantom.

    Parameters
    ----------
    n_filaments : int
        Number of filaments to draw across the field of view.
    size_px : int
        Image width/height in pixels.
    pixel_size_nm : float
        Pixel size in nanometers.
    filament_radius_nm : float
        Outer radius of the microtubule filaments in nanometers (~12.5 nm for 25 nm outer diameter).
    seed : int
        Random seed for reproducibility.

    Returns
    -------
    np.ndarray
        2D floating point array of shape (size_px, size_px) with filament intensities.
    """
    rng = np.random.default_rng(seed)
    phantom = np.zeros((size_px, size_px), dtype=np.float64)
    radius_px = filament_radius_nm / pixel_size_nm

    y_grid, x_grid = np.ogrid[:size_px, :size_px]

    for _ in range(n_filaments):
        # Pick random starting point and initial angle
        x0 = rng.uniform(0, size_px)
        y0 = rng.uniform(0, size_px)
        theta = rng.uniform(0, 2 * np.pi)
        curvature = rng.uniform(-0.02, 0.02)
        step_len = 1.0
        n_steps = int(size_px * 1.5)

        curr_x, curr_y = x0, y0
        for _ in range(n_steps):
            # Draw sphere/cylinder slice at (curr_x, curr_y)
            dist_sq = (x_grid - curr_x)**2 + (y_grid - curr_y)**2
            mask = dist_sq <= (radius_px**2)
            phantom[mask] += 1.0

            # Advance along curved trajectory
            theta += rng.normal(0, 0.05) + curvature
            curr_x += step_len * np.cos(theta)
            curr_y += step_len * np.sin(theta)

            if curr_x < -10 or curr_x > size_px + 10 or curr_y < -10 or curr_y > size_px + 10:
                break

    return phantom


if __name__ == "__main__":
    print("Generating 2D Tubulin Phantom...")
    phantom = generate_tubulin_phantom(n_filaments=20, size_px=256)

    fig, ax = plt.subplots(figsize=(6, 6))
    im = ax.imshow(phantom, cmap='magma', origin='lower')
    ax.set_title("Simulated 2D Tubulin Microtubule Network Phantom")
    plt.colorbar(im, ax=ax, label="Intensity (a.u.)")
    plt.tight_layout()
    plt.savefig("tubulin_phantom_example.png", dpi=150)
    print("Saved tubulin_phantom_example.png")

"""
Photon-level eSRRF reassignment.

This module implements the stochastic reassignment of individual photons onto a
magnified grid using an RGC field as the spatial probability density.

Key properties (these become tests):
1. N_out == N_in exactly — overall, per frame, and per routing channel
2. E[I'] = sum_{x,y} I(x,y) * W_{x,y} (RGC redistribution)
3. Micro-time histogram and routing-channel multiset are bit-identical before/after
4. sensitivity=0 degenerates to uniform upsampling
5. Output independent of thread count (deterministic seeded RNG)
"""

from __future__ import annotations

import numpy as np
from typing import Literal, Tuple, Optional
from dataclasses import dataclass

# Deterministic RNG for reproducibility regardless of thread count
# Uses numpy's Generator with a seed derived from global seed + photon index


def _get_rng_seed(seed: int, index: int) -> int:
    """Derive a deterministic RNG seed from global seed and photon index."""
    # Use coprime multipliers to mix the values
    return seed * 10007 + index * 1009


def _categorical_sample(
    weights: np.ndarray,
    seed: int,
    index: int,
) -> int:
    """
    Sample from a categorical distribution using deterministic RNG.

    Args:
        weights: (K,) non-negative probability weights (need not sum to 1)
        seed: global seed for the run
        index: photon index (used to derive the deterministic RNG)

    Returns:
        sampled index in [0, K-1]
    """
    # Normalize weights
    w_sum = weights.sum()
    if w_sum == 0:
        # Uniform fallback
        K = len(weights)
        rng = np.random.default_rng(_get_rng_seed(seed, index))
        return int(rng.random() * K)

    weights = weights / w_sum

    # Fresh deterministic random for this photon
    rng = np.random.default_rng(_get_rng_seed(seed, index))
    u = rng.random()

    # Cumulative sum sampling
    cumsum = np.cumsum(weights)
    return np.searchsorted(cumsum, u)


@dataclass
class PhotonData:
    """
    Container for photon-level data needed for reassignment.

    Attributes:
        n_photons: total number of photons
        frame: (n_photons,) frame index for each photon
        line: (n_photons,) line index for each photon
        pixel: (n_photons,) pixel index (within line) for each photon
        x_exact: (n_photons,) exact fractional x coordinate from macro time
        y_line: (n_photons,) y coordinate (line index, discrete)
        micro_time: (n_photons,) micro time (lifetime, preserved through reassignment)
        routing_channel: (n_photons,) routing channel (preserved)
    """
    n_photons: int
    frame: np.ndarray
    line: np.ndarray
    pixel: np.ndarray
    x_exact: np.ndarray
    y_line: np.ndarray
    micro_time: np.ndarray
    routing_channel: np.ndarray


def intensity_from_photons(
    photons: PhotonData,
    nx: int,
    ny: int,
    magnification: int = 1,
) -> np.ndarray:
    """
    Build an intensity image from exact photon positions.

    This is the photon-level analogue of the CLSM intensity image, with
    the important difference that x coordinates are exact fractional values
    from macro time rather than binned to pixel indices.

    Args:
        photons: photon container
        nx, ny: native image dimensions
        magnification: if > 1, build the image on a magnified grid

    Returns:
        (M*ny, M*nx) intensity image (photon counts per pixel)
    """
    mx = magnification * nx
    my = magnification * ny

    intensity = np.zeros((my, mx), dtype=float)

    # Direct histogram using exact positions
    if magnification == 1:
        # Native grid
        for i in range(photons.n_photons):
            x = int(np.floor(photons.x_exact[i]))
            y = int(photons.y_line[i])
            if 0 <= x < nx and 0 <= y < ny:
                intensity[y, x] += 1
    else:
        # Magnified grid: bin exact positions
        for i in range(photons.n_photons):
            xM = int(np.floor(photons.x_exact[i] * magnification))
            yM = int(np.floor(photons.y_line[i] * magnification))
            if 0 <= xM < mx and 0 <= yM < my:
                intensity[yM, xM] += 1

    return intensity


def reassign_photons(
    photons: PhotonData,
    rgc_field: np.ndarray,
    magnification: int,
    search_radius: float,
    seed: int = 20260731,
) -> Tuple[np.ndarray, np.ndarray]:
    """
    Reassign photons to new (x, y) positions by stochastic sampling from the RGC field.

    For each photon, we:
      1. Find all sub-pixels within search_radius of its current position
      2. Extract the RGC weights at those sub-pixels
      3. Normalize to a probability distribution (uniform fallback if all-zero)
      4. Sample from that distribution using a counter-based (seed, photon_index) RNG
      5. Assign the photon to the sampled sub-pixel

    Args:
        photons: input photon data (only positions are modified)
        rgc_field: (M*ny, M*nx) RGC map on the magnified grid
        magnification: magnification factor M
        search_radius: search radius in native pixels (typically fwhm/2)
        seed: RNG seed for reproducibility

    Returns:
        (x_new, y_new): reassigned (x, y) coordinates for each photon
        (magnification * ny, magnification * nx)
    """
    n = photons.n_photons
    my, mx = rgc_field.shape

    x_new = np.zeros(n, dtype=float)
    y_new = np.zeros(n, dtype=float)

    # Precompute the pixel neighbourhood for each native position
    # to avoid repeated sqrt inside the photon loop
    sr_pixels = int(np.ceil(search_radius * magnification))

    for i in range(n):
        # Current photon position in magnified pixel units
        xM = photons.x_exact[i] * magnification
        yM = photons.y_line[i] * magnification

        # Define the search window
        x0 = int(np.floor(xM - sr_pixels))
        x1 = int(np.ceil(xM + sr_pixels))
        y0 = int(np.floor(yM - sr_pixels))
        y1 = int(np.ceil(yM + sr_pixels))

        # Clip to image bounds
        x0 = max(x0, 0)
        x1 = min(x1, mx - 1)
        y0 = max(y0, 0)
        y1 = min(y1, my - 1)

        # Collect RGC weights in the search window
        weights = []
        positions = []

        for y_idx in range(y0, y1 + 1):
            for x_idx in range(x0, x1 + 1):
                dx = x_idx - xM
                dy = y_idx - yM
                distance = np.sqrt(dx * dx + dy * dy)
                if distance <= search_radius * magnification:
                    weights.append(rgc_field[y_idx, x_idx])
                    positions.append((x_idx, y_idx))

        if not positions:
            # No valid sub-pixels (shouldn't happen with reasonable params)
            # Keep photon in original position
            x_new[i] = xM
            y_new[i] = yM
            continue

        weights = np.array(weights)

        # Sample from the categorical distribution
        sample_idx = _categorical_sample(weights, seed, i)

        if sample_idx >= len(positions):
            # Fallback
            sample_idx = 0

        x_new[i], y_new[i] = positions[sample_idx]

    return x_new, y_new


def reassign_photons_multichannel(
    photons: PhotonData,
    rgc_fields: dict[int, np.ndarray],  # channel -> RGC field
    magnification: int,
    search_radius: float,
    seed: int = 20260731,
) -> Tuple[np.ndarray, np.ndarray]:
    """
    Reassign photons with per-channel RGC fields (split mode).

    Each photon is reassigned using only the RGC field of its own channel.

    Args:
        photons: input photon data
        rgc_fields: dict mapping routing channel to (M*ny, M*nx) RGC field
        magnification: magnification factor M
        search_radius: search radius in native pixels
        seed: RNG seed

    Returns:
        (x_new, y_new): reassigned coordinates
    """
    n = photons.n_photons
    my, mx = next(iter(rgc_fields.values())).shape

    x_new = np.zeros(n, dtype=float)
    y_new = np.zeros(n, dtype=float)

    sr_pixels = int(np.ceil(search_radius * magnification))

    for i in range(n):
        ch = photons.routing_channel[i]

        # Get the RGC field for this photon's channel
        if ch not in rgc_fields:
            # No field for this channel — keep original position
            x_new[i] = photons.x_exact[i] * magnification
            y_new[i] = photons.y_line[i] * magnification
            continue

        rgc_field = rgc_fields[ch]

        xM = photons.x_exact[i] * magnification
        yM = photons.y_line[i] * magnification

        x0 = max(int(np.floor(xM - sr_pixels)), 0)
        x1 = min(int(np.ceil(xM + sr_pixels)), mx - 1)
        y0 = max(int(np.floor(yM - sr_pixels)), 0)
        y1 = min(int(np.ceil(yM + sr_pixels)), my - 1)

        weights = []
        positions = []

        for y_idx in range(y0, y1 + 1):
            for x_idx in range(x0, x1 + 1):
                dx = x_idx - xM
                dy = y_idx - yM
                distance = np.sqrt(dx * dx + dy * dy)
                if distance <= search_radius * magnification:
                    weights.append(rgc_field[y_idx, x_idx])
                    positions.append((x_idx, y_idx))

        if not positions:
            x_new[i] = xM
            y_new[i] = yM
            continue

        weights = np.array(weights)
        sample_idx = _categorical_sample(weights, seed, i)

        if sample_idx >= len(positions):
            sample_idx = 0

        x_new[i], y_new[i] = positions[sample_idx]

    return x_new, y_new

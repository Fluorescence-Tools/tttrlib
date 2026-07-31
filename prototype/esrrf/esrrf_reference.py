"""
Reference eSRRF RGC computation — direct numpy transcription of the OpenCL kernels.

This is the oracle: slow, obvious, no cleverness. Every other implementation
(C++, numba-accelerated variants) is validated against this one.

Reference sources:
- junk/NanoJ-eSRRF/resources/liveSRRF.cl:238-401 (calculateRadialGradientConvergence)
- junk/NanoJ-eSRRF/resources/RadialGradientConvergence.cl:123-261 (standalone version)
- junk/NanoJ-eSRRF/src/nanoj/liveSRRF/LiveSRRF_CL.java:266-345 (parameter derivation)

All formulas are reproduced verbatim; variable names match the OpenCL where
possible. This module exposes:
- rgc_map() — the RGC field on a magnified grid
- temporal_combine() — AVG/VAR/TAC2 accumulation on a stack of RGC-weighted frames
"""

from __future__ import annotations

import numpy as np
from typing import Literal, Tuple


def _precompute_derived_params(fwhm: float, gradient_magnification: int = 2) -> dict:
    """
    Derived parameters from NanoJ LiveSRRF_CL.java:266-345.

    The GUI calls fwhm "Radius" but it is used as a FWHM in native pixels.
    """
    sigma = fwhm / 2.354  # FWHM → sigma
    # Search radius in native pixels, snapped to the gradient sub-grid
    radius = (
        np.floor(gradient_magnification * 2 * sigma) / gradient_magnification + 1
    )
    tss = 2 * sigma * sigma  # two sigma squared
    tso = 2 * sigma + 1  # two sigma plus one (hard cutoff distance)
    return {"sigma": sigma, "radius": radius, "tss": tss, "tso": tso}


def _gradient_2point(img: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
    """
    2-point backward difference gradient, matching liveSRRF.cl:187-214.

    Note this places the gradient on a half-pixel-shifted grid, which is why
    vxy_offset = 0.5 and vxy_ArrayShift = 1 exist in the original code.

    Args:
        img: (ny, nx) image frame

    Returns:
        Gx, Gy: gradients on the same shape, half-pixel shifted
    """
    ny, nx = img.shape
    Gx = np.zeros_like(img, dtype=float)
    Gy = np.zeros_like(img, dtype=float)

    # Inner pixels only — borders are handled by vxy_offset / vxy_ArrayShift in the RGC loop
    for y in range(ny):
        for x in range(nx):
            x0 = max(x - 1, 0)
            x1 = min(x + 1, nx - 1)
            y0 = max(y - 1, 0)
            y1 = min(y + 1, ny - 1)

            # 2-point backward difference
            Gx[y, x] = img[y, x] - img[y, x0]
            Gy[y, x] = img[y, x] - img[y0, x]

    return Gx, Gy


def _bicubic_upsample_2x(img: np.ndarray) -> np.ndarray:
    """
    2× bicubic upsampling of a field, matching liveSRRF.cl:217-233.

    Uses Catmull-Rom spline (a = 0.5). This is the interpolation used on the
    gradient field before RGC evaluation.

    Args:
        img: (ny, nx) field to upsample

    Returns:
        (2*ny, 2*nx) upsampled field
    """
    ny, nx = img.shape
    out = np.zeros((2 * ny, 2 * nx), dtype=float)

    # Catmull-Rom cubic kernel
    def cubic(x: float) -> float:
        a = 0.5
        if x < 0:
            x = -x
        if x < 1:
            return x * x * (x * (-a + 2) + (a - 3)) + 1
        elif x < 2:
            return -a * x * x * x + 5 * a * x * x - 8 * a * x + 4 * a
        return 0.0

    for yM in range(2 * ny):
        for xM in range(2 * nx):
            # Continuous position in the source grid
            x = xM / 2.0
            y = yM / 2.0

            u0 = int(np.floor(x))
            v0 = int(np.floor(y))

            q = 0.0
            for j in range(4):
                v = min(max(v0 - 1 + j, 0), ny - 1)
                p = 0.0
                for i in range(4):
                    u = min(max(u0 - 1 + i, 0), nx - 1)
                    p += img[v, u] * cubic(x - u)
                q += p * cubic(y - v)

            out[yM, xM] = q

    return out


def _interpolated_value(img: np.ndarray, x: float, y: float) -> float:
    """
    Bicubic interpolation at a continuous position (x, y), matching
    liveSRRF.cl getInterpolatedValue and the intensity-weighting path.

    Used for intensity weighting (the "v" in the RGC kernel) and for the
    optional interpolated widefield image.

    Args:
        img: (ny, nx) image
        x, y: continuous coordinates (may be fractional)

    Returns:
        Interpolated value
    """
    ny, nx = img.shape
    u0 = int(np.floor(x))
    v0 = int(np.floor(y))

    # Catmull-Rom cubic
    def cubic(x: float) -> float:
        a = 0.5
        if x < 0:
            x = -x
        if x < 1:
            return x * x * (x * (-a + 2) + (a - 3)) + 1
        elif x < 2:
            return -a * x * x * x + 5 * a * x * x - 8 * a * x + 4 * a
        return 0.0

    q = 0.0
    for j in range(4):
        v = min(max(v0 - 1 + j, 0), ny - 1)
        p = 0.0
        for i in range(4):
            u = min(max(u0 - 1 + i, 0), nx - 1)
            p += img[v, u] * cubic(x - u)
        q += p * cubic(y - v)

    return q


def rgc_map(
    img: np.ndarray,
    magnification: int = 5,
    fwhm: float = 1.5,
    sensitivity: int = 1,
    intensity_weighting: bool = True,
    gradient_magnification: int = 2,
) -> np.ndarray:
    """
    Compute the Radial Gradient Convergence (RGC) map on a magnified grid.

    This is a line-by-line transcription of calculateRadialGradientConvergence
    from liveSRRF.cl:238-401 and RadialGradientConvergence.cl:123-261.

    Args:
        img: (ny, nx) native-resolution image frame
        magnification: output grid is (M*ny, M*nx)
        fwhm: PSF FWHM in native pixels (NanoJ's GUI labels this "Radius")
        sensitivity: exponent applied to the normalized RGC (higher = sparser/sharper)
        intensity_weighting: if True, multiply RGC by the interpolated intensity
        gradient_magnification: upsampling factor for the gradient field (default 2)

    Returns:
        RGC: (M*ny, M*nx) array of RGC values in [0, 1] after the sensitivity exponent
    """
    ny, nx = img.shape
    my = magnification * ny
    mx = magnification * nx

    # Derived parameters
    params = _precompute_derived_params(fwhm, gradient_magnification)
    sigma = params["sigma"]
    radius = params["radius"]
    tss = params["tss"]
    tso = params["tso"]

    # Gradient and its 2× upsampling
    Gx, Gy = _gradient_2point(img)
    Gx_up = _bicubic_upsample_2x(Gx)
    Gy_up = _bicubic_upsample_2x(Gy)

    # Constants matching the OpenCL
    vxy_offset = 0.5
    vxy_ArrayShift = 1
    vxy_PixelShift = 0.0  # drift correction, zero for static data

    RGC = np.zeros((my, mx), dtype=float)

    # Main RGC loop — one magnified pixel at a time
    for yM in range(my):
        for xM in range(mx):
            # Continuous position in native space, centre of magnified pixel
            xc = (xM + 0.5) / magnification
            yc = (yM + 0.5) / magnification

            CGLH = 0.0
            wSum = 0.0

            # Sample the gradient neighbourhood on the upsampled gradient grid
            # The loop bounds match liveSRRF.cl:168-173
            j_min = int(-(gradient_magnification * radius))
            j_max = int(gradient_magnification * radius + 1)
            i_min = j_min
            i_max = j_max

            for j in range(j_min, j_max + 1):
                # vy position in continuous space (liveSRRF.cl:170 / :175)
                vy = (
                    float(int(gradient_magnification * (yc - vxy_PixelShift)) + j)
                ) / gradient_magnification + vxy_PixelShift

                for i in range(i_min, i_max + 1):
                    # vx position in continuous space
                    vx = (
                        float(int(gradient_magnification * (xc - vxy_PixelShift)) + i)
                    ) / gradient_magnification + vxy_PixelShift

                    # Distance from magnified pixel centre to sample point
                    dx = vx - xc
                    dy = vy - yc
                    distance = np.sqrt(dx * dx + dy * dy)

                    if distance == 0 or distance > tso:
                        continue

                    # Fetch gradient at this sample, applying half-pixel offset
                    # liveSRRF.cl:187-188
                    gx_idx = int(
                        gradient_magnification * (vx - vxy_offset) + vxy_ArrayShift
                    )
                    gy_idx = int(gradient_magnification * (vy - vxy_offset))
                    gx_idx = min(max(gx_idx, 0), 2 * nx - 1)
                    gy_idx = min(max(gy_idx, 0), 2 * ny - 1)

                    Gx_val = Gx_up[gy_idx, gx_idx]
                    Gy_val = Gy_up[gy_idx, gx_idx]

                    # dGauss^4 distance weight (liveSRRF.cl:190-192)
                    distanceWeight = distance * np.exp(-(distance * distance) / tss)
                    distanceWeight = distanceWeight ** 4
                    wSum += distanceWeight

                    # Convergence test — gradient must point inward
                    # liveSRRF.cl:193
                    GdotR = Gx_val * dx + Gy_val * dy
                    if GdotR < 0:
                        GMag = np.sqrt(Gx_val * Gx_val + Gy_val * Gy_val)
                        if GMag == 0:
                            Dk = distance  # fallback, will become 0 in the next line
                        else:
                            # Dk = distance * sin(theta), liveSRRF.cl:199
                            Dk = abs(Gy_val * dx - Gx_val * dy) / GMag

                        # Linear angular kernel: 1 - sin(theta), liveSRRF.cl:204
                        Dk = 1 - Dk / distance
                        # Dk is now in [0, 1]; 1 if vector points precisely to centre

                        CGLH += Dk * distanceWeight

            # Normalize and apply sensitivity exponent
            if wSum > 0:
                CGLH /= wSum
                if CGLH >= 0:
                    CGLH = CGLH ** sensitivity
                else:
                    CGLH = 0.0

            # Intensity weighting (optional)
            if intensity_weighting:
                # Interpolate the original image at the magnified pixel centre
                # liveSRRF.cl:258 (note the -0.5 grid convention)
                v = _interpolated_value(
                    img,
                    xM / magnification - 0.5,
                    yM / magnification - 0.5,
                )
                RGC[yM, xM] = v * CGLH
            else:
                RGC[yM, xM] = CGLH

    return RGC


def temporal_combine(
    stack: np.ndarray,
    mode: Literal["AVG", "VAR", "TAC2", "INT"] = "AVG",
) -> np.ndarray:
    """
    Temporal combination of RGC-weighted frames, matching liveSRRF_noInterpolation.cl:363-435.

    The input stack is assumed to be a sequence of per-frame RGC (or intensity-weighted
    RGC, X = v * RGC) images. This function computes:
      - AVG: mean of X over frames
      - VAR: variance of X over frames (2nd order SOFI, tau = 0)
      - TAC2: temporal autocorrelation at lag 1 (2nd order cumulant, tau = 1)
      - INT: interpolated widefield intensity (no RGC applied)

    The implementation follows liveSRRF_noInterpolation.cl:363-393 (accumulation)
    and kernelCalculateVar (lines 427-435) (cumulant conversion).

    Args:
        stack: (n_frames, ny, nx) sequence of per-frame images
        mode: which temporal statistic to compute

    Returns:
        (ny, nx) combined image

    Note on TAC2 normalisation:
        liveSRRF.cl has a bug where the previous frame is divided by (n-1) twice.
        liveSRRF_noInterpolation.cl:372 is the correct reference (no pre-division),
        and this implementation follows the latter.
    """
    if stack.ndim != 3:
        raise ValueError(f"stack must be 3D (n_frames, ny, nx), got shape {stack.shape}")

    n_frames, ny, nx = stack.shape

    if mode == "AVG":
        return stack.mean(axis=0)

    elif mode == "VAR":
        # Var[X] = E[X^2] - E[X]^2
        mean = stack.mean(axis=0)
        return (stack * stack).mean(axis=0) - mean * mean

    elif mode == "TAC2":
        # TAC2[X] = E[X(t) X(t+1)] - E[X]^2
        # Compute the lag-1 product mean
        lag1_product = (stack[:-1] * stack[1:]).mean(axis=0)
        mean = stack.mean(axis=0)
        return lag1_product - mean * mean

    elif mode == "INT":
        # Just the interpolated intensity (if input was intensity, not RGC)
        return stack.mean(axis=0)

    else:
        raise ValueError(f"Unknown mode: {mode}")

"""
Ground-truth simulation generators for eSRRF validation and benchmarking.

This module provides phantom generators with KNOWN emitter positions, so the
reconstruction can be validated by comparing against the true structure.

Phantoms include:
- Isolated point emitters
- Two-point pairs at varying separations (resolution test)
- Crossing filaments
- Ring/vesicle
- Siemens-star resolution target
- Dense random emitter field
- Two-colour phantoms (for channel mode validation)
"""

from __future__ import annotations

import numpy as np
from typing import Tuple, List
from dataclasses import dataclass


@dataclass
class Emitter:
    """
    A single point emitter with known position and brightness.

    Attributes:
        x: x position in native pixels (continuous, may be fractional)
        y: y position in native pixels (continuous, may be fractional)
        photons: expected photon count per frame (Poisson mean)
        channel: routing channel (default 0)
    """
    x: float
    y: float
    photons: float
    channel: int = 0


@dataclass
class CLSMScanParameters:
    """
    CLSM raster scan parameters.

    Attributes:
        nx: number of pixels per line (x axis)
        ny: number of lines (y axis)
        pixel_duration: dwell time per pixel (macro time units)
        line_duration: total time per line (including flyback)
        frame_marker_delay: delay between frames (if any)
    """
    nx: int = 256
    ny: int = 256
    pixel_duration: int = 100
    line_duration: int = 30000  # includes flyback
    frame_marker_delay: int = 5000


def gaussian_psf(
    x: float,
    y: float,
    emitters: List[Emitter],
    sigma: float = 1.0,
    background: float = 0.0,
) -> float:
    """
    Expected intensity at a point from a sum of Gaussian emitters.

    Args:
        x, y: query position
        emitters: list of Emitters with known positions and brightnesses
        sigma: Gaussian PSF sigma (in pixels)
        background: constant background level

    Returns:
        Expected photon count at (x, y) (before Poisson noise)
    """
    intensity = background
    for e in emitters:
        r2 = (x - e.x) ** 2 + (y - e.y) ** 2
        intensity += e.photons * np.exp(-r2 / (2 * sigma * sigma))
    return intensity


def render_frame(
    emitters: List[Emitter],
    params: CLSMScanParameters,
    sigma: float = 1.0,
    background: float = 0.0,
    noise_seed: Optional[int] = None,
) -> np.ndarray:
    """Vectorized rendering of a CLSM frame from emitters."""
    ny, nx = params.ny, params.nx
    if len(emitters) == 0:
        return np.full((ny, nx), background, dtype=float)
    
    x_grid = np.arange(nx, dtype=float)[None, :]
    y_grid = np.arange(ny, dtype=float)[:, None]
    ex = np.array([e.x for e in emitters])[None, None, :]
    ey = np.array([e.y for e in emitters])[None, None, :]
    ep = np.array([e.photons for e in emitters])[None, None, :]

    dx = x_grid[:, :, None] - ex
    dy = y_grid[:, :, None] - ey
    r2 = dx * dx + dy * dy
    frame = background + np.sum(ep * np.exp(-r2 / (2.0 * sigma * sigma)), axis=2)

    if noise_seed is not None:
        rng = np.random.default_rng(noise_seed)
        frame = rng.poisson(frame).astype(float)

    return frame


def render_blinking_frame_stack(
    emitters: List[Emitter],
    params: CLSMScanParameters,
    n_frames: int = 20,
    p_on: float = 0.20,
    sigma: float = 1.0,
    background: float = 0.0,
    seed: int = 42,
) -> np.ndarray:
    """Vectorized multi-frame rendering with ON/OFF blinking kinetics."""
    rng = np.random.default_rng(seed)
    stack = np.zeros((n_frames, params.ny, params.nx), dtype=float)

    for f in range(n_frames):
        active = [e for e in emitters if rng.random() < p_on]
        frame = render_frame(active, params, sigma=sigma, background=background)
        stack[f] = rng.poisson(frame).astype(float)

    return stack


def render_photon_stream(
    emitters: List[Emitter],
    params: CLSMScanParameters,
    n_frames: int,
    sigma: float = 1.0,
    background: float = 0.0,
    noise_seed: Optional[int] = None,
) -> Tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """
    Render a CLSM photon stream from known emitters.

    This generates a TTTR-like representation of the photons, with:
    - Exact macro times (for exact x reconstruction)
    - Frame, line, and pixel indices (for CLSMImage compatibility)
    - Micro times (dummy, all zero for now)
    - Routing channels

    Args:
        emitters: list of Emitters
        params: scan parameters
        n_frames: number of frames to render
        sigma: Gaussian PSF sigma
        background: background level
        noise_seed: random seed for Poisson noise

    Returns:
        macro_times: (N,) macro time for each photon
        frames: (N,) frame index for each photon
        lines: (N,) line index for each photon
        pixels: (N,) pixel index for each photon
        routing_channels: (N,) routing channel for each photon
    """
    rng = np.random.default_rng(noise_seed)

    # First pass: count total photons (expensive but straightforward)
    all_photons = []

    for f in range(n_frames):
        for y in range(params.ny):
            # Line start time
            line_start = f * params.frame_marker_delay + y * params.line_duration

            for x in range(params.nx):
                # Pixel start time
                pixel_start = line_start + x * params.pixel_duration

                # Expected photons at this pixel
                expected = gaussian_psf(
                    float(x), float(y), emitters, sigma, background
                )

                # Sample photon count
                n_photons = rng.poisson(expected)

                # For each photon, assign exact macro time and channel
                for _ in range(n_photons):
                    # Exact macro time within the pixel (uniform)
                    macro_time = pixel_start + rng.random() * params.pixel_duration

                    # Assign to a channel (weighted by emitter brightness)
                    # This is a simplification: real multi-colour depends on labelling
                    total_photons = sum(e.photons for e in emitters)
                    if total_photons > 0:
                        r = rng.random() * total_photons
                        acc = 0.0
                        for e in emitters:
                            acc += e.photons
                            if r < acc:
                                channel = e.channel
                                break
                        else:
                            channel = 0
                    else:
                        channel = 0

                    all_photons.append(
                        (macro_time, f, y, x, channel)
                    )

    if not all_photons:
        return (
            np.array([], dtype=np.uint64),
            np.array([], dtype=int),
            np.array([], dtype=int),
            np.array([], dtype=int),
            np.array([], dtype=int),
        )

    # Convert to arrays
    data = np.array(all_photons, dtype=object)
    macro_times = np.array(data[:, 0], dtype=np.uint64)
    frames = np.array(data[:, 1], dtype=int)
    lines = np.array(data[:, 2], dtype=int)
    pixels = np.array(data[:, 3], dtype=int)
    routing_channels = np.array(data[:, 4], dtype=int)

    return macro_times, frames, lines, pixels, routing_channels


# ----------------------------------------------------------------------
# Specific phantom generators
# ----------------------------------------------------------------------


def isolated_point_emitters(
    n_points: int = 9,
    spacing: float = 20.0,
    photons_per_point: float = 1000.0,
    nx: int = 128,
    ny: int = 128,
) -> Tuple[List[Emitter], CLSMScanParameters]:
    """
    Generate isolated point emitters on a regular grid.

    Useful for checking that reassignment localizes correctly and doesn't create
    spurious structures.
    """
    emitters = []
    grid_side = int(np.ceil(np.sqrt(n_points)))

    offset_x = (nx - spacing * (grid_side - 1)) / 2
    offset_y = (ny - spacing * (grid_side - 1)) / 2

    for i in range(grid_side):
        for j in range(grid_side):
            if len(emitters) >= n_points:
                break
            x = offset_x + i * spacing
            y = offset_y + j * spacing
            emitters.append(Emitter(x=x, y=y, photons=photons_per_point))

    params = CLSMScanParameters(nx=nx, ny=ny)
    return emitters, params


def two_point_pairs(
    separations: List[float],
    photons_per_point: float = 1000.0,
    nx: int = 128,
    ny: int = 128,
) -> List[Tuple[List[Emitter], float, CLSMScanParameters]]:
    """
    Generate two-point emitter pairs at varying separations.

    Returns a list of (emitters, separation, params) for testing resolution.
    """
    results = []

    for sep in separations:
        # Center the pair
        x0 = (nx - sep) / 2
        x1 = x0 + sep
        y = ny / 2

        emitters = [
            Emitter(x=x0, y=y, photons=photons_per_point),
            Emitter(x=x1, y=y, photons=photons_per_point),
        ]
        params = CLSMScanParameters(nx=nx, ny=ny)
        results.append((emitters, sep, params))

    return results


def crossing_filaments(
    nx: int = 128,
    ny: int = 128,
    photons_per_pixel: float = 100.0,
    width: float = 2.0,
) -> Tuple[List[Emitter], CLSMScanParameters]:
    """
    Generate two crossing filaments (horizontal and vertical).

    Filaments are modeled as dense chains of point emitters.
    """
    emitters = []

    # Horizontal filament
    for x in np.linspace(10, nx - 10, nx // 2):
        emitters.append(Emitter(x=x, y=ny / 2, photons=photons_per_pixel))

    # Vertical filament
    for y in np.linspace(10, ny - 10, ny // 2):
        emitters.append(Emitter(x=nx / 2, y=y, photons=photons_per_pixel))

    params = CLSMScanParameters(nx=nx, ny=ny)
    return emitters, params


def ring_phantom(
    radius: float = 30.0,
    nx: int = 128,
    ny: int = 128,
    photons_per_point: float = 100.0,
    n_points: int = 60,
) -> Tuple[List[Emitter], CLSMScanParameters]:
    """
    Generate a ring/vesicle phantom.

    The ring is approximated by point emitters on a circle.
    """
    emitters = []

    cx, cy = nx / 2, ny / 2

    for angle in np.linspace(0, 2 * np.pi, n_points, endpoint=False):
        x = cx + radius * np.cos(angle)
        y = cy + radius * np.sin(angle)
        emitters.append(Emitter(x=x, y=y, photons=photons_per_point))

    params = CLSMScanParameters(nx=nx, ny=ny)
    return emitters, params


def siemens_star(
    n_spokes: int = 12,
    max_radius: float = 40.0,
    nx: int = 128,
    ny: int = 128,
    photons_per_pixel: float = 100.0,
) -> Tuple[List[Emitter], CLSMScanParameters]:
    """
    Generate a Siemens-star resolution target.

    Alternating spokes with angular width pi / n_spokes.
    """
    emitters = []
    cx, cy = nx / 2, ny / 2

    # Sample a dense grid and keep only points in the spokes
    for x in np.linspace(cx - max_radius, cx + max_radius, int(max_radius * 2)):
        for y in np.linspace(cy - max_radius, cy + max_radius, int(max_radius * 2)):
            dx, dy = x - cx, y - cy
            r = np.sqrt(dx * dx + dy * dy)
            if r > max_radius or r < 5:
                continue

            angle = np.arctan2(dy, dx)
            spoke_index = int((angle + np.pi) / (2 * np.pi) * n_spokes) % n_spokes
            if spoke_index % 2 == 0:
                emitters.append(Emitter(x=x, y=y, photons=photons_per_pixel))

    params = CLSMScanParameters(nx=nx, ny=ny)
    return emitters, params


def dense_random_field(
    n_emitters: int = 50,
    nx: int = 128,
    ny: int = 128,
    photons_per_point: float = 500.0,
    seed: int = 20260731,
) -> Tuple[List[Emitter], CLSMScanParameters]:
    """
    Generate a dense random emitter field.

    This tests whether the method hallucinates structure where there is none.
    """
    rng = np.random.default_rng(seed)

    emitters = []
    for _ in range(n_emitters):
        x = rng.uniform(10, nx - 10)
        y = rng.uniform(10, ny - 10)
        emitters.append(Emitter(x=x, y=y, photons=photons_per_point))

    params = CLSMScanParameters(nx=nx, ny=ny)
    return emitters, params


def two_colour_filaments(
    nx: int = 128,
    ny: int = 128,
    photons_per_pixel: float = 100.0,
    channel_separation: float = 5.0,
) -> Tuple[List[Emitter], CLSMScanParameters]:
    """
    Generate two crossing filaments in different channels.

    Tests the split channel mode: a shared prior should bleed one colour
    into the other, while split mode keeps them separate.
    """
    emitters = []

    # Channel 0: horizontal filament
    for x in np.linspace(10, nx - 10, nx // 2):
        emitters.append(
            Emitter(x=x, y=ny / 2 - channel_separation / 2,
                   photons=photons_per_pixel, channel=0)
        )

    # Channel 1: vertical filament
    for y in np.linspace(10, ny - 10, ny // 2):
        emitters.append(
            Emitter(x=nx / 2 + channel_separation / 2, y=y,
                   photons=photons_per_pixel, channel=1)
        )

    params = CLSMScanParameters(nx=nx, ny=ny)
    return emitters, params


def detector_grid(n_side: int, geometry: str = "rect") -> np.ndarray:
    """
    Normalized coordinates of the detector element centres.

    Ported from BrightEyes-ISM ``detector.rect_grid`` / ``hex_grid``. A
    hexagonal packing is what real SPAD arrays use -- the 23-element array of
    the CW-SOFISM work, for instance -- not the square lattice a Gaussian toy
    model usually assumes.

    Parameters
    ----------
    n_side : int
        Elements per side of the generating square.
    geometry : str
        ``'rect'`` or ``'hex'``.

    Returns
    -------
    np.ndarray
        ``(n_elements, 2)`` array of (x, y) centres in element units. A
        hexagonal grid is trimmed to the inscribed region, so it returns fewer
        than ``n_side ** 2`` elements.
    """
    x = np.arange(-(n_side // 2), n_side // 2 + 1)
    if geometry == "rect":
        return np.array([[i, j] for i in x for j in x], dtype=float)
    if geometry == "hex":
        s = np.array([[0.5 * np.sqrt(3) * i, j - 0.5 * (i % 2)] for i in x for j in x])
        return s[np.abs(s[:, 1]) <= (n_side // 2)]
    raise ValueError("geometry must be 'rect' or 'hex'")


def airy_psf(shape, na: float, wavelength_nm: float, pixel_size_nm: float,
             centre=None) -> np.ndarray:
    """
    Scalar diffraction-limited PSF: the Airy pattern ``(2 J1(v) / v) ** 2``.

    The exact scalar result rather than the Gaussian approximation to it. The
    difference is in the *wings*: a Gaussian has none, so it understates both
    the out-of-focus background and the crosstalk between neighbouring
    detector elements.
    """
    from scipy.special import j1

    ny, nx = shape
    cy, cx = ((ny - 1) / 2.0, (nx - 1) / 2.0) if centre is None else centre
    y, x = np.mgrid[0:ny, 0:nx]
    r_nm = np.hypot(x - cx, y - cy) * pixel_size_nm
    v = 2.0 * np.pi * na * r_nm / wavelength_nm
    out = np.ones_like(v)
    nz = v > 1e-12
    out[nz] = (2.0 * j1(v[nz]) / v[nz]) ** 2
    return out


def vectorial_psf(shape, na: float, wavelength_nm: float, pixel_size_nm: float,
                  n_immersion: float = 1.518, polarization: str = "circular",
                  z_nm: float = 0.0, centre=None, n_theta: int = 300) -> np.ndarray:
    r"""
    Vectorial (polarization-aware) PSF from the Richards-Wolf integral.

    Above roughly NA 1.0 the scalar approximation stops being defensible: the
    strong focusing of an aplanatic lens tips the field out of the transverse
    plane, and the longitudinal component :math:`E_z` it creates is not small.
    With *linear* illumination that makes the focal spot measurably **elongated
    along the polarization axis** -- an asymmetry a scalar or Gaussian model
    cannot produce at all, and which propagates straight into any ISM shift
    vector or reconstruction derived from it.

    Richards & Wolf, *Proc. R. Soc. A* **253**, 358 (1959):

    .. math::

        E_x \propto I_0 + I_2\cos 2\phi, \quad
        E_y \propto I_2 \sin 2\phi, \quad
        E_z \propto -2 i I_1 \cos\phi

    with

    .. math::

        I_0 &= \int_0^\alpha \sqrt{\cos\theta}\,\sin\theta\,(1+\cos\theta)\,
                J_0(k r \sin\theta)\, e^{i k z \cos\theta}\, d\theta \\
        I_1 &= \int_0^\alpha \sqrt{\cos\theta}\,\sin^2\theta\,
                J_1(k r \sin\theta)\, e^{i k z \cos\theta}\, d\theta \\
        I_2 &= \int_0^\alpha \sqrt{\cos\theta}\,\sin\theta\,(1-\cos\theta)\,
                J_2(k r \sin\theta)\, e^{i k z \cos\theta}\, d\theta

    where :math:`\alpha = \arcsin(\mathrm{NA}/n)` and :math:`k = 2\pi n/\lambda`.

    Parameters
    ----------
    shape : tuple
        ``(ny, nx)``.
    na, wavelength_nm, pixel_size_nm : float
        Numerical aperture, wavelength, pixel size.
    n_immersion : float
        Refractive index of the immersion medium; 1.518 for oil, 1.0 for air.
        ``na`` must not exceed it.
    polarization : str
        ``'x'`` or ``'y'`` for linear, ``'circular'`` for circular or
        unpolarized (the two coincide in intensity after the azimuthal average).
    z_nm : float
        Defocus, in nanometres.
    n_theta : int
        Quadrature points over the aperture angle.

    Returns
    -------
    np.ndarray
        Intensity PSF :math:`|E_x|^2 + |E_y|^2 + |E_z|^2`, peak-normalized.

    Notes
    -----
    As NA/n falls this converges to the scalar Airy pattern, which is the check
    :func:`airy_psf` provides.
    """
    from scipy.special import jv

    if na >= n_immersion:
        raise ValueError(f"NA {na} must be below the immersion index {n_immersion}")
    if polarization not in ("x", "y", "circular"):
        raise ValueError("polarization must be 'x', 'y' or 'circular'")

    ny, nx = shape
    cy, cx = ((ny - 1) / 2.0, (nx - 1) / 2.0) if centre is None else centre
    yy, xx = np.mgrid[0:ny, 0:nx]
    dx = (xx - cx) * pixel_size_nm
    dy = (yy - cy) * pixel_size_nm
    r = np.hypot(dx, dy).ravel()
    phi = np.arctan2(dy, dx).ravel()

    alpha = np.arcsin(na / n_immersion)
    theta = np.linspace(0.0, alpha, n_theta)
    k = 2.0 * np.pi * n_immersion / wavelength_nm

    st, ct = np.sin(theta), np.cos(theta)
    common = np.sqrt(ct) * st * np.exp(1j * k * z_nm * ct)
    krs = k * r[:, None] * st[None, :]

    i0 = np.trapezoid(common * (1.0 + ct) * jv(0, krs), theta, axis=1)
    i1 = np.trapezoid(common * st * jv(1, krs), theta, axis=1)
    i2 = np.trapezoid(common * (1.0 - ct) * jv(2, krs), theta, axis=1)

    if polarization == "circular":
        # averaged over the polarization angle; the cross term integrates away
        intensity = np.abs(i0) ** 2 + 2.0 * np.abs(i1) ** 2 + np.abs(i2) ** 2
    else:
        angle = phi if polarization == "x" else phi - np.pi / 2.0
        ex = i0 + i2 * np.cos(2.0 * angle)
        ey = i2 * np.sin(2.0 * angle)
        ez = -2.0 * i1 * np.cos(angle)
        intensity = np.abs(ex) ** 2 + np.abs(ey) ** 2 + np.abs(ez) ** 2

    intensity = intensity.reshape(ny, nx)
    peak = intensity.max()
    return intensity / peak if peak > 0 else intensity


def generate_ism_psf(
    na: float = 1.4,
    wavelength_exc: float = 488.0,
    wavelength_det: float = 520.0,
    n_det: int = 5,
    pitch_au: float = 0.25,
    nx: int = 64,
    ny: int = 64,
    pixel_size_nm: float = 10.0,
    pinhole_shape: str = 'square',
    geometry: str = 'rect',
    model: str = 'gaussian',
    n_immersion: float = 1.518,
    polarization: str = 'circular'
):
    """
    Generate physical Image Scanning Microscopy (ISM) Point Spread Functions (PSFs).

    Simulates the excitation PSF, detection PSF, SPAD detector array geometry,
    and calculates individual detector channel PSFs, standard CLSM sum image,
    and shift-reassigned ISM PSF.

    Parameters
    ----------
    geometry : str
        Detector lattice, ``'rect'`` or ``'hex'`` (see :func:`detector_grid`).
    model : str
        ``'gaussian'`` for the usual approximation, ``'airy'`` for the exact
        scalar diffraction PSF, or ``'vectorial'`` for the Richards-Wolf
        calculation. Use ``'vectorial'`` above about NA 1.0, where the
        longitudinal field is not negligible.
    n_immersion : float
        Immersion index, used by the vectorial model only.
    polarization : str
        Excitation polarization for the vectorial model: ``'x'``, ``'y'`` or
        ``'circular'``. Linear polarization elongates the focal spot along its
        own axis, which a scalar model cannot reproduce.
    """
    airy_radius_nm = 0.61 * wavelength_det / na
    sigma_exc = (0.5 * wavelength_exc / na) / 2.35482
    sigma_det = (0.5 * wavelength_det / na) / 2.35482

    sigma_exc_px = sigma_exc / pixel_size_nm
    sigma_det_px = sigma_det / pixel_size_nm
    pitch_px = (pitch_au * airy_radius_nm) / pixel_size_nm

    x_grid = np.arange(nx, dtype=float) - nx / 2.0
    y_grid = np.arange(ny, dtype=float)[:, None] - ny / 2.0
    r2_grid = x_grid[None, :]**2 + y_grid**2

    if model == 'vectorial':
        h_exc = vectorial_psf((ny, nx), na, wavelength_exc, pixel_size_nm,
                              n_immersion=n_immersion, polarization=polarization,
                              centre=(ny / 2.0, nx / 2.0))
    elif model == 'airy':
        h_exc = airy_psf((ny, nx), na, wavelength_exc, pixel_size_nm,
                         centre=(ny / 2.0, nx / 2.0))
    elif model == 'gaussian':
        h_exc = np.exp(-r2_grid / (2.0 * sigma_exc_px**2))
    else:
        raise ValueError("model must be 'gaussian', 'airy' or 'vectorial'")

    # element centres on the requested lattice, scaled to the pitch
    det_offsets = detector_grid(n_det, geometry) * pitch_px
    channel_psfs = np.zeros((len(det_offsets), ny, nx), dtype=float)

    for idx, (dx_ch, dy_ch) in enumerate(det_offsets):
        if model == 'vectorial':
            # detection is incoherent over dipole orientation, so the
            # circularly-averaged form is the right one whatever the excitation
            h_det_k = vectorial_psf((ny, nx), na, wavelength_det, pixel_size_nm,
                                    n_immersion=n_immersion,
                                    polarization='circular',
                                    centre=(ny / 2.0 + dy_ch, nx / 2.0 + dx_ch))
        elif model == 'airy':
            h_det_k = airy_psf((ny, nx), na, wavelength_det, pixel_size_nm,
                               centre=(ny / 2.0 + dy_ch, nx / 2.0 + dx_ch))
        else:
            r2_ch = (x_grid[None, :] - dx_ch)**2 + (y_grid - dy_ch)**2
            h_det_k = np.exp(-r2_ch / (2.0 * sigma_det_px**2))
        ch_psf = h_exc * h_det_k
        channel_psfs[idx] = ch_psf / ch_psf.sum()
    sum_psf = channel_psfs.sum(axis=0)

    # Shift-reassigned ISM PSF
    reassigned_psf = np.zeros((ny, nx), dtype=float)
    from scipy.ndimage import shift as nd_shift
    for k in range(len(det_offsets)):
        shift_vec = -0.5 * det_offsets[k]
        reassigned_psf += nd_shift(channel_psfs[k], shift=(shift_vec[1], shift_vec[0]), order=1)

    return {
        'channel_psfs': channel_psfs,
        'detector_offsets': det_offsets,
        'sum_psf': sum_psf,
        'reassigned_psf': reassigned_psf,
        'airy_radius_nm': airy_radius_nm
    }

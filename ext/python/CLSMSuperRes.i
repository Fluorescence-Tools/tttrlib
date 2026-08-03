// SPDX-License-Identifier: BSD-3-Clause
// CLSMSuperRes SWIG interface (embedded into the main tttrlib module)
%include "misc_types.i"

%{
#include "CLSMSuperRes.h"
%}

// Ignore pointer-returning overloads; only expose the array+dims variants
%ignore CLSMSuperRes::rgc_map(const double*, int, int, int, double, int, bool);
%ignore CLSMSuperRes::temporal_combine(const double*, int, int, int, const char*);

// Apply NumPy typemaps for input arrays (dimension arguments auto-extracted).
%apply (double* IN_ARRAY2, int DIM1, int DIM2) {(const double* img, int ny, int nx)};
%apply (double* IN_ARRAY3, int DIM1, int DIM2, int DIM3) {(const double* stack, int n_frames, int ny, int nx)};
%apply (double* IN_ARRAY1, int DIM1) {(const double* detector_offsets, int n_detector_offsets)};
%apply (double* IN_ARRAY1, int DIM1) {(const double* detector_coords, int detector_coords_len)};
%apply (double** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {(double** output, int* out_dim1, int* out_dim2)};
%apply (double* IN_ARRAY4, int DIM1, int DIM2, int DIM3, int DIM4) {(const double* data, int n_time, int n_det, int ny, int nx)};
%apply (double* IN_ARRAY3, int DIM1, int DIM2, int DIM3) {(const double* data, int n_ch, int ny, int nx)};
%apply (double* IN_ARRAY4, int DIM1, int DIM2, int DIM3, int DIM4) {(const double* psf, int psf_nz, int psf_nch, int psf_ny, int psf_nx)};

// Typemap for temporal_combine's double** output parameter
#ifdef SWIGPYTHON
%typemap(in, numinputs=0) double** output (double* temp = NULL) {
  $1 = &temp;
}

%typemap(argout, fragment="NumPy_Backward_Compatibility,NumPy_Utilities")
  (const double* stack, int n_frames, int ny, int nx, const char* mode, double** output)
{
  npy_intp dims[2] = { (npy_intp)$3, (npy_intp)$4 };
  PyObject* obj = PyArray_SimpleNewFromData(2, dims, NPY_DOUBLE, (void*)(*$6));
  PyArrayObject* array = (PyArrayObject*) obj;
  if (!array) SWIG_fail;
  PyObject* cap = PyCapsule_New((void*)(*$6), SWIGPY_CAPSULE_NAME, free_cap);
  PyArray_SetBaseObject(array, cap);
  $result = SWIG_AppendOutput($result, obj);
}
#endif

// Rename native methods for Python shim binding
%rename(_native_rgc_map) CLSMSuperRes::rgc_map;
%rename(_native_temporal_combine) CLSMSuperRes::temporal_combine;
%rename(_native_reassign_photons) CLSMSuperRes::reassign_photons;
%rename(_native_shift_vectors) CLSMSuperRes::shift_vectors;
%rename(_native_sofism_reconstruction) CLSMSuperRes::sofism_reconstruction;
%rename(_native_s2ism_reconstruction) CLSMSuperRes::s2ism_reconstruction;
%rename(_native_apr_reconstruction) CLSMSuperRes::apr_reconstruction(const double* data, int dim0, int dim1, int dim2, bool channels_last, double** output, int* out_dim1, int* out_dim2, int* out_dim3, int usf, int ref_idx, double filter_sigma, int n_det);
%rename(_native_focus_reconstruction) CLSMSuperRes::focus_reconstruction(const double* data, int dim0, int dim1, int dim2, bool channels_last, double** output, int* out_dim1, int* out_dim2, int* out_dim3, double sigma_bound, double threshold, int calibration_size, bool parallelize, int n_det, const double* detector_coords, int detector_coords_len);

// get_photon_positions: five output arrays sharing a single count (n_photons).
#ifdef SWIGPYTHON
%typemap(in, numinputs=0)
  (int** out_frame, int** out_line, double** out_x_exact, double** out_y_line, int** out_event_idx, int* n_photons)
  (int* frame_temp = NULL, int* line_temp = NULL, double* x_temp = NULL, double* y_temp = NULL, int* event_temp = NULL, int count_temp = 0)
{
  $1 = &frame_temp; $2 = &line_temp; $3 = &x_temp; $4 = &y_temp; $5 = &event_temp; $6 = &count_temp;
}

%typemap(argout, fragment="NumPy_Backward_Compatibility,NumPy_Utilities")
  (int** out_frame, int** out_line, double** out_x_exact, double** out_y_line, int** out_event_idx, int* n_photons)
{
  npy_intp dims[1] = { *$6 };
  {
    PyObject* obj = PyArray_SimpleNewFromData(1, dims, NPY_INT, (void*)(*$1));
    PyArrayObject* array = (PyArrayObject*) obj;
    if (!array) SWIG_fail;
    PyObject* cap = PyCapsule_New((void*)(*$1), SWIGPY_CAPSULE_NAME, free_cap);
    PyArray_SetBaseObject(array, cap);
    $result = SWIG_AppendOutput($result, obj);
  }
  {
    PyObject* obj = PyArray_SimpleNewFromData(1, dims, NPY_INT, (void*)(*$2));
    PyArrayObject* array = (PyArrayObject*) obj;
    if (!array) SWIG_fail;
    PyObject* cap = PyCapsule_New((void*)(*$2), SWIGPY_CAPSULE_NAME, free_cap);
    PyArray_SetBaseObject(array, cap);
    $result = SWIG_AppendOutput($result, obj);
  }
  {
    PyObject* obj = PyArray_SimpleNewFromData(1, dims, NPY_DOUBLE, (void*)(*$3));
    PyArrayObject* array = (PyArrayObject*) obj;
    if (!array) SWIG_fail;
    PyObject* cap = PyCapsule_New((void*)(*$3), SWIGPY_CAPSULE_NAME, free_cap);
    PyArray_SetBaseObject(array, cap);
    $result = SWIG_AppendOutput($result, obj);
  }
  {
    PyObject* obj = PyArray_SimpleNewFromData(1, dims, NPY_DOUBLE, (void*)(*$4));
    PyArrayObject* array = (PyArrayObject*) obj;
    if (!array) SWIG_fail;
    PyObject* cap = PyCapsule_New((void*)(*$4), SWIGPY_CAPSULE_NAME, free_cap);
    PyArray_SetBaseObject(array, cap);
    $result = SWIG_AppendOutput($result, obj);
  }
  {
    PyObject* obj = PyArray_SimpleNewFromData(1, dims, NPY_INT, (void*)(*$5));
    PyArrayObject* array = (PyArrayObject*) obj;
    if (!array) SWIG_fail;
    PyObject* cap = PyCapsule_New((void*)(*$5), SWIGPY_CAPSULE_NAME, free_cap);
    PyArray_SetBaseObject(array, cap);
    $result = SWIG_AppendOutput($result, obj);
  }
}
#endif

%include "CLSMSuperRes.h"

#ifdef SWIGPYTHON
%pythoncode %{
import tttrlib
import numpy as np
from pathlib import Path

tttrlib.mark_experimental(
    CLSMSuperRes,
    "Photon-level super-resolution (eSRRF) is experimental. API may change."
)

# Keyword-friendly wrappers over the flat native entry points. These must not be
# wrapped in a try/except: a failure here would silently leave the class without
# its methods, which is far harder to diagnose than an import error.
_cls = globals()['CLSMSuperRes']


def _rgc_map(img, magnification=2, fwhm=1.5, sensitivity=1, intensity_weighting=False):
    """Radial Gradient Convergence map of a 2-D image on an M-times finer grid."""
    arr = np.ascontiguousarray(img, dtype=np.float64)
    if arr.ndim != 2:
        raise ValueError(f"rgc_map expects a 2-D image, got shape {arr.shape}")
    return _cls._native_rgc_map(
        arr, int(magnification), float(fwhm), int(sensitivity), bool(intensity_weighting)
    )


def _temporal_combine(stack, mode="AVG"):
    """Combine a stack of per-frame images: AVG, VAR, TAC2 or INT."""
    arr = np.ascontiguousarray(stack, dtype=np.float64)
    if arr.ndim != 3:
        raise ValueError(f"temporal_combine expects a 3-D stack, got shape {arr.shape}")
    return _cls._native_temporal_combine(arr, str(mode))


def _read_tiff(filename):
    """Read a TIFF stack as a float64 array."""
    try:
        import tifffile
    except ImportError as e:
        raise RuntimeError(
            f"Reading {filename} needs the 'tifffile' package"
        ) from e
    return np.ascontiguousarray(tifffile.imread(str(filename)), dtype=np.float64)


def _as_detector_cube(data):
    """Coerce a TIFF path, CLSMImage or array-like into a (n_det, ny, nx) cube."""
    if isinstance(data, (str, Path)):
        arr = _read_tiff(data)
    elif hasattr(data, 'intensity'):
        arr = np.asarray(getattr(data, 'intensity'), dtype=np.float64)
    else:
        arr = np.asarray(data, dtype=np.float64)
    if arr.ndim != 3:
        raise ValueError(
            f"Expected a 3-D detector cube (n_det, ny, nx), got shape {arr.shape}"
        )
    return np.ascontiguousarray(arr, dtype=np.float64)


def _shift_vectors(data, usf=10, ref_idx=-1, filter_sigma=0.0,
                   channels_last=False, n_det=-1):
    """Per-element shift vectors of an array-detector cube, as (n_det, 2) = (dy, dx)."""
    cube = _as_detector_cube(data)
    return _cls._native_shift_vectors(
        cube, bool(channels_last), int(usf), int(ref_idx), float(filter_sigma), int(n_det)
    )


def _apr_reconstruction(data, usf=10, ref_idx=-1, filter_sigma=0.0,
                        channels_last=False, n_det=-1):
    """Adaptive pixel reassignment of an array-detector cube; returns (1, ny, nx)."""
    cube = _as_detector_cube(data)
    return _cls._native_apr_reconstruction(
        cube, bool(channels_last), int(usf), int(ref_idx), float(filter_sigma), int(n_det)
    )


def _focus_reconstruction(data, sigma_bound=2.0, threshold=0.0, calibration_size=10,
                          parallelize=False, channels_last=False, n_det=-1,
                          detector_coords=None):
    """Focus-ISM: returns (3, ny, nx) = in-focus signal, background, APR sum."""
    cube = _as_detector_cube(data)
    if detector_coords is None:
        coords1d = np.empty((0,), dtype=np.float64)
    else:
        coords1d = np.ascontiguousarray(
            np.asarray(detector_coords, dtype=np.float64).ravel(order='C'), dtype=np.float64
        )
    return _cls._native_focus_reconstruction(
        cube, bool(channels_last), float(sigma_bound), float(threshold),
        int(calibration_size), bool(parallelize), int(n_det), coords1d
    )


def _sofism_reconstruction(data, lag=0, usf=10, ref_idx=-1, filter_sigma=0.0,
                           include_auto=False):
    """
    SOFISM image from a time-resolved array-detector acquisition.

    `data` is (n_time, n_det, ny, nx): at every scan position the array records
    a short time series, one plane per detector element. Returns (ny, nx).
    """
    cube = np.ascontiguousarray(np.asarray(data, dtype=np.float64))
    if cube.ndim != 4:
        raise ValueError(
            f"sofism_reconstruction expects (n_time, n_det, ny, nx), got shape {cube.shape}"
        )
    return _cls._native_sofism_reconstruction(
        cube, int(lag), int(usf), int(ref_idx), float(filter_sigma), bool(include_auto)
    )


def _s2ism_reconstruction(data, psf, max_iter=100, threshold=1e-3,
                          auto_stop=False, init_from_sum=False):
    """
    s2ISM: joint super-resolution and optical sectioning by adaptive
    maximum-likelihood deconvolution over a stack of axial planes.

    `data` is (n_ch, ny, nx); `psf` is (nz, n_ch, ny, nx), centred in the frame.
    Returns the (nz, ny, nx) object estimate, focal plane at index nz // 2.
    """
    cube = np.ascontiguousarray(np.asarray(data, dtype=np.float64))
    h = np.ascontiguousarray(np.asarray(psf, dtype=np.float64))
    if cube.ndim != 3:
        raise ValueError(f"s2ism_reconstruction expects (n_ch, ny, nx), got {cube.shape}")
    if h.ndim != 4:
        raise ValueError(f"the PSF must be (nz, n_ch, ny, nx), got {h.shape}")
    return _cls._native_s2ism_reconstruction(
        cube, h, int(max_iter), float(threshold), bool(auto_stop), bool(init_from_sum)
    )


def _fourier_reweight(image, otf, epsilon=1e-3):
    """
    Wiener-type Fourier reweighting of a SOFISM image (Sroda et al., Eq. 3):
    multiply the spectrum by W(k) = 1 / (OTF(k)^2 + epsilon).

    `otf` is the modulus of the ISM optical transfer function on the same grid,
    normalised to 1 at zero frequency -- in practice measured on a calibration
    bead, or computed from a known PSF.
    """
    img = np.asarray(image, dtype=np.float64)
    h = np.asarray(otf, dtype=np.float64)
    if img.shape != h.shape:
        raise ValueError("image and otf must have the same shape")
    h = np.abs(h) / (np.abs(h).max() + 1e-300)
    weighted = np.fft.fft2(img) * np.fft.ifftshift(1.0 / (h ** 2 + epsilon))
    return np.real(np.fft.ifft2(weighted))


def _frc_hann2d(ny, nx):
    """Separable Hann window, matching BrightEyes-ISM FRC_lib.hann2d."""
    wy = 0.5 * (1 - np.cos(2 * np.pi * np.arange(ny) / (ny - 1)))
    wx = 0.5 * (1 - np.cos(2 * np.pi * np.arange(nx) / (nx - 1)))
    return np.outer(wy, wx)


def _frc_radial_profile(data, center):
    """Angular sum per integer radius, matching FRC_lib.radial_profile."""
    y, x = np.indices(data.shape)
    r = np.sqrt((x - center[0]) ** 2 + (y - center[1]) ** 2).astype(int)
    tbin = np.bincount(r.ravel(), np.real(data).ravel()).astype(np.complex128)
    tbin += 1j * np.bincount(r.ravel(), np.imag(data).ravel())
    return tbin, np.bincount(r.ravel())


def _frc_curve(image_a, image_b):
    """
    Fourier ring correlation of two images that differ only in their noise.

    Port of BrightEyes-ISM FRC_lib.FRC: both images are Hann-apodized before
    the transform -- without that the spectral leakage from the frame edges
    correlates perfectly between the two halves and holds the curve up at every
    frequency -- and the rings are summed by integer radius about the centre.
    """
    i1 = np.asarray(image_a, dtype=np.float64)
    i2 = np.asarray(image_b, dtype=np.float64)
    if i1.ndim != 2 or i1.shape != i2.shape:
        raise ValueError("frc_curve expects two 2-D images of equal shape")

    m, n = i1.shape
    centre = [int((n + n % 2) / 2), int((m + m % 2) / 2)]
    window = _frc_hann2d(m, n)

    ft1 = np.fft.fftshift(np.fft.fft2(i1 * window))
    ft2 = np.fft.fftshift(np.fft.fft2(i2 * window))

    num = np.real(_frc_radial_profile(ft1 * np.conj(ft2), centre)[0])
    den = np.real(_frc_radial_profile(np.abs(ft1) ** 2, centre)[0])
    den = den * np.real(_frc_radial_profile(np.abs(ft2) ** 2, centre)[0])
    with np.errstate(divide="ignore", invalid="ignore"):
        return np.nan_to_num(num / np.sqrt(den))


def _frc_smooth(x, y, frac=0.05):
    """
    LOWESS smoothing of the FRC curve on a 100x finer axis (FRC_lib.smooth).

    Locally weighted linear regression with a tricube kernel; equivalent to
    statsmodels' lowess with it=0, reimplemented so the FRC does not drag in a
    statistics package.
    """
    x_interp = np.linspace(x[0], x[-1], num=100 * len(x))
    y_interp = np.interp(x_interp, x, y)

    n = len(x_interp)
    r = int(np.ceil(frac * n))
    out = np.empty(n)
    for i in range(n):
        # the r *nearest* points, as statsmodels' lowess does -- a symmetric
        # +-r window would be twice as wide and oversmooth
        lo = min(max(i - r // 2, 0), max(n - r, 0))
        hi = min(lo + r, n)
        xs, ys = x_interp[lo:hi], y_interp[lo:hi]
        d = np.abs(xs - x_interp[i])
        dmax = d.max()
        w = (1 - (d / dmax) ** 3) ** 3 if dmax > 0 else np.ones_like(d)
        sw = w.sum()
        mx = (w * xs).sum() / sw
        my = (w * ys).sum() / sw
        var = (w * (xs - mx) ** 2).sum()
        slope = (w * (xs - mx) * (ys - my)).sum() / var if var > 0 else 0.0
        out[i] = my + slope * (x_interp[i] - mx)
    return x_interp, out


def _frc_fixed_threshold(frc, y):
    """First crossing of a constant threshold (FRC_lib.fixed_threshold)."""
    th = np.ones(len(frc)) * y
    idx = np.argwhere(np.diff(np.sign(frc - y))).flatten()
    return th, (int(idx[0]) if idx.size else 0)


def _frc_nsigma_threshold(k, frc, img, sigma):
    """n-sigma threshold curve (FRC_lib.nsigma_threshold)."""
    m, n = np.asarray(img).shape
    centre = [int((n + n % 2) / 2), int((m + m % 2) / 2)]
    nr = _frc_radial_profile(np.asarray(img, dtype=np.float64), centre)[1]
    with np.errstate(divide="ignore"):
        th = sigma / np.sqrt(nr / 2)
    _, th_interp = _frc_smooth(k, th)
    idx = np.argwhere(np.diff(np.sign(frc - th_interp))).flatten()
    return th_interp, (int(idx[1]) if idx.size > 1 else 0)


def _frc_resolution(image_a, image_b, pixel_size=1.0, method="fixed",
                    smoothing="lowess"):
    """
    Resolution from the FRC curve of two independent images.

    Port of BrightEyes-ISM FRC_lib.FRC_resolution. `method` is 'fixed' (the 1/7
    criterion), '3sigma' or '5sigma'; `smoothing` is 'lowess' or 'fit' (a
    sigmoid fit that also removes a high-frequency offset).

    Returns (resolution, k, frc, k_interp, frc_smooth, threshold), with the
    resolution in the units of `pixel_size`.

    Note that FRC needs two *independent* acquisitions of the same object. On a
    simulation where only the shot noise differs, the two halves agree wherever
    there is signal and the crossing lands where the object's spectrum dies
    rather than where the method's resolution is.
    """
    frc = _frc_curve(image_a, image_b)
    n_bins = len(frc)
    k = np.linspace(0, 1 / np.sqrt(2), n_bins, endpoint=True) / pixel_size

    if smoothing == "lowess":
        k_interp, frc_smooth = _frc_smooth(k, frc)
    elif smoothing == "fit":
        from scipy.optimize import curve_fit
        kpx = k * pixel_size
        sigmoid = lambda x, a, b, c, d: a / (1 + np.exp((x - b) / c)) + d
        popt, _ = curve_fit(sigmoid, k[kpx < 0.5], frc[kpx < 0.5], (1, 1, 10, 0),
                            bounds=((0, 0, 0, 0), (np.inf,) * 4))
        amplitude, offset = popt[0], popt[-1]
        k_interp = np.linspace(0, 1 / np.sqrt(2), n_bins * 100,
                               endpoint=True) / pixel_size
        frc_smooth = (sigmoid(k_interp, *popt) - offset) / amplitude
        frc = (frc - offset) / amplitude
    else:
        raise ValueError("smoothing must be 'lowess' or 'fit'")

    if method == "fixed":
        th, idx = _frc_fixed_threshold(frc_smooth, 1 / 7)
    elif method in ("3sigma", "5sigma"):
        th, idx = _frc_nsigma_threshold(k, frc_smooth, image_a, int(method[0]))
    else:
        raise ValueError("method must be 'fixed', '3sigma' or '5sigma'")

    resolution = np.inf if idx == 0 else 1.0 / k_interp[idx]
    return resolution, k, frc, k_interp, frc_smooth, th


def _reassign_photons(clsm, tttr, magnification=2, fwhm=1.5, sensitivity=1,
                      search_radius=0.75, channel_mode="merged", seed=42,
                      method="esrrf", detector_offsets=None, ism_shift_factor=0.5):
    """Redistribute the photons of a CLSMImage onto an M-times finer raster."""
    if detector_offsets is None:
        coords1d = np.empty((0,), dtype=np.float64)
    else:
        coords1d = np.ascontiguousarray(
            np.asarray(detector_offsets, dtype=np.float64).ravel(order='C'), dtype=np.float64
        )
    return _cls._native_reassign_photons(
        clsm, tttr, int(magnification), float(fwhm), int(sensitivity),
        float(search_radius), str(channel_mode), int(seed), str(method),
        coords1d, float(ism_shift_factor)
    )


_cls.read_tiff = staticmethod(_read_tiff)
_cls.rgc_map = staticmethod(_rgc_map)
_cls.temporal_combine = staticmethod(_temporal_combine)
_cls.reassign_photons = staticmethod(_reassign_photons)
_cls.shift_vectors = staticmethod(_shift_vectors)
_cls.sofism_reconstruction = staticmethod(_sofism_reconstruction)
_cls.s2ism_reconstruction = staticmethod(_s2ism_reconstruction)
_cls.fourier_reweight = staticmethod(_fourier_reweight)
_cls.apr_reconstruction = staticmethod(_apr_reconstruction)
_cls.focus_reconstruction = staticmethod(_focus_reconstruction)
_cls.frc_curve = staticmethod(_frc_curve)
_cls.frc_resolution = staticmethod(_frc_resolution)
%}
#endif

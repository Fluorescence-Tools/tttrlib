// SPDX-License-Identifier: BSD-3-Clause
%{
#include "Watershed.h"
%}

// The landscape, the marker labels and the mask all arrive as 2D arrays of the
// same shape; each carries its own dims, matching the Cluster family
// convention.
%apply (double* IN_ARRAY2, int DIM1, int DIM2) {(const double* image, int n_rows, int n_cols)}
%apply (long long* IN_ARRAY2, int DIM1, int DIM2) {(const long long* markers, int m_rows, int m_cols)}
%apply (unsigned char* IN_ARRAY2, int DIM1, int DIM2) {(const unsigned char* mask, int k_rows, int k_cols)}

// Out: the label image (rows x cols), and the contour segments (n x 4).
%apply (long long** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {(long long** out_labels, int* out_rows, int* out_cols)}
%apply (double** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {(double** out_segments, int* out_n_segments, int* out_n_cols)}

// `connectivity` is optional and means "faces only", matching skimage's
// default of 1. Same device as GopichSzabo.i's `offsets`: SWIG treats the
// argument it has a `default` typemap for as optional.
%typemap(default) (int connectivity) {
    $1 = 1;
}

// Both kernels are long-running and touch no Python object.
TTTRLIB_NOGIL(tttrlib::watershed)
TTTRLIB_NOGIL(tttrlib::marching_squares)

// The marching-squares flood is the only frame-level float loop here, but it
// is a subtraction and a division per edge -- see Watershed.cpp for why the
// fp-contract discipline matters.
%exception {
    try {
        $action
    } catch (const std::invalid_argument& e) {
        SWIG_exception(SWIG_ValueError, e.what());
    } catch (const std::exception& e) {
        SWIG_exception(SWIG_RuntimeError, e.what());
    }
}

#ifdef SWIGPYTHON
// scikit-image's call is `watershed(image, markers, mask=None, connectivity=1)`;
// the kernel wants an explicit uint8 mask (an all-ones one is `mask=None`) and
// int64 markers, so the Python name takes the same optional arguments and
// supplies both.
%rename(_watershed_native) tttrlib::watershed;
#endif
%include "Watershed.h"
#ifdef SWIGPYTHON
%pythoncode %{
def watershed(image, markers, mask=None, connectivity=1):
    """Marker-based watershed of `image` (2-D float64), scikit-image-exact.

    Parameters
    ----------
    image : (ny, nx) array_like
    markers : (ny, nx) integer array; 0 = not a marker
    mask : (ny, nx) array_like of bool/uint8, optional (default: all True)
    connectivity : 1 (4-neighbourhood) or 2 (8-neighbourhood)

    Returns
    -------
    labels : (ny, nx) int64 array
    """
    import numpy as _np
    image = _np.ascontiguousarray(image, dtype=_np.float64)
    markers = _np.ascontiguousarray(markers, dtype=_np.int64)
    if mask is None:
        mask = _np.ones(image.shape, dtype=_np.uint8)
    else:
        mask = _np.ascontiguousarray(_np.asarray(mask) != 0, dtype=_np.uint8)
    if connectivity not in (1, 2) or isinstance(connectivity, float):
        raise ValueError("watershed: connectivity must be 1 or 2, got %r" % (connectivity,))
    return _watershed_native(image, markers, mask, int(connectivity))
%}
#endif
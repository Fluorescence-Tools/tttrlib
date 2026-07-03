// SPDX-License-Identifier: BSD-3-Clause
//
// TIFF I/O for 2D / 3D numeric arrays (see include/TiffArrayIO.h).
//
// The C++ core is a single template pair, read_tiff<T>() / write_tiff<T>().
// SWIG instantiates it once per pixel type into hidden `_tiff_read_*` /
// `_tiff_write_*` entry points (the language boundary needs a concrete type per
// wrapper). Users only ever touch the auto-dispatching imread()/imwrite() below
// - read decides the dtype from the file, write from the array.
%{
#include "TiffArrayIO.h"
%}

// ---- Array marshalling typemaps (names shared across numpy/rarrays/jarrays) --
// Readers allocate and hand back a NumPy array of the file's native dtype.
%apply (unsigned char**  ARGOUTVIEWM_ARRAY3, int* DIM1, int* DIM2, int* DIM3) {(unsigned char**  output, int* dim1, int* dim2, int* dim3)}
%apply (unsigned short** ARGOUTVIEWM_ARRAY3, int* DIM1, int* DIM2, int* DIM3) {(unsigned short** output, int* dim1, int* dim2, int* dim3)}
%apply (unsigned int**   ARGOUTVIEWM_ARRAY3, int* DIM1, int* DIM2, int* DIM3) {(unsigned int**   output, int* dim1, int* dim2, int* dim3)}
%apply (int**            ARGOUTVIEWM_ARRAY3, int* DIM1, int* DIM2, int* DIM3) {(int**            output, int* dim1, int* dim2, int* dim3)}
%apply (float**          ARGOUTVIEWM_ARRAY3, int* DIM1, int* DIM2, int* DIM3) {(float**          output, int* dim1, int* dim2, int* dim3)}
%apply (double**         ARGOUTVIEWM_ARRAY3, int* DIM1, int* DIM2, int* DIM3) {(double**         output, int* dim1, int* dim2, int* dim3)}

// Writers consume a NumPy array as (data, n_frames, height, width).
%apply (unsigned char*  IN_ARRAY3, int DIM1, int DIM2, int DIM3) {(unsigned char*  data, int n_frames, int height, int width)}
%apply (unsigned short* IN_ARRAY3, int DIM1, int DIM2, int DIM3) {(unsigned short* data, int n_frames, int height, int width)}
%apply (unsigned int*   IN_ARRAY3, int DIM1, int DIM2, int DIM3) {(unsigned int*   data, int n_frames, int height, int width)}
%apply (int*            IN_ARRAY3, int DIM1, int DIM2, int DIM3) {(int*            data, int n_frames, int height, int width)}
%apply (float*          IN_ARRAY3, int DIM1, int DIM2, int DIM3) {(float*          data, int n_frames, int height, int width)}
%apply (double*         IN_ARRAY3, int DIM1, int DIM2, int DIM3) {(double*         data, int n_frames, int height, int width)}

// Wrap the enum, TiffInfo struct, tiff_info(), tiff_dtype() and the templates.
%include "TiffArrayIO.h"

// One binding entry point per pixel type - the six concrete instantiations of
// the single read_tiff/write_tiff template. Python names them with a leading
// underscore because users call imread()/imwrite(); R cannot parse generated
// wrapper references to symbols starting with "_" (for example "f <- _name"),
// so use R-safe names there.
#ifdef SWIGR
%template(tiff_read_u8)   tttrlib::read_tiff<unsigned char>;
%template(tiff_read_u16)  tttrlib::read_tiff<unsigned short>;
%template(tiff_read_u32)  tttrlib::read_tiff<unsigned int>;
%template(tiff_read_i32)  tttrlib::read_tiff<int>;
%template(tiff_read_f32)  tttrlib::read_tiff<float>;
%template(tiff_read_f64)  tttrlib::read_tiff<double>;

%template(tiff_write_u8)  tttrlib::write_tiff<unsigned char>;
%template(tiff_write_u16) tttrlib::write_tiff<unsigned short>;
%template(tiff_write_u32) tttrlib::write_tiff<unsigned int>;
%template(tiff_write_i32) tttrlib::write_tiff<int>;
%template(tiff_write_f32) tttrlib::write_tiff<float>;
%template(tiff_write_f64) tttrlib::write_tiff<double>;
#else
%template(_tiff_read_u8)   tttrlib::read_tiff<unsigned char>;
%template(_tiff_read_u16)  tttrlib::read_tiff<unsigned short>;
%template(_tiff_read_u32)  tttrlib::read_tiff<unsigned int>;
%template(_tiff_read_i32)  tttrlib::read_tiff<int>;
%template(_tiff_read_f32)  tttrlib::read_tiff<float>;
%template(_tiff_read_f64)  tttrlib::read_tiff<double>;

%template(_tiff_write_u8)  tttrlib::write_tiff<unsigned char>;
%template(_tiff_write_u16) tttrlib::write_tiff<unsigned short>;
%template(_tiff_write_u32) tttrlib::write_tiff<unsigned int>;
%template(_tiff_write_i32) tttrlib::write_tiff<int>;
%template(_tiff_write_f32) tttrlib::write_tiff<float>;
%template(_tiff_write_f64) tttrlib::write_tiff<double>;
#endif

// ---- Single, auto-dispatching user interface (Python) -----------------------
#ifdef SWIGPYTHON
%pythoncode %{
import os as _os
import numpy as _np

_TIFF_READERS = {
    "uint8":   _tiff_read_u8,
    "uint16":  _tiff_read_u16,
    "uint32":  _tiff_read_u32,
    "int32":   _tiff_read_i32,
    "float32": _tiff_read_f32,
    "float64": _tiff_read_f64,
    # int8/int16 have no direct NumPy-typed reader; promote losslessly to int32.
    "int8":    _tiff_read_i32,
    "int16":   _tiff_read_i32,
}

_TIFF_WRITERS = {
    "uint8":   _tiff_write_u8,
    "uint16":  _tiff_write_u16,
    "uint32":  _tiff_write_u32,
    "int32":   _tiff_write_i32,
    "float32": _tiff_write_f32,
    "float64": _tiff_write_f64,
}


def imread(path, squeeze=True):
    """Read a TIFF file into a NumPy array, auto-detecting the pixel type.

    A single-page file returns a 2-D ``(height, width)`` array; a multi-page
    file returns a 3-D ``(n_frames, height, width)`` array (set ``squeeze=False``
    to always get 3-D). The array dtype matches the file's native pixel type;
    ``int8``/``int16`` are promoted to ``int32``. ``path`` may be a string or any
    ``os.PathLike`` (e.g. ``pathlib.Path``).
    """
    path = _os.fspath(path)
    name = tiff_dtype(path)
    reader = _TIFF_READERS.get(name)
    if reader is None:
        raise TypeError("unsupported TIFF pixel type: %r" % name)
    arr = reader(path)
    if squeeze and arr.shape[0] == 1:
        arr = arr[0]
    return arr


def imwrite(path, data, compression="lzw"):
    """Write a 2-D or 3-D NumPy array to a (multi-page) TIFF file.

    The array's dtype selects the on-disk pixel type. ``compression`` is one of
    ``"none"``, ``"lzw"`` (default), ``"packbits"`` or ``"deflate"`` (deflate
    requires libtiff built with ``WITH_TIFF_ZLIB``). Unsupported dtypes are
    promoted to the nearest lossless supported type. ``path`` may be a string or
    any ``os.PathLike`` (e.g. ``pathlib.Path``).
    """
    path = _os.fspath(path)
    a = _np.ascontiguousarray(data)
    if a.ndim == 2:
        a = a[_np.newaxis, ...]
    elif a.ndim != 3:
        raise ValueError("imwrite expects a 2-D or 3-D array, got %dD" % a.ndim)
    writer = _TIFF_WRITERS.get(a.dtype.name)
    if writer is None:
        kind = a.dtype.kind
        if kind == "f":
            a, writer = a.astype(_np.float64), _tiff_write_f64
        elif kind == "i":
            a, writer = a.astype(_np.int32), _tiff_write_i32
        elif kind in ("u", "b"):
            a, writer = a.astype(_np.uint32), _tiff_write_u32
        else:
            raise TypeError("unsupported array dtype for TIFF: %s" % a.dtype)
        a = _np.ascontiguousarray(a)
    writer(path, a, compression)
%}
#endif

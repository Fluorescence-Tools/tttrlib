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


# ---- ImageJ hyperstack metadata --------------------------------------------
# A TIFF is a flat sequence of pages, so six pages cannot say by themselves
# whether they are six time points or two time points in three colours. ImageJ
# records that split as plain "key=value" lines in the ImageDescription tag of
# the first page, and every microscopy tool that writes stacks follows it. The
# C++ layer carries the tag verbatim; the axis bookkeeping lives here.
#
# The page order of a hyperstack is fixed: channel varies fastest, then slice,
# then frame - so the pages reshape to (frames, slices, channels, h, w), and
# axes with a single element are dropped.

#: ImageJ writer version stamped into the ImageDescription tag. ImageJ only
#: checks that the key exists, but readers reject a description without it.
_IMAGEJ_VERSION = "1.54f"

#: ImageJ dimension keys, slowest-varying page axis first, with their labels.
_IMAGEJ_DIMS = (("frames", "T"), ("slices", "Z"), ("channels", "C"))


def _parse_imagej(description, n_pages):
    """Return ``(axes, shape)`` for the leading page axes, or ``None``.

    ``shape`` covers only the page axes (Y/X are appended by the caller).
    Returns ``None`` when *description* is not ImageJ metadata, or when it
    describes a page count other than *n_pages* - a stale or truncated
    description must not silently reshape the pixels into the wrong grid.
    """
    if not description or "ImageJ" not in description:
        return None
    fields = {}
    for line in description.splitlines():
        key, sep, value = line.partition("=")
        if sep:
            fields[key.strip()] = value.strip()
    if "ImageJ" not in fields:
        return None
    axes, shape = "", []
    for key, label in _IMAGEJ_DIMS:
        try:
            size = int(fields.get(key, 1))
        except ValueError:
            return None
        if size > 1:
            axes += label
            shape.append(size)
    product = 1
    for size in shape:
        product *= size
    if product != n_pages:
        return None
    return axes, tuple(shape)


def _imagej_description(axes, shape, extra=None):
    """Format ImageJ ImageDescription lines for a stack of *shape* with *axes*.

    *extra* adds further ImageJ fields verbatim - ``spacing`` (the z step) and
    ``unit`` (its name, e.g. ``"um"``) are the ones that give a stack a physical
    voxel size, alongside the x/y resolution tags.
    """
    sizes = {label: 1 for _, label in _IMAGEJ_DIMS}
    pages = 1
    for label, size in zip(axes[:-2], shape[:-2]):
        sizes[label] = int(size)
        pages *= int(size)
    lines = ["ImageJ=" + _IMAGEJ_VERSION, "images=%d" % pages]
    for key, label in _IMAGEJ_DIMS:
        if sizes[label] > 1:
            lines.append("%s=%d" % (key, sizes[label]))
    lines += ["hyperstack=true", "mode=grayscale", "loop=false"]
    for key, value in (extra or {}).items():
        if key in ("ImageJ", "images") or any(key == k for k, _ in _IMAGEJ_DIMS):
            raise ValueError("metadata key %r is derived from the array, not set" % key)
        lines.append("%s=%s" % (key, value))
    return "\n".join(lines)


def tiff_metadata(path):
    """Describe the layout of a TIFF file without decoding its pixels.

    Returns a dict with ``axes`` (a label per dimension: ``T`` frames, ``Z``
    slices, ``C`` channels, ``I`` an unlabelled page index, ``Y``/``X`` the
    image plane), ``shape``, ``dtype`` and the raw ``description`` tag. A file
    without ImageJ metadata is reported as ``"YX"`` (single page) or ``"IYX"``.

    Parameters
    ----------
    path : str or os.PathLike
        TIFF file to inspect.

    Returns
    -------
    dict
        ``{"axes": str, "shape": tuple, "dtype": str, "description": str}``.
    """
    info = tiff_info(_os.fspath(path))
    plane = (info.height, info.width)
    parsed = _parse_imagej(info.description, info.n_frames)
    if parsed is not None:
        axes, shape = parsed
    elif info.n_frames > 1:
        axes, shape = "I", (info.n_frames,)
    else:
        axes, shape = "", ()
    return {
        "axes": axes + "YX",
        "shape": shape + plane,
        "dtype": tiff_dtype_name(info.dtype),
        "description": info.description,
    }


def imread(path, squeeze=True):
    """Read a TIFF file into a NumPy array, auto-detecting the pixel type.

    A single-page file returns a 2-D ``(height, width)`` array; a multi-page
    file returns a 3-D ``(n_frames, height, width)`` array (set ``squeeze=False``
    to always get 3-D). A file carrying ImageJ hyperstack metadata is reshaped
    to the frames/slices/channels grid that metadata declares, so a six-page
    two-frame three-colour stack reads back as ``(2, 3, height, width)`` rather
    than as six anonymous pages; :func:`tiff_metadata` names those axes. The
    array dtype matches the file's native pixel type; ``int8``/``int16`` are
    promoted to ``int32``. ``path`` may be a string or any ``os.PathLike``.
    """
    path = _os.fspath(path)
    info = tiff_info(path)
    name = tiff_dtype_name(info.dtype)
    reader = _TIFF_READERS.get(name)
    if reader is None:
        raise TypeError("unsupported TIFF pixel type: %r" % name)
    arr = reader(path)
    parsed = _parse_imagej(info.description, arr.shape[0])
    if parsed is not None:
        arr = arr.reshape(parsed[1] + arr.shape[1:])
    elif squeeze and arr.shape[0] == 1:
        arr = arr[0]
    return arr


def imwrite(path, data, compression="lzw", axes=None, resolution=None, metadata=None):
    """Write a NumPy array to a (multi-page) TIFF file.

    A 2-D array is one page and a 3-D array is a page per leading index. Arrays
    with more dimensions - and any array whose leading axes you want *named* -
    are written as an ImageJ hyperstack: pass ``axes`` as a label string ending
    in ``"YX"``, using ``T`` for frames, ``Z`` for slices and ``C`` for
    channels (for example ``"TCYX"``). :func:`imread` restores that shape.
    Without ``axes``, an array of more than three dimensions takes the last
    labels of ``"TZCYX"``.

    ``resolution`` is an ``(x, y)`` pair in *pixels per unit* - the reciprocal
    of the pixel size - and ``metadata`` adds further ImageJ fields, of which
    ``spacing`` (the z step) and ``unit`` (what the numbers are in, e.g.
    ``"um"``) are what give a stack a physical voxel size. ImageJ needs both:
    the tags carry x/y, the description carries z and the unit name.

    The array's dtype selects the on-disk pixel type. ``compression`` is one of
    ``"none"``, ``"lzw"`` (default), ``"packbits"`` or ``"deflate"`` (deflate
    requires libtiff built with ``WITH_TIFF_ZLIB``). Unsupported dtypes are
    promoted to the nearest lossless supported type. ``path`` may be a string or
    any ``os.PathLike`` (e.g. ``pathlib.Path``).
    """
    path = _os.fspath(path)
    a = _np.ascontiguousarray(data)
    if a.ndim < 2:
        raise ValueError("imwrite expects an array of at least 2 dimensions, got %dD" % a.ndim)
    if axes is None and (a.ndim > 3 or metadata):
        axes = "TZCYX"[-a.ndim:]
    description = ""
    if axes is not None:
        axes = str(axes).upper()
        if len(axes) != a.ndim:
            raise ValueError("axes %r does not match a %dD array" % (axes, a.ndim))
        if not axes.endswith("YX"):
            raise ValueError("axes %r must end in 'YX' (the image plane)" % axes)
        unknown = set(axes[:-2]) - {label for _, label in _IMAGEJ_DIMS}
        if unknown:
            raise ValueError("axes %r uses labels %s; only T, Z and C name pages"
                             % (axes, "".join(sorted(unknown))))
        if a.ndim > 2 or metadata:
            description = _imagej_description(axes, a.shape, metadata)
    x_res = y_res = 0.0
    if resolution is not None:
        x_res, y_res = (float(v) for v in resolution)
    if a.ndim == 2:
        a = a[_np.newaxis, ...]
    elif a.ndim > 3:
        a = a.reshape((-1,) + a.shape[-2:])
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
    writer(path, a, compression, description, x_res, y_res)
%}
#endif

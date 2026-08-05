# SPDX-License-Identifier: BSD-3-Clause
#
# Module-level support for HistogramNd's Python surface.
#
# These live at module scope, not inside %extend: SWIG injects a %pythoncode
# file into the CLASS namespace, so anything defined there becomes a method and
# is not visible as a plain name from inside another method.

import numpy as _np_hist

# `range` is shadowed by the parameter of the same name below, and these
# functions mirror numpy's signatures exactly rather than renaming it.
_builtin_range = range


class _OwnedView(_np_hist.ndarray):
    """An ndarray that keeps the histogram it points into alive.

    numpy's ARGOUTVIEW arrays carry no base object, so nothing would stop the
    C++ histogram being collected while a view into its buffer is still in use.
    Holding the owner here makes that impossible: the array cannot outlive what
    it points at.
    """

    _owner = None

    def __array_finalize__(self, obj):
        # Without this the owner is lost the moment the array is reshaped or
        # sliced -- and view() does both -- leaving exactly the dangling pointer
        # the subclass exists to prevent. numpy creates derived arrays through
        # this hook, so the reference has to be propagated here.
        if obj is not None:
            self._owner = getattr(obj, "_owner", None)


_AXIS_KIND_NAMES = {
    0: "regular", 1: "log", 2: "sqrt", 3: "pow",
    4: "variable", 5: "integer", 6: "category", 7: "boolean",
}


def _hist_wrap_view(owner, flat):
    v = flat.view(_OwnedView)
    v._owner = owner
    return v


def _hist_shape(h):
    """Stored shape, flow bins included."""
    return tuple(int(i) for i in h.get_shape())


def _hist_interior(h):
    """The slice that drops the flow bins, per axis."""
    out = []
    for d in range(h.rank()):
        o = h.axis(d).options()
        out.append(slice(1 if o.underflow else 0, -1 if o.overflow else None))
    return tuple(out)


# --- numpy-compatible entry points ------------------------------------------
#
# Signatures deliberately mirror numpy.histogram / numpy.histogram2d, so a
# caller can swap the import and change nothing else. NDXplorer is the reason
# these exist: it has (bins, range) and wants (counts, edges) back.


def _resolve_axis(bins, rng, data, log=False):
    """A bin count plus a range, or an explicit edge array, into an Axis."""
    if _np_hist.ndim(bins) > 0:
        edges = _np_hist.ascontiguousarray(bins, dtype=_np_hist.float64)
        return Axis.variable(edges, AxisOptions.flow())
    n = int(bins)
    if rng is None:
        lo, hi = float(_np_hist.min(data)), float(_np_hist.max(data))
        if lo == hi:                     # a constant column still needs a width
            lo, hi = lo - 0.5, hi + 0.5
    else:
        lo, hi = float(rng[0]), float(rng[1])
    if log:
        return Axis.log(n, lo, hi, AxisOptions.flow())
    return Axis.regular(n, lo, hi, AxisOptions.flow())


def histogram(x, bins=10, range=None, weights=None, threads=0, flow=False):
    """Like numpy.histogram, but returns ``(counts, edges)`` filled in parallel.

    ``bins`` may be a count or an array of edges. With ``flow=True`` the counts
    include the underflow and overflow bins, which numpy has no way to report.
    """
    x = _np_hist.ascontiguousarray(x, dtype=_np_hist.float64)
    h = HistogramNd(AxisVector([_resolve_axis(bins, range, x)]))
    h.fill(x, weight=weights, threads=threads)
    return h.view(flow), h.axes[0].edges


def histogram2d(x, y, bins=10, range=None, weights=None, threads=0, flow=False):
    """Like numpy.histogram2d, returning ``(H, xedges, yedges)``.

    ``bins`` is a count, a pair of counts, or a pair of edge arrays; ``range``
    is ``((xlo, xhi), (ylo, yhi))``.
    """
    x = _np_hist.ascontiguousarray(x, dtype=_np_hist.float64)
    y = _np_hist.ascontiguousarray(y, dtype=_np_hist.float64)
    if _np_hist.ndim(bins) == 0:
        bins = (bins, bins)
    xr, yr = (None, None) if range is None else (range[0], range[1])
    h = HistogramNd(AxisVector([_resolve_axis(bins[0], xr, x),
                                _resolve_axis(bins[1], yr, y)]))
    h.fill(x, y, weight=weights, threads=threads)
    return h.view(flow), h.axes[0].edges, h.axes[1].edges


def histogramdd(sample, bins=10, range=None, weights=None, threads=0, flow=False):
    """Like numpy.histogramdd, returning ``(H, [edges, ...])``."""
    sample = _np_hist.ascontiguousarray(sample, dtype=_np_hist.float64)
    if sample.ndim != 2:
        raise ValueError("sample must be an (n_points, n_dims) array")
    n_dims = sample.shape[1]
    if _np_hist.ndim(bins) == 0:
        bins = [bins] * n_dims
    ranges = [None] * n_dims if range is None else list(range)
    axes = [_resolve_axis(bins[d], ranges[d], sample[:, d]) for d in _builtin_range(n_dims)]
    h = HistogramNd(AxisVector(axes))
    h.fill(sample, weight=weights, threads=threads)
    return h.view(flow), [a.edges for a in h.axes]

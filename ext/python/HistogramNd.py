# SPDX-License-Identifier: BSD-3-Clause
#
# Pythonic surface for HistogramNd, injected as methods by SWIG (%extend).
#
# Design rule: NO COPIES on the read path. `view()` wraps the C++ buffer
# directly; reshaping to the stored shape is a numpy view, and slicing the flow
# bins off is a strided view. Looking at a 1024x1024 histogram therefore costs
# nothing rather than 8 MB.
#
# The safety that buys is real and is enforced, not documented away -- see
# `view()` below. The module-level helpers it uses live in hist_support.py,
# because a %pythoncode file lands in the CLASS namespace and would otherwise
# not be reachable by name from inside these methods.


def view(self, flow=False):
    """The bin values as a numpy array, without copying.

    :param flow: include the underflow/overflow bins.

    The array shares memory with the histogram, so writing to it writes to the
    histogram. It also holds a reference to the histogram, so it cannot dangle.

    One case cannot be a view: a histogram with a growing axis, because the next
    fill may reallocate the buffer and there is no way to reach back and
    invalidate an array already handed out. That returns a copy instead.
    """
    flat, _ = self.get_values_view(), None
    if flat is None or flat.size == 0:
        # can_view() said no (a growing axis), or the histogram is empty.
        flat = self.get_values()
        arr = flat.reshape(_hist_shape(self))
        return arr if flow else arr[_hist_interior(self)]
    arr = _hist_wrap_view(self, flat).reshape(_hist_shape(self))
    return arr if flow else arr[_hist_interior(self)]


def variance_view(self, flow=False):
    """Sum of squared weights, or None when the histogram does not track it."""
    if not self.tracks_variance():
        return None
    flat = self.get_variances_view()
    if flat is None or flat.size == 0:
        flat = self.get_variances()
        if flat.size == 0:
            return None
        arr = flat.reshape(_hist_shape(self))
        return arr if flow else arr[_hist_interior(self)]
    arr = _hist_wrap_view(self, flat).reshape(_hist_shape(self))
    return arr if flow else arr[_hist_interior(self)]


@property
def axes(self):
    """The axes, as a list."""
    return [self.axis(d) for d in range(self.rank())]


@property
def ndim(self):
    return self.rank()


@property
def shape(self):
    """Shape WITHOUT flow bins -- what `view()` returns by default."""
    return tuple(self.axis(d).size() for d in range(self.rank()))


def fill(self, *columns, **kwargs):
    """Add points.

    :param columns: one array per axis, or a single (n, rank) array
    :param weight: per-point weights, or None
    :param threads: 0 to decide, 1 to force serial, or an explicit count

    Inputs are passed straight through to the C++ fill; numpy arrays that are
    already contiguous float64 are borrowed rather than copied.
    """
    weight = kwargs.pop("weight", None)
    threads = int(kwargs.pop("threads", 0))
    if kwargs:
        raise TypeError("unexpected keyword arguments: %s" % sorted(kwargs))

    np = _np_hist
    cols = [np.ascontiguousarray(c, dtype=np.float64) for c in columns]
    if len(cols) == 1 and self.rank() > 1:
        rows = cols[0]
        if rows.ndim != 2 or rows.shape[1] != self.rank():
            raise ValueError(
                "a single argument must be an (n, %d) array" % self.rank())
        if weight is None:
            self.fill_rows(rows, threads)
        else:
            self.fill_rows_weighted(
                rows, np.ascontiguousarray(weight, dtype=np.float64), threads)
        return self

    if len(cols) != self.rank():
        raise ValueError("expected %d columns, got %d" % (self.rank(), len(cols)))

    if self.rank() == 1:
        if weight is None:
            self.fill_1d(cols[0], threads)
        else:
            self.fill_1d_weighted(
                cols[0], np.ascontiguousarray(weight, dtype=np.float64), threads)
    elif self.rank() == 2:
        if weight is None:
            self.fill_2d(cols[0], cols[1], threads)
        else:
            self.fill_2d_weighted(
                cols[0], cols[1],
                np.ascontiguousarray(weight, dtype=np.float64), threads)
    else:
        # column_stack copies once; the alternative is an array of pointers,
        # which a language binding cannot pass.
        rows = np.column_stack(cols)
        if weight is None:
            self.fill_rows(rows, threads)
        else:
            self.fill_rows_weighted(
                rows, np.ascontiguousarray(weight, dtype=np.float64), threads)
    return self


def fill_profile(self, *columns, **kwargs):
    """Add points carrying a sample, for a Mean or WeightedMean histogram.

    :param columns: one array per axis
    :param sample: the value being averaged, one per point
    :param weight: per-point weights (WeightedMean only)

    The bin ends up holding the mean of ``sample`` over the points that landed
    in it. ``mean()`` and ``mean_variance()`` read it back.
    """
    np = _np_hist
    sample = kwargs.pop("sample", None)
    weight = kwargs.pop("weight", None)
    if kwargs:
        raise TypeError("unexpected keyword arguments: %s" % sorted(kwargs))
    if sample is None:
        raise TypeError("fill_profile needs a sample=")
    if not self.is_profile():
        raise TypeError("this histogram does not accumulate a mean; build it "
                        "with HistStorage_Mean or HistStorage_WeightedMean")

    cols = [np.ascontiguousarray(c, dtype=np.float64) for c in columns]
    if len(cols) != self.rank():
        raise ValueError("expected %d columns, got %d" % (self.rank(), len(cols)))
    s = np.ascontiguousarray(sample, dtype=np.float64)
    w = None if weight is None else np.ascontiguousarray(weight, dtype=np.float64)

    if self.rank() == 1:
        if w is None:
            self.fill_1d_sample(cols[0], s)
        else:
            self.fill_1d_sample_weighted(cols[0], s, w)
    elif self.rank() == 2:
        if w is None:
            self.fill_2d_sample(cols[0], cols[1], s)
        else:
            self.fill_2d_sample_weighted(cols[0], cols[1], s, w)
    else:
        raise NotImplementedError("profiles are supported for rank 1 and 2")
    return self


def mean(self, flow=False):
    """The per-bin mean of a profile histogram."""
    a = self.get_means().reshape(_hist_shape(self))
    return a if flow else a[_hist_interior(self)]


def mean_variance(self, flow=False):
    """Variance OF THE MEAN -- the squared standard error, for error bars.

    This is what boost-histogram's ``variances()`` returns for a profile. It is
    the sample variance divided by the count again; ``sample_variance()`` gives
    the other one.
    """
    a = self.get_mean_variances().reshape(_hist_shape(self))
    return a if flow else a[_hist_interior(self)]


def sample_variance(self, flow=False):
    """How spread out the samples in each bin were. \see mean_variance"""
    a = self.get_sample_variances().reshape(_hist_shape(self))
    return a if flow else a[_hist_interior(self)]


def counts(self, flow=False):
    """Entries per bin (Mean), or the sum of weights (WeightedMean)."""
    a = self.get_counts().reshape(_hist_shape(self))
    return a if flow else a[_hist_interior(self)]


def to_numpy(self, flow=False):
    """`(values, edges_0, edges_1, ...)`, the shape numpy.histogramdd returns."""
    return (self.view(flow),) + tuple(a.edges for a in self.axes)


def __array__(self, dtype=None):
    a = self.view(False)
    return a if dtype is None else a.astype(dtype)


def __repr__(self):
    return "HistogramNd(%s)" % ", ".join(repr(a) for a in self.axes)

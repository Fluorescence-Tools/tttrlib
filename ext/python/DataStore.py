# SPDX-License-Identifier: BSD-3-Clause
#
# Pythonic surface for the DataStore.


def add(self, name, values):
    """Add a column from a numpy array (or a sequence of strings).

    The dtype is kept: a float32 column stays float32. That is half the memory
    of the float64 it would otherwise be widened to, and the histogram fill
    reads it in place rather than converting it.
    """
    np = _np_ds
    a = np.asarray(values)
    if a.dtype.kind in ("U", "S", "O"):
        t = ColumnType_String
    else:
        t = _DS_DTYPE_TO_TYPE.get(a.dtype.name, ColumnType_Float64)
    i = self.add_column(name, t)
    self.column(i).set_numpy(a)
    if self.n_rows() == 0:
        self.set_n_rows(len(a))
    return self[i]                      # via __getitem__, so it holds the store


def __getitem__(self, key):
    c = self.column_by_name(key) if isinstance(key, str) else self.column(int(key))
    # A Column proxy is a BORROWED reference into the store, so holding one
    # does not keep the store alive -- and a zero-copy view into the column
    # then points at freed memory. Attaching the store here makes the chain
    # view -> column -> store unbreakable.
    c._store = self
    return c


def __contains__(self, name):
    return self.find(name) >= 0


def __len__(self):
    return self.n_rows()


@property
def columns(self):
    return [self[i] for i in range(self.n_columns())]


@property
def names(self):
    return [self.column(i).name() for i in range(self.n_columns())]


def select(self, mask):
    """Set the row selection from a bool array, or None to clear it."""
    if mask is None:
        self.clear_row_mask()
        return self
    m = _np_ds.ascontiguousarray(mask, dtype=_np_ds.uint8)
    self.set_row_mask(m)
    return self


def where(self, column, lo=None, hi=None, equals=None, how="replace"):
    """Narrow the selection by a condition on one column.

    :param lo, hi: keep rows with lo <= value < hi
    :param equals: keep rows equal to this value
    :param how: "replace", "and", "or", "andnot"

    Evaluated in C++ over the column's own type and written into the bit-packed
    selection, 64 rows to a word. The pattern this replaces -- a bool array per
    condition, combined with numpy, then flatnonzero, then fancy indexing --
    moves eight times the memory for the mask alone, and then copies the rows.

    Chainable::

        store.where("E", 0.2, 0.8).where("S", 0.3, 0.7, how="and")
    """
    i = _ds_col(self, column)
    if equals is not None:
        self.select_equal(i, float(equals), _ds_mode(how))
    else:
        if lo is None or hi is None:
            raise TypeError("give lo and hi, or equals")
        self.select_range(i, float(lo), float(hi), _ds_mode(how))
    return self


def region(self, x, y, kind="rectangle", how="replace", invert=False, **kw):
    """Select the rows inside a region drawn on the (x, y) plane.

    :param x, y: the two columns the region was drawn on
    :param kind: "rectangle", "ellipse", "polygon" or "mask"
    :param how: "replace", "and", "or", "andnot"
    :param invert: select what is OUTSIDE the region instead

    ==========  ============================================================
    kind        keywords
    ==========  ============================================================
    rectangle   x0, y0, x1, y1
    ellipse     cx, cy, rx, ry, angle (radians, default 0)
    quadratic   cx, cy, a, b, c, threshold -- a Mahalanobis gate
    polygon     xs, ys -- the vertices
    mask        image (2-D bool/uint8), x0, y0, x1, y1 -- the extent it covers
    ==========  ============================================================

    Evaluated over the columns in place. The alternative -- handing two columns
    to the front end, testing them there, and handing back a mask the size of
    the whole table -- is what this exists to avoid.

    An inverted region is "not inside", which is a combination rather than a
    second scan: with ``how="replace"`` the region is evaluated and the result
    flipped, and with ``how="and"`` it is subtracted from what is already
    selected. ``invert`` with ``how="or"`` has no such form -- it needs the
    complement of the region as a mask of its own -- and raises. Note that
    flipping the selection also flips the rows whose coordinates are missing;
    :meth:`where_finite` before the region is how to keep them out.
    """
    np = _np_ds
    if kind not in ("rectangle", "ellipse", "quadratic", "polygon", "mask"):
        raise ValueError("kind must be rectangle, ellipse, quadratic, polygon "
                         "or mask")
    # Validate everything BEFORE touching the selection. A method that raises
    # halfway leaves the store holding an answer nobody asked for, and the
    # caller catching the exception has no way to know that.
    if invert and how not in ("replace", "and"):
        raise NotImplementedError(
            "invert with how=%r needs the complement as its own mask; "
            "invert the region instead" % how)
    ix, iy = _ds_col(self, x), _ds_col(self, y)
    if invert:
        # "already selected AND NOT inside" -- one scan, no flip, and it leaves
        # rows the region could not be evaluated on alone.
        mode = _ds_mode("andnot") if how == "and" else _ds_mode("replace")
    else:
        mode = _ds_mode(how)

    if kind == "rectangle":
        self.select_rectangle(ix, iy, float(kw["x0"]), float(kw["y0"]),
                              float(kw["x1"]), float(kw["y1"]), mode)
    elif kind == "ellipse":
        self.select_ellipse(ix, iy, float(kw["cx"]), float(kw["cy"]),
                            float(kw["rx"]), float(kw["ry"]),
                            float(kw.get("angle", 0.0)), mode)
    elif kind == "quadratic":
        self.select_quadratic(ix, iy, float(kw["cx"]), float(kw["cy"]),
                              float(kw["a"]), float(kw["b"]), float(kw["c"]),
                              float(kw["threshold"]), mode)
    elif kind == "polygon":
        xs = np.ascontiguousarray(kw["xs"], dtype=np.float64)
        ys = np.ascontiguousarray(kw["ys"], dtype=np.float64)
        self.select_polygon(ix, iy, xs, ys, mode)
    elif kind == "mask":
        img = np.ascontiguousarray(np.asarray(kw["image"]) != 0, dtype=np.uint8)
        self.select_mask_image(ix, iy, img, float(kw["x0"]), float(kw["y0"]),
                               float(kw["x1"]), float(kw["y1"]), mode)

    if invert and how == "replace":
        self.invert_selection()
    return self


def interval(self, column, lo=None, hi=None, lo_closed=True, hi_closed=True,
             missing_selected=False, how="replace"):
    """Narrow the selection by a closed (or half-open) interval on one column.

    :param lo, hi: the bounds; None means unbounded on that side
    :param lo_closed, hi_closed: whether each endpoint is included
    :param missing_selected: whether a row with no value passes this gate
    :param how: "replace", "and", "or", "andnot"

    :meth:`where` is the library's own rule -- half-open, missing values
    dropped. This is for reproducing a gate that was defined elsewhere with
    different conventions; see ``select_interval`` in ``DataStore.h`` for why
    both exist.
    """
    i = _ds_col(self, column)
    inf = _np_ds.inf
    self.select_interval(i,
                         -inf if lo is None else float(lo),
                         inf if hi is None else float(hi),
                         bool(lo_closed), bool(hi_closed),
                         bool(missing_selected), _ds_mode(how))
    return self


def select_rows(self, mask, how="replace"):
    """Combine a boolean array into the selection.

    The way in for a condition with no primitive here: read the column or two
    it needs as zero-copy views, decide outside, and combine the answer back in.
    The selection stays one bit per row and stays in one place, which is the
    difference between this and taking the mask out and putting a new one back.
    """
    m = _np_ds.ascontiguousarray(_np_ds.asarray(mask) != 0, dtype=_np_ds.uint8)
    self.select_mask_rows(m, _ds_mode(how))
    return self


def where_finite(self, columns=None, how="and"):
    """Drop rows where any of `columns` is missing or non-finite."""
    names = self.names if columns is None else columns
    idx = VectorInt32([_ds_col(self, n) for n in names])
    self.select_finite(idx, _ds_mode(how))
    return self


def selection(self):
    """The current row selection as a bool array, or None."""
    if not self.has_row_mask():
        return None
    out = _np_ds.empty(self.n_rows(), dtype=_np_ds.uint8)
    self.row_mask().to_bytes(out)
    return out.astype(bool)


def _hist_axes(self, names, bins, rng, edges=None, scale=None):
    """``(column indices, axes)`` for the named columns. Shared so that a
    histogram and a profile over the same columns bin identically.

    A column may be given by name or by index. By index matters for a caller
    whose table can carry the same column name twice -- a name lookup would
    quietly answer with the first one and histogram the wrong data.

    `edges` gives explicit bin boundaries per axis, for a caller that has
    already decided them -- a log-scaled plot axis, a pixel grid. It takes
    precedence over `bins` and `range` for the axes it is given for.
    """
    np = _np_ds
    idx = [_ds_col(self, n) for n in names]
    if np.ndim(bins) == 0:
        bins = [bins] * len(idx)
    ranges = [None] * len(idx) if rng is None else list(rng)
    explicit = [None] * len(idx) if edges is None else list(edges)
    scales = [None] * len(idx) if scale is None else list(scale)

    axes = []           # what the fill uses
    real = []           # what the caller asked for
    extra = []          # axes carrying the extra top bin; see _ds_axis_from_edges
    for k, i in enumerate(idx):
        col = self.column(i)
        if col.type() == ColumnType_String:
            axes.append(category_axis_for(col))
            real.append(axes[-1])
            continue
        if explicit[k] is not None:
            axis, extended = _ds_axis_from_edges(explicit[k], col.name())
            axes.append(axis)
            real.append(axis)
            if extended:
                extra.append(k)
            continue
        lo, hi = (None, None) if ranges[k] is None else ranges[k]
        if lo is None:
            a = col.numpy()
            sel = self.selection()
            if sel is not None:
                a = a[sel]
            lo, hi = float(np.min(a)), float(np.max(a))
            if lo == hi:
                lo, hi = lo - 0.5, hi + 0.5
        # A plain lin or log axis is a formula, not a list of edges: the bin
        # index is a multiply (and a logarithm), with nothing to search. Passing
        # the edges instead costs a binary search per point per axis, which on a
        # few million points is the whole cost of the fill.
        options = AxisOptions.flow()
        # NumPy's last bin includes the top of the range, whatever the range
        # came from, and a caller replacing a NumPy histogram needs the same
        # answer -- otherwise the topmost bin is short by however many points
        # sit exactly at the top, which for a range taken from the data is at
        # least one and for an integer axis can be a whole column of an image.
        options.closed_upper = True
        if str(scales[k]).lower().startswith("log") and lo > 0.0 and hi > 0.0:
            axes.append(Axis.log(int(bins[k]), float(lo), float(hi),
                                 options, col.name()))
        else:
            axes.append(Axis.regular(int(bins[k]), float(lo), float(hi),
                                     options, col.name()))
        real.append(axes[-1])
    return idx, axes, real, extra


def _hist_column(self, name):
    """One optional column, by name or index; -1 for None."""
    return -1 if name is None else _ds_col(self, name)


def histogram(self, *names, **kwargs):
    """Histogram one or more columns of this store.

    :param names: column names or indices, one per axis
    :param bins: bin count, or one per axis
    :param range: (lo, hi) per axis, or None to take it from the data
    :param edges: explicit bin edges per axis, overriding bins and range
    :param scale: "linear" or "log" per axis -- a formula rather than an edge
        array, so the bin index costs a multiply instead of a binary search
    :param weight: a column name to weight by
    :param threads: 0 to decide, 1 to force serial, or a count

    The selection and every column's validity mask are honoured, and nothing is
    converted: a float32 column is read as float32, a string column as its
    dictionary codes on a category axis.
    """
    bins = kwargs.pop("bins", 128)
    rng = kwargs.pop("range", None)
    edges = kwargs.pop("edges", None)
    scale = kwargs.pop("scale", None)
    weight = kwargs.pop("weight", None)
    threads = int(kwargs.pop("threads", 0))
    if kwargs:
        raise TypeError("unexpected keyword arguments: %s" % sorted(kwargs))

    idx, axes, real, extended = self._hist_axes(names, bins, rng, edges, scale)
    h = HistogramNd(AxisVector(axes))
    fill_histogram(h, self, VectorInt32(idx), self._hist_column(weight), threads)
    return _ds_fold_extra_bin(h, real, extended) if extended else h


def profile(self, *names, **kwargs):
    """Bin the store by `names` and hold the MEAN of another column per bin.

    :param names: column names, one per axis
    :param sample: the column being averaged -- required
    :param weight: a column name to weight by; makes it a weighted mean
    :param bins: bin count, or one per axis
    :param range: (lo, hi) per axis, or None to take it from the data

    A parameter map: ``store.profile("x", "y", sample="tau", bins=256)`` is a
    lifetime image. Read it back with ``h.mean()`` and ``h.mean_variance()``.

    The selection and the validity of every column involved are honoured, and a
    non-finite sample is skipped rather than allowed to poison the bin -- see
    ``fill_histogram_sample`` in ``DataStore.h``.
    """
    sample = kwargs.pop("sample", None)
    weight = kwargs.pop("weight", None)
    bins = kwargs.pop("bins", 128)
    rng = kwargs.pop("range", None)
    edges = kwargs.pop("edges", None)
    scale = kwargs.pop("scale", None)
    if kwargs:
        raise TypeError("unexpected keyword arguments: %s" % sorted(kwargs))
    if sample is None:
        raise TypeError("profile needs a sample=")

    idx, axes, _real, _extended = self._hist_axes(names, bins, rng, edges, scale)
    storage = HistStorage_Mean if weight is None else HistStorage_WeightedMean
    h = HistogramNd(AxisVector(axes), storage)
    fill_histogram_sample(h, self, VectorInt32(idx), self._hist_column(sample),
                          self._hist_column(weight))
    return h


def memory_report(self):
    """Bytes held, per column and in total -- for sizing a cache.

    A group's columns appear under their path, so ``results/Tau`` sits beside
    the root's own columns and it is visible which table is the expensive one.
    Only leaves are listed, never a per-group subtotal: the total is the sum of
    the entries, and a subtotal would be counted twice.
    """
    out = {"total": self.nbytes()}
    for c in self.columns:
        out[c.name()] = c.nbytes()
    for name in self.group_names():
        for key, value in self.group(name).memory_report().items():
            if key != "total":
                out[name + "/" + key] = value
    return out


@property
def groups(self):
    """The direct child groups as ``{name: DataStore}``, in insertion order.

    ``store[...]`` is a COLUMN and stays one -- a string key that silently
    switched between a column and a group depending on what happened to exist
    is exactly the ambiguity that produces a bug report about the wrong thing.
    So groups have their own accessors, and this one is for ``"results" in
    store.groups`` and for printing.

    A fresh proxy per call, each holding the root alive. Caching them would
    build a reference cycle through ``_store`` and stop a dropped store from
    freeing its memory, so use ``store.group(path)`` in a loop.
    """
    return {name: self.group(name) for name in self.group_names()}


def __repr__(self):
    # The group clause only when there are groups: every store that has none
    # should read exactly as it did before they existed.
    if self.n_groups():
        return "DataStore(%d rows, %d columns, %d groups, %.1f MB)" % (
            self.n_rows(), self.n_columns(), self.n_groups(), self.nbytes() / 1e6)
    return "DataStore(%d rows, %d columns, %.1f MB)" % (
        self.n_rows(), self.n_columns(), self.nbytes() / 1e6)

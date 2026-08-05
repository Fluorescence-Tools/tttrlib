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
    return self.column(i)


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
    polygon     xs, ys -- the vertices
    mask        image (2-D bool/uint8), x0, y0, x1, y1 -- the extent it covers
    ==========  ============================================================

    Evaluated over the columns in place. The alternative -- handing two columns
    to the front end, testing them there, and handing back a mask the size of
    the whole table -- is what this exists to avoid.
    """
    np = _np_ds
    ix, iy = _ds_col(self, x), _ds_col(self, y)
    # An inverted region is "not inside", so it is evaluated normally and the
    # combination is what changes -- there is no second scan.
    mode = _ds_mode("replace" if invert else how)

    if kind == "rectangle":
        self.select_rectangle(ix, iy, float(kw["x0"]), float(kw["y0"]),
                              float(kw["x1"]), float(kw["y1"]), mode)
    elif kind == "ellipse":
        self.select_ellipse(ix, iy, float(kw["cx"]), float(kw["cy"]),
                            float(kw["rx"]), float(kw["ry"]),
                            float(kw.get("angle", 0.0)), mode)
    elif kind == "polygon":
        xs = np.ascontiguousarray(kw["xs"], dtype=np.float64)
        ys = np.ascontiguousarray(kw["ys"], dtype=np.float64)
        self.select_polygon(ix, iy, xs, ys, mode)
    elif kind == "mask":
        img = np.ascontiguousarray(np.asarray(kw["image"]) != 0, dtype=np.uint8)
        self.select_mask_image(ix, iy, img, float(kw["x0"]), float(kw["y0"]),
                               float(kw["x1"]), float(kw["y1"]), mode)
    else:
        raise ValueError("kind must be rectangle, ellipse, polygon or mask")

    if invert:
        self.invert_selection()
        if how != "replace":
            raise NotImplementedError(
                "invert with how=%r needs two masks; invert the region instead" % how)
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


def histogram(self, *names, **kwargs):
    """Histogram one or more columns of this store.

    :param names: column names, one per axis
    :param bins: bin count, or one per axis
    :param range: (lo, hi) per axis, or None to take it from the data
    :param weight: a column name to weight by
    :param threads: 0 to decide, 1 to force serial, or a count

    The selection and every column's validity mask are honoured, and nothing is
    converted: a float32 column is read as float32, a string column as its
    dictionary codes on a category axis.
    """
    np = _np_ds
    bins = kwargs.pop("bins", 128)
    rng = kwargs.pop("range", None)
    weight = kwargs.pop("weight", None)
    threads = int(kwargs.pop("threads", 0))
    if kwargs:
        raise TypeError("unexpected keyword arguments: %s" % sorted(kwargs))

    idx = [self.find(n) for n in names]
    for n, i in zip(names, idx):
        if i < 0:
            raise KeyError("no column named %r" % n)
    if np.ndim(bins) == 0:
        bins = [bins] * len(idx)
    ranges = [None] * len(idx) if rng is None else list(rng)

    axes = []
    for k, i in enumerate(idx):
        col = self.column(i)
        if col.type() == ColumnType_String:
            axes.append(category_axis_for(col))
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
        axes.append(Axis.regular(int(bins[k]), float(lo), float(hi),
                                 AxisOptions.flow(), col.name()))

    h = HistogramNd(AxisVector(axes))
    w = -1 if weight is None else self.find(weight)
    if weight is not None and w < 0:
        raise KeyError("no column named %r" % weight)
    fill_histogram(h, self, VectorInt32(idx), w, threads)
    return h


def memory_report(self):
    """Bytes held, per column and in total -- for sizing a cache."""
    out = {"total": self.nbytes()}
    for c in self.columns:
        out[c.name()] = c.nbytes()
    return out


def __repr__(self):
    return "DataStore(%d rows, %d columns, %.1f MB)" % (
        self.n_rows(), self.n_columns(), self.nbytes() / 1e6)

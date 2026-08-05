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

# SPDX-License-Identifier: BSD-3-Clause
#
# Pythonic surface for a DataStore column.


def numpy(self):
    """The column as a numpy array, without copying where possible.

    Numeric columns are a view into the C++ buffer, in their OWN dtype -- a
    float32 column comes back float32, not widened to float64. String columns
    come back as the decoded labels, which is necessarily a new array: they are
    stored dictionary-encoded and the strings do not exist contiguously.
    """
    t = self.type()
    if t == ColumnType_Float64:
        return _ds_wrap(self, self.get_f64_view())
    if t == ColumnType_Float32:
        return _ds_wrap(self, self.get_f32_view())
    if t == ColumnType_Int64:
        return _ds_wrap(self, self.get_i64_view())
    if t == ColumnType_Int32:
        return _ds_wrap(self, self.get_i32_view())
    if t == ColumnType_Int16:
        return _ds_wrap(self, self.get_i16_view())
    if t == ColumnType_Int8:
        return _ds_wrap(self, self.get_i8_view())
    if t == ColumnType_UInt64:
        return _ds_wrap(self, self.get_u64_view())
    if t == ColumnType_UInt32:
        return _ds_wrap(self, self.get_u32_view())
    if t == ColumnType_UInt16:
        return _ds_wrap(self, self.get_u16_view())
    if t == ColumnType_UInt8:
        return _ds_wrap(self, self.get_u8_view())
    if t == ColumnType_Bool:
        out = _np_ds.empty(self.size(), dtype=bool)
        for i in range(self.size()):
            out[i] = self.value_at(i) != 0.0
        return out
    return _np_ds.array([self.string_at(i) for i in range(self.size())], dtype=object)


def codes(self):
    """A string column's integer codes, as a zero-copy view.

    What to histogram a text column by -- the codes are already a category
    axis, and the labels come from ``dictionary()``.
    """
    if self.type() != ColumnType_String:
        raise TypeError("only a string column has codes")
    return _ds_wrap(self, self.get_codes_view())


def labels(self):
    """The distinct values of a string column, in code order."""
    return [self.dictionary()[i] for i in range(len(self.dictionary()))]


@property
def dtype(self):
    return _DS_TYPE_TO_DTYPE.get(self.type(), "float64")


def mask_numpy(self):
    """The validity as a bool array, or None when every row is valid.

    Answers from whichever form the column stores. `has_mask()` is about
    storage and says False for a column whose gaps are ranges; the gaps are
    just as real, so asking here must not depend on that.
    """
    if not self.has_missing():
        return None
    out = _np_ds.empty(self.size(), dtype=_np_ds.uint8)
    self.validity().to_bytes(out)
    return out.astype(bool)



def set_numpy(self, values):
    """Store a numpy array, keeping its dtype where there is a column for it."""
    np = _np_ds
    a = np.asarray(values)
    if a.dtype.kind in ("U", "S", "O"):
        for s in a:
            self.push_string(str(s))
        return self
    name = a.dtype.name
    if name == "int8":
        self.set_i8(np.ascontiguousarray(a, dtype=np.int8))
    elif name == "int16":
        self.set_i16(np.ascontiguousarray(a, dtype=np.int16))
    elif name == "uint64":
        self.set_u64(np.ascontiguousarray(a, dtype=np.uint64))
    elif name == "uint32":
        self.set_u32(np.ascontiguousarray(a, dtype=np.uint32))
    elif name == "uint16":
        self.set_u16(np.ascontiguousarray(a, dtype=np.uint16))
    elif name == "uint8":
        self.set_u8(np.ascontiguousarray(a, dtype=np.uint8))
    elif name == "float32":
        self.set_f32(np.ascontiguousarray(a, dtype=np.float32))
    elif name == "int32":
        self.set_i32(np.ascontiguousarray(a, dtype=np.int32))
    elif name == "int64":
        self.set_i64(np.ascontiguousarray(a, dtype=np.int64))
    elif name == "bool":
        self.set_bool(np.ascontiguousarray(a, dtype=np.uint8))
    else:
        self.set_f64(np.ascontiguousarray(a, dtype=np.float64))
    return self


def __array__(self, dtype=None, copy=None):
    """`np.asarray(column)` and every numpy function that calls it.

    The other half of why the old chains read badly: ``store.group("r")["Tau"]
    .numpy()`` has two verbs in it, and ``np.mean(col)`` had to be
    ``np.mean(col.numpy())``. Hands back what ``numpy()`` does, so the
    ``_DsView`` that keeps the store alive is unchanged.

    `copy` is numpy 2's: False means "give me a view or refuse". A string
    column can never satisfy it -- it is dictionary-encoded and the strings do
    not exist contiguously, so reading it always builds an array.
    """
    a = self.numpy()
    if copy is False and self.type() == ColumnType_String:
        raise ValueError(
            "a string column is dictionary-encoded, so it cannot be viewed "
            "without a copy")
    if dtype is not None and _np_ds.dtype(dtype) != a.dtype:
        if copy is False:
            raise ValueError("cannot view a %s column as %s without a copy"
                             % (a.dtype, _np_ds.dtype(dtype)))
        return a.astype(dtype)
    return a.copy() if copy is True else a


def __len__(self):
    return self.size()


def __repr__(self):
    extra = ""
    if self.type() == ColumnType_String:
        extra = ", %d labels" % len(self.dictionary())
    if self.has_missing():
        extra += ", masked"
    return "Column(%r, %s, %d rows%s, %.1f kB)" % (
        self.name(), self.dtype, self.size(), extra, self.nbytes() / 1024.0)


def __eq__(self, other):
    """Elementwise, like an array -- NOT identity.

    This used to be SWIG's default, so `column == "m000.spc"` was `False`
    rather than a mask, and a selection built from it quietly matched nothing.
    A wrong answer that raises nothing is worse than a missing feature, which
    is why this is here ahead of the rest of the array protocol.
    """
    return _ds_compare(self, other, "eq")


def __ne__(self, other):
    return _ds_compare(self, other, "ne")


def __lt__(self, other):
    return _ds_compare(self, other, "lt")


def __le__(self, other):
    return _ds_compare(self, other, "le")


def __gt__(self, other):
    return _ds_compare(self, other, "gt")


def __ge__(self, other):
    return _ds_compare(self, other, "ge")


# Defining __eq__ in Python sets __hash__ to None, which would make every
# Column unhashable -- and a column in a set or a dict key would start failing
# somewhere with no connection to this change. Columns were hashable by
# identity before and stay that way.
__hash__ = object.__hash__


def __getitem__(self, key):
    """One value for an integer, an array for a slice or an index array.

    **Element access says nothing about validity.** A masked row returns the
    value stored in it, exactly as :meth:`numpy` does, and "measured or not"
    stays an explicit question -- :meth:`valid` or :meth:`mask_numpy`. The
    alternative, returning NaN where masked, cannot be done for an integer or a
    text column without changing its dtype, which is the whole reason the mask
    exists.

    An integer index does NOT go through :meth:`numpy` for the two types whose
    array form is a copy: a text column is dictionary-encoded and a bool column
    is bit-packed, so ``numpy()[i]`` would decode the whole column to read one
    row. Measured on 200 000 rows, that is 44 ms per access against 0.2 us.
    """
    if isinstance(key, slice) or not isinstance(key, int):
        # A slice or an index array materialises either way, so numpy does it.
        # For a text column that decodes the whole column each time -- compare
        # `codes()` against a dictionary index when that is in a loop.
        return self.numpy()[key]

    n = self.size()
    i = key + n if key < 0 else key
    if i < 0 or i >= n:
        raise IndexError("column index %d out of range for %d rows" % (key, n))
    t = self.type()
    if t == ColumnType_String:
        return self.string_at(i)
    if t == ColumnType_Bool:
        return bool(self.value_at(i))
    # Through numpy, not value_at: value_at hands back a double, so an int64
    # above 2**53 would come back as a different number.
    return self.numpy()[i]


def __setitem__(self, key, value):
    """Write through to the column's buffer. Numeric columns only.

    A bool column is bit-packed and a text one is dictionary-encoded, so both
    decode through a copy and a write to that copy is **lost** -- verified.
    A write that vanishes is worse than one that refuses, so those two raise
    and name the route that works.
    """
    t = self.type()
    if t == ColumnType_String:
        raise TypeError(
            "a string column is dictionary-encoded, so writing through an "
            "index would be lost; rebuild it with set_numpy() or set_codes()")
    if t == ColumnType_Bool:
        raise TypeError(
            "a bool column is bit-packed, so writing through an index would "
            "be lost; rebuild it with set_numpy() or set_bool()")
    self.numpy()[key] = value


def __iter__(self):
    """The values, in order. One decode for a text column, not one per row."""
    return iter(self.numpy())

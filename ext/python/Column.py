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
    """The validity mask as a bool array, or None when everything is valid."""
    if not self.has_mask():
        return None
    out = _np_ds.empty(self.size(), dtype=_np_ds.uint8)
    self.mask().to_bytes(out)
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


def __len__(self):
    return self.size()


def __repr__(self):
    extra = ""
    if self.type() == ColumnType_String:
        extra = ", %d labels" % len(self.dictionary())
    if self.has_mask():
        extra += ", masked"
    return "Column(%r, %s, %d rows%s, %.1f kB)" % (
        self.name(), self.dtype, self.size(), extra, self.nbytes() / 1024.0)

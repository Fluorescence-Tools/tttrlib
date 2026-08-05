# SPDX-License-Identifier: BSD-3-Clause
#
# Module-level support for the DataStore Python surface. At module scope for the
# same reason hist_support.py is: a %pythoncode file inside %extend lands in the
# class namespace, where a method cannot see it by name.

import numpy as _np_ds

# numpy dtype <-> ColumnType. float64 is the fallback rather than an error,
# because a caller handing over a float16 or a datetime column means "store this
# number", and refusing is less useful than widening.
_DS_DTYPE_TO_TYPE = {
    "float64": ColumnType_Float64,
    "float32": ColumnType_Float32,
    "int64": ColumnType_Int64,
    "int32": ColumnType_Int32,
    "bool": ColumnType_Bool,
}

_DS_TYPE_TO_DTYPE = {
    ColumnType_Float64: "float64",
    ColumnType_Float32: "float32",
    ColumnType_Int64: "int64",
    ColumnType_Int32: "int32",
    ColumnType_Bool: "bool",
    ColumnType_String: "str",
}


class _DsView(_np_ds.ndarray):
    """A column view that keeps its store alive. See hist_support._OwnedView."""

    _owner = None

    def __array_finalize__(self, obj):
        if obj is not None:
            self._owner = getattr(obj, "_owner", None)


def _ds_wrap(owner, flat):
    v = flat.view(_DsView)
    v._owner = owner
    return v

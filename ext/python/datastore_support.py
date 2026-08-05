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
    "int16": ColumnType_Int16,
    "int8": ColumnType_Int8,
    "uint64": ColumnType_UInt64,
    "uint32": ColumnType_UInt32,
    "uint16": ColumnType_UInt16,
    "uint8": ColumnType_UInt8,
    "bool": ColumnType_Bool,
}

_DS_TYPE_TO_DTYPE = {
    ColumnType_Float64: "float64",
    ColumnType_Float32: "float32",
    ColumnType_Int64: "int64",
    ColumnType_Int32: "int32",
    ColumnType_Int16: "int16",
    ColumnType_Int8: "int8",
    ColumnType_UInt64: "uint64",
    ColumnType_UInt32: "uint32",
    ColumnType_UInt16: "uint16",
    ColumnType_UInt8: "uint8",
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


# --- what is live ------------------------------------------------------------


def data_stores():
    """Every DataStore alive in this process, largest first.

    A session accumulates these without meaning to -- a TTTR file is one, the
    bursts extracted from it another, an SMLM table a third -- and each can be
    most of the memory in the process. Returns dicts so the result can be
    printed, sorted or turned into a table without knowing about the C++ type.
    """
    return [
        {
            "id": i.id,
            "label": i.label,
            "rows": i.n_rows,
            "columns": i.n_columns,
            "bytes": i.nbytes,
            "MB": round(i.nbytes / 1e6, 1),
            "selected": i.n_selected,
        }
        for i in live_data_stores()
    ]


def data_store_report():
    """`data_stores()` as a table, for printing at a prompt."""
    rows = data_stores()
    if not rows:
        return "no data stores"
    width = max(len(r["label"]) or 1 for r in rows)
    out = ["%-4s %-*s %12s %8s %10s" % ("id", width, "label", "rows", "cols", "MB")]
    for r in rows:
        out.append("%-4d %-*s %12d %8d %10.1f"
                   % (r["id"], width, r["label"] or "-", r["rows"], r["columns"], r["MB"]))
    out.append("%s total %.1f MB in %d stores"
               % (" " * (width + 5), live_data_store_bytes() / 1e6, len(rows)))
    return "\n".join(out)

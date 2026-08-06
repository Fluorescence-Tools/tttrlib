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
            # Direct children. Their bytes are already in "bytes" -- the
            # registry lists roots, and a root reports its whole tree.
            "groups": i.n_groups,
        }
        for i in live_data_stores()
    ]


def data_store_report():
    """`data_stores()` as a table, for printing at a prompt."""
    rows = data_stores()
    if not rows:
        return "no data stores"
    width = max(len(r["label"]) or 1 for r in rows)
    out = ["%-4s %-*s %12s %8s %7s %10s"
           % ("id", width, "label", "rows", "cols", "groups", "MB")]
    for r in rows:
        out.append("%-4d %-*s %12d %8d %7d %10.1f"
                   % (r["id"], width, r["label"] or "-", r["rows"], r["columns"],
                      r["groups"], r["MB"]))
    out.append("%s total %.1f MB in %d stores"
               % (" " * (width + 5), live_data_store_bytes() / 1e6, len(rows)))
    return "\n".join(out)


# --- selection helpers -------------------------------------------------------
#
# Module scope, not inside %extend: a %pythoncode file lands in the CLASS
# namespace, so anything defined there is a method and is invisible by name from
# inside another method. This has now caught me twice.


def _ds_mode(how):
    modes = {
        "replace": DataStore.Combine_Replace,
        "and": DataStore.Combine_And,
        "or": DataStore.Combine_Or,
        "andnot": DataStore.Combine_AndNot,
    }
    if how not in modes:
        raise ValueError("how must be one of %s" % sorted(modes))
    return modes[how]


def _ds_col(store, c):
    i = store.find(c) if isinstance(c, str) else int(c)
    if i < 0:
        raise KeyError("no column named %r" % c)
    return i


def _ds_axis_from_edges(edges, label=""):
    """An Axis over explicit bin boundaries.

    NumPy's last bin includes its upper edge while every other bin is half-open,
    and a caller replacing a NumPy histogram must not lose the highest point of
    the dataset -- bin edges are routinely taken from the data's own maximum. So
    the stored top edge is nudged up by one ulp, which cannot move any value
    except one sitting exactly on it.

    A VARIABLE axis even when the edges are evenly spaced, which costs a binary
    search per point rather than a multiply. Two faster shapes were tried and
    both are wrong:

    - Nudging the top edge of a REGULAR axis moves every interior boundary with
      it, and on an integer-valued axis -- pixel indices -- a point sitting
      exactly on a boundary then falls in the bin below.
    - Adding one extra bin of the same width keeps the interior boundaries
      exactly, but its extra bin spans ``[hi, hi + w)`` and so also catches the
      values ABOVE the top edge, which numpy drops. Folding it back into the
      last bin silently pulls in points that are off the axis.

    Both are ~2x faster and neither gives the same answer, so neither is here.
    """
    np = _np_ds
    e = np.ascontiguousarray(np.asarray(edges, dtype=np.float64))
    if e.ndim != 1 or e.size < 2:
        raise ValueError("bin edges must be a 1-D array of at least two values")
    e = e.copy()
    e[-1] = np.nextafter(e[-1], np.inf)
    return Axis.variable(e, AxisOptions.flow(), label), False

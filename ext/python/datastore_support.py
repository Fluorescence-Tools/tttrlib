# SPDX-License-Identifier: BSD-3-Clause
#
# Module-level support for the DataStore Python surface. At module scope for the
# same reason hist_support.py is: a %pythoncode file inside %extend lands in the
# class namespace, where a method cannot see it by name.

import fnmatch as _fnmatch_ds

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


class _DsBuffer(object):
    """The owner of a zero-copy column view, in a form numpy will hold on to.

    An ndarray SUBCLASS carrying the owner is not enough, and this is not
    theoretical -- it is what the code here did, and it read freed memory:

        a = np.asarray(store["x"].numpy())
        del store; gc.collect()
        a                      # [7.6e-310, 2.2e-321, 2.0]  -- was arange(3)

    numpy collapses a base chain through any array that does not own its data,
    subclass or not, so `np.asarray` on such a view hands back an array whose
    `.base` is the raw SWIG ARGOUTVIEW array and whose owner has been dropped
    on the way. The store is then free to go and nothing raises.

    An object that merely EXPOSES __array_interface__ is not an ndarray, so
    the collapse stops here and `.base` keeps this alive -- and with it the
    column, and with it the store.

    hist_support._OwnedView is the same shape and is NOT known to have the same
    problem: its chain is two views deep, so the collapse lands on an
    _OwnedView that still carries the owner, and the two other array-returning
    paths there (an axis's edges, a profile's mean) are ARGOUTVIEWM, where
    numpy owns the buffer through a capsule and nothing can dangle. Its root
    still owns nothing, so it is fragile rather than broken -- worth the same
    treatment if it is ever touched, not worth a change on its own.
    """

    __slots__ = ("_owner", "__array_interface__")

    def __init__(self, owner, flat):
        self._owner = owner
        self.__array_interface__ = flat.__array_interface__


def _ds_wrap(owner, flat):
    return _np_ds.asarray(_DsBuffer(owner, flat))


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


# --- paths through the tree --------------------------------------------------
#
# A store is a tree and reaching into it used to be a four-link chain,
# `store.group("results")["Tau"].numpy()`. These give it pathlib's shape: `/`
# composes a path and nothing is looked up until the path is used.
#
# `store[...]` still means COLUMN and a slash-free key still behaves exactly as
# it did -- only a key containing a separator walks the tree. That keeps the
# ambiguity refused: a str key must not switch between a column and a group
# depending on what happens to exist.


def _ds_split_path(path):
    """A path as its components, by the rules the C++ walker uses.

    "" and "/" are this store, a leading and a trailing separator are both
    optional. An empty component, "." and ".." are rejected rather than
    reinterpreted -- same as DataStore::check_component.
    """
    if isinstance(path, StorePath):
        return path.parts
    if isinstance(path, (tuple, list)):
        out = ()
        for p in path:
            out += _ds_split_path(p)
        return out
    s = str(path).strip("/")
    if not s:
        return ()
    parts = tuple(s.split("/"))
    for p in parts:
        if not p or p in (".", ".."):
            raise KeyError("bad path component %r in %r" % (p, path))
    return parts


def _ds_resolve_parts(store, parts):
    """The column or group `parts` names, relative to `store`.

    The column wins when a name is both. That is the existing rule -- `find()`
    never sees a group and `store["meta"]` is the column -- carried down the
    tree rather than reversed halfway along it. The group of that name is still
    `store.group("meta")`.
    """
    if not parts:
        return store
    if len(parts) > 1:
        where = "/".join(parts[:-1])
        if not store.has_group(where):
            raise KeyError("no group %r on the way to %r"
                           % (where, "/".join(parts)))
        store = store.group(where)      # keeps the root alive; see DataStore.i
    last = parts[-1]
    if store.find(last) >= 0:
        return store[last]              # via __getitem__, so it holds the root
    if store.has_group(last):
        return store.group(last)
    raise KeyError("no column or group %r in %s" % (
        last, ("group %r" % "/".join(parts[:-1])) if len(parts) > 1 else "the store"))


def _ds_resolve(store, path):
    """`_ds_resolve_parts` for a path written as one string."""
    return _ds_resolve_parts(store, _ds_split_path(path))


class StorePath:
    """A path into a `DataStore` tree. Composed with ``/``, resolved on use.

    ``store / "results" / "Tau"`` looks nothing up: it builds a path, exactly as
    ``pathlib.Path("a") / "b"`` does not touch the filesystem. The lookup
    happens when the path is used -- ``p.numpy()``, ``np.mean(p)``, ``len(p)``.
    So a path can be built before the group exists, held, passed on and
    resolved later.

    Everything this class does not define itself is delegated to whatever the
    path resolves to, so ``p.dtype``, ``p.n_rows()`` and ``p.where(...)`` work
    whether it lands on a column or on a group. The names it does define --
    ``parts``, ``name``, ``parent``, ``exists``, ``resolve``, ``column``,
    ``group``, ``histogram``, ``profile`` -- shadow the target's; reach past
    them with ``p.resolve()``.

    It holds the store it is relative to, which is what keeps the memory alive:
    a column or group proxy is a borrowed reference into the root's tree, so a
    zero-copy view taken through one dangles once the root is collected. It
    does NOT hold the resolved proxy -- that would be a reference cycle through
    ``_store`` and would stop a dropped store from freeing its tree, the thing
    ``DataStore.groups`` documents itself as avoiding.
    """

    __slots__ = ("_base", "_parts")

    def __init__(self, base, parts=()):
        self._base = base
        self._parts = tuple(parts)

    # -- composition ----------------------------------------------------------

    def __truediv__(self, other):
        return StorePath(self._base, self._parts + _ds_split_path(other))

    @property
    def parts(self):
        return self._parts

    @property
    def name(self):
        """The last component, or "" for the path to the store itself."""
        return self._parts[-1] if self._parts else ""

    @property
    def parent(self):
        return StorePath(self._base, self._parts[:-1])

    # -- resolution -----------------------------------------------------------

    def resolve(self):
        """The `Column` or `DataStore` this path names. Raises `KeyError`."""
        return _ds_resolve_parts(self._base, self._parts)

    def exists(self):
        try:
            self.resolve()
        except KeyError:
            return False
        return True

    @property
    def column(self):
        """The column this path names; `TypeError` when it names a group."""
        t = self.resolve()
        if isinstance(t, DataStore):
            raise TypeError("%r is a group, not a column" % str(self))
        return t

    @property
    def group(self):
        """The group this path names; `TypeError` when it names a column."""
        t = self.resolve()
        if not isinstance(t, DataStore):
            raise TypeError("%r is a column, not a group" % str(self))
        return t

    def __getattr__(self, name):
        # Not for the slots and not for anything private: __getattr__ runs only
        # when normal lookup fails, and answering a dunder here would resolve
        # the path during copy, pickle or repr of a broken object.
        if name.startswith("_"):
            raise AttributeError(name)
        return getattr(self.resolve(), name)

    # -- the dunders, which have to be explicit -------------------------------
    #
    # Special-method lookup goes to the TYPE and bypasses __getattr__, so none
    # of these would be found by the delegation above.

    def __len__(self):
        return len(self.resolve())

    def __bool__(self):
        # A path is always truthy. Without this __len__ would make the path to
        # an empty column falsy, which is not what `if p:` is asking.
        return True

    def __getitem__(self, key):
        t = self.resolve()
        # A path that names a group indexes like a store: by column. A path
        # that names a column indexes like an array.
        return t[key] if isinstance(t, DataStore) else t.numpy()[key]

    def __iter__(self):
        t = self.resolve()
        # A column path iterates its VALUES, matching p[0] and p[1:3]. The
        # Column proxy itself is not iterable, so delegating would fail on a
        # path that indexes perfectly well.
        if not isinstance(t, DataStore):
            return iter(t.numpy())
        # A store has no __iter__, so Python would fall back to __getitem__(0),
        # (1), ... and walk off the end of the column deque -- which surfaces
        # as `RuntimeError: deque`. Say what to iterate instead.
        raise TypeError(
            "%r is a group; iterate its columns with .names, its rows through "
            "a column, or the tree with .walk()" % str(self))

    def __contains__(self, item):
        return item in self.resolve()

    def __array__(self, dtype=None, copy=None):
        t = self.resolve()
        if isinstance(t, DataStore):
            raise TypeError("%r is a group; a group is a table, not an array"
                            % str(self))
        return t.__array__(dtype, copy)

    def __str__(self):
        return "/".join(self._parts)

    def __repr__(self):
        return "StorePath(%r)" % str(self)

    def __eq__(self, other):
        if not isinstance(other, StorePath):
            return NotImplemented
        # Two identical paths into different stores are not the same path.
        return (id(self._base), self._parts) == (id(other._base), other._parts)

    def __hash__(self):
        return hash((id(self._base), self._parts))

    # -- histogramming a path -------------------------------------------------

    def histogram(self, *names, **kwargs):
        """Histogram this path.

        On a column path, bins that column: ``(s / "results" / "Tau")
        .histogram(bins=100)``. On a group path, the group's own
        ``histogram``, so extra axes are named relative to the group.
        """
        t = self.resolve()
        if isinstance(t, DataStore):
            return t.histogram(*names, **kwargs)
        return self.parent.resolve().histogram(self.name, *names, **kwargs)

    def profile(self, *names, **kwargs):
        """`DataStore.profile`, reached through a path. \\see histogram."""
        t = self.resolve()
        if isinstance(t, DataStore):
            return t.profile(*names, **kwargs)
        return self.parent.resolve().profile(self.name, *names, **kwargs)


# --- one table per histogram, named by path ----------------------------------


def _ds_hist_split(spec):
    """`(group path, leaf)` for a column spec. An index has no group path."""
    if not isinstance(spec, str) or "/" not in spec:
        return "", spec
    head, _, leaf = spec.rpartition("/")
    return head.strip("/"), leaf


def _ds_hist_retarget(store, names, kwargs, extra_keys):
    """Resolve path-shaped column specs to the one group they all live in.

    A histogram fills from ONE table -- `fill_histogram` takes a store and
    column indices in it -- and two groups have different row counts and no row
    correspondence, so axes from two of them cannot be filled together. That
    stays true; what this adds is only the reach, so `store.histogram(
    "results/Tau", "results/E")` finds its way to the group instead of the
    caller having to.

    Returns `(target store, names with the group path stripped)` and rewrites
    the optional column kwargs (`weight`, `sample`) in place.
    """
    n = len(names)
    specs = list(names) + [kwargs.get(k) for k in extra_keys]
    # The common case, and every call on a flat table: nothing to do, and no
    # per-axis cost for having groups in the library.
    if not any(isinstance(s, str) and "/" in s for s in specs):
        return store, names

    seen = {}          # group path -> the first spec that asked for it
    leaves = []
    for s in specs:
        if s is None:
            leaves.append(None)
            continue
        head, leaf = _ds_hist_split(s)
        seen.setdefault(head, s)
        leaves.append(leaf)
    if len(seen) > 1:
        a, b = list(seen.values())[:2]
        raise ValueError(
            "a histogram fills from one table: %r and %r are in different "
            "groups, which have different row counts and no row "
            "correspondence" % (a, b))

    head = next(iter(seen))
    target = store.group(head) if head else store
    for i, k in enumerate(extra_keys):
        if kwargs.get(k) is not None:
            kwargs[k] = leaves[n + i]
    return target, tuple(leaves[:n])


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


# --- building a group in one call --------------------------------------------
#
# `add_group` and `ensure_group` are wrapped C++ methods and the keep-alive
# pythonappend that hands the root to the returned proxy lives INSIDE the
# generated function (see TTTRLIB_DS_KEEP_ROOT in DataStore.i). So the optional
# columns argument is added by wrapping the generated method here, at module
# scope where the class exists -- redefining it in the %pythoncode block would
# shadow the generated one and lose the keep-alive with it.


def _ds_fill_group(g, columns):
    if columns is None:
        return g
    for name, values in dict(columns).items():
        g.add(str(name), _np_ds.asarray(values))
    return g


def _ds_group_adder(native, what):
    def add(self, name, columns=None):
        return _ds_fill_group(native(self, name), columns)
    add.__name__ = native.__name__
    add.__doc__ = (native.__doc__ or "") + """
`columns` is an optional ``{name: values}``, so a side table is one call:

    store.%s("meta", {"source": ["run.ptu"], "n_frames": [40]})

which is the add-then-loop-then-set_n_rows the callers were writing by hand.
The row count follows from the first column, exactly as ``add`` does it.
""" % what
    return add


DataStore.add_group = _ds_group_adder(DataStore.add_group, "add_group")
DataStore.ensure_group = _ds_group_adder(DataStore.ensure_group, "ensure_group")


# --- combining stores --------------------------------------------------------


def concat(stores, axis="rows", join="outer", on_duplicate="refuse"):
    """Combine stores, the way ``pd.concat`` does.

    ``axis="rows"`` stacks separate measurements; ``axis="columns"`` puts files
    describing the *same* rows side by side. ``axis=0`` and ``axis=1`` are
    accepted too. There is no ``ignore_index``: a store has no index.

        bursts = tttrlib.concat([tttrlib.read_hdf5(f) for f in files])

    :param join: for ``axis="rows"`` -- ``"outer"`` keeps the union of columns
        and marks the rows that had no value **not measured**; ``"inner"`` keeps
        only the columns every store has.
    :param on_duplicate: for ``axis="columns"`` -- ``"refuse"`` (the default)
        names the column that clashed, ``"keep-first"`` keeps the earlier one.

    A column missing from one store **keeps its dtype**. That is the reason to
    use this rather than a data frame: pandas must widen an ``int64`` column to
    ``float64`` to hold a ``NaN``, and the dtype cannot be recovered afterwards.
    The validity mask says "not measured" without touching the type.

    Groups are not descended into -- row counts are per group, so combining two
    trees is ambiguous. Concatenate the groups you mean:
    ``concat([a.group("results"), b.group("results")])``.
    """
    items = list(stores)
    if not items:
        raise ValueError("concat needs at least one store")
    axes = {"rows": "rows", "columns": "columns", 0: "rows", 1: "columns",
            "index": "rows"}
    if axis not in axes:
        raise ValueError('axis must be "rows" or "columns" (or 0 or 1)')
    axis = axes[axis]

    # Seed with a copy of the first. The row count goes on BEFORE the columns:
    # append_columns refuses a mismatch, and an empty store has none.
    out = DataStore()
    out.set_n_rows(items[0].n_rows())
    out.set_label(items[0].label())
    out.append_columns(items[0])
    if axis == "rows":
        joins = {"outer": DataStore.Join_Outer, "inner": DataStore.Join_Inner}
        if join not in joins:
            raise ValueError('join must be "outer" or "inner"')
        for s in items[1:]:
            out.append_rows(s, joins[join])
    else:
        dups = {"refuse": DataStore.OnDuplicate_Refuse,
                "keep-first": DataStore.OnDuplicate_KeepFirst}
        if on_duplicate not in dups:
            raise ValueError('on_duplicate must be "refuse" or "keep-first"')
        for s in items[1:]:
            out.append_columns(s, dups[on_duplicate])
    return out

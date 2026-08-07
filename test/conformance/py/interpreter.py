# SPDX-License-Identifier: BSD-3-Clause
"""The Python dispatch table for the conformance vocabulary.

This is the reference implementation of `../OPS.md`: the R, Java and JavaScript
runners are ports of this file and must produce the same values from the same
case list.

It is imported by two callers with different jobs, and the sharing is the point:

* ``test/python/test_conformance.py`` runs a case and asserts against the
  committed ``expect`` block;
* ``tools/conformance_update.py`` runs a case and *writes* that block.

If the generator used a different interpreter from the runner, a regenerated
expectation would be self-consistent and still wrong.
"""
from __future__ import annotations

import json
import math
import os
import re
from typing import Any, Dict, List, Sequence

import numpy as np

import tttrlib

# The dtype spelling in OPS.md. NumPy's own names differ per platform for the
# C types (`long` is 32-bit on Windows), so map explicitly rather than reading
# `dtype.name` and hoping.
_DTYPES = {
    np.dtype(np.float64): "float64", np.dtype(np.float32): "float32",
    np.dtype(np.int64): "int64", np.dtype(np.int32): "int32",
    np.dtype(np.int16): "int16", np.dtype(np.int8): "int8",
    np.dtype(np.uint64): "uint64", np.dtype(np.uint32): "uint32",
    np.dtype(np.uint16): "uint16", np.dtype(np.uint8): "uint8",
    np.dtype(np.bool_): "bool",
}

_ADD_DTYPE = {
    "f64": np.float64, "f32": np.float32,
    "i64": np.int64, "i32": np.int32, "i16": np.int16, "i8": np.int8,
    "u64": np.uint64, "u32": np.uint32, "u16": np.uint16, "u8": np.uint8,
}

# ds.column_<suffix> asks for a column of exactly this type; a mismatch is an
# error, which is what makes a dtype round trip a test rather than a coercion.
_COLUMN_DTYPE = dict(_ADD_DTYPE)


class ConformanceError(RuntimeError):
    """A case is malformed, or names an op this runner does not implement."""


class UnsupportedOp(ConformanceError):
    """The op exists in the vocabulary but not in this dispatch table."""


def _as_array(v: Any) -> np.ndarray:
    if isinstance(v, np.ndarray):
        return v
    raise ConformanceError(f"expected an array, got {type(v).__name__}")


def _as_strings(v: Any) -> List[str]:
    if isinstance(v, list) and all(isinstance(x, str) for x in v):
        return v
    raise ConformanceError(f"expected a string list, got {type(v).__name__}")


def _sequence(v: Any):
    """Anything `len`/`nth`/`slice`/`first` accept: an array or a string list.

    A multi-dimensional array is flattened row-major first. NumPy's `len` is the
    length of the FIRST AXIS, so a (40, 256, 256) image would answer 40 here and
    2621440 in the other three runners, which flatten at the binding boundary.
    Row-major is the layout every binding already agrees on, so flattening makes
    the element ops mean the same thing everywhere. `shape` still reports the
    real shape -- it reads the array directly.
    """
    if isinstance(v, np.ndarray):
        return v.ravel() if v.ndim > 1 else v
    if isinstance(v, (list, str)):
        return v
    raise ConformanceError(f"expected a sequence, got {type(v).__name__}")


def _scalar(v: Any) -> Any:
    """Collapse a NumPy scalar to a plain Python one, so JSON sees int/float."""
    if isinstance(v, np.generic):
        v = v.item()
    if isinstance(v, bool):
        return v
    if isinstance(v, (int, float, str)):
        return v
    raise ConformanceError(f"not a comparable scalar: {type(v).__name__}")


class Interpreter:
    """Runs one case's steps and returns the bindings named by `expect`."""

    def __init__(self, data_root: str, tmp_paths: Sequence[str]):
        self.data_root = data_root
        self.tmp_paths = list(tmp_paths)
        self.bindings: Dict[str, Any] = {}

    # -- argument substitution ------------------------------------------------

    def _arg(self, a: Any) -> Any:
        if isinstance(a, str) and a.startswith("$"):
            name = a[1:]
            if name.startswith("data") and name[4:].isdigit():
                return os.path.join(self.data_root, self._data[int(name[4:])])
            # `$tmp0.dstore` -- a scratch path with a suffix, because a writer
            # that picks its format from the extension needs one and a bare
            # scratch name has none.
            m = re.match(r"tmp(\d+)(\..*)?$", name)
            if m:
                return self.tmp_paths[int(m.group(1))] + (m.group(2) or "")
            if name not in self.bindings:
                raise ConformanceError(f"step reads unbound '{name}'")
            return self.bindings[name]
        if isinstance(a, list):
            return [self._arg(x) for x in a]
        return a

    # -- the case loop --------------------------------------------------------

    def run(self, case: Dict[str, Any]) -> Dict[str, Any]:
        self._data = case.get("data", [])
        self.produced_by: Dict[str, str] = {}
        for i, step in enumerate(case["steps"]):
            op = step["op"]
            args = [self._arg(a) for a in step.get("args", [])]
            on = self.bindings[step["on"]] if "on" in step else None
            if "on" in step and step["on"] not in self.bindings:
                raise ConformanceError(f"step {i} reads unbound '{step['on']}'")

            if step.get("throws"):
                # The contract is "this raises", so the exception type is
                # deliberately not part of it: SWIG maps the same C++ throw to
                # RuntimeError, an R condition, a Java RuntimeException and a JS
                # Error, and pinning four different names would test SWIG.
                try:
                    self._dispatch(op, on, args)
                    threw = False
                except Exception:
                    threw = True
                if "as" in step:
                    self.bindings[step["as"]] = threw
                    # "did it throw" is always an expectation, whatever the op
                    # would otherwise have produced.
                    self.produced_by[step["as"]] = "throws"
                continue

            result = self._dispatch(op, on, args)
            if "as" in step:
                if step["as"] in self.bindings:
                    raise ConformanceError(f"rebinds '{step['as']}'")
                self.bindings[step["as"]] = result
                self.produced_by[step["as"]] = op
        return self.bindings

    # -- dispatch -------------------------------------------------------------

    def _dispatch(self, op: str, on: Any, args: List[Any]) -> Any:
        fn = _OPS.get(op)
        if fn is None:
            raise UnsupportedOp(op)
        return fn(on, args)


# ---------------------------------------------------------------------------
# Generic ops
# ---------------------------------------------------------------------------

def _op_len(on, args):
    return len(_sequence(on))


def _op_sum(on, args):
    a = _as_array(on)
    if a.dtype == np.bool_ or np.issubdtype(a.dtype, np.integer):
        # Python ints are unbounded, so the sum is exact whatever the width.
        # The 2^53 guard lives in the comparison, not here.
        return int(a.sum(dtype=np.uint64 if a.dtype == np.uint64 else np.int64))
    return float(a.sum())


def _op_mean(on, args):
    return float(_as_array(on).mean())


def _op_min(on, args):
    return _scalar(_as_array(on).min())


def _op_max(on, args):
    return _scalar(_as_array(on).max())


def _op_argmax(on, args):
    return int(np.argmax(_as_array(on)))


def _op_argmin(on, args):
    return int(np.argmin(_as_array(on)))


def _op_first(on, args):
    return _scalar(_sequence(on)[0])


def _op_last(on, args):
    return _scalar(_sequence(on)[-1])


def _op_nth(on, args):
    return _scalar(_sequence(on)[int(args[0])])


def _op_slice(on, args):
    return _sequence(on)[int(args[0]):int(args[1])]


def _op_to_list(on, args):
    if isinstance(on, np.ndarray):
        return [_scalar(x) for x in on.ravel()]
    return list(_as_strings(on))


def _op_unique_sorted(on, args):
    return [_scalar(x) for x in np.unique(_as_array(on))]


def _op_shape(on, args):
    return [int(x) for x in _as_array(on).shape]


def _op_contains(on, args):
    if not isinstance(on, str):
        raise ConformanceError("contains expects a string")
    return str(args[0]) in on


def _op_round(on, args):
    return round(float(on), int(args[0]))


def _op_identity(on, args):
    return on


def _op_count_gt(on, args):
    """How many elements exceed a threshold.

    The way to compare a 2.6-million-pixel float image without writing it into
    the case file: `mean_micro_time > 0` counts the pixels that got a value.
    """
    return int((_as_array(on) > float(args[0])).sum())


# ---------------------------------------------------------------------------
# tttr.*
# ---------------------------------------------------------------------------

def _op_tttr_open(on, args):
    return tttrlib.TTTR(str(args[0]), str(args[1]))


def _op_tttr_burst_search(on, args):
    return np.asarray(on.burst_search(int(args[0]), int(args[1]),
                                      float(args[2]), str(args[3])))


def _op_tttr_microtime_histogram(on, args):
    hist, _ = on.get_microtime_histogram(micro_time_coarsening=int(args[0]))
    return np.asarray(hist)


def _op_tttr_by_channel(on, args):
    return on.get_tttr_by_channel([int(c) for c in args[0]])


# ---------------------------------------------------------------------------
# correlator.*
# ---------------------------------------------------------------------------

def _op_correlator_new(on, args):
    """A correlator with its geometry set, ready for set_tttr."""
    c = tttrlib.Correlator()
    c.n_bins = int(args[0])
    c.n_casc = int(args[1])
    return c


def _op_correlator_set_tttr(on, args):
    on.set_tttr(args[0], args[1])


def _op_correlator_curve_size(on, args):
    cc = tttrlib.CorrelatorCurve()
    cc.n_bins = int(args[0])
    cc.n_casc = int(args[1])
    return int(cc.size())


# ---------------------------------------------------------------------------
# DataStore
# ---------------------------------------------------------------------------

def _add_numeric(suffix):
    def add(on, args):
        on.add(str(args[0]), np.asarray(args[1], dtype=_ADD_DTYPE[suffix]))
    return add


def _op_ds_add_string(on, args):
    on.add(str(args[0]), [str(s) for s in args[1]])


def _column_typed(suffix):
    want = np.dtype(_COLUMN_DTYPE[suffix])

    def get(on, args):
        col = on[str(args[0])]
        a = col.numpy()
        if a.dtype != want:
            raise ConformanceError(
                f"column '{args[0]}' is {a.dtype}, the case asked for {want}")
        return a
    return get


def _op_ds_column_strings(on, args):
    col = on[str(args[0])]
    return [col.string_at(i) for i in range(col.size())]


def _op_ds_column_dtype(on, args):
    # Column.dtype is already a string; only the text column needs renaming,
    # because the binding says "str" and OPS.md says "string".
    d = str(on[str(args[0])].dtype)
    if d == "str":
        return "string"
    if np.dtype(d) not in _DTYPES:
        raise ConformanceError(f"dtype {d} is not in the canonical set")
    return d


def _op_ds_column_names(on, args):
    return [str(s) for s in on.names]


def _op_ds_select_range(on, args):
    # The C++ selector takes a column INDEX; the case names the column, because
    # a positional index would make every case depend on the order columns were
    # added. The range is half-open, which is what the expectation pins.
    idx = on.find(str(args[0]))
    if idx < 0:
        raise ConformanceError(f"no column '{args[0]}'")
    on.select_range(idx, float(args[1]), float(args[2]))


# ---------------------------------------------------------------------------
# tiff.*
# ---------------------------------------------------------------------------
#
# The suite's IN_ARRAY3 case, paired with a 3-D output view on the way back.
# The values arrive flat with the dimensions as separate arguments and each
# runner shapes them -- the same rule as hist.update, and for the same reason.

def _op_tiff_write_f64(on, args):
    n_frames, height, width = int(args[1]), int(args[2]), int(args[3])
    block = np.asarray(args[4], dtype=np.float64).reshape(n_frames, height, width)
    tttrlib._tiff_write_f64(str(args[0]), block)


def _op_tiff_read_f64(on, args):
    return np.asarray(tttrlib._tiff_read_f64(str(args[0])), dtype=np.float64)


# ---------------------------------------------------------------------------
# burst.*
# ---------------------------------------------------------------------------
#
# find_bursts returns an (n_bursts, 2) block of start/stop indices and also
# leaves the bursts on the filter, which is why `burst.find` binds its result
# AND later ops read the same filter.

def _op_burst_new(on, args):
    return tttrlib.BurstFilter(args[0])


def _op_burst_find(on, args):
    return np.asarray(on.find_bursts(), dtype=np.int64)


def _op_burst_properties(on, args):
    return np.asarray(on.get_all_burst_properties(), dtype=np.float64)


# ---------------------------------------------------------------------------
# mask.*
# ---------------------------------------------------------------------------
#
# Photon selection. A TTTRMask is a per-event boolean over one TTTR, and the
# selections compose -- which is the part worth pinning across languages, since
# each binding reaches select_channels with a different array spelling.

def _op_mask_new(on, args):
    m = tttrlib.TTTRMask()
    m.set_tttr(args[0])
    return m


def _op_mask_select_channels(on, args):
    on.select_channels(args[0], np.asarray(args[1], dtype=np.int8), bool(args[2]))


def _op_mask_array(on, args):
    """The per-event mask, one byte per event.

    get_indices() would say the same thing, but SWIG-R names it
    TTTRMask__get_indices__SWIG_0 -- a doubly-underscored generated symbol with
    no stable dispatcher -- while get_mask_array() is plainly named everywhere.
    Summing the mask is also order-independent, so it cannot be fooled by a
    binding that returns the right events in the wrong order.
    """
    return np.asarray(on.get_mask_array(), dtype=np.int64)


# ---------------------------------------------------------------------------
# nn.* / csvfile.* / feature.*
# ---------------------------------------------------------------------------
#
# Three subsystems that reached only Python and JavaScript until the R and Java
# modules gained them. Cases here are what stops that from silently regressing.

def _op_nn_from_json(on, args):
    return tttrlib.NeuralNet.from_json_string(str(args[0]))


def _op_nn_predict(on, args):
    return np.asarray(on.predict(tttrlib.VectorDouble([float(v) for v in args[0]])),
                      dtype=np.float64)


def _op_csv_write(on, args):
    # _write_csv_native, not write_csv: the latter is a %pythoncode convenience
    # taking keyword arguments, so it exists only in Python. The native call --
    # (filename, store, CsvWriteOptions) -- is the one every binding exports.
    tttrlib._write_csv_native(str(args[0]), args[1], tttrlib.CsvWriteOptions())


def _op_csv_read(on, args):
    store = tttrlib.DataStore()
    tttrlib.read_csv_into(store, str(args[0]), tttrlib.CsvOptions())
    return store


def _bursts_2d(flat):
    """A flat start/stop list as the (n, 2) block BVA and 2CDE take.

    The vocabulary passes arrays flat and each runner shapes them -- the same
    rule as hist.update and tiff.write_f64.
    """
    a = np.asarray(flat, dtype=np.int64)
    return a.reshape(-1, 2)


def _op_feature_new(on, args):
    cls = tttrlib.BVA if str(args[0]) == "bva" else tttrlib.TwoCDE
    f = cls(args[1])
    f.set_donor(tttrlib.VectorInt32([int(c) for c in args[2]]))
    f.set_acceptor(tttrlib.VectorInt32([int(c) for c in args[3]]))
    return f


def _op_feature_compute(on, args):
    bursts = _bursts_2d(args[0])
    if isinstance(on, tttrlib.BVA):
        on.compute_bursts(bursts, int(args[1]), float(args[2]))
    else:
        on.compute_bursts(bursts, float(args[1]), 0, 0)


def _op_feature_values(on, args):
    v = (on.get_proximity_ratio_mean() if isinstance(on, tttrlib.BVA)
         else on.get_two_cde())
    return np.asarray(v, dtype=np.float64)


# ---------------------------------------------------------------------------
# phasor.*
# ---------------------------------------------------------------------------
#
# Static methods on DecayPhasor: pure arithmetic on scalars, plus one that
# takes a bin-count vector. Nothing here reads a file, so these cases run
# wherever the binding exists at all.

def _op_phasor_from_bincounts(on, args):
    # phasor_of_bincounts, not compute_phasor_bincounts: the native method takes
    # a std::vector<int>& , which R cannot pass at all. See DecayPhasor.i.
    return np.asarray(tttrlib.DecayPhasor.phasor_of_bincounts(
        np.asarray(args[0], dtype=np.int32),
        float(args[1]), int(args[2]), float(args[3]), float(args[4])),
        dtype=np.float64)


# ---------------------------------------------------------------------------
# pda.*
# ---------------------------------------------------------------------------
#
# Photon-distribution analysis, and the suite's ARGOUTVIEW_ARRAY2 case: the
# S1S2 matrix is (nmax+1) x (nmax+1). Python and JavaScript carry that shape,
# R and Java hand back a flat block, so -- as with CLSM -- the cases reduce the
# matrix rather than asking it for its shape.

def _op_pda_new(on, args):
    return tttrlib.Pda(int(args[0]), int(args[1]), float(args[2]), float(args[3]),
                       np.asarray(args[4], dtype=np.float64))


def _op_pda_s1s2(on, args):
    return np.asarray(on.get_S1S2_matrix())


def _op_pda_histogram_y(on, args):
    _, hy = on.get_1dhistogram()
    return np.asarray(hy)


# ---------------------------------------------------------------------------
# fit.*
# ---------------------------------------------------------------------------
#
# The decay fitters, through the raw registry-driven interface rather than each
# binding's sugar: decay_fit_setup_vector / DecayFit2 / DecayFitProblem exist
# under those names in all four, while Python's setup_vector() and
# results_as_dict() are %pythoncode and exist in one.

def _op_fit_setup_vector(on, args):
    return np.asarray(tttrlib.decay_fit_setup_vector(str(args[0]), str(args[1])),
                      dtype=np.float64)


def _op_fit_problem(on, args):
    prob = tttrlib.DecayFitProblem(int(args[0]), int(args[1]), float(args[2]))
    prob.irf = tttrlib.VectorDouble([float(v) for v in args[3]])
    prob.background = tttrlib.VectorDouble([float(v) for v in args[4]])
    prob.data = tttrlib.VectorDouble([float(v) for v in args[5]])
    return prob


def _op_fit_new(on, args):
    return tttrlib.DecayFit2(str(args[0]),
                             tttrlib.VectorDouble([float(v) for v in args[1]]),
                             [float(v) for v in args[2]])


def _op_fit_run(on, args):
    constraints = tttrlib.DecayFitConstraints(
        tttrlib.VectorInt32([int(v) for v in args[1]]))
    return on.fit([float(v) for v in args[0]], constraints, args[2])


# ---------------------------------------------------------------------------
# clsm.*
# ---------------------------------------------------------------------------
#
# The suite's ARGOUTVIEW_ARRAY3 case: an intensity image is (frame, line, pixel).
# Only Python and JavaScript carry that shape with the array; R flattens and
# Java fills a 1-D buffer, so the cases pin the three dimensions as separate
# scalars and reduce the image rather than asking for its shape.

def _op_clsm_open(on, args):
    return tttrlib.CLSMImage(args[0], channels=[int(c) for c in args[1]], fill=True)


def _op_clsm_intensity(on, args):
    return np.asarray(on.get_intensity())


def _op_clsm_mean_micro_time(on, args):
    return np.asarray(on.get_mean_micro_time(args[0]))


def _op_clsm_decay_of_pixels(on, args):
    """The masked decay -- the library's only live INPLACE_ARRAY3 user.

    The mask is (frame, line, pixel) uint8; the case gives its rectangle as
    [frame0, line0, line1, pixel0, pixel1] because the vocabulary has no way to
    build an array, and a literal 2.6-million-element mask in a case file would
    be absurd.
    """
    img, tttr = on, args[0]
    f0, l0, l1, p0, p1 = (int(v) for v in args[1])
    mask = np.zeros((int(img.n_frames), int(img.n_lines), int(img.n_pixel)),
                    dtype=np.uint8)
    mask[f0:, l0:l1, p0:p1] = 1
    return np.asarray(img.get_decay_of_pixels_v(tttr, mask, int(args[2]),
                                                bool(args[3])), dtype=np.int64)


def _op_clsm_fluorescence_decay(on, args):
    """(frame, line, pixel, tac), flattened.

    The `_v` accessor rather than the native one: SWIG's R overload dispatcher
    cannot reach the parameterised form of `get_fluorescence_decay` (it matches
    against the C++ parameter list, output pointers and all). Using the vector
    return everywhere keeps the four runners calling the same thing.
    """
    return np.asarray(on.get_fluorescence_decay_v(
        args[0], int(args[1]), bool(args[2])), dtype=np.int64)


# ---------------------------------------------------------------------------
# hist.*
# ---------------------------------------------------------------------------
#
# The suite's IN_ARRAY2 case. The samples arrive as a flat list and every runner
# shapes them (n, 1) -- one column, n rows -- because that is the shape the
# histogram wants and shaping it in the case file would make the case format
# grow a shape.

def _op_hist_new(on, args):
    return tttrlib.doubleHistogram()


def _op_hist_update(on, args):
    on.update(np.asarray(args[0], dtype=np.float64).reshape(-1, 1))


def _op_hist_counts(on, args):
    return np.asarray(on.get_histogram())


# ---------------------------------------------------------------------------
# bitmask.*
# ---------------------------------------------------------------------------
#
# The one place the suite exercises INPLACE_ARRAY1: to_bytes fills a buffer the
# caller allocates. Every binding spells that differently -- Python and
# JavaScript mutate the array they are handed, Java copies back on release, and
# R cannot mutate at all (copy-on-modify) so rarrays.i returns the result
# instead. The dispatch table is exactly where that difference belongs.

def _op_bitmask_new(on, args):
    return tttrlib.BitMask(int(args[0]))


def _op_bitmask_to_bytes(on, args):
    out = np.zeros(int(on.size()), dtype=np.uint8)
    on.to_bytes(out)
    return out


# ---------------------------------------------------------------------------
# registry.*
# ---------------------------------------------------------------------------

def _op_registry_json(on, args):
    return tttrlib.registry_json()


def _op_registry_category_json(on, args):
    return tttrlib.registry_category_json(str(args[0]))


def _op_registry_categories(on, args):
    return [str(s) for s in tttrlib.registry_categories()]


# ---------------------------------------------------------------------------
# hdf5.*
# ---------------------------------------------------------------------------

def _op_file_write_text(on, args):
    with open(str(args[0]), "w", encoding="utf-8") as fh:
        fh.write(str(args[1]))


def _op_hdf5_write(on, args):
    return bool(tttrlib.write_hdf5(str(args[0]), args[1], str(args[2])))


def _op_hdf5_read(on, args):
    return tttrlib.read_hdf5(str(args[0]), str(args[1]))


def _op_hdf5_groups(on, args):
    return [str(s) for s in tttrlib.hdf5_table_groups(str(args[0]))]


def _op_hdf5_has(on, args):
    return bool(tttrlib.hdf5_table_has(str(args[0]), str(args[1])))


# ---------------------------------------------------------------------------
# table.* -- one vocabulary, whichever format the spec names
# ---------------------------------------------------------------------------
#
# The cases here run the SAME steps against a .dstore, an HDF5 file and a store
# inside a PTO, which is the only way to assert that the formats are
# interchangeable rather than merely similar. `$spec` is substituted by the
# runner from the case's `formats` list.


def _op_table_read(on, args):
    columns = [str(c) for c in (args[2] if len(args) > 2 and args[2] else [])]
    first = int(args[3]) if len(args) > 3 else 0
    n = int(args[4]) if len(args) > 4 else 0
    return tttrlib.read_table(str(args[0]), str(args[1]) if len(args) > 1 else "",
                              columns or None, first, n)


def _op_table_write(on, args):
    group = str(args[2]) if len(args) > 2 else ""
    return bool(tttrlib.write_table(str(args[0]), args[1], group))


def _op_table_groups(on, args):
    return [str(s) for s in tttrlib.table_groups(str(args[0]))]


def _op_table_columns(on, args):
    group = str(args[1]) if len(args) > 1 else ""
    return [str(s) for s in tttrlib.table_columns(str(args[0]), group)]


def _op_table_has(on, args):
    group = str(args[1]) if len(args) > 1 else ""
    return bool(tttrlib.table_has(str(args[0]), group))


# ---------------------------------------------------------------------------
# pto.*
# ---------------------------------------------------------------------------
#
# A uid is 53 random bits -- narrow enough to survive a double, which is what
# two of the four bindings represent every integer as, and still random -- so it
# is raw material in every case it appears in: bound, passed on, never expected.
# Everything comparable here is a name, a count, a column, or bytes decoded as
# latin-1, the one text encoding that round-trips an arbitrary octet in all four
# languages.

def _op_pto_create(on, args):
    f = tttrlib.PtoFile()
    if not f.create(str(args[0]), str(args[1])):
        raise ConformanceError(f.error())
    return f


def _op_pto_open(on, args):
    f = tttrlib.PtoFile()
    if not f.open(str(args[0])):
        raise ConformanceError(f.error())
    return f


def _op_pto_add_file(on, args):
    uid = on.add_file(str(args[0]), str(args[1]), str(args[2]), str(args[3]))
    if uid == 0:
        raise ConformanceError(on.error())
    return int(uid)


def _op_pto_add_store(on, args):
    uid = tttrlib.pto_add_store(on, str(args[0]), str(args[1]), args[2])
    if uid == 0:
        raise ConformanceError(on.error())
    return int(uid)


def _op_pto_read_store(on, args):
    return tttrlib.pto_store(on, int(args[0]), columns=list(args[1]) or None,
                             first_row=int(args[2]), n_rows=int(args[3]))


def _op_pto_read_text(on, args):
    return on.read(int(args[0]), int(args[1]), int(args[2])).decode("latin-1")


def _op_pto_build_cues(on, args):
    n = on.build_cues(int(args[0]), int(args[1]))
    if n == 0:
        raise ConformanceError(on.error())
    return int(n)


def _op_pto_events(on, args):
    return tttrlib.pto_events("%s|%s" % (args[0], args[1]),
                              int(args[2]), int(args[3]))


# ---------------------------------------------------------------------------
# stream.* -- decoding a buffer, and reading a container in pieces
# ---------------------------------------------------------------------------
#
# The ops are deliberately primitive enough that a chunked decode is written out
# as steps rather than hidden inside a runner-side loop: two `stream.read_records`
# and two `stream.decode` sharing one `stream.state`. That is the whole point of
# the feature -- the library does not own the loop -- so a case that hid the loop
# would be testing the runner.

def _op_stream_read_records(on, args):
    # A uint8 array rather than the bytes the binding hands back, so `len` and
    # the other element ops mean the same thing here as in the three runners
    # whose natural shape for this is a typed array.
    raw = tttrlib.container_read_records(str(args[0]), int(args[2]), int(args[3]),
                                         int(args[1]))
    return np.frombuffer(raw, dtype=np.uint8)


def _op_stream_decode(on, args):
    return int(on.decode_records(_as_array(args[0]), int(args[1]), args[2]))


def _op_stream_events(on, args):
    return tttrlib.container_events(str(args[0]), int(args[2]), int(args[3]),
                                    int(args[1]))


def _op_bhset_value(on, args):
    parsed = tttrlib.bh_set(str(args[0]))
    return str(parsed[str(args[1])][str(args[2])])


_OPS = {
    # generic
    "len": _op_len, "sum": _op_sum, "mean": _op_mean,
    "min": _op_min, "max": _op_max, "argmax": _op_argmax, "argmin": _op_argmin,
    "first": _op_first, "last": _op_last, "nth": _op_nth, "slice": _op_slice,
    "to_list": _op_to_list, "unique_sorted": _op_unique_sorted,
    "shape": _op_shape, "contains": _op_contains,
    "round": _op_round, "identity": _op_identity, "count_gt": _op_count_gt,

    # tttr
    "tttr.open": _op_tttr_open,
    "tttr.size": lambda on, a: int(on.size()),
    "tttr.n_valid_events": lambda on, a: int(on.get_n_valid_events()),
    "tttr.n_micro_channels": lambda on, a: int(on.get_number_of_micro_time_channels()),
    "tttr.macro_times": lambda on, a: np.asarray(on.macro_times),
    "tttr.micro_times": lambda on, a: np.asarray(on.micro_times),
    "tttr.routing_channels": lambda on, a: np.asarray(on.routing_channels),
    "tttr.macro_time_at": lambda on, a: int(on.get_macro_time_at(int(a[0]))),
    "tttr.micro_time_at": lambda on, a: int(on.get_micro_time_at(int(a[0]))),
    "tttr.routing_channel_at": lambda on, a: int(on.get_routing_channel_at(int(a[0]))),
    "tttr.used_routing_channels": lambda on, a: np.asarray(on.get_used_routing_channels()),
    "tttr.by_channel": _op_tttr_by_channel,
    "tttr.burst_search": _op_tttr_burst_search,
    "tttr.microtime_histogram": _op_tttr_microtime_histogram,
    "tttr.header_json": lambda on, a: str(on.header.get_json()),
    "tttr.micro_time_resolution": lambda on, a: float(on.header.micro_time_resolution),
    "tttr.macro_time_resolution": lambda on, a: float(on.header.macro_time_resolution),

    # correlator
    "correlator.curve_size": _op_correlator_curve_size,
    "correlator.new": _op_correlator_new,
    "correlator.set_tttr": _op_correlator_set_tttr,
    "correlator.x_axis": lambda on, a: np.asarray(on.get_x_axis(), dtype=np.float64),
    "correlator.correlation": lambda on, a: np.asarray(on.get_corr_normalized(),
                                                       dtype=np.float64),

    # datastore
    "ds.new": lambda on, a: tttrlib.DataStore(),
    "ds.n_rows": lambda on, a: int(on.n_rows()),
    "ds.n_columns": lambda on, a: int(on.n_columns()),
    "ds.n_groups": lambda on, a: int(on.n_groups()),
    "ds.column_names": _op_ds_column_names,
    "ds.add_group": lambda on, a: on.add_group(str(a[0])),
    "ds.ensure_group": lambda on, a: on.ensure_group(str(a[0])),
    "ds.group": lambda on, a: on.group(str(a[0])),
    "ds.has_group": lambda on, a: bool(on.has_group(str(a[0]))),
    "ds.remove_group": lambda on, a: bool(on.remove_group(str(a[0]))),
    "ds.group_names": lambda on, a: [str(s) for s in on.group_names()],
    "ds.group_paths": lambda on, a: [str(s) for s in on.group_paths()],
    "ds.set_label": lambda on, a: on.set_label(str(a[0])),
    "ds.label": lambda on, a: str(on.label()),
    "ds.nbytes": lambda on, a: int(on.nbytes()),
    "ds.add_string": _op_ds_add_string,
    "ds.column_strings": _op_ds_column_strings,
    "ds.column_dtype": _op_ds_column_dtype,
    "ds.select_range": _op_ds_select_range,
    "ds.n_selected": lambda on, a: int(on.n_selected()),

    # tiff
    "tiff.write_f64": _op_tiff_write_f64,
    "tiff.read_f64": _op_tiff_read_f64,

    # bursts
    "burst.new": _op_burst_new,
    "burst.find": _op_burst_find,
    "burst.properties": _op_burst_properties,

    # photon selection
    "mask.new": _op_mask_new,
    "mask.select_channels": _op_mask_select_channels,
    "mask.select_count_rate": lambda on, a: on.select_count_rate(
        a[0], float(a[1]), int(a[2]), bool(a[3])),
    "mask.size": lambda on, a: int(on.size()),
    "mask.mask_array": _op_mask_array,

    # neural net
    "nn.from_json": _op_nn_from_json,
    "nn.predict": _op_nn_predict,
    "nn.n_layers": lambda on, a: int(on.n_layers()),
    "nn.n_inputs": lambda on, a: int(on.n_inputs()),
    "nn.n_outputs": lambda on, a: int(on.n_outputs()),

    # csv files
    "csvfile.write": _op_csv_write,
    "csvfile.read": _op_csv_read,

    # burst features
    "feature.new": _op_feature_new,
    "feature.compute": _op_feature_compute,
    "feature.values": _op_feature_values,

    # phasor
    "phasor.g": lambda on, a: float(tttrlib.DecayPhasor.g(float(a[0]), float(a[1]),
                                                          float(a[2]), float(a[3]))),
    "phasor.s": lambda on, a: float(tttrlib.DecayPhasor.s(float(a[0]), float(a[1]),
                                                          float(a[2]), float(a[3]))),
    "phasor.from_bincounts": _op_phasor_from_bincounts,

    # pda
    "pda.new": _op_pda_new,
    "pda.append": lambda on, a: on.append(float(a[0]), float(a[1])),
    "pda.evaluate": lambda on, a: on.evaluate(),
    "pda.s1s2": _op_pda_s1s2,
    "pda.histogram_y": _op_pda_histogram_y,

    # decay fitting
    "fit.names": lambda on, a: [str(x) for x in tttrlib.decay_fit_names()],
    "fit.setup_names": lambda on, a: [str(x) for x in tttrlib.decay_fit_setup_names(str(a[0]))],
    "fit.result_names": lambda on, a: [str(x) for x in tttrlib.decay_fit_result_names(str(a[0]), 0)],
    "fit.setup_vector": _op_fit_setup_vector,
    "fit.problem": _op_fit_problem,
    "fit.new": _op_fit_new,
    "fit.run": _op_fit_run,
    "fit.objective": lambda on, a: float(on.objective),
    "fit.parameters": lambda on, a: np.asarray(on.parameters, dtype=np.float64),
    "fit.results": lambda on, a: np.asarray(on.results, dtype=np.float64),

    # clsm
    "clsm.open": _op_clsm_open,
    "clsm.n_frames": lambda on, a: int(on.n_frames),
    "clsm.n_lines": lambda on, a: int(on.n_lines),
    "clsm.n_pixel": lambda on, a: int(on.n_pixel),
    "clsm.intensity": _op_clsm_intensity,
    "clsm.mean_micro_time": _op_clsm_mean_micro_time,
    "clsm.fluorescence_decay": _op_clsm_fluorescence_decay,
    "clsm.decay_of_pixels": _op_clsm_decay_of_pixels,

    # histogram
    "hist.new": _op_hist_new,
    "hist.set_axis": lambda on, a: on.set_axis(int(a[0]), str(a[1]), float(a[2]),
                                               float(a[3]), int(a[4]), str(a[5])),
    "hist.update": _op_hist_update,
    "hist.counts": _op_hist_counts,

    # bitmask
    "bitmask.new": _op_bitmask_new,
    "bitmask.set": lambda on, a: on.set(int(a[0]), bool(a[1])),
    "bitmask.size": lambda on, a: int(on.size()),
    "bitmask.count": lambda on, a: int(on.count()),
    "bitmask.to_bytes": _op_bitmask_to_bytes,

    # registry
    "registry.json": _op_registry_json,
    "registry.category_json": _op_registry_category_json,
    "registry.categories": _op_registry_categories,

    # files -- only enough to put a non-HDF5 file in front of the probes
    "file.write_text": _op_file_write_text,

    # hdf5
    "hdf5.write": _op_hdf5_write,
    "hdf5.read": _op_hdf5_read,
    "hdf5.groups": _op_hdf5_groups,
    "hdf5.has": _op_hdf5_has,

    # table -- the format-agnostic vocabulary
    "table.read": _op_table_read,
    "table.write": _op_table_write,
    "table.groups": _op_table_groups,
    "table.columns": _op_table_columns,
    "table.has": _op_table_has,

    # pto
    "pto.create": _op_pto_create,
    "pto.open": _op_pto_open,
    "pto.close": lambda on, a: on.close(),
    "pto.commit": lambda on, a: bool(on.commit()),
    "pto.add_file": _op_pto_add_file,
    "pto.add_store": _op_pto_add_store,
    "pto.n_objects": lambda on, a: int(on.n_objects()),
    "pto.names": lambda on, a: [str(o.name) for o in on.objects()],
    "pto.kinds": lambda on, a: [str(o.kind) for o in on.objects()],
    "pto.size_of": lambda on, a: int(on.object(int(a[0])).size),
    "pto.read_text": _op_pto_read_text,
    "pto.store_columns": lambda on, a: [str(s) for s in
                                        tttrlib.pto_store_columns(on, int(a[0]))],
    "pto.store_groups": lambda on, a: [str(s) for s in
                                       tttrlib.pto_store_groups(on, int(a[0]))],
    "pto.read_store": _op_pto_read_store,
    "pto.build_cues": _op_pto_build_cues,
    "pto.cue_events": lambda on, a: [int(c.event) for c in on.cues(int(a[0]))],
    "pto.events": _op_pto_events,

    # record streams
    "tttr.new": lambda on, a: tttrlib.TTTR(),
    "stream.n_records": lambda on, a: int(
        tttrlib.container_n_records(str(a[0]), int(a[1]))),
    "stream.record_type": lambda on, a: int(
        tttrlib.container_records(str(a[0]), int(a[1])).record_type),
    "stream.ranged": lambda on, a: bool(
        tttrlib.container_records(str(a[0]), int(a[1])).ranged),
    "stream.record_name": lambda on, a: str(tttrlib.record_type_name(int(a[0]))),
    "stream.record_bytes": lambda on, a: int(tttrlib.record_bytes(int(a[0]))),
    "stream.can_decode": lambda on, a: bool(
        tttrlib.record_type_is_decodable(int(a[0]))),
    "stream.read_records": _op_stream_read_records,
    "stream.state": lambda on, a: tttrlib.TTTRDecodeState(),
    "stream.overflows": lambda on, a: int(on.overflow_counter),
    "stream.decode": _op_stream_decode,
    "stream.events": _op_stream_events,
    "stream.apply_channels": lambda on, a: on.apply_container_channels(int(a[0])),

    # the Becker & Hickl ".set" sidecar
    "bhset.n": lambda on, a: len(tttrlib.read_set_file(str(a[0]))),
    "bhset.sections": lambda on, a: sorted(tttrlib.bh_set(str(a[0])).keys()),
    "bhset.value": _op_bhset_value,
}

for _s in _ADD_DTYPE:
    _OPS[f"ds.add_{_s}"] = _add_numeric(_s)
    _OPS[f"ds.column_{_s}"] = _column_typed(_s)


# ---------------------------------------------------------------------------
# Which ops produce an expectation
# ---------------------------------------------------------------------------
#
# `tools/conformance_update.py` writes the `expect` block from the bindings a
# case produced, and it has to know which of them are answers and which are raw
# material. The distinction is a property of the op, not of the value: a whole
# macro-time array and the whole header JSON are both perfectly comparable and
# both belong nowhere near a reviewed diff -- 183657 numbers, or a serialiser's
# exact whitespace, pin the wrong thing and hide the one number that changed.
#
# So an op is classified once, here, and a case reduces its raw material with
# `sum`, `contains`, `to_list` and the rest. A new op must be added to one of
# the two sets or the check below fails; silently dropping a binding would be
# the worst outcome, because the case would still pass while testing less.

YIELDS_COMPARABLE = {
    # generic reducers -- the whole point of which is to be comparable
    "len", "sum", "mean", "min", "max", "argmax", "argmin", "first", "last",
    "nth", "to_list", "unique_sorted", "shape", "contains", "round",
    "identity", "count_gt",
    # scalar-valued library getters
    "tttr.size", "tttr.n_valid_events", "tttr.n_micro_channels",
    "tttr.macro_time_at", "tttr.micro_time_at", "tttr.routing_channel_at",
    "tttr.micro_time_resolution", "tttr.macro_time_resolution",
    "correlator.curve_size", "phasor.g", "phasor.s", "mask.size",
    "nn.n_layers", "nn.n_inputs", "nn.n_outputs",
    "clsm.n_frames", "clsm.n_lines", "clsm.n_pixel",
    "fit.names", "fit.setup_names", "fit.result_names", "fit.objective",
    "ds.n_rows", "ds.n_columns", "ds.n_groups", "ds.column_names",
    "ds.has_group", "ds.remove_group", "ds.group_names", "ds.group_paths",
    "ds.label", "ds.nbytes", "ds.column_strings", "ds.column_dtype",
    "ds.n_selected",
    "hdf5.write", "hdf5.groups", "hdf5.has",
    "registry.categories",
    "bitmask.size", "bitmask.count",
    "pto.commit", "pto.n_objects", "pto.names", "pto.kinds", "pto.size_of",
    "pto.read_text", "pto.store_columns", "pto.store_groups",
    "pto.build_cues", "pto.cue_events",
    "stream.n_records", "stream.record_type", "stream.ranged",
    "stream.record_name", "stream.record_bytes", "stream.can_decode",
    "stream.decode", "stream.overflows",
    "bhset.n", "bhset.sections", "bhset.value",
    # the format-agnostic vocabulary: three queries and one predicate, all of
    # which answer with a list or a bool and are the same in every format --
    # which is the whole claim being made.
    "table.groups", "table.columns", "table.has", "table.write",
}

RAW_MATERIAL = {
    # object handles -- not comparable across languages at all
    "tttr.open", "tttr.by_channel", "ds.new", "ds.add_group",
    "ds.ensure_group", "ds.group", "hdf5.read",
    # arrays and long strings: reduce them, do not pin them
    "tttr.macro_times", "tttr.micro_times", "tttr.routing_channels",
    "tttr.used_routing_channels", "tttr.burst_search",
    "tttr.microtime_histogram", "tttr.header_json", "slice",
    "registry.json", "registry.category_json",
    "bitmask.new", "bitmask.set", "bitmask.to_bytes",
    "hist.new", "hist.set_axis", "hist.update", "hist.counts",
    "clsm.open", "clsm.intensity", "clsm.mean_micro_time",
    "clsm.fluorescence_decay", "clsm.decay_of_pixels",
    "fit.setup_vector", "fit.problem", "fit.new", "fit.run",
    "fit.parameters", "fit.results",
    "pda.new", "pda.append", "pda.evaluate", "pda.s1s2", "pda.histogram_y",
    "burst.new", "burst.find", "burst.properties",
    "correlator.new", "correlator.set_tttr", "correlator.x_axis",
    "correlator.correlation", "phasor.from_bincounts",
    "nn.from_json", "nn.predict", "csvfile.write", "csvfile.read",
    "feature.new", "feature.compute", "feature.values",
    "mask.new", "mask.select_channels", "mask.select_count_rate", "mask.mask_array",
    "tiff.write_f64", "tiff.read_f64",
    # pto -- a uid is random, and a file or store handle is a handle
    "pto.create", "pto.open", "pto.add_file", "pto.add_store",
    "pto.read_store", "pto.events",
    # a table read gives back a store, which is a handle like any other
    "table.read",
    # record streams -- a TTTR, a decode state and a record buffer are handles
    "tttr.new", "stream.state", "stream.read_records", "stream.events",
    # side effects that bind nothing worth expecting
    "ds.add_string", "ds.set_label", "ds.select_range", "file.write_text",
    "pto.close", "stream.apply_channels",
}
RAW_MATERIAL |= {f"ds.add_{s}" for s in _ADD_DTYPE}
RAW_MATERIAL |= {f"ds.column_{s}" for s in _ADD_DTYPE}

_unclassified = set(_OPS) - YIELDS_COMPARABLE - RAW_MATERIAL
if _unclassified:
    raise AssertionError(
        "these ops are in neither YIELDS_COMPARABLE nor RAW_MATERIAL, so the "
        f"generator would not know what to do with them: {sorted(_unclassified)}")

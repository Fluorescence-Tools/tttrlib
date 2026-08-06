"""More than one table in one HDF5 file.

The writer used to call H5Fcreate with H5F_ACC_TRUNC on every call, so writing
``/results`` and then ``/meta`` left only ``/meta`` -- and both calls returned
true. A file could not be built a group at a time, which is the shape imaging
data actually has: one row per pixel in ``results``, one row of provenance in
``meta``.
"""
import os
import stat

import numpy as np
import pytest
import tttrlib

pytestmark = pytest.mark.skipif(not tttrlib.hdf5_table_available(),
                                reason="built without HDF5")


def table(n, first=0.0, name="x"):
    s = tttrlib.DataStore()
    s.set_n_rows(n)
    s.add(name, np.arange(n, dtype=np.float64) + first)
    return s


def test_writing_a_second_group_does_not_destroy_the_first(tmp_path):
    """The bug, stated as plainly as it can be."""
    path = str(tmp_path / "two.h5")
    assert tttrlib.write_hdf5(path, table(10), group="/results")
    assert tttrlib.write_hdf5(path, table(1, 99.0, "source"), group="/meta")

    assert tttrlib.read_hdf5(path, "/results").n_rows() == 10
    assert tttrlib.read_hdf5(path, "/meta").n_rows() == 1
    assert tttrlib.read_hdf5(path, "/meta")["source"].numpy()[0] == 99.0


def test_the_groups_may_hold_different_numbers_of_rows(tmp_path):
    """Which is the whole reason for wanting two of them."""
    path = str(tmp_path / "shapes.h5")
    tttrlib.write_hdf5(path, table(4096), group="/results")
    tttrlib.write_hdf5(path, table(1, name="frames"), group="/meta")
    assert tttrlib.read_hdf5(path, "/results").n_rows() == 4096
    assert tttrlib.read_hdf5(path, "/meta").n_rows() == 1


def test_rewriting_a_group_replaces_it_and_leaves_its_neighbour(tmp_path):
    """Whole-group replacement, not a column-wise merge.

    A table that silently keeps a stale column from a previous run is worse
    than one that lost it, because it looks current.
    """
    path = str(tmp_path / "replace.h5")
    first = table(5)
    first.add("dropped", np.ones(5))
    tttrlib.write_hdf5(path, first, group="/results")
    tttrlib.write_hdf5(path, table(1, 7.0, "source"), group="/meta")

    tttrlib.write_hdf5(path, table(3), group="/results")

    back = tttrlib.read_hdf5(path, "/results")
    assert back.n_rows() == 3
    assert back.names == ["x"], "a column only the previous write had survived"
    assert tttrlib.read_hdf5(path, "/meta")["source"].numpy()[0] == 7.0


def test_the_column_order_survives_a_group_being_replaced(tmp_path):
    """The order lives in the `columns` attribute, and an attribute travels with
    the object -- so moving the new group into place cannot reorder it."""
    path = str(tmp_path / "order.h5")
    s = tttrlib.DataStore()
    s.set_n_rows(3)
    for name in ["zulu", "alpha", "mike"]:
        s.add(name, np.arange(3, dtype=np.float64))
    tttrlib.write_hdf5(path, s, group="/results")
    tttrlib.write_hdf5(path, s, group="/results")
    assert list(tttrlib.read_hdf5_table_columns(path, "/results")) == \
        ["zulu", "alpha", "mike"]


def test_a_nested_path_creates_what_it_needs(tmp_path):
    """"/a/b" used to fail unless "/a" already existed, and nothing ever made it."""
    path = str(tmp_path / "nested.h5")
    assert tttrlib.write_hdf5(path, table(6), group="/a/b")
    assert tttrlib.read_hdf5(path, "/a/b").n_rows() == 6

    assert tttrlib.write_hdf5(path, table(2), group="/a/c")
    assert tttrlib.read_hdf5(path, "/a/b").n_rows() == 6
    assert tttrlib.read_hdf5(path, "/a/c").n_rows() == 2


def test_a_leading_or_trailing_separator_is_optional(tmp_path):
    path = str(tmp_path / "sep.h5")
    tttrlib.write_hdf5(path, table(4), group="results")
    assert tttrlib.read_hdf5(path, "/results").n_rows() == 4
    tttrlib.write_hdf5(path, table(5), group="/results/")
    assert tttrlib.read_hdf5(path, "results").n_rows() == 5


def test_truncate_leaves_only_what_was_just_written(tmp_path):
    path = str(tmp_path / "trunc.h5")
    tttrlib.write_hdf5(path, table(10), group="/results")
    tttrlib.write_hdf5(path, table(1, name="source"), group="/meta")

    assert tttrlib.write_hdf5(path, table(3), group="/results",
                              mode=tttrlib.Hdf5WriteMode_Truncate)
    assert tttrlib.read_hdf5(path, "/results").n_rows() == 3
    with pytest.raises(Exception):
        tttrlib.read_hdf5(path, "/meta")


def test_writing_the_root_replaces_the_whole_file(tmp_path):
    """The root is a group like any other, and writing a group replaces
    everything under it."""
    path = str(tmp_path / "root.h5")
    tttrlib.write_hdf5(path, table(4), group="/meta")
    assert tttrlib.write_hdf5(path, table(8))
    assert tttrlib.read_hdf5(path).n_rows() == 8
    with pytest.raises(Exception):
        tttrlib.read_hdf5(path, "/meta")


def test_update_refuses_a_file_that_is_not_hdf5_and_leaves_it_alone(tmp_path):
    """Refused rather than replaced by inference.

    The check asks HDF5 whether the file is one, rather than inferring it from
    a failed open -- an open also fails on a permission error, and truncating
    then would destroy a file the caller cannot even read.
    """
    path = tmp_path / "notes.txt"
    path.write_bytes(b"this is not an HDF5 file\n")
    before = path.read_bytes()

    assert tttrlib.write_hdf5(str(path), table(3)) is False
    assert path.read_bytes() == before, "a foreign file was modified"


def test_truncate_is_how_you_replace_a_foreign_file(tmp_path):
    path = tmp_path / "notes.txt"
    path.write_bytes(b"this is not an HDF5 file\n")
    assert tttrlib.write_hdf5(str(path), table(3),
                              mode=tttrlib.Hdf5WriteMode_Truncate)
    assert tttrlib.read_hdf5(str(path)).n_rows() == 3


def test_a_failed_write_leaves_the_previous_group_readable(tmp_path):
    """No half-written group. The new contents go to a temporary and are moved
    into place only once every column is safely written."""
    path = tmp_path / "readonly.h5"
    tttrlib.write_hdf5(str(path), table(10), group="/results")
    tttrlib.write_hdf5(str(path), table(1, name="source"), group="/meta")
    before = path.read_bytes()
    os.chmod(str(path), stat.S_IRUSR)
    try:
        assert tttrlib.write_hdf5(str(path), table(3), group="/results") is False
    finally:
        os.chmod(str(path), stat.S_IRUSR | stat.S_IWUSR)

    assert path.read_bytes() == before
    assert tttrlib.read_hdf5(str(path), "/results").n_rows() == 10
    assert tttrlib.read_hdf5(str(path), "/meta").n_rows() == 1


def test_no_temporary_group_is_left_behind(tmp_path):
    """The scratch group a replacement writes into must not survive it, in
    either direction."""
    h5py = pytest.importorskip("h5py")
    path = str(tmp_path / "clean.h5")
    tttrlib.write_hdf5(path, table(4), group="/results")
    tttrlib.write_hdf5(path, table(4), group="/results")
    with h5py.File(path, "r") as f:
        assert sorted(f.keys()) == ["results"]


def test_the_selection_still_applies_per_group(tmp_path):
    """Gating is per store, so two groups written from two gated stores each
    carry their own subset."""
    path = str(tmp_path / "gated.h5")
    a = table(10)
    a.where("x", 0.0, 4.5)
    b = table(10, 100.0)
    b.where("x", 106.0, 200.0)
    tttrlib.write_hdf5(path, a, group="/a")
    tttrlib.write_hdf5(path, b, group="/b")

    np.testing.assert_array_equal(tttrlib.read_hdf5(path, "/a")["x"].numpy(),
                                  np.arange(5, dtype=np.float64))
    np.testing.assert_array_equal(tttrlib.read_hdf5(path, "/b")["x"].numpy(),
                                  np.arange(106.0, 110.0))

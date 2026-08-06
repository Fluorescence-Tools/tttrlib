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


def test_hdf5_table_groups_lists_them_in_file_order(tmp_path):
    """Not alphabetical: a listing that reorders is a listing that cannot be
    used to rebuild what the file held."""
    path = str(tmp_path / "listing.h5")
    tttrlib.write_hdf5(path, table(3), group="/zulu")
    tttrlib.write_hdf5(path, table(3), group="/alpha")
    tttrlib.write_hdf5(path, table(3), group="/mike")
    assert list(tttrlib.hdf5_table_groups(path)) == ["/alpha", "/mike", "/zulu"]


def test_a_root_table_with_a_group_beside_it_lists_as_both(tmp_path):
    """The imaging layout. Sub-groups are not datasets, so a group holding a
    table AND a child is still a table."""
    path = str(tmp_path / "both.h5")
    tttrlib.write_hdf5(path, table(64))
    tttrlib.write_hdf5(path, table(1, name="source"), group="/meta")
    assert list(tttrlib.hdf5_table_groups(path)) == ["/", "/meta"]


def test_nested_groups_are_reached(tmp_path):
    path = str(tmp_path / "deep.h5")
    tttrlib.write_hdf5(path, table(3), group="/a/b")
    tttrlib.write_hdf5(path, table(3), group="/a/c/d")
    assert list(tttrlib.hdf5_table_groups(path)) == ["/a/b", "/a/c/d"]


def test_a_group_that_only_holds_other_groups_is_not_a_table(tmp_path):
    path = str(tmp_path / "container.h5")
    tttrlib.write_hdf5(path, table(3), group="/a/b")
    assert "/a" not in list(tttrlib.hdf5_table_groups(path))
    assert tttrlib.hdf5_table_has(path, "/a") is False
    assert tttrlib.hdf5_table_has(path, "/a/b") is True


def test_a_foreign_file_is_empty_and_silent(tmp_path, capfd):
    """Probing is a normal thing for a caller to do, so it must not narrate."""
    text = tmp_path / "notes.txt"
    text.write_bytes(b"not hdf5 at all\n")
    missing = tmp_path / "nope.h5"

    for p in (text, missing):
        assert list(tttrlib.hdf5_table_groups(str(p))) == []
        assert tttrlib.hdf5_table_has(str(p)) is False
        assert tttrlib.hdf5_table_remove(str(p), "/x") is False

    out, err = capfd.readouterr()
    assert out == "" and err == "", "a query printed something"


def test_an_hdf5_file_with_no_table_in_it(tmp_path, capfd):
    """A valid HDF5 file that is not ours -- a 2-D dataset, which is not a
    column -- answers no rather than failing."""
    h5py = pytest.importorskip("h5py")
    path = str(tmp_path / "foreign.h5")
    with h5py.File(path, "w") as f:
        f.create_dataset("image", data=np.zeros((4, 4)))
        f.create_group("empty")

    assert list(tttrlib.hdf5_table_groups(path)) == []
    assert tttrlib.hdf5_table_has(path) is False
    capfd.readouterr()


def test_a_ragged_group_is_not_a_table(tmp_path):
    """1-D datasets of different lengths are not columns of one table. The
    reader still salvages what it can; the predicate says no."""
    h5py = pytest.importorskip("h5py")
    path = str(tmp_path / "ragged.h5")
    with h5py.File(path, "w") as f:
        f.create_dataset("a", data=np.arange(5.0))
        f.create_dataset("b", data=np.arange(3.0))
    assert tttrlib.hdf5_table_has(path) is False
    assert list(tttrlib.hdf5_table_groups(path)) == []


def test_hdf5_table_has_agrees_with_hdf5_table_groups(tmp_path):
    path = str(tmp_path / "agree.h5")
    tttrlib.write_hdf5(path, table(8))
    tttrlib.write_hdf5(path, table(2), group="/meta")
    tttrlib.write_hdf5(path, table(3), group="/a/b")

    listed = list(tttrlib.hdf5_table_groups(path))
    assert sorted(listed) == ["/", "/a/b", "/meta"]
    for group in listed:
        assert tttrlib.hdf5_table_has(path, group) is True
    for absent in ("/a", "/nope", "/meta/deeper"):
        assert tttrlib.hdf5_table_has(path, absent) is False


def test_remove_drops_one_group_and_keeps_the_others(tmp_path):
    path = str(tmp_path / "remove.h5")
    tttrlib.write_hdf5(path, table(8), group="/results")
    tttrlib.write_hdf5(path, table(2), group="/meta")

    assert tttrlib.hdf5_table_remove(path, "/results") is True
    assert list(tttrlib.hdf5_table_groups(path)) == ["/meta"]
    assert tttrlib.hdf5_table_remove(path, "/results") is False, "already gone"


def test_remove_at_the_root_drops_the_table_and_keeps_the_children(tmp_path):
    """The root cannot be unlinked, so removing it means removing what it
    holds -- and its sub-groups are their own tables."""
    path = str(tmp_path / "rootremove.h5")
    tttrlib.write_hdf5(path, table(8))
    tttrlib.write_hdf5(path, table(2, name="source"), group="/meta")

    assert tttrlib.hdf5_table_remove(path, "/") is True
    assert list(tttrlib.hdf5_table_groups(path)) == ["/meta"]
    assert tttrlib.read_hdf5(path, "/meta").n_rows() == 2


# -- a whole tree, in one call ------------------------------------------------

def imaging_tree():
    """A results table of one row per pixel, a one-row meta beside it, and a
    nested pair -- the shape this whole feature exists for."""
    s = tttrlib.DataStore("acquisition")
    results = s.add_group("results")
    results.set_n_rows(64)
    results.add("Tau", np.linspace(1.0, 4.0, 64))
    results.add("Number of Photons", np.arange(64, dtype=np.int64))
    mask = np.ones(64, dtype=np.uint8)
    mask[::9] = 0
    results["Number of Photons"].set_mask(mask)
    meta = s.add_group("meta")
    meta.set_n_rows(1)
    meta.add("source", np.array(["run.ptu"], dtype=object))
    deep = s.ensure_group("a/b")
    deep.set_n_rows(3)
    deep.add("x", np.arange(3.0))
    return s


def test_a_tree_round_trips_equal(tmp_path):
    """The property everything else in this file supports."""
    s = imaging_tree()
    path = str(tmp_path / "tree.h5")
    assert tttrlib.write_hdf5(path, s)
    back = tttrlib.read_hdf5(path)

    assert back.group_paths() == s.group_paths()
    for group in s.group_paths():
        a, b = s.group(group), back.group(group)
        assert b.names == a.names, group
        assert b.n_rows() == a.n_rows(), group
        for name in a.names:
            if a[name].dtype == "str":
                assert b[name].numpy()[0] == a[name].numpy()[0]
                continue
            assert b[name].numpy().dtype == a[name].numpy().dtype, name
            np.testing.assert_array_equal(b[name].numpy(), a[name].numpy())
            if a[name].has_mask():
                np.testing.assert_array_equal(b[name].mask_numpy(), a[name].mask_numpy())


def test_a_container_group_with_no_columns_survives(tmp_path):
    """"a" holds nothing but "a/b". Without the writer recording its children
    there would be no way to tell it from a group something else left behind."""
    path = str(tmp_path / "container.h5")
    tttrlib.write_hdf5(path, imaging_tree())
    back = tttrlib.read_hdf5(path)
    assert "a" in back.group_names()
    assert back.group("a").n_columns() == 0
    assert back.group("a").group_names() == ["b"]


def test_the_group_order_is_insertion_order(tmp_path):
    """HDF5 lists links however it likes, so the order is kept as data. It also
    has to survive a group being replaced, which moves a link to the end."""
    s = tttrlib.DataStore()
    for name in ("zulu", "alpha", "mike"):
        s.add_group(name).set_n_rows(0)
        s.group(name).add("x", np.zeros(1))
        s.group(name).set_n_rows(1)
    path = str(tmp_path / "order.h5")
    tttrlib.write_hdf5(path, s)
    assert tttrlib.read_hdf5(path).group_names() == ["zulu", "alpha", "mike"]

    tttrlib.write_hdf5(path, table(2), group="/alpha")
    assert tttrlib.read_hdf5(path).group_names() == ["zulu", "alpha", "mike"]


def test_with_groups_false_reads_only_the_table(tmp_path):
    path = str(tmp_path / "tree.h5")
    tttrlib.write_hdf5(path, imaging_tree())
    flat = tttrlib.read_hdf5(path, "/", with_groups=False)
    assert flat.n_groups() == 0


def test_a_group_name_needing_encoding_comes_back(tmp_path):
    """The same percent-encoding the columns use -- a group called "50%" must
    not become something else on the way through."""
    s = tttrlib.DataStore()
    g = s.add_group("50% done")
    g.set_n_rows(2)
    g.add("x", np.zeros(2))
    path = str(tmp_path / "enc.h5")
    assert tttrlib.write_hdf5(path, s)
    assert tttrlib.read_hdf5(path).group_names() == ["50% done"]


def test_a_foreign_group_beside_our_tables_is_skipped_not_failed(tmp_path):
    """Reading foreign layouts is not this module's job. Failing on them is not
    either -- the tables that are there should still come back."""
    h5py = pytest.importorskip("h5py")
    path = str(tmp_path / "mixed.h5")
    tttrlib.write_hdf5(path, table(5), group="/results")
    with h5py.File(path, "a") as f:
        g = f.create_group("pictures")
        g.create_dataset("frame", data=np.zeros((4, 4)))

    back = tttrlib.read_hdf5(path)
    assert back.group_names() == ["results"]
    assert back.group("results").n_rows() == 5


def test_a_column_and_a_group_of_the_same_name_is_refused(tmp_path):
    """In memory the two namespaces are separate, deliberately. HDF5 has one
    link namespace per group, so this cannot be written -- and it is refused
    before the file is touched, rather than halfway through."""
    s = tttrlib.DataStore()
    s.set_n_rows(3)
    s.add("meta", np.zeros(3))
    s.add_group("meta").set_n_rows(1)

    path = tmp_path / "clash.h5"
    tttrlib.write_hdf5(str(path), table(9))
    before = path.read_bytes()

    assert tttrlib.write_hdf5(str(path), s) is False
    assert path.read_bytes() == before, "the previous file was damaged"


def test_the_imaging_shape(tmp_path):
    """Why this exists, readable as documentation.

    A results table of one row per pixel and a one-row meta saying where it
    came from: two row counts, one file, one object. Write it in one call,
    reopen it, and both halves are there -- then rewrite results alone and meta
    is still there, which is the case that silently lost data before.
    """
    path = str(tmp_path / "imaging.h5")
    acquisition = imaging_tree()
    assert tttrlib.write_hdf5(path, acquisition)

    reopened = tttrlib.read_hdf5(path)
    assert reopened.group("results").n_rows() == 64
    assert reopened.group("meta")["source"].numpy()[0] == "run.ptu"

    better = tttrlib.DataStore()
    better.set_n_rows(64)
    better.add("Tau", np.linspace(1.1, 4.1, 64))
    assert tttrlib.write_hdf5(path, better, group="/results")

    after = tttrlib.read_hdf5(path)
    assert after.group("results").names == ["Tau"]
    assert after.group("results")["Tau"].numpy()[0] == pytest.approx(1.1)
    assert after.group("meta")["source"].numpy()[0] == "run.ptu"


def test_the_two_formats_agree_on_the_imaging_shape(tmp_path):
    """The native format and HDF5 should give back the same tree. They have
    different jobs, not different answers."""
    s = imaging_tree()
    h5, native = str(tmp_path / "i.h5"), str(tmp_path / "i.dstore")
    assert tttrlib.write_hdf5(h5, s)
    assert tttrlib.save_store(native, s)

    a, b = tttrlib.read_hdf5(h5), tttrlib.load_store(native)
    assert a.group_paths() == b.group_paths() == s.group_paths()
    for group in s.group_paths():
        assert a.group(group).names == b.group(group).names, group
        assert a.group(group).n_rows() == b.group(group).n_rows(), group


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

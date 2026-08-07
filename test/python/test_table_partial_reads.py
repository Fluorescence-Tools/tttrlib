"""Reading part of a table, in whichever format it is in.

The two formats had complementary holes: ``.dstore`` could take a column subset
and a row range but not one group; HDF5 could take one group and neither of the
others. So a caller who swapped an extension to get a subset read would get the
opposite, and the two were not interchangeable however alike they looked.

Emulation was rejected — reading a whole file and slicing gives the right
answer at the wrong cost, which is the one thing a partial read exists to avoid.
Each gap is filled where the format actually is: a shorter loop over the
directory, and a hyperslab.

**These tests count bytes, not seconds.** A wall clock on a warm page cache
measures the cache, so "the columns you did not ask for were never read" is
asserted with the reader's own counter or it is not asserted at all.
"""
import os
import sys

import numpy as np
import pytest

sys.path.insert(0, os.path.dirname(__file__))
import tttrlib

N = 20_000
COLUMNS = ("Tau", "E", "Duration", "n_photons")
WIDTH = 8 * N                       # one float64 column


@pytest.fixture
def tree():
    s = tttrlib.DataStore("run")
    s.set_n_rows(N)
    for name in COLUMNS:
        s.add(name, np.arange(N, dtype=np.float64))
    results = s.add_group("results")
    results.set_n_rows(N)
    results.add("Tau", np.arange(N, dtype=np.float64))
    deep = s.ensure_group("a/b")
    deep.set_n_rows(50)
    deep.add("x", np.arange(50.0))
    return s


@pytest.fixture
def dstore(tmp_path, tree):
    path = str(tmp_path / "r.dstore")
    tttrlib.save_store(path, tree)
    return path


@pytest.fixture
def h5(tmp_path, tree):
    path = str(tmp_path / "r.h5")
    tttrlib.write_hdf5(path, tree, "/")
    return path


def dstore_bytes(fn):
    before = tttrlib.store_bytes_read()
    out = fn()
    return tttrlib.store_bytes_read() - before, out


def hdf5_bytes(fn):
    before = tttrlib.hdf5_bytes_read()
    out = fn()
    return tttrlib.hdf5_bytes_read() - before, out


hdf5_only = pytest.mark.skipif(not tttrlib.hdf5_table_available(),
                               reason="built without HDF5")


# --- one group ---------------------------------------------------------------


def test_the_native_format_reads_one_group(dstore):
    """The gap that was HDF5's alone. Reaching a group costs a scan of the
    directory -- a few kilobytes -- and not one byte of any group stepped
    over."""
    moved, g = dstore_bytes(lambda: tttrlib.load_store(dstore, group="results"))
    assert g.names == ["Tau"]
    assert g.n_rows() == N
    assert moved == WIDTH, "a group read moved more than that group's data"


def test_a_group_read_is_a_store_and_not_a_view(dstore, tmp_path):
    """The tree BELOW comes with it and the tree above does not, which is what
    makes it writable back out as a file in its own right."""
    g = tttrlib.load_store(dstore, group="results")
    assert g.n_groups() == 0

    again = str(tmp_path / "just-results.dstore")
    tttrlib.save_store(again, g)
    back = tttrlib.load_store(again)
    assert back.names == ["Tau"]
    np.testing.assert_array_equal(back["Tau"].numpy(), np.arange(N, dtype=np.float64))


def test_a_nested_group_is_reached_by_path(dstore):
    g = tttrlib.load_store(dstore, group="a/b")
    assert g.names == ["x"] and g.n_rows() == 50


def test_the_separators_are_optional_at_both_ends(dstore):
    """The same rule the tree walker follows, so a path taken out of
    ``store_groups`` or typed by hand both work."""
    for spelling in ("results", "/results", "results/", "/results/"):
        assert tttrlib.load_store(dstore, group=spelling).names == ["Tau"], spelling


def test_a_group_that_is_not_there_raises_and_names_it(dstore):
    with pytest.raises(RuntimeError) as e:
        tttrlib.load_store(dstore, group="nope")
    assert "nope" in str(e.value)


def test_a_sibling_with_a_longer_name_is_not_entered(tmp_path):
    """``results2`` starts with ``results``; looking for ``results/x`` must not
    descend into it."""
    s = tttrlib.DataStore()
    for name in ("results2", "results"):
        g = s.add_group(name)
        g.set_n_rows(1)
        g.add("which", np.array([1.0 if name == "results" else 2.0]))
    s.ensure_group("results/x").set_n_rows(0)

    path = str(tmp_path / "siblings.dstore")
    tttrlib.save_store(path, s)
    assert tttrlib.load_store(path, group="results")["which"].numpy()[0] == 1.0
    assert tttrlib.load_store(path, group="results2")["which"].numpy()[0] == 2.0


def test_store_has_answers_without_reading(dstore):
    moved, _ = dstore_bytes(lambda: tttrlib.store_has(dstore, "results"))
    assert moved == 0, "a question about the directory read payload"
    assert tttrlib.store_has(dstore, "a/b")
    assert not tttrlib.store_has(dstore, "a/c")
    assert tttrlib.store_has(dstore), "the root is always there"


def test_store_has_is_silent_on_a_file_that_is_not_ours(tmp_path, h5):
    assert not tttrlib.store_has(h5, "results")
    assert not tttrlib.store_has(str(tmp_path / "missing.dstore"))


# --- a column subset ---------------------------------------------------------


def test_the_native_format_reads_the_columns_asked_for(dstore):
    moved, s = dstore_bytes(lambda: tttrlib.load_store(dstore, columns=["Tau"]))
    assert s.names == ["Tau"]
    # `Tau` is in the root AND in `results`; the filter is per node.
    assert moved == 2 * WIDTH


@hdf5_only
def test_hdf5_reads_the_columns_asked_for(h5):
    """The gap that was the native format's alone. One dataset per column is
    already the layout, so this is a shorter loop, not a new design."""
    whole, _ = hdf5_bytes(lambda: tttrlib.read_hdf5(h5))
    moved, s = hdf5_bytes(lambda: tttrlib.read_hdf5(h5, columns=["Tau"]))
    assert s.names == ["Tau"]
    assert moved == 2 * WIDTH
    assert whole == 5 * WIDTH + 8 * 50, "four root, one in results, one in a/b"


@hdf5_only
def test_a_column_missing_from_one_group_is_not_an_error(h5):
    """Matched per node. `E` is in the root and not in `results`, and a tree
    read must not turn that into a failure."""
    s = tttrlib.read_hdf5(h5, columns=["Tau", "E"])
    assert s.names == ["Tau", "E"]
    assert s.group("results").names == ["Tau"]


@hdf5_only
def test_the_two_formats_move_the_same_bytes_for_the_same_request(dstore, h5):
    """The invariant that makes them interchangeable, stated as a number rather
    than as a hope."""
    for kwargs in ({}, {"columns": ["Tau"]}, {"first_row": 100, "n_rows": 50}):
        native, _ = dstore_bytes(lambda: tttrlib.load_store(dstore, **kwargs))
        foreign, _ = hdf5_bytes(lambda: tttrlib.read_hdf5(h5, **kwargs))
        assert native == foreign, kwargs


# --- a row range -------------------------------------------------------------


@hdf5_only
def test_hdf5_reads_a_row_range(h5):
    """A hyperslab, which HDF5 resolves to the chunks the range falls in.
    Nothing is read and discarded, which is why this is here and not in a
    slicing layer above."""
    moved, s = hdf5_bytes(
        lambda: tttrlib.read_hdf5(h5, first_row=100, n_rows=50))
    assert s.n_rows() == 50
    np.testing.assert_array_equal(s["Tau"].numpy(), np.arange(100.0, 150.0))
    assert moved == 5 * 8 * 50


@hdf5_only
def test_both_knobs_at_once(h5, dstore):
    for reader, path in ((tttrlib.read_hdf5, h5), (tttrlib.load_store, dstore)):
        s = reader(path, columns=["Tau"], first_row=100, n_rows=5)
        assert s.names == ["Tau"], reader
        np.testing.assert_array_equal(s["Tau"].numpy(), np.arange(100.0, 105.0))


@hdf5_only
def test_a_group_shorter_than_the_window_comes_back_empty(h5):
    """Clamped to its own length rather than throwing -- the range applies to
    every table in the tree and they need not agree on how long they are."""
    s = tttrlib.read_hdf5(h5, first_row=100, n_rows=50)
    assert s.group("a").group("b").n_rows() == 0
    assert s.n_rows() == 50


@hdf5_only
def test_n_rows_of_zero_means_to_the_end(h5):
    s = tttrlib.read_hdf5(h5, first_row=N - 3)
    assert s.n_rows() == 3
    np.testing.assert_array_equal(s["Tau"].numpy(),
                                  np.arange(float(N - 3), float(N)))


@hdf5_only
def test_a_window_keeps_the_dtypes_and_the_text(tmp_path):
    """The codes are sliced and the dictionary is not, which is the case a
    window can get wrong."""
    s = tttrlib.DataStore()
    s.set_n_rows(10)
    s.add("f32", np.arange(10, dtype=np.float32))
    s.add("i16", np.arange(10, dtype=np.int16))
    s.add("flag", np.arange(10) % 2 == 0)
    s.add("label", np.array(["a", "b", "c", "d", "e"] * 2, dtype=object))

    path = str(tmp_path / "typed.h5")
    tttrlib.write_hdf5(path, s, "/t")
    w = tttrlib.read_hdf5(path, "/t", first_row=3, n_rows=4)

    assert w.n_rows() == 4
    assert w["f32"].dtype == "float32" and w["i16"].dtype == "int16"
    np.testing.assert_array_equal(w["f32"].numpy(), np.arange(3, 7, dtype=np.float32))
    np.testing.assert_array_equal(w["flag"].numpy(), [False, True, False, True])
    assert w["label"].numpy().tolist() == ["d", "e", "a", "b"]


@hdf5_only
def test_a_window_keeps_the_validity_mask(tmp_path):
    s = tttrlib.DataStore()
    s.set_n_rows(8)
    s.add("n", np.arange(8, dtype=np.int32))
    s["n"].set_mask(np.array([1, 1, 0, 1, 0, 1, 1, 1], dtype=np.uint8))

    path = str(tmp_path / "masked.h5")
    tttrlib.write_hdf5(path, s, "/t")
    w = tttrlib.read_hdf5(path, "/t", first_row=2, n_rows=3)
    np.testing.assert_array_equal(w["n"].mask_numpy(), [False, True, False])
    np.testing.assert_array_equal(w["n"].numpy(), [2, 3, 4])


# --- nothing regressed -------------------------------------------------------


def test_a_whole_read_is_unchanged(dstore, h5, tree):
    for reader, path in ((tttrlib.load_store, dstore),
                         (tttrlib.read_hdf5, h5)):
        if reader is tttrlib.read_hdf5 and not tttrlib.hdf5_table_available():
            continue
        s = reader(path)
        assert list(s.names) == list(COLUMNS), reader
        assert s.n_rows() == N
        assert s.group("results").names == ["Tau"]
        np.testing.assert_array_equal(s["Tau"].numpy(), tree["Tau"].numpy())


@hdf5_only
def test_the_byte_counter_only_counts_payload(h5):
    """Structure -- the links and attributes HDF5 reads to find a dataset --
    is not counted, so the number is comparable with the native format's."""
    moved, _ = hdf5_bytes(lambda: tttrlib.read_hdf5(h5, columns=["Tau"]))
    assert moved == 2 * WIDTH, "an attribute or a link was counted as payload"

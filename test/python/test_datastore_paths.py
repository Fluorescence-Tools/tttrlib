"""Reaching into a DataStore tree from Python, pathlib-style.

A store is a tree and reaching into it used to be a four-link chain,
``store.group("results")["Tau"].numpy()``. ``/`` composes a path and nothing is
looked up until the path is used, exactly as ``pathlib.Path("a") / "b"`` does
not touch the filesystem.

Two rules the tests here exist to hold down:

* **A slash-free key is unchanged.** ``store["meta"]`` is the column it always
  was; only a key carrying a separator walks the tree. That is the ambiguity
  the tree refuses to have -- a str key must not switch between a column and
  a group depending on what happens to exist -- and ``test_datastore_groups.py::
  test_find_never_sees_a_group`` still passes untouched as the guard.
* **A path is a name, not a pointer.** It resolves on every use and caches
  nothing, so it cannot outlive a ``remove_group`` into a dangling proxy, and
  it cannot build the reference cycle that would stop a dropped store from
  freeing its tree.
"""
import gc
import os
import sys
import weakref

import numpy as np
import pytest

sys.path.insert(0, os.path.dirname(__file__))
import tttrlib


@pytest.fixture
def tree():
    """A root that only associates tables, the shape groups exist for."""
    s = tttrlib.DataStore("acquisition")
    s.add_group("results", {"Tau": np.linspace(0.5, 5.0, 64),
                            "E": np.linspace(0.0, 1.0, 64),
                            "w": np.ones(64)})
    s.add_group("meta", {"source": np.array(["run.ptu"], dtype=object)})
    s.ensure_group("a/b", {"x": np.arange(10.0)})
    return s


# --- the operator -----------------------------------------------------------


def test_a_path_composed_with_slash_reaches_the_same_column(tree):
    a = (tree / "results" / "Tau").numpy()
    b = tree["results/Tau"].numpy()
    c = tree.group("results")["Tau"].numpy()
    np.testing.assert_array_equal(a, b)
    np.testing.assert_array_equal(b, c)


def test_a_slash_composes_without_resolving(tree):
    """The whole point of the operator: it builds a name, it does not walk.

    A path into a group that is not there yet is legal, and starts working the
    moment the group exists. An eager accessor cannot do this.
    """
    p = tree / "later" / "x"
    assert not p.exists()
    tree.add_group("later", {"x": np.arange(4.0)})
    np.testing.assert_array_equal(p.numpy(), np.arange(4.0))


def test_a_separator_inside_one_component_still_splits(tree):
    np.testing.assert_array_equal((tree / "a/b/x").numpy(),
                                  (tree / "a" / "b" / "x").numpy())


def test_a_path_from_a_group_is_relative_to_that_group(tree):
    g = tree.group("a")
    np.testing.assert_array_equal((g / "b" / "x").numpy(), np.arange(10.0))


def test_the_pathlib_parts(tree):
    p = tree / "results" / "Tau"
    assert p.parts == ("results", "Tau")
    assert p.name == "Tau"
    assert str(p) == "results/Tau"
    assert repr(p) == "StorePath('results/Tau')"
    assert str(p.parent) == "results"
    assert p.exists() and not (tree / "results" / "nope").exists()


def test_paths_compare_and_hash(tree):
    assert (tree / "a/b/x") == (tree / "a" / "b" / "x")
    assert len({tree / "a/b/x", tree / "a" / "b" / "x"}) == 1
    # The same path into a different store is a different path.
    other = tttrlib.DataStore()
    other.ensure_group("a/b", {"x": np.arange(10.0)})
    assert (tree / "a/b/x") != (other / "a/b/x")


def test_a_path_delegates_to_whatever_it_lands_on(tree):
    assert (tree / "results" / "Tau").dtype == "float64"
    assert len(tree / "results" / "Tau") == 64
    assert (tree / "results").n_rows() == 64          # a group, delegated
    assert (tree / "results").n_columns() == 3


def test_a_group_path_is_not_an_array(tree):
    with pytest.raises(TypeError):
        np.asarray(tree / "results")


def test_a_column_path_indexes_and_iterates_its_values(tree):
    p = tree / "a" / "b" / "x"
    assert p[0] == 0.0
    np.testing.assert_array_equal(p[1:3], [1.0, 2.0])
    assert list(p) == list(np.arange(10.0))
    assert sum(p) == 45.0


def test_iterating_a_group_path_says_what_to_iterate_instead(tree):
    """A store has no __iter__, so Python falls back to __getitem__(0), (1),
    ... and walks off the end of the column deque -- which surfaces as
    `RuntimeError: deque`. Not a message to hand anyone through a new API."""
    with pytest.raises(TypeError) as e:
        list(tree / "results")
    assert "walk()" in str(e.value)


# --- string keys ------------------------------------------------------------


def test_a_key_with_a_separator_walks_the_tree(tree):
    np.testing.assert_array_equal(tree["a/b/x"].numpy(), np.arange(10.0))
    assert tree["a/b"].n_rows() == 10
    assert isinstance(tree["results/"], tttrlib.DataStore)


def test_a_slash_free_key_is_exactly_what_it_was(tree):
    """The decision this whole design turns on. ``store["meta"]`` is a column
    and a name that is only a group still raises -- reaching a group by name is
    ``store / "meta"`` or ``store.group("meta")``."""
    with pytest.raises(Exception):
        tree["results"]
    assert tree.find("results") == -1


def test_a_column_literally_named_with_a_slash_still_wins():
    """"Sg/Sr" is an ordinary name for a signal ratio, and HDF5 already has a
    test of its own for it. The column is looked up FIRST and the tree only
    once that has missed, so the literal name wins over the path -- which is
    the column-wins rule applied one level up."""
    s = tttrlib.DataStore()
    s.set_n_rows(4)
    s.add("Sg/Sr", np.arange(4.0))
    s.add_group("Sg", {"Sr": np.arange(9.0)})

    np.testing.assert_array_equal(s["Sg/Sr"].numpy(), np.arange(4.0))
    assert "Sg/Sr" in s
    # the group's column is still reachable, by the accessor that means it
    np.testing.assert_array_equal(s.group("Sg")["Sr"].numpy(), np.arange(9.0))


def test_the_column_lookup_did_not_get_slower():
    """__getitem__ is the hottest lookup in the class. The path key space adds
    nothing to a hit: no separator scan, and the try costs nothing until it
    raises. Measured as a ratio against the call it wraps, so the number does
    not depend on the machine."""
    import time
    s = tttrlib.DataStore()
    s.set_n_rows(1000)
    s.add("x", np.arange(1000.0))

    n = 20000
    for _ in range(2000):           # warm up
        s["x"], s.column_by_name("x")
    t0 = time.perf_counter()
    for _ in range(n):
        s.column_by_name("x")
    bare = time.perf_counter() - t0
    t0 = time.perf_counter()
    for _ in range(n):
        s["x"]
    sugar = time.perf_counter() - t0

    assert sugar < bare * 3.0, (
        "store['x'] is %.2fx the bare column_by_name it wraps" % (sugar / bare))


def test_contains_follows_getitem(tree):
    assert "a/b/x" in tree
    assert "a/b/nope" not in tree
    assert "nope/x" not in tree
    # and a slash-free key is still columns-only, as it always was
    assert "results" not in tree


def test_the_root_path_is_the_store_itself(tree):
    assert tree["/"].n_groups() == tree.n_groups()
    assert (tree / "").resolve().n_groups() == tree.n_groups()
    # "" carries no separator, so it is a column lookup like any other bare
    # key -- and there is no column of that name.
    with pytest.raises(ValueError):
        tree[""]


@pytest.mark.parametrize("bad", ["a//b", "a/./b", "a/../b", "./x", "../x"])
def test_a_malformed_path_is_refused_rather_than_reinterpreted(tree, bad):
    with pytest.raises(KeyError):
        tree[bad]
    with pytest.raises(KeyError):
        tree / bad


def test_a_missing_path_names_what_is_missing(tree):
    with pytest.raises(KeyError) as e:
        tree["results/nope"]
    assert "nope" in str(e.value) and "results" in str(e.value)
    with pytest.raises(KeyError) as e:
        tree["nope/x"]
    assert "nope" in str(e.value)


def test_a_missing_bare_column_raises_what_it_always_did(tree):
    """Unchanged: column_by_name's ValueError, not the path layer's KeyError."""
    with pytest.raises(ValueError):
        tree.group("results")["nope"]


# --- the collision ----------------------------------------------------------


def test_the_column_wins_when_a_name_is_both():
    """The existing rule carried down the tree rather than reversed halfway.

    ``find()`` never sees a group, so ``store["meta"]`` is the column; a path
    ending at such a name gives the column too, and the group is still
    ``store.group("meta")``.
    """
    s = tttrlib.DataStore()
    s.set_n_rows(3)
    s.add("meta", np.arange(3.0))
    s.add_group("meta", {"source": np.array(["run.ptu"], dtype=object)})

    assert isinstance(s["meta"], tttrlib.Column)
    assert isinstance((s / "meta").resolve(), tttrlib.Column)
    assert s.group("meta").n_rows() == 1
    assert (s / "meta" / "source") is not None
    np.testing.assert_array_equal((s / "meta").numpy(), np.arange(3.0))


def test_the_collision_one_level_down():
    s = tttrlib.DataStore()
    g = s.add_group("g")
    g.set_n_rows(2)
    g.add("both", np.arange(2.0))
    g.add_group("both").set_n_rows(1)
    np.testing.assert_array_equal(s["g/both"].numpy(), np.arange(2.0))
    assert s.group("g/both").n_rows() == 1


# --- lifetime ---------------------------------------------------------------


def test_a_view_through_a_store_path_outlives_the_root():
    """A path holds the store it is relative to, which holds the root.

    Same hazard and same proof as test_a_view_through_a_group_outlives_the_root
    in test_datastore_groups.py: a value check would prove nothing on its own,
    because freed memory usually still reads back fine. What matters is that
    the root was NOT collected.
    """
    root = tttrlib.DataStore()
    root.ensure_group("a/b", {"x": np.arange(1000.0)})
    view = (root / "a" / "b" / "x").numpy()
    alive = weakref.ref(root)

    del root
    gc.collect()

    assert alive() is not None, "the root went while a view through a path was alive"
    assert view.sum() == np.arange(1000.0).sum()


def test_a_path_alone_keeps_the_root_alive():
    """Holding only the path -- no view, no proxy -- and resolving it later."""
    root = tttrlib.DataStore()
    root.ensure_group("a/b", {"x": np.arange(1000.0)})
    p = root / "a" / "b" / "x"
    alive = weakref.ref(root)

    del root
    gc.collect()

    assert alive() is not None
    assert p.numpy().sum() == np.arange(1000.0).sum()


def test_dropping_every_path_frees_the_whole_tree():
    """The counterpart, and what fails if a path ever caches its proxy."""
    gc.collect()
    before = tttrlib.live_data_store_bytes()
    s = tttrlib.DataStore()
    g = s.ensure_group("a/b")
    g.set_n_rows(200000)
    g.add("x", np.zeros(200000))
    paths = [s / "a/b/x", s / "a" / "b"]
    for p in paths:
        p.resolve()
    assert tttrlib.live_data_store_bytes() >= before + 1600000

    del s, g, paths, p
    gc.collect()
    assert tttrlib.live_data_store_bytes() == before


def test_a_path_does_not_hold_a_removed_group():
    """A path is a name, not a pointer: it revalidates rather than dangling."""
    s = tttrlib.DataStore()
    s.ensure_group("a", {"x": np.arange(4.0)})
    p = s / "a" / "x"
    assert p.exists()

    s.remove_group("a")

    assert not p.exists()
    with pytest.raises(KeyError):
        p.numpy()


def test_np_asarray_of_a_column_outlives_the_root():
    """np.asarray used to drop the owner and read freed memory.

    numpy collapses a base chain through any array that does not own its data,
    subclass or not, so the _DsView carrying `_owner` was dropped by asarray
    and the store was free to go. It came back as
    ``[7.6e-310, 2.2e-321, 2.0]`` from an ``arange(3)``. See _DsBuffer.
    """
    root = tttrlib.DataStore()
    root.set_n_rows(1000)
    root.add("x", np.arange(1000.0))
    a = np.asarray(root["x"])
    alive = weakref.ref(root)

    del root
    gc.collect()

    assert alive() is not None, "the root went while an asarray of it was alive"
    np.testing.assert_array_equal(a, np.arange(1000.0))


@pytest.mark.parametrize("how", [
    lambda s: s["x"],
    lambda s: s.column(0),
    lambda s: s.column_by_name("x"),
    lambda s: s.columns[0],
])
def test_every_way_of_getting_a_column_keeps_the_store(how):
    """BUGS.md: a zero-copy view outliving its store, reading reused memory.

    The fix has to hold for EVERY accessor, not just the two that go through
    DataStore.py -- ``store.column(0)`` and ``store.column_by_name("x")`` are
    wrapped C++ and had no link back to the store at all.
    """
    s = tttrlib.DataStore()
    s.set_n_rows(1000)
    s.add("x", np.arange(1000.0))
    a = np.asarray(how(s))
    alive = weakref.ref(s)

    del s
    gc.collect()

    assert alive() is not None
    np.testing.assert_array_equal(a, np.arange(1000.0))


def test_holding_the_store_from_every_column_does_not_leak_it():
    """The other side of the fix: every Column proxy now references the store,
    so a store dropped after its columns have been handed out must still go."""
    gc.collect()
    before = tttrlib.live_data_store_bytes()
    for _ in range(20):
        s = tttrlib.DataStore()
        s.set_n_rows(100000)
        s.add("x", np.zeros(100000))
        cols = (s.column(0), s.column_by_name("x"), s["x"], s.columns)
        del s, cols
    gc.collect()
    assert tttrlib.live_data_store_bytes() == before


def test_a_csv_column_survives_the_store_it_was_read_from(tmp_path):
    """The reproduction from BUGS.md, unchanged.

    Eight runs of it gave ``1, 1, 0, 0, 1, 1, 1, 1`` values wrong -- always row
    2, read back as 0.0 or 6.001000000000001e-05 instead of 6.0, i.e. a
    partially overwritten double. Intermittent, because it came down to
    allocator timing; the loop is what makes it a test rather than a coin toss.
    """
    p = tmp_path / "t.csv"
    expected = np.arange(1000, dtype=float) * 3.0
    p.write_text("a\tb\n" + "".join("%s\t%s\n" % (v, v * 2) for v in expected))

    def read():
        store = tttrlib.read_csv(str(p), delimiter="\t")
        # np.asarray, not np.array: with a matching dtype this does NOT copy,
        # which is exactly how the defect got into shipped code.
        return {store[i].name(): np.asarray(store[i].numpy(), dtype=float)
                for i in range(store.n_columns())}      # <- the store dies here

    for _ in range(8):
        got = read()
        gc.collect()
        np.testing.assert_allclose(got["a"], expected)
        np.testing.assert_allclose(got["b"], expected * 2)


def test_the_root_of_a_derived_view_owns_the_buffer():
    """numpy collapses a view-of-a-view to the root, so the root is the only
    thing that can hold the store. That it collapses is fine -- what matters is
    what it collapses TO."""
    s = tttrlib.DataStore()
    s.set_n_rows(10)
    s.add("x", np.arange(10.0))
    x = s["x"].numpy()
    y = np.asarray(x, dtype=float)

    assert not x.flags["OWNDATA"]
    assert np.shares_memory(x, y)
    assert y.base is x.base, "numpy still collapses the chain"
    assert getattr(y.base, "_owner", None) is not None, "the root owns nothing"


def test_np_asarray_through_a_group_outlives_the_root():
    root = tttrlib.DataStore()
    root.ensure_group("a/b", {"x": np.arange(500.0)})
    a = np.asarray(root / "a" / "b" / "x")
    alive = weakref.ref(root)

    del root
    gc.collect()

    assert alive() is not None
    np.testing.assert_array_equal(a, np.arange(500.0))


# --- numpy interop ----------------------------------------------------------


def test_a_column_is_an_array_without_saying_numpy(tree):
    col = tree.group("results")["Tau"]
    np.testing.assert_array_equal(np.asarray(col), col.numpy())
    assert np.mean(col) == pytest.approx(np.mean(col.numpy()))
    np.testing.assert_array_equal(np.asarray(tree / "results" / "Tau"),
                                  col.numpy())


def test_asarray_is_still_zero_copy(tree):
    col = tree.group("results")["Tau"]
    np.asarray(col)[0] = 42.0
    assert col.numpy()[0] == 42.0


def test_asarray_of_a_string_column_cannot_avoid_a_copy(tree):
    """copy=False means "a view or nothing", and a dictionary-encoded column
    has no contiguous strings to view. Called directly: numpy only started
    passing `copy` in 2.0, and this environment is 1.26."""
    col = tree.group("meta")["source"]
    assert np.asarray(col)[0] == "run.ptu"
    with pytest.raises(ValueError):
        col.__array__(copy=False)
    with pytest.raises(ValueError):
        tree.group("results")["Tau"].__array__(np.int32, copy=False)


# --- building ---------------------------------------------------------------


def test_add_group_takes_its_columns_with_it():
    """The four-line add_group + loop + set_n_rows, in one call."""
    s = tttrlib.DataStore()
    g = s.add_group("meta", {"source": np.array(["run.ptu"], dtype=object),
                             "n_frames": np.array([40], dtype=np.int32)})
    assert g.n_rows() == 1
    assert g.names == ["source", "n_frames"]
    assert g["n_frames"].dtype == "int32"
    assert s.group("meta")["source"].numpy()[0] == "run.ptu"


def test_ensure_group_takes_columns_too_and_nests():
    s = tttrlib.DataStore()
    s.ensure_group("a/b/c", {"x": np.arange(3.0)})
    np.testing.assert_array_equal(s["a/b/c/x"].numpy(), np.arange(3.0))


def test_a_group_built_with_columns_still_holds_the_root():
    """The keep-alive pythonappend is inside the wrapped C++ method; wrapping
    it in Python must not step around it."""
    root = tttrlib.DataStore()
    view = root.add_group("g", {"x": np.arange(100.0)})["x"].numpy()
    alive = weakref.ref(root)

    del root
    gc.collect()

    assert alive() is not None
    assert view.sum() == np.arange(100.0).sum()


def test_add_group_still_refuses_a_path_and_a_duplicate():
    s = tttrlib.DataStore()
    s.add_group("a")
    with pytest.raises(Exception):
        s.add_group("a/b")
    with pytest.raises(Exception):
        s.add_group("a")


def test_setitem_makes_the_group_it_needs():
    s = tttrlib.DataStore()
    s["meta/source"] = np.array(["run.ptu"], dtype=object)
    assert s.group("meta").n_rows() == 1
    assert s["meta/source"].numpy()[0] == "run.ptu"

    s["a/b/c/x"] = np.arange(3.0)
    np.testing.assert_array_equal(s["a/b/c/x"].numpy(), np.arange(3.0))


def test_setitem_replaces_rather_than_appending(tree):
    """Assignment means replace. It matters for a string column especially:
    set_numpy pushes onto the dictionary, so an in-place write would double
    the rows instead of replacing them."""
    tree["meta/source"] = np.array(["other.ptu"], dtype=object)
    assert tree.group("meta").n_rows() == 1
    assert len(tree.group("meta")["source"]) == 1
    assert tree["meta/source"].numpy()[0] == "other.ptu"


def test_setitem_keeps_the_dtype():
    s = tttrlib.DataStore()
    s["g/x"] = np.arange(4, dtype=np.float32)
    assert s["g/x"].dtype == "float32"


# --- enumeration ------------------------------------------------------------


def test_walk_is_depth_first_with_the_root_first(tree):
    assert [p for p, _ in tree.walk()] == ["", "results", "meta", "a", "a/b"]
    assert dict(tree.walk())["a/b"].n_rows() == 10


def test_paths_is_the_memory_report_key_space(tree):
    assert sorted(tree.paths()) == sorted(
        k for k in tree.memory_report() if k != "total")


def test_rglob_finds_a_column_anywhere(tree):
    assert [str(p) for p in tree.rglob("*")] == tree.paths()
    assert [str(p) for p in tree.rglob("Tau")] == ["results/Tau"]
    assert [str(p) for p in tree.rglob("x")] == ["a/b/x"]
    assert all(p.exists() for p in tree.rglob("*"))


def test_glob_stays_in_this_store(tree):
    assert tree.glob("*") == []          # the root has no columns of its own
    assert [str(p) for p in tree.group("results").glob("*")] == ["Tau", "E", "w"]


def test_tree_says_what_is_in_the_file(tree):
    text = tree.tree()
    assert "results" in text and "meta" in text and "a/" not in text.split("\n")[0]
    assert "Tau float64" in text
    assert "64 rows" in text and "1 row," in text


def test_the_repr_stays_one_line(tree):
    assert "\n" not in repr(tree)
    assert repr(tree).startswith("DataStore(")


def test_tree_truncates_a_big_one():
    s = tttrlib.DataStore()
    for i in range(200):
        s.add_group("g%03d" % i, {"x": np.zeros(1)})
    text = s.tree(max_lines=20)
    assert len(text.split("\n")) <= 22
    assert "more" in text


# --- histograms -------------------------------------------------------------


def test_a_column_path_histograms_itself(tree):
    """One expression from a loaded .dstore to a histogram."""
    a = (tree / "results" / "Tau").histogram(bins=16, range=[(0.0, 5.0)])
    b = tree.group("results").histogram("Tau", bins=16, range=[(0.0, 5.0)])
    np.testing.assert_array_equal(np.asarray(a), np.asarray(b))


def test_histogram_takes_paths_as_column_names(tree):
    a = tree.histogram("results/Tau", "results/E", bins=8)
    b = tree.group("results").histogram("Tau", "E", bins=8)
    np.testing.assert_array_equal(np.asarray(a), np.asarray(b))


def test_a_weight_may_be_a_path_too(tree):
    a = tree.histogram("results/Tau", bins=8, weight="results/w")
    b = tree.group("results").histogram("Tau", bins=8, weight="w")
    np.testing.assert_array_equal(np.asarray(a), np.asarray(b))


def test_profile_takes_paths(tree):
    a = tree.profile("results/Tau", sample="results/E", bins=8)
    b = tree.group("results").profile("Tau", sample="E", bins=8)
    np.testing.assert_allclose(a.mean(), b.mean(), equal_nan=True)


def test_a_histogram_across_two_groups_refuses(tree):
    """A histogram fills from ONE table. Two groups have different row counts
    and no row correspondence, so this is not a thing that can be filled --
    and saying so is better than an answer nobody can check."""
    with pytest.raises(ValueError) as e:
        tree.histogram("results/Tau", "meta/source")
    assert "results/Tau" in str(e.value) and "meta/source" in str(e.value)
    assert "row correspondence" in str(e.value)

    with pytest.raises(ValueError):
        tree.profile("results/Tau", sample="meta/source")


def test_a_flat_histogram_is_untouched(tree):
    g = tree.group("results")
    h = g.histogram("Tau", bins=4, range=[(0.0, 5.0)])
    assert np.asarray(h).sum() == 64


# --- the raw vector returns -------------------------------------------------


def test_the_listings_are_real_lists(tree):
    assert tree.group("results").column_names() == ["Tau", "E", "w"]
    assert tree.group_names() == ["results", "meta", "a"]
    assert tree.group_paths() == ["results", "meta", "a", "a/b"]


# --- the round trip the request came from -----------------------------------


def test_a_dstore_reads_back_through_paths(tmp_path, tree):
    path = str(tmp_path / "run.dstore")
    tttrlib.save_store(path, tree)
    back = tttrlib.load_store(path)

    assert tttrlib.store_groups(path) == tree.group_paths()
    for p in tree.paths():
        a, b = tree[p], back[p]
        if a.dtype == "str":
            assert b.numpy()[0] == a.numpy()[0], p
            continue
        np.testing.assert_array_equal(np.asarray(b), np.asarray(a), err_msg=p)

    np.testing.assert_array_equal(
        np.asarray(back / "results" / "Tau"), np.linspace(0.5, 5.0, 64))
    np.testing.assert_array_equal(
        np.asarray((back / "results" / "Tau").histogram(bins=8)),
        np.asarray(tree.histogram("results/Tau", bins=8)))

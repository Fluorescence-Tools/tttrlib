"""A store is a tree: named child stores, each a full table of its own.

One table is not the shape the data has. An imaging run is a ``results`` table
with one row per pixel and a one-row ``meta`` beside it saying where it came
from -- two row counts, one object. Before this there was nowhere to put the
second one.

A group keeps everything a store keeps, independently: its own columns, row
count, selection, validity masks and label. Nothing propagates between them,
because two groups with different row counts have nothing to share.
"""
import gc

import numpy as np
import pytest
import tttrlib


def table(n, first=0.0, name="x"):
    s = tttrlib.DataStore()
    s.set_n_rows(n)
    s.add(name, np.arange(n, dtype=np.float64) + first)
    return s


@pytest.fixture
def imaging():
    """The shape this exists for."""
    s = tttrlib.DataStore("acquisition")
    results = s.add_group("results")
    results.set_n_rows(64)
    results.add("Tau", np.linspace(1.0, 4.0, 64))
    results.add("Number of Photons", np.arange(64, dtype=np.int64))
    meta = s.add_group("meta")
    meta.set_n_rows(1)
    meta.add("source", np.array(["run.ptu"], dtype=object))
    return s


# -- the model ----------------------------------------------------------------

def test_a_group_keeps_its_own_rows_columns_and_label(imaging):
    assert imaging.n_rows() == 0, "a root that only associates tables has no rows"
    assert imaging.n_columns() == 0
    assert imaging.n_groups() == 2

    assert imaging.group("results").n_rows() == 64
    assert imaging.group("meta").n_rows() == 1
    assert imaging.group("results").names == ["Tau", "Number of Photons"]
    assert imaging.group("meta")["source"].numpy()[0] == "run.ptu"


def test_a_group_row_count_differing_from_its_parent_is_not_an_inconsistency(imaging):
    """It is the point of the feature, so it must not be reported as a fault."""
    assert list(imaging.inconsistent_columns()) == []
    assert list(imaging.group("results").inconsistent_columns()) == []


def test_a_selection_in_one_group_does_not_reach_another(imaging):
    imaging.group("results").where("Tau", 1.0, 2.0)
    assert imaging.group("results").n_selected() < 64
    assert imaging.group("meta").selection() is None


# -- reference stability ------------------------------------------------------

def test_a_group_handle_survives_fifty_siblings():
    """The bug this library has already had once, one level up.

    Columns used to live in a vector and add() handed out a reference into it,
    so the next add() reallocated and every handle read freed memory --
    reporting an empty name and an empty array rather than raising. A group
    must not be able to do the same.
    """
    s = tttrlib.DataStore()
    g = s.add_group("results")
    g.set_n_rows(7)
    g.add("x", np.arange(7.0))

    for i in range(50):
        s.add_group("other%d" % i)

    assert g.n_rows() == 7
    assert g.names == ["x"]
    np.testing.assert_array_equal(g["x"].numpy(), np.arange(7.0))


def test_a_group_handle_survives_a_sibling_being_removed():
    """Stronger than the requirement, and it is what pins the container choice:
    a vector of stores would relocate the survivors on erase."""
    s = tttrlib.DataStore()
    for name in ("a", "b", "c"):
        g = s.add_group(name)
        g.set_n_rows(3)
        g.add("x", np.arange(3.0))
    c = s.group("c")

    assert s.remove_group("a") is True
    assert c.n_rows() == 3
    np.testing.assert_array_equal(c["x"].numpy(), np.arange(3.0))


# -- paths --------------------------------------------------------------------

@pytest.mark.parametrize("path", ["", "/"])
def test_the_empty_path_is_this_store(path, imaging):
    assert imaging.has_group(path) is True
    assert imaging.group(path).n_groups() == 2


@pytest.mark.parametrize("path", ["results", "/results", "results/"])
def test_a_separator_at_either_end_is_optional(path, imaging):
    assert imaging.group(path).n_rows() == 64


def test_a_path_nests():
    s = tttrlib.DataStore()
    s.ensure_group("a/b")
    assert s.has_group("a") and s.has_group("a/b")
    assert s.group("a/b") is not None
    assert s.has_group("a/c") is False


@pytest.mark.parametrize("path", ["a//b", "a/./b", "a/../b", ".", "..", "a\x00b"])
def test_a_malformed_path_throws_rather_than_being_reinterpreted(path):
    s = tttrlib.DataStore()
    s.ensure_group("a")
    with pytest.raises(Exception):
        s.has_group(path)


def test_add_group_takes_a_name_and_not_a_path():
    """add_group("a/b") reads as "make b inside a" and does not do that, so it
    refuses rather than making a group with a slash in its name."""
    s = tttrlib.DataStore()
    with pytest.raises(Exception):
        s.add_group("a/b")


def test_a_duplicate_sibling_name_throws():
    """Replacing one is remove_group then add_group, said out loud."""
    s = tttrlib.DataStore()
    s.add_group("results")
    with pytest.raises(Exception):
        s.add_group("results")


def test_ensure_group_is_idempotent_and_makes_intermediates():
    s = tttrlib.DataStore()
    first = s.ensure_group("a/b/c")
    first.set_n_rows(5)
    again = s.ensure_group("a/b/c")

    assert again.n_rows() == 5, "a second call made a second group"
    assert s.n_groups() == 1
    assert s.has_group("a") and s.has_group("a/b")


def test_remove_group_says_whether_there_was_one():
    s = tttrlib.DataStore()
    s.ensure_group("a/b")
    assert s.remove_group("a/b") is True
    assert s.remove_group("a/b") is False
    assert s.has_group("a") is True
    assert s.remove_group("") is False, "the store is not one of its own groups"


# -- order --------------------------------------------------------------------

def test_group_names_are_insertion_order_not_alphabetical():
    s = tttrlib.DataStore()
    for name in ("zulu", "alpha", "mike"):
        s.add_group(name)
    assert s.group_names() == ["zulu", "alpha", "mike"]


def test_group_paths_are_depth_first_and_include_every_intermediate():
    s = tttrlib.DataStore()
    s.add_group("results")
    s.ensure_group("a/b")
    s.add_group("meta")
    assert s.group_paths() == ["results", "a", "a/b", "meta"]


# -- copying ------------------------------------------------------------------

def test_a_copy_is_deep():
    """A group belongs to its parent, not to whoever else has a reference."""
    s = tttrlib.DataStore()
    g = s.add_group("results")
    g.set_n_rows(4)
    g.add("x", np.zeros(4))

    other = tttrlib.DataStore(s)
    other.group("results")["x"].set_numpy(np.ones(4))
    other.group("results").set_label("changed")

    np.testing.assert_array_equal(s.group("results")["x"].numpy(), np.zeros(4))
    assert s.group("results").label() != "changed"


def test_a_cycle_cannot_be_expressed():
    """No group operation takes an existing store, so there is nothing to check.

    add_group and ensure_group only ever make a fresh empty child; a store can
    be filled in place (read_csv_into(s.ensure_group("results"), path)) but
    never inserted. That structural impossibility is the real guarantee, and it
    is worth an assertion because the day someone adds an adopt API is the day
    the guarantee needs a runtime check instead. C++ copy-assignment does have
    one; SWIG does not wrap operator=, so it is unreachable from here.
    """
    s = tttrlib.DataStore()
    a = s.add_group("a")
    assert a.n_groups() == 0 and a.n_columns() == 0 and a.n_rows() == 0
    assert not [m for m in dir(s) if m in ("adopt", "set_group", "insert_group",
                                           "assign", "take_group")], \
        "a way to insert an existing store appeared -- it needs a cycle check"


# -- namespaces ---------------------------------------------------------------

def test_a_column_and_a_group_may_share_a_name():
    """Nothing has to disambiguate, because the accessors differ."""
    s = tttrlib.DataStore()
    s.set_n_rows(3)
    s.add("meta", np.arange(3.0))
    s.add_group("meta").set_n_rows(1)

    assert s.find("meta") == 0
    assert "meta" in s
    assert s["meta"].numpy().tolist() == [0.0, 1.0, 2.0]
    assert s.group("meta").n_rows() == 1


def test_find_never_sees_a_group():
    """find() is the hottest lookup in the class -- column_by_name, and every
    where/region/histogram call goes through it. Making it group-aware would
    cost a branch there and reintroduce exactly the ambiguity above."""
    s = tttrlib.DataStore()
    s.set_n_rows(3)
    s.add_group("tau")

    assert s.find("tau") == -1
    assert "tau" not in s
    with pytest.raises(Exception):
        s["tau"]


def test_column_names_lists_columns_only():
    s = tttrlib.DataStore()
    s.set_n_rows(2)
    s.add("x", np.zeros(2))
    s.add_group("g")
    assert s.names == ["x"]
    assert list(s.column_names()) == ["x"]


# -- accounting ---------------------------------------------------------------

def test_nbytes_counts_the_whole_tree():
    s = tttrlib.DataStore()
    empty = s.nbytes()
    g = s.ensure_group("a/b")
    g.set_n_rows(1000)
    g.add("x", np.zeros(1000))
    assert s.nbytes() >= empty + 8000


def test_a_group_is_not_a_second_registry_entry():
    """The registry holds roots. A child that registered itself would be
    counted twice the moment nbytes() recurses, and would show up in the
    listing as something nobody can drop."""
    before = len(tttrlib.data_stores())
    s = tttrlib.DataStore("root")
    s.add_group("a")
    s.add_group("b")
    assert len(tttrlib.data_stores()) == before + 1
    assert s.group("a").id() == 0


def test_the_root_entry_reports_the_tree_total():
    s = tttrlib.DataStore("with a group")
    g = s.add_group("big")
    g.set_n_rows(100000)
    g.add("x", np.zeros(100000))

    entry = [e for e in tttrlib.data_stores() if e["id"] == s.id()][0]
    assert entry["bytes"] >= 800000


def test_release_frees_the_whole_tree():
    """release() promises to free the memory. A release that left most of a
    tree alive would defeat the one thing it is for."""
    s = tttrlib.DataStore()
    g = s.ensure_group("a/b")
    g.set_n_rows(1000)
    g.add("x", np.zeros(1000))

    s.release()
    assert s.nbytes() == 0
    assert s.n_groups() == 0


# -- selection over a tree ----------------------------------------------------

def test_clearing_every_gate_at_once(imaging):
    """The one tree-wide selection operation, because it is well defined
    whatever the row counts are."""
    imaging.group("results").where("Tau", 1.0, 2.0)
    assert imaging.group("results").n_selected() < 64

    imaging.select_all_recursive()
    assert imaging.group("results").n_selected() == 64
    assert imaging.group("results").selection() is None, "the mask was freed too"


def test_selecting_nothing_reaches_every_group(imaging):
    imaging.ensure_group("a/b").set_n_rows(5)
    imaging.select_none_recursive()
    assert imaging.group("results").n_selected() == 0
    assert imaging.group("a/b").n_selected() == 0


def test_a_plain_select_does_not_propagate(imaging):
    """Groups have different row counts, so a mask over one means nothing
    over another."""
    imaging.group("results").set_n_rows(64)
    imaging.select_none()
    assert imaging.group("results").n_selected() == 64


# -- the hot path -------------------------------------------------------------

def test_groups_do_not_slow_the_selection_path():
    """The canary for the whole design.

    A store that HAS groups must scan at the same speed as one that does not,
    because nothing on the scan path looks at them. A ratio rather than a wall
    clock, so it does not depend on the machine -- the honest answer is 1.0,
    and anything past 2 means a branch or an indirection reached
    find/apply/scan_column.
    """
    import time

    s = tttrlib.DataStore()
    n = 1_000_000
    s.set_n_rows(n)
    s.add("x", np.random.default_rng(0).random(n))

    def timed():
        best = float("inf")
        for _ in range(5):
            t = time.perf_counter()
            for _ in range(4):
                s.where("x", 0.2, 0.8)
            best = min(best, time.perf_counter() - t)
        return best

    before = timed()
    for i in range(32):
        s.add_group("g%d" % i)
    after = timed()

    assert after < before * 2.0 + 1e-3, "scanning got slower once groups existed"


def test_dropping_the_root_frees_the_whole_tree():
    before = tttrlib.live_data_store_bytes()
    s = tttrlib.DataStore()
    g = s.ensure_group("a/b")
    g.set_n_rows(200000)
    g.add("x", np.zeros(200000))
    assert tttrlib.live_data_store_bytes() >= before + 1600000
    del s, g
    gc.collect()
    assert tttrlib.live_data_store_bytes() == before

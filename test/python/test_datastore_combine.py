"""Combining and subsetting stores: concat, take, compact.

PRD-023 Part 4. `BUGS.md` counts these in the shipped downstream package —
`concat` at 24 call sites in 13 files, `take`/`compact` at 9 plus every filtered
export — and without them a caller that reads N files gets N stores it cannot
make into one table, so it keeps building data frames and the store never
reaches memory.

The load-bearing test is :func:`test_a_missing_column_keeps_its_dtype`. That is
the one thing a store does here that a frame cannot: pandas has to widen an
``int64`` column to ``float64`` to hold a ``NaN``, and the dtype cannot be
recovered afterwards. The validity mask says "not measured" without touching the
type — which is what the mask is for.
"""
import os
import sys

import numpy as np
import pytest

sys.path.insert(0, os.path.dirname(__file__))
import tttrlib


def store(columns, n_rows=None):
    s = tttrlib.DataStore()
    s.set_n_rows(len(next(iter(columns.values()))) if n_rows is None else n_rows)
    for name, values in columns.items():
        s.add(name, values)
    return s


# --- stacking rows ----------------------------------------------------------


def test_rows_stack_and_the_row_count_is_the_sum():
    a = store({"Tau": np.arange(3.0)})
    b = store({"Tau": np.arange(3.0, 5.0)})
    r = tttrlib.concat([a, b])
    assert r.n_rows() == 5
    np.testing.assert_array_equal(r["Tau"].numpy(), np.arange(5.0))


def test_columns_line_up_by_name_not_by_position():
    """Two burst tables written by different runs need not have listed their
    columns in the same order, and a positional append would silently
    interleave two different measurements."""
    a = store({"Tau": np.arange(2.0), "E": np.array([0.1, 0.2])})
    b = store({"E": np.array([0.3, 0.4]), "Tau": np.arange(2.0, 4.0)})
    r = tttrlib.concat([a, b])
    assert r.names == ["Tau", "E"]                  # the first store's order
    np.testing.assert_array_equal(r["Tau"].numpy(), np.arange(4.0))
    np.testing.assert_allclose(r["E"].numpy(), [0.1, 0.2, 0.3, 0.4])


def test_a_missing_column_keeps_its_dtype():
    """The reason to have written this rather than kept using pandas."""
    a = store({"Tau": np.arange(3.0), "n": np.arange(3, dtype=np.int64)})
    b = store({"Tau": np.arange(3.0, 5.0)})         # no "n"
    r = tttrlib.concat([a, b])

    assert r["n"].dtype == "int64", "pandas would have widened this to float64"
    assert r.n_rows() == 5
    np.testing.assert_array_equal(r["n"].mask_numpy(),
                                  [True, True, True, False, False])
    np.testing.assert_array_equal(r["n"].numpy()[:3], [0, 1, 2])


def test_a_column_new_to_the_first_store_marks_the_earlier_rows():
    a = store({"Tau": np.arange(3.0)})
    b = store({"Tau": np.arange(3.0, 5.0), "E": np.array([0.5, 0.6])})
    r = tttrlib.concat([a, b])
    assert r.names == ["Tau", "E"]
    np.testing.assert_array_equal(r["E"].mask_numpy(),
                                  [False, False, False, True, True])
    np.testing.assert_allclose(r["E"].numpy()[3:], [0.5, 0.6])


def test_inner_keeps_only_what_every_store_has():
    a = store({"Tau": np.arange(3.0), "n": np.arange(3, dtype=np.int64)})
    b = store({"Tau": np.arange(3.0, 5.0)})
    r = tttrlib.concat([a, b], join="inner")
    assert r.names == ["Tau"]
    assert r.n_rows() == 5


def test_a_type_conflict_refuses_and_names_the_column():
    """Promoting a float32 to meet a float64 would lose the dtype the store
    exists to keep, and would do it silently."""
    a = store({"Tau": np.arange(3.0)})
    b = store({"Tau": np.arange(2, dtype=np.int32)})
    with pytest.raises(ValueError) as e:
        tttrlib.concat([a, b])
    assert "Tau" in str(e.value) and "float64" in str(e.value) and "int32" in str(e.value)


def test_a_type_conflict_leaves_the_first_store_untouched():
    """The check runs over every column before anything is appended. Half an
    append and then a throw leaves a store no caller could put back."""
    a = store({"Tau": np.arange(3.0), "n": np.arange(3, dtype=np.int64)})
    b = store({"n": np.arange(2, dtype=np.int64), "Tau": np.arange(2, dtype=np.int32)})
    with pytest.raises(ValueError):
        a.append_rows(b)
    assert a.n_rows() == 3
    np.testing.assert_array_equal(a["n"].numpy(), [0, 1, 2])


def test_strings_merge_their_dictionaries():
    a = store({"src": np.array(["m1.ptu", "m1.ptu"], dtype=object)})
    b = store({"src": np.array(["m2.ptu"], dtype=object)})
    r = tttrlib.concat([a, b])
    assert r["src"].numpy().tolist() == ["m1.ptu", "m1.ptu", "m2.ptu"]
    assert sorted(r["src"].labels()) == ["m1.ptu", "m2.ptu"]


def test_every_dtype_survives_a_concat():
    dtypes = [np.float64, np.float32, np.int64, np.int32, np.int16, np.int8,
              np.uint64, np.uint32, np.uint16, np.uint8]
    a = store({d().dtype.name: np.arange(2, dtype=d) for d in dtypes})
    b = store({d().dtype.name: np.arange(2, 4, dtype=d) for d in dtypes})
    r = tttrlib.concat([a, b])
    for d in dtypes:
        name = d().dtype.name
        assert r[name].dtype == name, name
        np.testing.assert_array_equal(r[name].numpy(), np.arange(4, dtype=d))


def test_a_bool_column_concatenates():
    a = store({"ok": np.array([True, False])})
    b = store({"ok": np.array([False, True, True])})
    r = tttrlib.concat([a, b])
    assert r["ok"].dtype == "bool"
    np.testing.assert_array_equal(r["ok"].numpy(),
                                  [True, False, False, True, True])


def test_the_selection_is_dropped_rather_than_guessed():
    """A gate over the old rows says nothing about the new ones, and extending
    it either way would be a guess."""
    a = store({"Tau": np.arange(4.0)})
    a.where("Tau", 0.0, 2.0)
    b = store({"Tau": np.arange(4.0, 6.0)})
    r = tttrlib.concat([a, b])
    assert r.selection() is None
    assert r.n_selected() == 6


# --- side by side -----------------------------------------------------------


def test_columns_go_side_by_side():
    a = store({"Tau": np.arange(3.0)})
    b = store({"E": np.arange(3.0) / 10})
    r = tttrlib.concat([a, b], axis="columns")
    assert r.names == ["Tau", "E"]
    assert r.n_rows() == 3


def test_axis_takes_the_pandas_spelling_too():
    a = store({"Tau": np.arange(3.0)})
    b = store({"E": np.arange(3.0)})
    assert tttrlib.concat([a, b], axis=1).names == ["Tau", "E"]
    assert tttrlib.concat([a, a], axis=0).n_rows() == 6


def test_a_row_count_mismatch_refuses_and_names_both():
    """The downstream warns and skips the file today, which is how a merge
    quietly loses a measurement."""
    a = store({"Tau": np.arange(3.0)})
    b = store({"E": np.arange(2.0)})
    with pytest.raises(ValueError) as e:
        tttrlib.concat([a, b], axis="columns")
    assert "3" in str(e.value) and "2" in str(e.value)


def test_a_duplicate_column_refuses_unless_asked():
    a = store({"Tau": np.arange(3.0)})
    b = store({"Tau": np.zeros(3)})
    with pytest.raises(ValueError) as e:
        tttrlib.concat([a, b], axis="columns")
    assert "Tau" in str(e.value)

    r = tttrlib.concat([a, b], axis="columns", on_duplicate="keep-first")
    assert r.names == ["Tau"]
    np.testing.assert_array_equal(r["Tau"].numpy(), np.arange(3.0))


# --- take and compact -------------------------------------------------------


def test_take_gathers_the_rows_asked_for_in_that_order():
    s = store({"Tau": np.arange(5.0), "n": np.arange(5, dtype=np.int32)})
    t = s.take([4, 0, 2])
    assert t.n_rows() == 3
    np.testing.assert_array_equal(t["Tau"].numpy(), [4.0, 0.0, 2.0])
    np.testing.assert_array_equal(t["n"].numpy(), [4, 0, 2])
    assert t["n"].dtype == "int32"


def test_take_carries_strings_and_validity():
    s = store({"src": np.array(["a", "b", "c"], dtype=object),
               "v": np.arange(3.0)})
    s["v"].set_mask(np.array([1, 0, 1], dtype=np.uint8))
    t = s.take([2, 1])
    assert t["src"].numpy().tolist() == ["c", "b"]
    np.testing.assert_array_equal(t["v"].mask_numpy(), [True, False])


def test_take_refuses_a_row_that_is_not_there():
    s = store({"Tau": np.arange(3.0)})
    with pytest.raises(ValueError):
        s.take([5])
    with pytest.raises(ValueError):
        s.take([-1])


def test_compact_materialises_the_selection():
    """29 select_* methods and, until this, not one that yielded a store."""
    s = store({"Tau": np.arange(6.0), "n": np.arange(6, dtype=np.int64)})
    s.where("Tau", 2.0, 5.0)
    c = s.compact()

    assert c.n_rows() == 3
    np.testing.assert_array_equal(c["Tau"].numpy(), [2.0, 3.0, 4.0])
    np.testing.assert_array_equal(c["n"].numpy(), [2, 3, 4])
    assert c["n"].dtype == "int64"
    # the result IS the selection, so it does not carry one
    assert c.selection() is None
    # and the original is untouched
    assert s.n_rows() == 6


def test_compact_with_no_selection_is_the_whole_table():
    s = store({"Tau": np.arange(4.0)})
    assert s.compact().n_rows() == 4


def test_compact_of_an_empty_selection_is_an_empty_store():
    s = store({"Tau": np.arange(4.0)})
    s.select_none()
    c = s.compact()
    assert c.n_rows() == 0 and c.names == ["Tau"]


def test_a_compacted_store_round_trips(tmp_path):
    """The point of materialising: a filtered table you can write back out."""
    s = store({"Tau": np.arange(10.0), "n": np.arange(10, dtype=np.int32)})
    s.where("Tau", 3.0, 7.0)
    path = str(tmp_path / "sub.dstore")
    tttrlib.save_store(path, s.compact())
    back = tttrlib.load_store(path)
    assert back.n_rows() == 4
    np.testing.assert_array_equal(back["Tau"].numpy(), [3.0, 4.0, 5.0, 6.0])
    assert back["n"].dtype == "int32"


# --- what is deliberately not done ------------------------------------------


def test_concat_does_not_descend_into_groups():
    """Row counts are per group, so combining two trees is ambiguous. The
    groups you mean are concatenated by name, out loud."""
    a = tttrlib.DataStore()
    a.add_group("results", {"Tau": np.arange(3.0)})
    b = tttrlib.DataStore()
    b.add_group("results", {"Tau": np.arange(3.0, 5.0)})

    r = tttrlib.concat([a, b])
    assert r.n_groups() == 0, "the tree is not descended into"

    g = tttrlib.concat([a.group("results"), b.group("results")])
    assert g.n_rows() == 5


def test_concat_needs_at_least_one_store():
    with pytest.raises(ValueError):
        tttrlib.concat([])


def test_a_store_cannot_be_appended_to_itself():
    s = store({"Tau": np.arange(3.0)})
    with pytest.raises(ValueError):
        s.append_rows(s)
    with pytest.raises(ValueError):
        s.append_columns(s)

"""Rows that were never measured, recorded as ranges rather than as bits.

A gap left by a concat is always a contiguous run — one store's whole
contribution — so a bit per row stores a million copies of one fact. The
saving is real but should not be oversold: 1.6% of a float64 column, 12.5% of
an int8 one. **The better reason is that a range can say why and a bit
cannot.** "These rows are not measured because that file did not have this
column" is information; a zero bit is the absence of it.

`valid(i)` is the interface either way. Which representation a column uses is
storage, and the tests here are mostly about that distinction holding: nothing
outside the column should have to ask.
"""
import json
import os
import sys

import numpy as np
import pytest

sys.path.insert(0, os.path.dirname(__file__))
import tttrlib


def store(columns, label="", n_rows=None):
    s = tttrlib.DataStore(label)
    s.set_n_rows(len(next(iter(columns.values()))) if n_rows is None else n_rows)
    for name, values in columns.items():
        s.add(name, values)
    return s


@pytest.fixture
def merged():
    """Two files, one of which never had `n`."""
    a = store({"Tau": np.arange(3.0), "n": np.arange(3, dtype=np.int64)}, "m001.ptu")
    b = store({"Tau": np.arange(3.0, 5.0)}, "m002.hdf5")
    return tttrlib.concat([a, b])


# --- the representation ------------------------------------------------------


def test_a_concat_gap_is_a_range_and_not_a_mask(merged):
    c = merged["n"]
    assert not c.has_mask(), "a bit mask was allocated for a contiguous run"
    assert c.has_missing(), "the rows are missing all the same"
    assert json.loads(c.metadata())["na"] == [
        {"rows": [3, 5], "why": "absent in 'm002.hdf5'"}]


def test_valid_answers_on_both_sides_of_the_boundary(merged):
    c = merged["n"]
    assert [c.valid(i) for i in range(5)] == [True, True, True, False, False]


def test_the_range_says_which_file_did_not_have_the_column(merged):
    """The whole reason to prefer a range. A caller merging twenty files can
    report which ones contributed what; a zero bit cannot be asked."""
    ranges = merged["n"].na_ranges()
    assert len(ranges) == 1
    assert (ranges[0].first, ranges[0].last) == (3, 5)
    assert ranges[0].why == "absent in 'm002.hdf5'"


def test_an_unlabelled_store_still_says_something(merged):
    """"absent in ''" would read as a bug rather than as a fact."""
    a = store({"Tau": np.arange(2.0), "n": np.arange(2, dtype=np.int32)})
    b = store({"Tau": np.arange(2.0)})
    r = tttrlib.concat([a, b])
    assert "no label" in r["n"].na_ranges()[0].why


def test_asking_for_the_mask_does_not_depend_on_the_storage(merged):
    """`mask_numpy()` is the question a caller actually asks, and it must not
    change answer because the column stored the same fact differently."""
    by_range = merged["n"].mask_numpy()

    bits = store({"n": np.arange(5, dtype=np.int64)})
    bits["n"].set_mask(np.array([1, 1, 1, 0, 0], dtype=np.uint8))
    np.testing.assert_array_equal(by_range, bits["n"].mask_numpy())
    assert bits["n"].has_mask() and not merged["n"].has_mask()


def test_a_column_new_to_the_first_store_gets_a_range_too():
    a = store({"Tau": np.arange(3.0)}, "m001.ptu")
    b = store({"Tau": np.arange(3.0, 5.0), "E": np.array([0.5, 0.6])}, "m002.ptu")
    r = tttrlib.concat([a, b])
    ranges = r["E"].na_ranges()
    assert (ranges[0].first, ranges[0].last) == (0, 3)
    assert "m001.ptu" in ranges[0].why


def test_ranges_accumulate_across_a_longer_merge():
    """Twenty files, a column in one. Nineteen ranges, not nineteen million
    bits — and each one names the file it came from."""
    stores = [store({"Tau": np.zeros(10)} if i else
                    {"Tau": np.zeros(10), "n": np.zeros(10, dtype=np.int8)},
                    "m%03d.ptu" % i) for i in range(20)]
    r = tttrlib.concat(stores)
    c = r["n"]

    assert len(c.na_ranges()) == 19
    assert not c.has_mask()
    assert [x.why for x in c.na_ranges()][0] == "absent in 'm001.ptu'"
    # every row from 10 on is missing, and every row before it is not
    assert all(c.valid(i) for i in range(10))
    assert not any(c.valid(i) for i in range(10, 200))


def test_a_scattered_pattern_still_becomes_a_bit_mask():
    """The representation is an implementation detail behind one interface,
    not two kinds of column. NaNs dotted through a column are not a run, and
    forcing them into ranges would be worse than the bits."""
    s = store({"Tau": np.array([1.0, np.nan, 3.0, np.nan, 5.0])})
    c = s["Tau"]
    assert c.mask_non_finite() == 2
    assert c.has_mask(), "a scattered pattern is what bits are for"
    assert c.na_ranges() == [] or len(c.na_ranges()) == 0
    assert [c.valid(i) for i in range(5)] == [True, False, True, False, True]


def test_the_two_representations_are_not_mixed_in_one_column():
    """A column that already carries bits gets bits. Two forms in one column
    would put both paths in every reader for no gain, and the bits are already
    allocated so the range would save nothing."""
    s = store({"n": np.arange(4, dtype=np.int32)})
    c = s["n"]
    c.set_mask(np.array([1, 0, 1, 1], dtype=np.uint8))
    c.add_na_range(2, 4, "later")

    assert c.has_mask() and len(c.na_ranges()) == 0
    assert [c.valid(i) for i in range(4)] == [True, False, False, False]


# --- what the rest of the library sees ---------------------------------------


def test_a_gate_excludes_the_rows_that_were_not_measured(merged):
    """"Not measured" cannot satisfy a condition, whichever way it is stored."""
    merged.where("n", -1.0, 10.0)
    np.testing.assert_array_equal(merged.selection(),
                                  [True, True, True, False, False])


def test_a_histogram_skips_them_too(merged):
    h = merged.histogram("n", bins=4, range=[(0.0, 4.0)])
    assert h.sum(True) == 3.0, "the two unmeasured rows were counted"


def test_select_finite_sees_them(merged):
    """An integer column has no NaN to be non-finite, so this is the only way
    it can say a value is missing -- and it says it as a range."""
    merged.select_finite([merged.find("n")])
    assert merged.n_selected() == 3


# --- through a file ----------------------------------------------------------


def test_the_native_format_keeps_the_ranges(tmp_path, merged):
    path = str(tmp_path / "m.dstore")
    tttrlib.save_store(path, merged)
    c = tttrlib.load_store(path)["n"]

    assert not c.has_mask(), "the file materialised what it did not need to"
    assert c.na_ranges()[0].why == "absent in 'm002.hdf5'"
    np.testing.assert_array_equal(c.mask_numpy(), [True, True, True, False, False])
    assert c.dtype == "int64"


@pytest.mark.skipif(not tttrlib.hdf5_table_available(), reason="built without HDF5")
def test_hdf5_keeps_the_ranges_and_writes_the_mask_as_well(tmp_path, merged):
    """Both, and for different readers. The description carries the reason back
    to tttrlib; the mask dataset is for the consumer this format exists to
    serve, which cannot be assumed to know what an `na` range is."""
    path = str(tmp_path / "m.h5")
    tttrlib.write_hdf5(path, merged, "/t")
    c = tttrlib.read_hdf5(path, "/t")["n"]

    assert c.na_ranges()[0].why == "absent in 'm002.hdf5'"
    assert c.has_mask(), "a foreign reader would see every row as measured"
    np.testing.assert_array_equal(c.mask_numpy(), [True, True, True, False, False])

    h5py = pytest.importorskip("h5py")
    with h5py.File(path, "r") as f:
        np.testing.assert_array_equal(f["/t/n__mask"][:], [1, 1, 1, 0, 0])


def test_csv_materialises_what_it_cannot_carry(tmp_path, merged):
    """CSV has nowhere to put a range, and the alternative is writing the
    zeroes that stand in for the missing rows as though they were measured."""
    path = str(tmp_path / "m.csv")
    tttrlib.write_csv(path, merged)
    rows = open(path).read().strip().split("\n")

    assert rows[0] == "Tau,n"
    assert rows[4] == "3," and rows[5] == "4,", rows
    assert rows[1] == "0,0"


# --- operations that make a range meaningless --------------------------------


def test_take_keeps_the_validity_and_drops_the_ranges(merged):
    """A gather reorders rows, so a range describing the source's rows says
    nothing true about these. The answer survives; the reason does not."""
    t = merged.take([4, 0, 3])
    c = t["n"]
    assert [c.valid(i) for i in range(3)] == [False, True, False]
    assert len(c.na_ranges()) == 0, "a range was carried onto reordered rows"
    assert "na" not in json.loads(c.metadata() or "{}")


def test_compact_keeps_the_validity_too(merged):
    # Tau is [0, 1, 2, 3, 4] and the range is half-open, so this selects rows
    # 2 and 3 -- TWO rows, not the three the comment here used to claim. The
    # assertion read `range(3)`, one past the end of the compacted column, and
    # passed only because the unchecked accessor returned whatever was there
    # and it happened to be False. Bounds-checking `Column::valid` surfaced it
    # (BUGS 2026-08-11).
    merged.where("Tau", 2.0, 4.0)          # rows 2 and 3
    c = merged.compact()["n"]
    assert c.size() == 2
    # row 2 came from m001.ptu and has `n`; row 3 came from m002.hdf5 and does not.
    assert [c.valid(i) for i in range(c.size())] == [True, False]
    assert len(c.na_ranges()) == 0


def test_take_keeps_the_rest_of_the_description(merged):
    """Only `na` is dropped. Units do not stop being true because rows moved."""
    merged["n"].set_units("counts")
    t = merged.take([1, 0])
    assert t["n"].units() == "counts"


# --- writing a range by hand -------------------------------------------------


def test_a_range_can_be_written_by_hand_in_the_short_form():
    """`[[2,4]]` is what a person types; refusing it would make the shorter
    spelling a silent no-op rather than an error."""
    s = store({"Tau": np.arange(5.0)})
    s["Tau"].set_attribute_json("na", "[[2,4]]")
    assert [s["Tau"].valid(i) for i in range(5)] == [True, True, False, False, True]
    assert s["Tau"].na_ranges()[0].why == ""


def test_a_string_that_looks_like_a_range_is_not_one():
    """The set_attribute / set_attribute_json split, in the case it matters
    most: a description is free-form, so storing text under `na` is allowed and
    must not silently blank out two rows."""
    s = store({"Tau": np.arange(5.0)})
    s["Tau"].set_attribute("na", "[[2,4]]")
    assert not s["Tau"].has_missing()
    assert all(s["Tau"].valid(i) for i in range(5))


def test_nonsense_under_na_is_ignored_rather_than_fatal():
    s = store({"Tau": np.arange(5.0)})
    for bad in ('"everything"', '[{"rows":[4,2]}]', '[[1]]', '[{"why":"no rows"}]', '42'):
        s["Tau"].set_attribute_json("na", bad)
        assert not s["Tau"].has_missing(), bad
        assert s["Tau"].attribute_json("na") == bad, "the value was not kept"


def test_add_na_range_appends_rather_than_replacing():
    s = store({"Tau": np.arange(10.0)})
    s["Tau"].add_na_range(1, 3, "first")
    s["Tau"].add_na_range(6, 8, "second")
    assert [r.why for r in s["Tau"].na_ranges()] == ["first", "second"]
    assert [s["Tau"].valid(i) for i in range(10)] == \
        [True, False, False, True, True, True, False, False, True, True]


def test_an_empty_range_is_not_recorded():
    s = store({"Tau": np.arange(4.0)})
    s["Tau"].add_na_range(2, 2, "nothing")
    assert not s["Tau"].has_missing()
    assert s["Tau"].metadata() == ""


# --- the claim, measured -----------------------------------------------------


def test_the_ranges_are_smaller_than_the_bits_would_be():
    """The saving is real and it is not the main reason. Stated as a test so
    the number is checked rather than asserted in a comment."""
    n, k = 100_000, 20
    stores = [store({"Tau": np.zeros(n)} if i else
                    {"Tau": np.zeros(n), "v": np.zeros(n, dtype=np.int8)},
                    "m%03d" % i) for i in range(k)]
    c = tttrlib.concat(stores)["v"]

    a_bit_mask_would_be = n * k // 8
    assert len(c.metadata()) < a_bit_mask_would_be / 100
    assert len(c.na_ranges()) == k - 1
    assert not c.has_mask()

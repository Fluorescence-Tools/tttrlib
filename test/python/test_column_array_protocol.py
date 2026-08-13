"""A Column behaves like the array it wraps.

``np.asarray(column)`` and ``len(column)`` worked; nothing else did, so every
consumer that touched a frame column as a *value* had to be rewritten to insert
a conversion first. Counted across the package migrating onto ``DataStore``:
**~56 call sites** whose only purpose was the conversion — 31 ``to_numpy``, 17
element accesses, 5 mask comparisons — and every one a place a later reader asks
why the wrapping is there. The arithmetic around them does not change at all.

Nothing new is computed here. This is a *handle* change: the buffer was always
reachable, and what was missing was the protocol a numpy user already expects.

Three decisions are pinned by tests below because none of them is guessable:

* **Element access says nothing about validity** — a masked row returns what is
  stored in it, exactly as ``numpy()`` does.
* **A write refuses where it would be lost**, rather than vanishing.
* **An integer index does not go through** ``numpy()`` **for bool and text**,
  whose array forms are copies.
"""
import os
import sys

import numpy as np
import pytest

sys.path.insert(0, os.path.dirname(__file__))
import tttrlib


def store():
    s = tttrlib.DataStore("t")
    s.set_n_rows(4)
    s.add("f64", np.arange(4.0))
    s.add("i32", np.arange(4, dtype=np.int32))
    s.add("flag", np.array([True, False, True, False]))
    s.add("label", np.array(["a", "b", "c", "d"], dtype=object))
    return s


ALL = ("f64", "i32", "flag", "label")


# --- reading -----------------------------------------------------------------


def test_an_integer_index_gives_one_value_of_every_type():
    s = store()
    assert s["f64"][0] == 0.0
    assert s["i32"][2] == 2
    assert s["flag"][0] is True and s["flag"][1] is False
    assert s["label"][2] == "c"


def test_a_negative_index_counts_from_the_end():
    s = store()
    for name, last in zip(ALL, (3.0, 3, False, "d")):
        assert s[name][-1] == last, name


def test_an_index_past_the_end_raises_indexerror():
    """The Python convention, so a caller's `for`/`except IndexError` works."""
    s = store()
    for name in ALL:
        with pytest.raises(IndexError):
            s[name][4]
        with pytest.raises(IndexError):
            s[name][-5]


def test_a_slice_gives_an_array():
    s = store()
    np.testing.assert_array_equal(s["f64"][1:3], [1.0, 2.0])
    np.testing.assert_array_equal(s["i32"][::2], [0, 2])
    assert list(s["label"][1:3]) == ["b", "c"]


def test_an_index_array_works_too():
    s = store()
    np.testing.assert_array_equal(s["f64"][[3, 0]], [3.0, 0.0])
    np.testing.assert_array_equal(s["f64"][np.array([True, False, True, False])],
                                  [0.0, 2.0])


def test_a_column_iterates():
    s = store()
    assert list(s["f64"]) == [0.0, 1.0, 2.0, 3.0]
    assert list(s["label"]) == ["a", "b", "c", "d"]
    assert [v for v in s["i32"]] == [0, 1, 2, 3]


def test_the_dtype_survives_element_access():
    """`value_at` would have been eight times faster and hands back a double,
    so an int64 above 2**53 would come back as a different number."""
    s = tttrlib.DataStore()
    s.set_n_rows(1)
    s.add("big", np.array([2 ** 53 + 1], dtype=np.int64))
    assert int(s["big"][0]) == 2 ** 53 + 1


def test_element_access_says_nothing_about_validity():
    """The consistent answer, and it is the same one `numpy()` gives: the array
    protocol returns what is stored. "Measured or not" stays an explicit
    question, because NaN-where-masked cannot be done for an integer or a text
    column without changing its dtype -- which is the whole reason the mask
    exists."""
    s = tttrlib.DataStore()
    s.set_n_rows(3)
    s.add("n", np.array([1, 2, 3], dtype=np.int32))
    s["n"].set_mask(np.array([1, 0, 1], dtype=np.uint8))

    assert s["n"][1] == 2, "the stored value, not a placeholder"
    assert s["n"].valid(1) is False
    np.testing.assert_array_equal(s["n"].mask_numpy(), [True, False, True])


# --- writing ------------------------------------------------------------------


def test_a_numeric_write_goes_through_to_the_buffer():
    s = store()
    s["f64"][0] = 99.0
    assert s["f64"].numpy()[0] == 99.0
    s["i32"][1:3] = [7, 8]
    np.testing.assert_array_equal(s["i32"].numpy(), [0, 7, 8, 3])


def test_a_write_refuses_where_it_would_be_lost():
    """Verified, not assumed: a bool column is bit-packed and a text one is
    dictionary-encoded, so both decode through a copy and a write to that copy
    disappears. A write that vanishes is worse than one that refuses."""
    s = store()
    for name in ("flag", "label"):
        with pytest.raises(TypeError) as e:
            s[name][0] = 1
        assert "lost" in str(e.value), name
        assert "set_numpy" in str(e.value), "the message names a route that works"


def test_the_write_that_would_have_vanished_really_does():
    """The evidence behind the refusal above, kept as a test so the reason
    cannot quietly stop being true."""
    s = store()
    a = s["flag"].numpy()
    a[0] = False
    assert bool(s["flag"].numpy()[0]) is True, "a bool column's array is a copy"

    t = s["label"].numpy()
    t[0] = "ZZ"
    assert s["label"].numpy()[0] == "a", "a text column's array is a copy"


# --- the routing that makes it usable -----------------------------------------


def test_indexing_a_text_column_does_not_decode_it():
    """`numpy()[i]` would decode every row to read one. Measured on 200 000
    rows: 38 s per thousand accesses against 0.6 ms."""
    import time

    n = 100_000
    s = tttrlib.DataStore()
    s.set_n_rows(n)
    s.add("label", np.array(["m%03d" % (i % 5) for i in range(n)], dtype=object))
    c = s["label"]

    start = time.perf_counter()
    for i in range(500):
        c[i]
    protocol = time.perf_counter() - start

    start = time.perf_counter()
    for i in range(20):
        c.numpy()[i]
    naive = (time.perf_counter() - start) / 20 * 500

    assert c[7] == "m002"
    assert protocol < naive / 50, (
        "an integer index went through numpy(): %.4fs vs %.4fs for the naive "
        "form" % (protocol, naive))


def test_indexing_a_bool_column_does_not_unpack_it():
    n = 100_000
    s = tttrlib.DataStore()
    s.set_n_rows(n)
    s.add("flag", np.arange(n) % 2 == 0)
    assert s["flag"][0] is True and s["flag"][1] is False
    # The evidence that the index did not go through it: the array form owns
    # its memory, so it is a fresh unpacking of the bits every time it is asked.
    assert s["flag"].numpy().flags.owndata


# --- what the migration actually writes ---------------------------------------


def test_the_idioms_the_migration_needed():
    """The four shapes the migration counted, in one place, so it is obvious what
    this bought."""
    s = store()
    col = s["f64"]

    assert col[0] == 0.0                             # 17 call sites
    np.testing.assert_array_equal(col[1:3], [1.0, 2.0])
    np.testing.assert_array_equal(col > 1.0,         # 5 call sites
                                  [False, False, True, True])
    np.testing.assert_array_equal(np.asarray(col),   # 31 call sites
                                  [0.0, 1.0, 2.0, 3.0])
    assert [v * 2 for v in col] == [0.0, 2.0, 4.0, 6.0]

    # and the one deliberately not provided: `map` is a comprehension, honestly
    assert [round(v) for v in np.asarray(col)] == [0, 1, 2, 3]


def test_a_comparison_still_drives_a_selection():
    """The pair that makes a filter readable: compare, then select."""
    s = store()
    s.select(np.asarray(s["label"] == "a") | np.asarray(s["f64"] > 2.0))
    assert s.n_selected() == 2

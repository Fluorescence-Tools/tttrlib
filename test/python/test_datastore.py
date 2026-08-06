"""The columnar store: typed columns, masks, strings, and histograms from it.

This is the piece that lets tttrlib be a data explorer's in-memory backend
rather than only a photon reader, so the assertions are about the properties
that makes it useful: dtypes are not widened, strings cost their dictionary and
not their length, a selection cannot leak into a plot, and reading a column
does not copy it.
"""
import numpy as np
import pytest
import tttrlib


@pytest.fixture
def store():
    s = tttrlib.DataStore()
    s.set_n_rows(6)
    s.add("tau", np.array([0.5, 1.5, 2.5, 3.5, 0.5, 1.5], dtype=np.float32))
    s.add("n", np.array([10, 20, 30, 40, 50, 60], dtype=np.int32))
    s.add("big", np.array([1, 2, 3, 4, 5, 6], dtype=np.int64))
    s.add("label", ["red", "green", "red", "blue", "red", "green"])
    s.add("ok", np.array([True, True, False, True, True, True]))
    return s


# --- types ------------------------------------------------------------------

def test_dtypes_are_kept_not_widened(store):
    """A float32 column stays float32. Widening it to float64 would double the
    memory of the largest thing in the store for nothing."""
    assert store["tau"].numpy().dtype == np.float32
    assert store["n"].numpy().dtype == np.int32
    assert store["big"].numpy().dtype == np.int64
    assert store["ok"].numpy().dtype == bool


def test_numeric_columns_are_zero_copy(store):
    a = store["tau"].numpy()
    b = store["tau"].numpy()
    assert np.shares_memory(a, b)
    a[0] = 42.0
    assert store["tau"].numpy()[0] == 42.0


def test_a_column_view_keeps_its_store_alive():
    import gc
    s = tttrlib.DataStore()
    s.set_n_rows(3)
    s.add("x", np.array([1.0, 2.0, 3.0]))
    v = s["x"].numpy()
    del s
    gc.collect()
    # A Column proxy is a borrowed reference into the store, so the chain has
    # to be view -> column -> store or this reads freed memory.
    assert v.sum() == 6.0, "the buffer was freed under the view"


def test_a_held_column_survives_later_columns_being_added():
    """A Column is a borrowed reference into the store's column container, and a
    table adapter fetches its columns once and keeps them. Adding a column must
    therefore not move the ones already handed out."""
    s = tttrlib.DataStore()
    s.set_n_rows(5)
    held = s.add("f", np.arange(5, dtype=np.float64))
    for i in range(8):
        s.add(f"x{i}", np.arange(5, dtype=np.float64))
    assert held.name() == "f", "the held column moved out from under the proxy"
    assert np.array_equal(held.numpy(), np.arange(5, dtype=np.float64))


def test_store_reports_its_own_size(store):
    r = store.memory_report()
    assert r["total"] == sum(v for k, v in r.items() if k != "total")
    assert r["tau"] == 24, "6 float32 rows"
    assert r["big"] == 48, "6 int64 rows"


# --- strings ----------------------------------------------------------------

def test_strings_are_dictionary_encoded(store):
    c = store["label"]
    assert c.labels() == ["red", "green", "blue"]
    assert list(np.asarray(c.codes())) == [0, 1, 0, 2, 0, 1]
    assert list(c.numpy()) == ["red", "green", "red", "blue", "red", "green"]


def test_a_repeated_string_column_costs_its_dictionary_not_its_length():
    """The whole point of dictionary encoding: a million rows of a few labels."""
    s = tttrlib.DataStore()
    labels = ["alpha", "beta", "gamma"]
    n = 100000
    s.add("k", [labels[i % 3] for i in range(n)])
    s.set_n_rows(n)
    # 4 bytes of code per row, plus three short strings. nbytes reports
    # CAPACITY, and a vector grown by push_back holds up to 2x what it needs --
    # which is exactly why shrink_to_fit exists and is worth calling after a
    # bulk load.
    assert len(s["k"].dictionary()) == 3
    assert s["k"].nbytes() < 2 * 4 * n + 4096
    s["k"].shrink_to_fit()
    assert s["k"].nbytes() < 4 * n + 4096


def test_a_string_column_histograms_by_its_labels(store):
    h = store.histogram("label")
    assert list(h.view()) == [3.0, 2.0, 1.0]      # red, green, blue
    assert h.sum(False) == 6.0


# --- masks and selection ----------------------------------------------------

def test_selection_is_honoured_by_a_histogram(store):
    store.select(np.array([True, True, False, False, True, False]))
    assert store.n_selected() == 3
    h = store.histogram("tau", bins=4, range=[(0.0, 4.0)])
    assert h.sum(True) == 3.0, "deselected rows must not reach the histogram"


def test_clearing_the_selection_restores_everything(store):
    store.select(np.array([True] + [False] * 5))
    assert store.n_selected() == 1
    store.select(None)
    assert store.n_selected() == 6
    assert store.histogram("tau", bins=4, range=[(0.0, 4.0)]).sum(True) == 6.0


def test_selection_round_trips(store):
    m = np.array([True, False, True, False, True, False])
    store.select(m)
    assert np.array_equal(store.selection(), m)


def test_non_finite_values_are_masked_not_binned():
    """A NaN means "not measured". Left unmasked it lands in a flow bin and is
    counted as data that merely fell off the axis."""
    s = tttrlib.DataStore()
    s.set_n_rows(5)
    s.add("x", np.array([1.0, np.nan, 2.0, np.inf, 3.0]))
    assert s["x"].mask_non_finite() == 2
    h = s.histogram("x", bins=4, range=[(0.0, 4.0)])
    assert h.sum(True) == 3.0
    assert np.array_equal(s["x"].mask_numpy(), [True, False, True, False, True])


def test_a_bit_mask_really_is_one_bit_per_row():
    s = tttrlib.DataStore()
    n = 800000
    s.set_n_rows(n)
    s.add("x", np.zeros(n, dtype=np.float32))
    s.select(np.ones(n, dtype=bool))
    # 800k bits is 100 kB; a bool array would be 800 kB
    assert s.nbytes() - s["x"].nbytes() < n / 8 + 1024


# --- the store as a whole ---------------------------------------------------

def test_lookup_by_name_and_index(store):
    assert store["tau"].name() == "tau"
    assert store[0].name() == "tau"
    assert "label" in store
    assert "nope" not in store
    assert store.names == ["tau", "n", "big", "label", "ok"]


def test_a_wrong_length_column_is_reported_not_padded():
    s = tttrlib.DataStore()
    s.set_n_rows(10)
    s.add("x", np.zeros(3))
    assert list(s.inconsistent_columns()) == ["x"]


def test_duplicate_column_names_are_refused(store):
    with pytest.raises(Exception):
        store.add("tau", np.zeros(6))


def test_two_columns_make_a_2d_histogram(store):
    h = store.histogram("tau", "n", bins=(4, 3),
                        range=[(0.0, 4.0), (0.0, 60.0)])
    assert h.shape == (4, 3)
    assert h.sum(True) == 6.0


def test_weighting_by_a_column(store):
    h = store.histogram("tau", bins=4, range=[(0.0, 4.0)], weight="n")
    assert h.sum(True) == float(np.sum([10, 20, 30, 40, 50, 60]))


def test_histogram_of_an_unknown_column_raises(store):
    with pytest.raises(KeyError):
        store.histogram("nope")


# --- selections -------------------------------------------------------------

def test_where_narrows_the_selection():
    rng = np.random.default_rng(4)
    n = 10000
    E = rng.uniform(0, 1, n)
    S = rng.uniform(0, 1, n)
    s = tttrlib.DataStore()
    s.set_n_rows(n)
    s.add("E", E)
    s.add("S", S)

    s.where("E", 0.2, 0.8)
    assert s.n_selected() == int(((E >= 0.2) & (E < 0.8)).sum())

    s.where("S", 0.3, 0.7, how="and")
    expected = ((E >= 0.2) & (E < 0.8)) & ((S >= 0.3) & (S < 0.7))
    assert s.n_selected() == int(expected.sum())
    assert np.array_equal(s.selection(), expected)


def test_selection_combinators():
    x = np.arange(100, dtype=np.float64)
    s = tttrlib.DataStore()
    s.set_n_rows(100)
    s.add("x", x)

    s.where("x", 0, 50).where("x", 40, 60, how="or")
    assert s.n_selected() == 60

    s.where("x", 0, 50).where("x", 40, 60, how="andnot")
    assert s.n_selected() == 40, "andnot removes the overlap"

    s.select_all()
    assert s.n_selected() == 100
    s.invert_selection()
    assert s.n_selected() == 0


def test_where_equals_for_categories():
    s = tttrlib.DataStore()
    s.set_n_rows(6)
    s.add("label", ["a", "b", "a", "c", "a", "b"])
    codes = {lab: i for i, lab in enumerate(s["label"].labels())}
    s.where("label", equals=codes["a"])
    assert s.n_selected() == 3


def test_a_missing_value_satisfies_no_condition():
    s = tttrlib.DataStore()
    s.set_n_rows(5)
    s.add("x", np.array([1.0, np.nan, 2.0, np.inf, 3.0]))
    s["x"].mask_non_finite()
    s.where("x", 0.0, 10.0)
    assert s.n_selected() == 3, "NaN and inf cannot be inside a range"


def test_where_finite_drops_unmeasured_rows():
    s = tttrlib.DataStore()
    s.set_n_rows(5)
    s.add("a", np.array([1.0, np.nan, 3.0, 4.0, 5.0]))
    s.add("b", np.array([1.0, 2.0, 3.0, np.inf, 5.0]))
    s.select_all()
    s.where_finite(["a", "b"])
    assert s.n_selected() == 3


def test_a_selection_restricts_the_histogram():
    rng = np.random.default_rng(5)
    n = 20000
    x = rng.uniform(0, 10, n)
    s = tttrlib.DataStore()
    s.set_n_rows(n)
    s.add("x", x)
    s.where("x", 2.0, 6.0)
    h = s.histogram("x", bins=8, range=[(0.0, 10.0)])
    assert h.sum(True) == s.n_selected()


def test_selection_is_bit_packed():
    n = 1_000_000
    s = tttrlib.DataStore()
    s.set_n_rows(n)
    s.add("x", np.zeros(n, dtype=np.float32))
    s.where("x", -1.0, 1.0)
    # one bit per row, not one byte
    assert s.nbytes() - s["x"].nbytes() < n / 8 + 4096

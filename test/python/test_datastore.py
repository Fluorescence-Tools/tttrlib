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

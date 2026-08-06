"""A table in HDF5: one dataset per column, straight into a DataStore.

The shape a DataStore already has, so a file written this way loads with no
conversion and no intermediate copy. What these check is the part that makes
that worth doing rather than reading the table through a DataFrame: the types
survive, the missing values survive, and the names survive.
"""
import numpy as np
import pytest
import tttrlib

pytestmark = pytest.mark.skipif(not tttrlib.hdf5_table_available(),
                                reason="built without HDF5")

N = 500


@pytest.fixture
def store():
    rng = np.random.default_rng(4)
    s = tttrlib.DataStore("round trip")
    s.set_n_rows(N)
    s.add("f64", rng.normal(size=N))
    s.add("f32", rng.normal(size=N).astype(np.float32))
    s.add("i64", rng.integers(-1000, 1000, N).astype(np.int64))
    s.add("i32", rng.integers(0, 100, N).astype(np.int32))
    s.add("i16", rng.integers(0, 300, N).astype(np.int16))
    s.add("u8", rng.integers(0, 250, N).astype(np.uint8))
    s.add("text", np.array(["label %d" % (i % 11) for i in range(N)], dtype=object))
    return s


def test_every_column_keeps_its_own_type(store, tmp_path):
    """The reason not to go through a DataFrame. A float32 column read back as
    float64 is twice the memory for no more information, and an integer column
    with one missing value comes back as floats -- neither recoverable after."""
    path = tmp_path / "t.h5"
    assert tttrlib.write_hdf5(str(path), store)
    back = tttrlib.read_hdf5(str(path))

    assert back.n_rows() == store.n_rows()
    assert back.names == store.names
    for name in store.names:
        original, restored = store[name].numpy(), back[name].numpy()
        assert restored.dtype == original.dtype, name
        assert np.array_equal(original, restored), name


def test_a_missing_value_survives_in_an_integer_column(store, tmp_path):
    """A float column can say "not measured" with a NaN. An integer column has
    nothing to spare, so the mask is the only way -- and losing it on the way
    through a file turns a missing count into a real zero."""
    store["i32"].mask_non_finite()          # a no-op on an integer column
    mask = np.ones(N, dtype=np.uint8)
    mask[::7] = 0
    store["i32"].set_mask(mask)

    path = tmp_path / "t.h5"
    tttrlib.write_hdf5(str(path), store)
    back = tttrlib.read_hdf5(str(path))
    np.testing.assert_array_equal(back["i32"].mask_numpy(), mask.astype(bool))
    assert back["f64"].mask_numpy() is None, "a column with no mask gains none"


def test_a_nan_survives_as_a_nan(store, tmp_path):
    values = store["f64"].numpy()
    values[3] = np.nan
    values[9] = np.inf
    path = tmp_path / "t.h5"
    tttrlib.write_hdf5(str(path), store)
    back = tttrlib.read_hdf5(str(path))
    assert np.isnan(back["f64"].numpy()[3])
    assert np.isinf(back["f64"].numpy()[9])


def test_a_column_name_with_a_slash_in_it(tmp_path):
    """"/" separates path components in HDF5, so a column called "Sg/Sr" -- an
    ordinary name for a signal ratio -- becomes a GROUP called "Sg" holding a
    dataset called "Sr", and the column does not come back at all."""
    s = tttrlib.DataStore()
    s.set_n_rows(4)
    s.add("Sg/Sr", np.array([1.0, 2.0, 3.0, 4.0]))
    s.add("100% of it", np.array([5.0, 6.0, 7.0, 8.0]))
    path = tmp_path / "t.h5"
    tttrlib.write_hdf5(str(path), s)

    back = tttrlib.read_hdf5(str(path))
    assert back.names == ["Sg/Sr", "100% of it"]
    assert np.array_equal(back["Sg/Sr"].numpy(), [1.0, 2.0, 3.0, 4.0])
    assert np.array_equal(back["100% of it"].numpy(), [5.0, 6.0, 7.0, 8.0])


def test_the_column_order_is_the_order_it_was_written(tmp_path):
    """Not alphabetical, which is how HDF5 lists a group. A table whose columns
    come back in a different order is a table whose column indices all moved."""
    s = tttrlib.DataStore()
    s.set_n_rows(3)
    for name in ["zulu", "alpha", "mike", "bravo"]:
        s.add(name, np.arange(3, dtype=np.float64))
    path = tmp_path / "t.h5"
    tttrlib.write_hdf5(str(path), s)
    assert list(tttrlib.read_hdf5_table_columns(str(path))) == \
        ["zulu", "alpha", "mike", "bravo"]
    assert tttrlib.read_hdf5(str(path)).names == ["zulu", "alpha", "mike", "bravo"]


def test_only_the_selected_rows_are_written(store, tmp_path):
    """Exporting a gated subset should not need an intermediate table."""
    store.where("i32", 0, 50)
    keep = store.selection()
    path = tmp_path / "subset.h5"
    tttrlib.write_hdf5(str(path), store)

    back = tttrlib.read_hdf5(str(path))
    assert back.n_rows() == int(keep.sum()) < N
    for name in ("f64", "i32", "u8"):
        np.testing.assert_array_equal(back[name].numpy(), store[name].numpy()[keep])
    assert [back["text"].numpy()[i] for i in range(3)] == \
        [store["text"].numpy()[j] for j in np.flatnonzero(keep)[:3]]


def test_a_group_other_than_the_root(store, tmp_path):
    path = tmp_path / "grouped.h5"
    tttrlib.write_hdf5(str(path), store, group="/results")
    assert tttrlib.read_hdf5(str(path), "/results").n_rows() == N
    assert list(tttrlib.read_hdf5_table_columns(str(path), "/results")) == store.names


def test_a_file_that_is_not_a_table(tmp_path):
    missing = tmp_path / "nope.h5"
    with pytest.raises(Exception):
        tttrlib.read_hdf5(str(missing))
    assert list(tttrlib.read_hdf5_table_columns(str(missing))) == []


def test_an_empty_table(tmp_path):
    s = tttrlib.DataStore()
    s.set_n_rows(0)
    s.add("x", np.array([], dtype=np.float64))
    path = tmp_path / "empty.h5"
    assert tttrlib.write_hdf5(str(path), s)
    back = tttrlib.read_hdf5(str(path))
    assert back.n_rows() == 0 and back.names == ["x"]


def test_the_table_histograms_without_being_converted(store, tmp_path):
    """The point of the whole exercise: what comes off disk is what the
    histogram fills out of."""
    path = tmp_path / "t.h5"
    tttrlib.write_hdf5(str(path), store)
    back = tttrlib.read_hdf5(str(path))
    h = back.histogram("f64", bins=16, range=[(-3.0, 3.0)])
    reference, _ = np.histogram(store["f64"].numpy(), bins=16, range=(-3.0, 3.0))
    assert np.array_equal(h.view(), reference)

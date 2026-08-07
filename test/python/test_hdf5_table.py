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


def test_a_gated_integer_column_keeps_values_a_double_cannot_hold(tmp_path):
    """The gated path used to funnel every type through a double.

    A double holds integers exactly only up to 2**53. Above that the gather
    silently rounded, so a gated int64 column -- macro times, event indices,
    anything counted rather than measured -- came back with different numbers
    and nothing said so. The ungated path was always safe, because it writes
    the column's own buffer, which is why this went unnoticed: the corruption
    appeared only once a selection was set.
    """
    big = np.array([2**53 + 1, 2**53 + 3, 2**62 - 1, 7], dtype=np.int64)
    s = tttrlib.DataStore()
    s.set_n_rows(len(big))
    s.add("big", big)
    s.add("keep", np.array([1, 1, 1, 0], dtype=np.uint8))
    s.where("keep", 0.5, 1.5)

    path = tmp_path / "big.h5"
    assert tttrlib.write_hdf5(str(path), s)
    back = tttrlib.read_hdf5(str(path))

    assert back["big"].numpy().dtype == np.int64
    np.testing.assert_array_equal(back["big"].numpy(), big[:3])


def test_a_gated_unsigned_column_keeps_the_top_of_its_range(tmp_path):
    """Same defect, and uint64 loses more of its range to a double than int64."""
    big = np.array([2**64 - 1, 2**63 + 5, 2**53 + 1], dtype=np.uint64)
    s = tttrlib.DataStore()
    s.set_n_rows(3)
    s.add("big", big)
    s.add("keep", np.array([1, 0, 1], dtype=np.uint8))
    s.where("keep", 0.5, 1.5)

    path = tmp_path / "ubig.h5"
    assert tttrlib.write_hdf5(str(path), s)
    back = tttrlib.read_hdf5(str(path))

    assert back["big"].numpy().dtype == np.uint64
    np.testing.assert_array_equal(back["big"].numpy(), big[[0, 2]])


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


def test_compression_changes_the_file_and_not_the_contents(store, tmp_path):
    """Compression is a storage decision, so it must be invisible on the way back.

    Pinned because the level is about to change: level 4 costs roughly thirty
    times the write for eight percent of the size, on tables that are written
    once and read many times.
    """
    none, most = tmp_path / "c0.h5", tmp_path / "c9.h5"
    assert tttrlib.write_hdf5(str(none), store, compression=0)
    assert tttrlib.write_hdf5(str(most), store, compression=9)

    a, b = tttrlib.read_hdf5(str(none)), tttrlib.read_hdf5(str(most))
    assert a.names == b.names == store.names
    for name in store.names:
        if name == "text":
            continue
        assert a[name].numpy().dtype == b[name].numpy().dtype, name
        np.testing.assert_array_equal(a[name].numpy(), b[name].numpy())


def test_a_bool_column_comes_back_as_uint8(tmp_path):
    """A known gap, pinned so it is visible rather than folklore.

    HDF5 has no boolean; the writer stores a byte per row as U8LE and the
    reader has no way to tell that byte apart from a real uint8 column. Fixing
    it needs somewhere in the file to say "this was a bool", which is a format
    change and its own decision. Until then, a round trip widens the type --
    the values are right, `dtype` is not.
    """
    s = tttrlib.DataStore()
    s.set_n_rows(6)
    s.add("flag", np.array([True, False, True, True, False, True]))

    path = tmp_path / "flag.h5"
    assert tttrlib.write_hdf5(str(path), s)
    back = tttrlib.read_hdf5(str(path))

    np.testing.assert_array_equal(back["flag"].numpy().astype(bool),
                                  s["flag"].numpy().astype(bool))
    assert back["flag"].numpy().dtype == np.uint8, "if this now says bool, the gap closed"


def test_a_fixed_length_string_column_written_by_something_else(tmp_path):
    """NumPy's "S8" is a fixed-width dataset, not the variable-length one we write.

    A file from h5py or a MATLAB export arrives this way, and the trailing NULs
    are padding rather than content -- read as-is they end up inside the string.
    """
    h5py = pytest.importorskip("h5py")
    path = tmp_path / "fixed.h5"
    with h5py.File(str(path), "w") as f:
        f.create_dataset("label", data=np.array([b"red", b"green", b"blue"], dtype="S8"))

    back = tttrlib.read_hdf5(str(path))
    assert back.n_rows() == 3
    assert [back["label"].numpy()[i] for i in range(3)] == ["red", "green", "blue"]


def test_the_by_value_read_returns_the_same_table(store, tmp_path):
    """read_hdf5_table is the overload the bindings avoid -- it copies the table
    at the moment it is largest -- but it is public and nothing covered it."""
    path = tmp_path / "t.h5"
    assert tttrlib.write_hdf5(str(path), store)

    back = tttrlib.read_hdf5_table(str(path))
    assert back.n_rows() == N
    assert [back.column(i).name() for i in range(back.n_columns())] == store.names
    np.testing.assert_array_equal(back["f64"].numpy(), store["f64"].numpy())


def test_the_table_histograms_without_being_converted(store, tmp_path):
    """The point of the whole exercise: what comes off disk is what the
    histogram fills out of."""
    path = tmp_path / "t.h5"
    tttrlib.write_hdf5(str(path), store)
    back = tttrlib.read_hdf5(str(path))
    h = back.histogram("f64", bins=16, range=[(-3.0, 3.0)])
    reference, _ = np.histogram(store["f64"].numpy(), bins=16, range=(-3.0, 3.0))
    assert np.array_equal(h.view(), reference)


# --- a text column keeps its dictionary through the file -----------------------
#
# Writing one string per row throws away the encoding at the file boundary, and
# on a burst table that is the whole comparison: a million rows drawn from four
# labels are 4 MB of codes and 44 MB of strings. Measured on that table, storing
# the codes took the file from 96.0 MB to 60.0 MB -- from *above* the DataFrame
# it replaces (72.6 MB) to below it -- and the read from 0.191 s to 0.011 s.


def test_a_text_column_is_stored_as_codes_and_a_dictionary(store, tmp_path):
    """The layout, not just the round trip: what makes the file small is that
    the labels are in an attribute and the rows are int32 codes."""
    h5py = pytest.importorskip("h5py")
    path = tmp_path / "t.h5"
    assert tttrlib.write_hdf5(str(path), store)

    with h5py.File(str(path), "r") as f:
        text = f["text"]
        assert text.dtype == np.int32
        labels = [s.decode() if isinstance(s, bytes) else s
                  for s in text.attrs["dictionary"]]
        assert sorted(labels) == sorted({"label %d" % (i % 11) for i in range(N)})
        assert set(text[:]) <= set(range(len(labels)))


def test_a_text_column_survives_the_file(store, tmp_path):
    path = tmp_path / "t.h5"
    assert tttrlib.write_hdf5(str(path), store)
    back = tttrlib.read_hdf5(str(path))

    assert back["text"].dtype == "str"
    assert list(back["text"].numpy()) == list(store["text"].numpy())


def test_a_dictionary_encoded_column_is_much_smaller(tmp_path):
    """The measurement the change exists for, in miniature: repeated labels cost
    a code each rather than a string each."""
    n = 20_000
    s = tttrlib.DataStore()
    s.add("text", np.array(["Green", "Red", "Yellow", "Blue"] * (n // 4), dtype=object))
    path = tmp_path / "codes.h5"
    assert tttrlib.write_hdf5(str(path), s)
    # 4 bytes a row plus a four-label dictionary, against ~6 bytes of string
    # plus a 16-byte variable-length descriptor a row.
    assert path.stat().st_size < n * 6


def test_a_selection_writes_the_labels_of_the_rows_it_kept(tmp_path):
    """The gathered path. The dictionary goes out whole -- an unused label costs
    nothing -- but the codes must be the selected rows' own."""
    s = tttrlib.DataStore()
    s.add("text", np.array(["a", "b", "c", "d"], dtype=object))
    s.set_row_mask(np.array([False, True, False, True]))
    path = tmp_path / "gated.h5"
    assert tttrlib.write_hdf5(str(path), s)

    back = tttrlib.read_hdf5(str(path))
    assert back.n_rows() == 2
    assert list(back["text"].numpy()) == ["b", "d"]


def test_a_text_column_with_no_rows_is_still_a_text_column(tmp_path):
    """An empty dictionary is not a missing one. Reading this back as integers
    would change a column's type on a table that merely has nothing in it."""
    s = tttrlib.DataStore()
    s.add("text", np.array([], dtype=object))
    path = tmp_path / "empty.h5"
    assert tttrlib.write_hdf5(str(path), s)

    back = tttrlib.read_hdf5(str(path))
    assert back["text"].dtype == "str"


def test_a_masked_text_column_keeps_both(store, tmp_path):
    """The mask is written beside the codes, the same as for any other column."""
    valid = np.ones(N, dtype=np.uint8)
    valid[3] = 0
    store["text"].set_mask(valid)
    path = tmp_path / "t.h5"
    assert tttrlib.write_hdf5(str(path), store)

    back = tttrlib.read_hdf5(str(path))
    assert back["text"].dtype == "str"
    assert not back["text"].valid(3)
    assert back["text"].valid(4)


def test_variable_length_strings_written_by_something_else_still_read(tmp_path):
    """The layout this library wrote until the codes replaced it, and the one
    every other HDF5 producer writes. It has to stay readable."""
    h5py = pytest.importorskip("h5py")
    path = tmp_path / "vlen.h5"
    with h5py.File(str(path), "w") as f:
        f.create_dataset("label", data=np.array(["red", "green", "blue"], dtype=object),
                         dtype=h5py.string_dtype())

    back = tttrlib.read_hdf5(str(path))
    assert back.n_rows() == 3
    assert list(back["label"].numpy()) == ["red", "green", "blue"]


def test_codes_that_do_not_index_their_dictionary_read_as_integers(tmp_path):
    """A named decline. Taking these as text would build a column whose every
    access is out of bounds, so the dataset is read as what it literally holds."""
    h5py = pytest.importorskip("h5py")
    path = tmp_path / "bad.h5"
    with h5py.File(str(path), "w") as f:
        ds = f.create_dataset("label", data=np.array([0, 7, 1], dtype=np.int32))
        ds.attrs["dictionary"] = np.array(["red", "green"], dtype=object)

    back = tttrlib.read_hdf5(str(path))
    assert back["label"].dtype == "int32"
    np.testing.assert_array_equal(back["label"].numpy(), [0, 7, 1])


# -- the column's description -------------------------------------------------


def test_a_column_description_survives_hdf5(tmp_path):
    """It did not, until this: the two formats disagreed about what a column
    is. A lifetime written in nanoseconds through HDF5 came back saying nothing
    about nanoseconds, so a store that round-tripped through a file the caller
    chose lost what the one they did not choose kept."""
    import json

    s = tttrlib.DataStore()
    s.set_n_rows(4)
    s.add("Tau", np.arange(4.0))
    s.add("n", np.arange(4, dtype=np.int32))
    s["Tau"].set_units("nanoseconds")
    s["Tau"].set_attribute("of", "run.ptu")

    path = str(tmp_path / "described.h5")
    tttrlib.write_hdf5(path, s, "/t")
    back = tttrlib.read_hdf5(path, "/t")

    assert back["Tau"].units() == "nanoseconds"
    assert json.loads(back["Tau"].metadata()) == json.loads(s["Tau"].metadata())
    assert back["n"].metadata() == "", "nothing acquires a description by being written"


def test_the_two_formats_carry_the_same_description(tmp_path):
    """The invariant that makes them interchangeable: what a caller gets back
    must not depend on which format they picked."""
    import json

    s = tttrlib.DataStore()
    s.set_n_rows(3)
    s.add("Tau", np.arange(3.0))
    s["Tau"].set_units("ns")
    s["Tau"].set_attribute_json("na", "[[1,2]]")
    s["Tau"].set_attribute_json("n", "9007199254740993")   # 2**53 + 1

    h5, dstore = str(tmp_path / "a.h5"), str(tmp_path / "a.dstore")
    tttrlib.write_hdf5(h5, s, "/t")
    tttrlib.save_store(dstore, s)

    via_hdf5 = tttrlib.read_hdf5(h5, "/t")["Tau"].metadata()
    via_store = tttrlib.load_store(dstore)["Tau"].metadata()
    assert json.loads(via_hdf5) == json.loads(via_store)
    # types survive both, which is what msgpack storage is for
    assert json.loads(via_hdf5)["n"] == 9007199254740993
    assert json.loads(via_hdf5)["na"] == [[1, 2]]


def test_a_name_hdf5_cannot_hold_as_a_link_comes_back_whole(tmp_path):
    """A `/` in a column name is a group separator to HDF5, so the dataset is
    written under an encoded name. The description carries the true one."""
    s = tttrlib.DataStore()
    s.set_n_rows(2)
    s.add("Sg/Sr", np.arange(2.0))
    s["Sg/Sr"].set_units("ratio")

    path = str(tmp_path / "slash.h5")
    tttrlib.write_hdf5(path, s, "/t")
    back = tttrlib.read_hdf5(path, "/t")
    assert back.names == ["Sg/Sr"]
    assert back["Sg/Sr"].units() == "ratio"

"""The native store file: a DataStore saved and reloaded, unchanged.

HDF5 is for getting a table out to something that is not tttrlib. This is for
the job that happens far more often -- save this store, load it back -- and for
that everything HDF5 does is overhead: chunking, a deflate pipeline, a link
index, an attribute system, a conversion layer between disk and memory. None of
it is needed to put a column of doubles somewhere and get the same column back.

So the contract here is stricter than HDF5's and simpler to state:
``load(save(s))`` is ``s``. These check the parts of "is" that a format can get
wrong.
"""
import os

import numpy as np
import pytest
import tttrlib


@pytest.fixture
def tree():
    """One of everything, three levels deep."""
    rng = np.random.default_rng(7)
    s = tttrlib.DataStore("acquisition")
    s.set_n_rows(64)
    s.add("f64", rng.normal(size=64))
    s.add("f32", rng.normal(size=64).astype(np.float32))
    s.add("i64", rng.integers(-1000, 1000, 64).astype(np.int64))
    s.add("u64", (np.arange(64, dtype=np.uint64) + 2**63))
    s.add("i16", rng.integers(0, 300, 64).astype(np.int16))
    s.add("u8", rng.integers(0, 250, 64).astype(np.uint8))
    s.add("flag", rng.random(64) > 0.5)
    s.add("text", np.array(["label %d" % (i % 5) for i in range(64)], dtype=object))

    results = s.add_group("results")
    results.set_n_rows(4096)
    results.add("Tau", np.linspace(0.5, 5.0, 4096))
    meta = s.add_group("meta")
    meta.set_n_rows(1)
    meta.add("source", np.array(["run.ptu"], dtype=object))
    deep = s.ensure_group("a/b")
    deep.set_n_rows(3)
    deep.add("x", np.arange(3.0))
    return s


def roundtrip(store, tmp_path, name="t.dstore"):
    path = str(tmp_path / name)
    assert tttrlib.save_store(path, store)
    return tttrlib.load_store(path), path


def same_table(a, b):
    assert a.names == b.names
    assert a.n_rows() == b.n_rows()
    assert a.label() == b.label()
    for name in a.names:
        original, restored = a[name], b[name]
        if original.dtype == "str":
            assert [original.numpy()[i] for i in range(a.n_rows())] == \
                   [restored.numpy()[i] for i in range(b.n_rows())], name
            assert list(original.dictionary()) == list(restored.dictionary()), name
        else:
            assert restored.numpy().dtype == original.numpy().dtype, name
            np.testing.assert_array_equal(restored.numpy(), original.numpy())
        if original.has_mask():
            np.testing.assert_array_equal(restored.mask_numpy(), original.mask_numpy())
        else:
            assert not restored.has_mask(), name


def test_a_whole_tree_comes_back_equal(tree, tmp_path):
    """The one property everything else supports."""
    back, _ = roundtrip(tree, tmp_path)
    same_table(tree, back)
    assert back.group_paths() == tree.group_paths()
    for path in tree.group_paths():
        same_table(tree.group(path), back.group(path))


def test_every_dtype_survives_including_bool(tree, tmp_path):
    """Bool is the one HDF5 cannot do: it has no boolean type, so that path
    gives back uint8. Here the column comes back a bool column."""
    back, _ = roundtrip(tree, tmp_path)
    assert back["flag"].numpy().dtype == np.bool_
    np.testing.assert_array_equal(back["flag"].numpy(), tree["flag"].numpy())


def test_a_uint64_above_the_double_limit_is_exact(tree, tmp_path):
    back, _ = roundtrip(tree, tmp_path)
    np.testing.assert_array_equal(back["u64"].numpy(), tree["u64"].numpy())
    assert back["u64"].numpy()[0] == 2**63


def test_the_row_selection_is_saved_rather_than_applied(tree, tmp_path):
    """The deliberate difference from write_hdf5, which exports the subset.

    This format's job is fidelity, so a gated store comes back gated -- with
    every row still there and the gate still set. Dropping the unselected rows
    would make save/load lossy for exactly the tables people gate.
    """
    tree.where("f64", -0.5, 0.5)
    keep = tree.selection()
    assert 0 < int(keep.sum()) < 64

    back, _ = roundtrip(tree, tmp_path)
    assert back.n_rows() == 64
    np.testing.assert_array_equal(back.selection(), keep)
    np.testing.assert_array_equal(back["f64"].numpy(), tree["f64"].numpy())


def test_a_selection_inside_a_group_survives_too(tree, tmp_path):
    tree.group("results").where("Tau", 1.0, 2.0)
    keep = tree.group("results").selection()
    back, _ = roundtrip(tree, tmp_path)
    np.testing.assert_array_equal(back.group("results").selection(), keep)


def test_a_validity_mask_survives(tmp_path):
    """The reason an integer column can have missing values at all."""
    s = tttrlib.DataStore()
    s.set_n_rows(20)
    s.add("i32", np.arange(20, dtype=np.int32))
    mask = np.ones(20, dtype=np.uint8)
    mask[::3] = 0
    s["i32"].set_mask(mask)

    back, _ = roundtrip(s, tmp_path)
    np.testing.assert_array_equal(back["i32"].mask_numpy(), mask.astype(bool))


def test_a_group_may_hold_no_columns_at_all(tmp_path):
    """A store that only associates other tables is the normal shape for a
    root, and has to survive being written."""
    s = tttrlib.DataStore()
    s.ensure_group("a/b").set_n_rows(0)
    back, _ = roundtrip(s, tmp_path)
    assert back.n_columns() == 0
    assert back.group_paths() == ["a", "a/b"]


def test_an_empty_store(tmp_path):
    back, _ = roundtrip(tttrlib.DataStore(), tmp_path)
    assert back.n_rows() == 0 and back.n_columns() == 0 and back.n_groups() == 0


def test_a_zero_row_column_keeps_its_name_and_type(tmp_path):
    s = tttrlib.DataStore()
    s.set_n_rows(0)
    s.add("x", np.array([], dtype=np.float32))
    back, _ = roundtrip(s, tmp_path)
    assert back.names == ["x"] and back["x"].numpy().dtype == np.float32


def test_column_order_is_write_order(tmp_path):
    s = tttrlib.DataStore()
    s.set_n_rows(2)
    for name in ["zulu", "alpha", "mike", "bravo"]:
        s.add(name, np.zeros(2))
    back, _ = roundtrip(s, tmp_path)
    assert back.names == ["zulu", "alpha", "mike", "bravo"]


def test_group_order_is_insertion_order(tmp_path):
    s = tttrlib.DataStore()
    for name in ("zulu", "alpha", "mike"):
        s.add_group(name)
    back, _ = roundtrip(s, tmp_path)
    assert back.group_names() == ["zulu", "alpha", "mike"]


def test_loading_replaces_whatever_the_store_held(tree, tmp_path):
    """load_store into a store with columns of its own must not append to
    them -- the result is the file, not a merge."""
    path = str(tmp_path / "t.dstore")
    assert tttrlib.save_store(path, tree)

    target = tttrlib.DataStore()
    target.set_n_rows(3)
    target.add("stale", np.zeros(3))
    tttrlib.read_store_into(target, path)
    assert "stale" not in target.names


# -- reading part of a file ---------------------------------------------------

def test_only_the_named_columns_are_read(tree, tmp_path):
    """What the directory is for: the columns not asked for are never touched."""
    path = str(tmp_path / "t.dstore")
    assert tttrlib.save_store(path, tree)

    part = tttrlib.load_store(path, columns=["f64", "text"])
    assert part.names == ["f64", "text"]
    np.testing.assert_array_equal(part["f64"].numpy(), tree["f64"].numpy())
    assert part["text"].numpy()[0] == tree["text"].numpy()[0]


def test_a_partial_read_still_rebuilds_the_tree(tree, tmp_path):
    """The tree is the directory, so it costs nothing to keep."""
    path = str(tmp_path / "t.dstore")
    tttrlib.save_store(path, tree)
    part = tttrlib.load_store(path, columns=["Tau"])
    assert part.group_paths() == tree.group_paths()
    np.testing.assert_array_equal(part.group("results")["Tau"].numpy(),
                                  tree.group("results")["Tau"].numpy())


def test_a_column_that_is_not_there_is_simply_absent(tree, tmp_path):
    path = str(tmp_path / "t.dstore")
    tttrlib.save_store(path, tree)
    part = tttrlib.load_store(path, columns=["f64", "no such column"])
    assert part.names == ["f64"]


# -- a window of rows ---------------------------------------------------------
#
# The projection along the other axis from a column subset. A table viewer that
# shows fifty rows of a million should read fifty rows of a million.

def test_a_row_range_of_a_fixed_width_column_is_the_slice(tree, tmp_path):
    path = str(tmp_path / "t.dstore")
    tttrlib.save_store(path, tree)
    win = tttrlib.load_store_region(path, 0, 0, first_row=17, n_rows=23)
    assert win.n_rows() == 23
    for name in ("f64", "f32", "i64", "u64", "i16", "u8"):
        np.testing.assert_array_equal(win[name].numpy(),
                                      tree[name].numpy()[17:17 + 23], name)


def test_a_row_range_of_a_bit_packed_column_is_shifted_down(tree, tmp_path):
    """The trap: bools and validity masks are bits, and a range that does not
    start on a word boundary has to be repacked from bit zero. Offsets 1 and 63
    are the two that catch a shift written the wrong way round."""
    path = str(tmp_path / "t.dstore")
    tttrlib.save_store(path, tree)
    for first, n in ((0, 64), (1, 62), (63, 1), (7, 40), (33, 31)):
        win = tttrlib.load_store_region(path, 0, 0, first_row=first, n_rows=n)
        np.testing.assert_array_equal(win["flag"].numpy(),
                                      tree["flag"].numpy()[first:first + n],
                                      "%d+%d" % (first, n))


def test_a_row_range_of_a_text_column_returns_the_right_labels(tree, tmp_path):
    """The codes are sliced; the dictionary is not, being labels rather than
    rows and small by construction."""
    path = str(tmp_path / "t.dstore")
    tttrlib.save_store(path, tree)
    win = tttrlib.load_store_region(path, 0, 0, first_row=11, n_rows=9)
    want = [tree["text"].numpy()[i] for i in range(11, 20)]
    assert [win["text"].numpy()[i] for i in range(9)] == want
    assert list(win["text"].dictionary()) == list(tree["text"].dictionary())


def test_a_row_range_reads_far_fewer_bytes_than_the_table(tmp_path):
    """Criterion 10, and the point of the whole part. Counted rather than
    timed: a wall clock on a warm page cache measures the cache."""
    n = 1_000_000
    s = tttrlib.DataStore("big")
    s.set_n_rows(n)
    s.add("a", np.arange(n, dtype=np.float64))
    s.add("b", np.arange(n, dtype=np.int64))
    path = str(tmp_path / "big.dstore")
    tttrlib.save_store(path, s)

    before = tttrlib.store_bytes_read()
    tttrlib.load_store(path)
    whole = tttrlib.store_bytes_read() - before

    before = tttrlib.store_bytes_read()
    win = tttrlib.load_store_region(path, 0, 0, first_row=500_000, n_rows=50)
    window = tttrlib.store_bytes_read() - before

    assert win.n_rows() == 50
    np.testing.assert_array_equal(win["a"].numpy(),
                                  np.arange(500_000, 500_050, dtype=np.float64))
    assert window < whole / 1000, "%d bytes for 50 rows of %d" % (window, whole)


def test_a_range_past_the_end_of_a_table_is_empty_not_an_error(tree, tmp_path):
    """A tree is one file and its tables need not agree on how long they are, so
    a group shorter than first_row comes back empty rather than throwing."""
    path = str(tmp_path / "t.dstore")
    tttrlib.save_store(path, tree)
    win = tttrlib.load_store_region(path, 0, 0, first_row=100, n_rows=10)
    assert win.n_rows() == 0
    assert win.group_paths() == tree.group_paths(), "the tree is still the tree"
    assert win.group("results").n_rows() == 10, "4096 rows, so 100..110 exists"


def test_a_row_range_runs_short_rather_than_over(tree, tmp_path):
    path = str(tmp_path / "t.dstore")
    tttrlib.save_store(path, tree)
    win = tttrlib.load_store_region(path, 0, 0, first_row=60, n_rows=1000)
    assert win.n_rows() == 4
    np.testing.assert_array_equal(win["f64"].numpy(), tree["f64"].numpy()[60:])


def test_a_row_range_can_be_narrowed_to_columns_too(tree, tmp_path):
    path = str(tmp_path / "t.dstore")
    tttrlib.save_store(path, tree)
    win = tttrlib.load_store_region(path, 0, 0, columns=["f64"],
                                    first_row=17, n_rows=23)
    assert win.names == ["f64"]
    np.testing.assert_array_equal(win["f64"].numpy(), tree["f64"].numpy()[17:40])


# -- asking about a file ------------------------------------------------------

def test_is_store_file_is_silent_on_anything(tmp_path, capfd):
    good = str(tmp_path / "t.dstore")
    tttrlib.save_store(good, tttrlib.DataStore())
    text = tmp_path / "notes.txt"
    text.write_bytes(b"not a store file")

    assert tttrlib.is_store_file(good) is True
    assert tttrlib.is_store_file(str(text)) is False
    assert tttrlib.is_store_file(str(tmp_path / "nope")) is False
    out, err = capfd.readouterr()
    assert out == "" and err == ""


def test_the_directory_can_be_read_without_the_data(tree, tmp_path):
    path = str(tmp_path / "t.dstore")
    tttrlib.save_store(path, tree)
    assert list(tttrlib.store_columns(path)) == tree.names
    assert list(tttrlib.store_groups(path)) == tree.group_paths()
    assert list(tttrlib.store_columns(path, "results")) == ["Tau"]


def test_the_queries_say_nothing_about_a_foreign_file(tmp_path):
    text = tmp_path / "notes.txt"
    text.write_bytes(b"not a store file")
    assert list(tttrlib.store_columns(str(text))) == []
    assert list(tttrlib.store_groups(str(text))) == []


# -- refusing what it cannot read ---------------------------------------------

def test_a_missing_file_says_so(tmp_path):
    with pytest.raises(Exception, match="cannot open"):
        tttrlib.load_store(str(tmp_path / "nope.dstore"))


def test_a_file_that_is_not_a_store_says_so(tmp_path):
    path = tmp_path / "notes.txt"
    path.write_bytes(b"this is not a store file, it is a note" * 10)
    with pytest.raises(Exception, match="not a tttrlib store file"):
        tttrlib.load_store(str(path))


def test_a_truncated_file_is_refused_rather_than_half_read(tree, tmp_path):
    """The byte count in the header catches this without a stat, and refusing
    is the point: half a table that reports success is worse than no table."""
    path = tmp_path / "t.dstore"
    tttrlib.save_store(str(path), tree)
    whole = path.read_bytes()
    path.write_bytes(whole[: len(whole) // 2])

    with pytest.raises(Exception):
        tttrlib.load_store(str(path))


def test_a_corrupt_directory_is_caught(tree, tmp_path):
    """The directory is checksummed because it is small enough that checking is
    free. The payload is not, because checksumming gigabytes on every open
    would cost more than the whole format saves."""
    path = tmp_path / "t.dstore"
    tttrlib.save_store(str(path), tree)
    raw = bytearray(path.read_bytes())
    raw[-8] ^= 0xFF                       # inside the directory, near its end
    path.write_bytes(bytes(raw))

    with pytest.raises(Exception, match="corrupt"):
        tttrlib.load_store(str(path))


def test_a_failed_write_leaves_no_half_file(tmp_path):
    """The write goes to a temporary and is renamed into place."""
    missing = tmp_path / "no such directory" / "t.dstore"
    assert tttrlib.save_store(str(missing), tttrlib.DataStore()) is False
    assert not missing.exists()
    assert not (tmp_path / "no such directory").exists()


def test_the_temporary_does_not_survive_a_good_write(tmp_path):
    path = tmp_path / "t.dstore"
    tttrlib.save_store(str(path), tttrlib.DataStore())
    assert sorted(p.name for p in tmp_path.iterdir()) == ["t.dstore"]


# -- the reason it exists -----------------------------------------------------

@pytest.mark.slow
@pytest.mark.smoke
def test_what_the_native_format_is_actually_faster_at(tmp_path, capsys):
    """The justification for a second format, measured rather than asserted.

    A million rows of mixed numeric types. Three comparisons, and only two of
    them are worth gating on:

    * Against **uncompressed** HDF5 the two are a wash, and this does not
      assert otherwise. Both are writing the same twenty-one megabytes through
      the same page cache, so the disk decides, not the format. Measured
      repeatedly on one machine the ratio wandered between 0.6x and 1.3x in
      both directions -- a test that demanded a win here would be a test that
      fails on a busy afternoon.
    * Against **compressed** HDF5 -- which is what the default was until this
      release, and what most existing files use -- the margin is two orders of
      magnitude, because deflate is CPU-bound and this format has no deflate.
    * A **partial read** touches only the columns asked for, which HDF5 through
      this library cannot do at all.

    The two gated numbers are far enough above the noise that failing one means
    something really regressed.
    """
    if not tttrlib.hdf5_table_available():
        pytest.skip("built without HDF5, so there is nothing to compare against")
    import time

    n = 1_000_000
    rng = np.random.default_rng(3)
    s = tttrlib.DataStore("benchmark")
    s.set_n_rows(n)
    s.add("f64", rng.normal(size=n))
    s.add("f32", rng.normal(size=n).astype(np.float32))
    s.add("i64", rng.integers(0, 2**40, n).astype(np.int64))
    s.add("u8", rng.integers(0, 250, n).astype(np.uint8))

    native = str(tmp_path / "b.dstore")
    plain, packed = str(tmp_path / "b0.h5"), str(tmp_path / "b4.h5")

    def clock(fn, repeats=5):
        best = float("inf")
        for _ in range(repeats):
            t = time.perf_counter()
            fn()
            best = min(best, time.perf_counter() - t)
        return best

    write_native = clock(lambda: tttrlib.save_store(native, s))
    write_plain = clock(lambda: tttrlib.write_hdf5(plain, s, compression=0))
    write_packed = clock(lambda: tttrlib.write_hdf5(packed, s, compression=4), repeats=2)
    read_native = clock(lambda: tttrlib.load_store(native))
    read_plain = clock(lambda: tttrlib.read_hdf5(plain))
    read_packed = clock(lambda: tttrlib.read_hdf5(packed))
    read_one = clock(lambda: tttrlib.load_store(native, columns=["u8"]))

    with capsys.disabled():
        print("\n  write   native %.4fs | hdf5 -0 %.4fs (%.1fx) | hdf5 -4 %.4fs (%.0fx)"
              % (write_native, write_plain, write_plain / write_native,
                 write_packed, write_packed / write_native))
        print("  read    native %.4fs | hdf5 -0 %.4fs (%.1fx) | hdf5 -4 %.4fs (%.1fx)"
              % (read_native, read_plain, read_plain / read_native,
                 read_packed, read_packed / read_native))
        print("  one column of four: %.4fs (%.0fx less than the whole table)"
              % (read_one, read_native / read_one))
        print("  size    native %.1f MB | hdf5 -0 %.1f MB | hdf5 -4 %.1f MB"
              % tuple(os.path.getsize(p) / 1e6 for p in (native, plain, packed)))

    assert write_packed > write_native * 20, "the win over compressed HDF5 has gone"
    assert read_one < read_native / 5, "a partial read is reading the whole table"


# -- a column is described, not just named -------------------------------

import json
import struct as _struct


def _units_store():
    s = tttrlib.DataStore("t")
    s.set_n_rows(10)
    s.add("Duration", np.arange(10, dtype=np.float64))
    s.add("Tau", np.linspace(1.0, 4.0, 10))
    s.add("label", ["a"] * 10)
    s[0].set_units("milliseconds")
    s[1].set_units("nanoseconds")
    s[2].set_attribute("description", "a text column")
    return s


def test_a_column_carries_its_description_through_a_file(tmp_path):
    """A burst duration is milliseconds and a lifetime is nanoseconds, and until
    a column could say so that was recorded only in its name -- when whoever
    wrote it remembered. `Duration (ms)` and `Tau` sit in the same table."""
    path = str(tmp_path / "u.dstore")
    tttrlib.write_store(path, _units_store())

    back = tttrlib.DataStore()
    tttrlib.read_store_into(back, path)
    assert [(back[i].name(), back[i].units()) for i in range(back.n_columns())] == [
        ("Duration", "milliseconds"),
        ("Tau", "nanoseconds"),
        ("label", ""),
    ]
    assert back[2].attribute("description") == "a text column"


def test_a_column_subset_keeps_the_description(tmp_path):
    """The description travels with the column, not with the file -- which is
    the point: a caller reading two columns of a four-gigabyte table still
    learns what they are."""
    path = str(tmp_path / "u.dstore")
    tttrlib.write_store(path, _units_store())
    sub = tttrlib.DataStore()
    tttrlib.read_store_into(sub, path, tttrlib.VectorString(["Tau"]))
    assert sub.n_columns() == 1
    assert sub[0].units() == "nanoseconds"


def test_listing_columns_still_steps_over_the_description(tmp_path):
    """The directory is positional. store_columns touches no blob but still has
    to step over every field, and one unconsumed string shifts every column and
    group after it."""
    path = str(tmp_path / "u.dstore")
    store = _units_store()
    store.add_group("child").set_n_rows(2)
    tttrlib.write_store(path, store)
    assert list(tttrlib.store_columns(path)) == ["Duration", "Tau", "label"]
    assert "child" in list(tttrlib.store_groups(path))


def test_a_column_with_no_description_does_not_grow_one(tmp_path):
    """Nothing fabricates a JSON object: a column that was never described
    reports nothing, and naming it does not describe it."""
    s = tttrlib.DataStore("t")
    s.set_n_rows(2)
    s.add("x", np.zeros(2))
    assert s[0].metadata() == ""
    assert s[0].attribute("units") == ""
    s[0].set_name("y")
    assert s[0].metadata() == ""

    path = str(tmp_path / "p.dstore")
    tttrlib.write_store(path, s)
    back = tttrlib.DataStore()
    tttrlib.read_store_into(back, path)
    assert back[0].metadata() == ""


def test_the_name_and_the_description_cannot_disagree():
    """Two ways in, and both have to update both -- or a column reports one
    name and serialises another."""
    s = tttrlib.DataStore("t")
    s.set_n_rows(1)
    s.add("a", np.zeros(1))
    s[0].set_units("seconds")
    s[0].set_name("b")
    assert s[0].name() == "b"
    assert '"name":"b"' in s[0].metadata().replace(" ", "")

    s[0].set_metadata('{"name": "c", "units": "hours"}')
    assert s[0].name() == "c"
    assert s[0].units() == "hours"


def test_metadata_that_is_not_a_json_object_is_refused():
    """Rejected at the call that got it wrong, not at some later read."""
    s = tttrlib.DataStore("t")
    s.set_n_rows(1)
    s.add("a", np.zeros(1))
    for bad in ("not json", "[1,2,3]", '"a string"', "42"):
        with pytest.raises(Exception):
            s[0].set_metadata(bad)


def _downgrade(src, dst, target):
    """Rewrite the current store version as a genuine older one.

    Built rather than committed as a binary so the fixture cannot rot: it is
    produced from whatever the current writer emits, with the one thing each
    version changed put back. The directory layout it walks is the writer's --
    a string is a u32 length and its bytes, a blob is two u64s.

    Both older versions differ from the current one in the *same slot*, which is
    what makes one walker enough: version 1 has no description at all, version 2
    holds it as JSON text where 3 holds msgpack.
    """
    raw = bytearray(open(src, "rb").read())
    version, = _struct.unpack_from("<I", raw, 8)
    assert version == 3, version
    dir_offset, dir_bytes = _struct.unpack_from("<QQ", raw, 16)
    d = raw[dir_offset : dir_offset + dir_bytes]

    out = bytearray()
    i = 0

    def take(n):
        nonlocal i
        out.extend(d[i : i + n])
        i += n

    def take_str():
        nonlocal i
        k, = _struct.unpack_from("<I", d, i)
        take(4 + k)

    def old_metadata():
        """The description slot, as the target version held it."""
        nonlocal i
        k, = _struct.unpack_from("<I", d, i)
        packed = bytes(d[i + 4 : i + 4 + k])
        i += 4 + k
        if target == 1:
            return                                  # version 1 has no slot
        text = b"" if not packed else json.dumps(
            msgpack_loads(packed), separators=(",", ":"), sort_keys=True).encode()
        out.extend(_struct.pack("<I", len(text)))
        out.extend(text)

    def node():
        nonlocal i
        take_str()                      # label
        take(8)                         # n_rows
        take(8 + 16)                    # row mask size + blob
        n_columns, = _struct.unpack_from("<I", d, i)
        take(4)
        for _ in range(n_columns):
            take_str()                  # name
            type_code = d[i]
            take(1 + 8 + 1)             # type, size, flags
            take(16)                    # data blob
            take(8 + 16)                # mask bits + blob
            old_metadata()              # <- the slot each version changed
            if type_code == 11:         # String: dictionary blob
                take(16)
        n_groups, = _struct.unpack_from("<I", d, i)
        take(4)
        for _ in range(n_groups):
            take_str()
            node()

    node()
    assert i == len(d), f"walked {i} of {len(d)} directory bytes"

    def fnv1a(b):
        h = 0x811C9DC5
        for byte in b:
            h = ((h ^ byte) * 0x01000193) & 0xFFFFFFFF
        return h

    body = raw[:dir_offset]
    new = bytearray(body) + out
    _struct.pack_into("<I", new, 8, target)                  # version
    _struct.pack_into("<QQ", new, 16, dir_offset, len(out))  # directory
    _struct.pack_into("<Q", new, 32, len(new))               # declared size
    _struct.pack_into("<I", new, 40, fnv1a(bytes(out)))      # checksum
    open(dst, "wb").write(bytes(new))


def msgpack_loads(b):
    """Enough msgpack to read what the writer emits, so the fixture needs no
    third-party package to be a genuine older file."""
    pos = 0

    def value():
        nonlocal pos
        c = b[pos]; pos += 1
        if c <= 0x7F: return c
        if 0xE0 <= c: return c - 0x100
        if 0xA0 <= c <= 0xBF: return text(c & 0x1F)
        if 0x80 <= c <= 0x8F: return obj(c & 0x0F)
        if 0x90 <= c <= 0x9F: return arr(c & 0x0F)
        if c == 0xC0: return None
        if c == 0xC2: return False
        if c == 0xC3: return True
        if c in (0xCC, 0xCD, 0xCE, 0xCF): return uint(1 << (c - 0xCC))
        if c in (0xD0, 0xD1, 0xD2, 0xD3): return int_(1 << (c - 0xD0))
        if c == 0xCB: return struct_(">d")
        if c == 0xCA: return struct_(">f")
        if c == 0xD9: return text(uint(1))
        if c == 0xDA: return text(uint(2))
        if c == 0xDC: return arr(uint(2))
        if c == 0xDE: return obj(uint(2))
        raise AssertionError(f"unhandled msgpack byte 0x{c:02x}")

    def uint(n):
        nonlocal pos
        v = int.from_bytes(b[pos:pos + n], "big"); pos += n
        return v

    def int_(n):
        nonlocal pos
        v = int.from_bytes(b[pos:pos + n], "big", signed=True); pos += n
        return v

    def struct_(fmt):
        nonlocal pos
        v, = _struct.unpack_from(fmt, b, pos); pos += _struct.calcsize(fmt)
        return v

    def text(n):
        nonlocal pos
        v = b[pos:pos + n].decode(); pos += n
        return v

    def arr(n): return [value() for _ in range(n)]

    def obj(n): return {value(): value() for _ in range(n)}

    return value()


def test_a_version_1_file_still_reads(tmp_path):
    """The format gained a field; files written before it did not."""
    cur = str(tmp_path / "cur.dstore")
    v1 = str(tmp_path / "v1.dstore")
    tttrlib.write_store(cur, _units_store())
    _downgrade(cur, v1, 1)

    back = tttrlib.DataStore()
    tttrlib.read_store_into(back, v1)
    assert [back[i].name() for i in range(back.n_columns())] == ["Duration", "Tau", "label"]
    assert all(back[i].metadata() == "" for i in range(back.n_columns()))
    np.testing.assert_array_equal(back["Duration"].numpy(), np.arange(10, dtype=np.float64))
    assert list(tttrlib.store_columns(v1)) == ["Duration", "Tau", "label"]


def test_a_version_2_file_still_reads(tmp_path):
    """Version 2 held the same description as JSON text.

    Unlike version 1 this loses nothing, so the assertion is the strong one:
    every column reads back with the description it was written with.
    """
    cur = str(tmp_path / "cur.dstore")
    v2 = str(tmp_path / "v2.dstore")
    tttrlib.write_store(cur, _units_store())
    _downgrade(cur, v2, 2)

    assert _struct.unpack_from("<I", open(v2, "rb").read(12), 8)[0] == 2
    back = tttrlib.DataStore()
    tttrlib.read_store_into(back, v2)
    now = tttrlib.load_store(cur)
    for i in range(back.n_columns()):
        assert json.loads(back[i].metadata() or "{}") == json.loads(now[i].metadata() or "{}")
    assert back["Tau"].units() == "nanoseconds"
    np.testing.assert_array_equal(back["Duration"].numpy(), np.arange(10, dtype=np.float64))


def test_the_description_is_stored_as_msgpack(tmp_path):
    """The encoding, asserted at the byte level rather than through the reader.

    A round trip alone would pass just as well if the writer had kept storing
    JSON text, so the test that the change happened has to look at the file.
    """
    path = str(tmp_path / "packed.dstore")
    tttrlib.write_store(path, _units_store())
    raw = open(path, "rb").read()

    assert _struct.unpack_from("<I", raw, 8)[0] == 3
    # The JSON form would carry these; msgpack carries the keys as text and
    # nothing else.
    assert b'"units":"nanoseconds"' not in raw and b'{"name":' not in raw
    assert b"units" in raw and b"nanoseconds" in raw


def test_a_typed_attribute_keeps_its_type(tmp_path):
    """What the encoding is for. JSON has one number type, so an integer row
    index would come back as a double and a caller would have to re-infer it."""
    s = tttrlib.DataStore()
    s.set_n_rows(2)
    s.add("Tau", np.zeros(2))
    c = s["Tau"]
    c.set_attribute_json("na", "[[2,4]]")
    c.set_attribute_json("n", "9007199254740993")     # 2^53 + 1
    c.set_attribute_json("ok", "true")
    c.set_attribute("of", "run.ptu")

    path = str(tmp_path / "typed.dstore")
    tttrlib.write_store(path, s)
    back = tttrlib.load_store(path)["Tau"]

    assert json.loads(back.metadata()) == {
        "na": [[2, 4]], "n": 9007199254740993, "ok": True, "of": "run.ptu"}
    # exact past 2^53, which a double could not be
    assert back.attribute_json("n") == "9007199254740993"
    # and the string setter still stores a string, whatever it looks like
    assert back.attribute_json("of") == '"run.ptu"'


def test_set_attribute_stores_a_string_even_when_it_looks_structured():
    """The two setters are not interchangeable, and the difference is the bug
    that made this necessary: `set_attribute` double-encoded structured data."""
    s = tttrlib.DataStore()
    s.set_n_rows(1)
    s.add("a", np.zeros(1))
    c = s["a"]

    c.set_attribute("na", "[[2,4]]")
    assert json.loads(c.metadata())["na"] == "[[2,4]]"      # the seven characters
    c.set_attribute_json("na", "[[2,4]]")
    assert json.loads(c.metadata())["na"] == [[2, 4]]       # the ranges

    # attribute() unquotes so a caller reading units need not parse;
    # attribute_json() does not, so the round trip is exact.
    assert c.attribute("na") == "[[2,4]]"
    assert c.attribute_json("na") == "[[2,4]]"
    c.set_units("ns")
    assert c.attribute("units") == "ns" and c.attribute_json("units") == '"ns"'


def test_a_typed_attribute_that_is_not_json_is_refused():
    s = tttrlib.DataStore()
    s.set_n_rows(1)
    s.add("a", np.zeros(1))
    with pytest.raises(Exception):
        s["a"].set_attribute_json("na", "[[2,4")
    assert s["a"].metadata() == ""


def test_an_empty_typed_attribute_erases_the_key():
    s = tttrlib.DataStore()
    s.set_n_rows(1)
    s.add("a", np.zeros(1))
    c = s["a"]
    c.set_attribute_json("na", "[[2,4]]")
    c.set_attribute_json("na", "")
    assert "na" not in json.loads(c.metadata() or "{}")

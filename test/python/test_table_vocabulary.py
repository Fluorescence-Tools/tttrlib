"""One vocabulary for a table in a file, whatever the file is.

Three formats can hold a ``DataStore`` and each was reached by a different verb
with a different spelling of the same argument — ``load_store(f, columns=)``,
``read_hdf5(f, group)``, ``pto_store(f, uid, columns=)`` — so a caller who
wanted to swap one for another rewrote their call sites, and one reading a
folder of mixed files carried a branch per format.

These five functions add no capability. Every one is a call to a reader that
already exists, chosen from the file. What they add is that the choosing happens
once, in the library, instead of at every call site in four languages.

**The load-bearing test is** :func:`test_the_same_read_gives_the_same_table`:
the same spec-shaped call against four files gives the same answer. That is what
"interchangeable" means, and without it this is only a fourth spelling.
"""
import os
import sys

import numpy as np
import pytest

sys.path.insert(0, os.path.dirname(__file__))
import tttrlib

N = 8


@pytest.fixture
def store():
    s = tttrlib.DataStore("run")
    s.set_n_rows(N)
    s.add("Tau", np.arange(N, dtype=np.float64))
    s.add("E", np.arange(N, dtype=np.float64) / 10.0)
    results = s.add_group("results")
    results.set_n_rows(N)
    results.add("Tau", np.arange(N, dtype=np.float64) * 2.0)
    results.add("n", np.arange(N, dtype=np.int32))
    return s


@pytest.fixture
def dstore(tmp_path, store):
    p = str(tmp_path / "r.dstore")
    tttrlib.save_store(p, store)
    return p


@pytest.fixture
def h5(tmp_path, store):
    p = str(tmp_path / "r.h5")
    tttrlib.write_hdf5(p, store, "/")
    return p


@pytest.fixture
def pto(tmp_path, store):
    p = str(tmp_path / "r.pto")
    f = tttrlib.PtoFile()
    assert f.create(p, "the test suite")
    tttrlib.pto_add_store(f, "table", "bursts", store)
    assert f.commit()
    f.close()
    return p + "|bursts"


@pytest.fixture
def csv(tmp_path, store):
    p = str(tmp_path / "r.csv")
    tttrlib.write_csv(p, store)
    return p


@pytest.fixture
def trees(dstore, h5, pto):
    """The three formats that hold a tree. CSV is deliberately not here — it is
    one flat table, and the tests that need that difference name it."""
    return {"dstore": dstore, "hdf5": h5, "pto": pto}


hdf5_only = pytest.mark.skipif(not tttrlib.hdf5_table_available(),
                               reason="built without HDF5")


# --- the point ---------------------------------------------------------------


@hdf5_only
def test_the_same_read_gives_the_same_table(trees, store):
    """One call shape, three files, one answer. Everything else here supports
    this one."""
    for name, spec in trees.items():
        t = tttrlib.read_table(spec, group="results", columns=["Tau"])
        assert t.names == ["Tau"], name
        assert t.n_rows() == N, name
        np.testing.assert_array_equal(t["Tau"].numpy(),
                                      np.arange(N, dtype=np.float64) * 2.0)


@hdf5_only
def test_a_row_window_is_the_same_in_every_format(trees):
    for name, spec in trees.items():
        t = tttrlib.read_table(spec, columns=["Tau"], first_row=2, n_rows=3)
        np.testing.assert_array_equal(t["Tau"].numpy(), [2.0, 3.0, 4.0]), name
        assert t.n_rows() == 3, name


@hdf5_only
def test_the_listings_agree(trees):
    """HDF5's own listing gives ``/results`` and includes the root; the native
    format's gives ``results`` and does not. One of them has to win or a path
    taken from one listing cannot be handed to the other."""
    for name, spec in trees.items():
        assert list(tttrlib.table_groups(spec)) == ["results"], name
        assert list(tttrlib.table_columns(spec)) == ["Tau", "E"], name
        assert list(tttrlib.table_columns(spec, "results")) == ["Tau", "n"], name


@hdf5_only
def test_a_path_from_one_listing_reads_in_another(dstore, h5):
    """The consequence, and the reason the normalisation is worth doing."""
    for group in tttrlib.table_groups(dstore):
        assert tttrlib.read_table(h5, group=group).names == ["Tau", "n"]
    for group in tttrlib.table_groups(h5):
        assert tttrlib.read_table(dstore, group=group).names == ["Tau", "n"]


@hdf5_only
def test_the_separators_are_optional_everywhere(trees):
    for name, spec in trees.items():
        for spelling in ("results", "/results", "results/", "/results/"):
            assert tttrlib.read_table(spec, group=spelling).names == ["Tau", "n"], \
                (name, spelling)


@hdf5_only
def test_table_has_answers_for_every_format(trees):
    for name, spec in trees.items():
        assert tttrlib.table_has(spec), name
        assert tttrlib.table_has(spec, "results"), name
        assert not tttrlib.table_has(spec, "nope"), name


# --- the format is taken from the file, not from the name --------------------


def test_content_beats_the_extension(tmp_path, store):
    """A `.dstore` named `.h5` is still a `.dstore`. An extension is a claim and
    the bytes are the fact."""
    p = str(tmp_path / "lying.h5")
    tttrlib.save_store(p, store)
    assert tttrlib.read_table(p).names == ["Tau", "E"]
    assert list(tttrlib.table_groups(p)) == ["results"]


def test_a_spec_that_names_no_table_raises_and_says_what_was_looked_for(tmp_path):
    p = str(tmp_path / "nothing.bin")
    open(p, "wb").write(b"not a table at all")
    with pytest.raises(RuntimeError) as e:
        tttrlib.read_table(p)
    assert "dstore" in str(e.value) and "HDF5" in str(e.value)


def test_a_missing_file_raises_rather_than_returning_an_empty_table(tmp_path):
    """An empty store returned to mean "could not read" cannot be told from one
    that read an empty table."""
    with pytest.raises(RuntimeError):
        tttrlib.read_table(str(tmp_path / "gone.dstore"))


def test_the_queries_are_silent_on_anything(tmp_path):
    """A question, unlike a read. A caller probes in a loop and a predicate that
    narrates is one nobody can use."""
    p = str(tmp_path / "nothing.bin")
    open(p, "wb").write(b"not a table at all")
    assert list(tttrlib.table_groups(p)) == []
    assert list(tttrlib.table_columns(p)) == []
    assert not tttrlib.table_has(p)
    assert not tttrlib.table_has(str(tmp_path / "gone.dstore"))


# --- CSV, which is one flat table ---------------------------------------------


def test_csv_reads_through_the_same_verb(csv):
    t = tttrlib.read_table(csv)
    assert t.names == ["Tau", "E"]
    assert t.n_rows() == N


def test_csv_refuses_the_knobs_it_does_not_have(csv):
    """A knob that silently does nothing is worse than one that is not there:
    a caller asking for a window would get the whole file and not know."""
    with pytest.raises(RuntimeError) as e:
        tttrlib.read_table(csv, group="results")
    assert "CSV" in str(e.value)
    with pytest.raises(RuntimeError) as e:
        tttrlib.read_table(csv, first_row=2, n_rows=2)
    assert "CSV" in str(e.value) and "row range" in str(e.value)


def test_csv_takes_a_column_subset_as_a_projection(csv):
    """It has no column filter — a text format cannot seek to a column — so
    this is honest about being after the fact rather than pretending."""
    t = tttrlib.read_table(csv, columns=["E"])
    assert t.names == ["E"]


def test_csv_has_no_groups_and_says_so(csv):
    assert list(tttrlib.table_groups(csv)) == []
    assert tttrlib.table_has(csv)
    assert not tttrlib.table_has(csv, "results")


# --- PTO, which has one addressing axis more ---------------------------------


def test_a_pto_must_name_its_object(tmp_path, store):
    p = str(tmp_path / "r.pto")
    f = tttrlib.PtoFile()
    f.create(p, "t")
    tttrlib.pto_add_store(f, "table", "bursts", store)
    f.commit()
    f.close()

    with pytest.raises(RuntimeError) as e:
        tttrlib.read_table(p)
    assert "|" in str(e.value), "the message should show the spec form"
    assert tttrlib.read_table(p + "|bursts").names == ["Tau", "E"]


def test_an_object_that_is_not_there_is_named_along_with_what_is(pto):
    path = pto.split("|")[0]
    with pytest.raises(RuntimeError) as e:
        tttrlib.read_table(path + "|nope")
    assert "nope" in str(e.value) and "bursts" in str(e.value)


# --- writing ------------------------------------------------------------------


@hdf5_only
def test_write_table_round_trips_in_every_format(tmp_path, store):
    for name in ("out.dstore", "out.h5", "out.csv"):
        p = str(tmp_path / name)
        assert tttrlib.write_table(p, store), name
        back = tttrlib.read_table(p)
        assert back.names == ["Tau", "E"], name
        np.testing.assert_allclose(back["Tau"].numpy(), store["Tau"].numpy())


@hdf5_only
def test_writing_a_group_builds_an_hdf5_file_one_group_at_a_time(tmp_path):
    a = tttrlib.DataStore()
    a.set_n_rows(2)
    a.add("x", np.arange(2.0))
    b = tttrlib.DataStore()
    b.set_n_rows(2)
    b.add("y", np.arange(2.0))

    p = str(tmp_path / "built.h5")
    assert tttrlib.write_table(p, a, group="first")
    assert tttrlib.write_table(p, b, group="second")
    assert sorted(tttrlib.table_groups(p)) == ["first", "second"]


@hdf5_only
def test_a_group_write_leaves_its_siblings_alone_in_every_format(tmp_path):
    """The same result whatever each does underneath. HDF5 replaces the group
    in place; the native format reads, replaces and writes back, because it
    holds one tree and cannot patch part of it. The caller is not told which,
    and the file contents are the same either way."""
    def one(name, value):
        s = tttrlib.DataStore()
        s.set_n_rows(2)
        s.add(name, np.array([value, value]))
        return s

    for filename in ("built.dstore", "built.h5"):
        p = str(tmp_path / filename)
        assert tttrlib.write_table(p, one("x", 1.0), group="first")
        assert tttrlib.write_table(p, one("y", 2.0), group="second")
        assert tttrlib.write_table(p, one("x", 9.0), group="first")   # replace

        assert sorted(tttrlib.table_groups(p)) == ["first", "second"], filename
        np.testing.assert_array_equal(
            tttrlib.read_table(p, group="first")["x"].numpy(), [9.0, 9.0])
        np.testing.assert_array_equal(
            tttrlib.read_table(p, group="second")["y"].numpy(), [2.0, 2.0])


def test_csv_refuses_a_group_write(tmp_path, store):
    """One flat table with nowhere to put a tree. The two tree formats do it by
    rewriting; CSV cannot do it at all, so it says so."""
    with pytest.raises(RuntimeError) as e:
        tttrlib.write_table(str(tmp_path / "out.csv"), store, group="results")
    assert "write_table" in str(e.value), "the message names the verb that failed"
    assert "CSV" in str(e.value)


def test_the_registry_publishes_what_each_format_can_be_asked(tmp_path):
    """So a caller can ask rather than try. The entry that matters most is not
    a capability but a COST: writing one group of a `.dstore` rewrites the file,
    and on four gigabytes a caller is entitled to know before they call."""
    import json

    d = json.loads(tttrlib.registry_category_json("table_format"))
    assert set(d) == {"dstore", "hdf5", "pto", "csv"}
    assert d["csv"]["groups"] is False and d["csv"]["row_range"] is False
    assert d["hdf5"]["rewrites_on_partial_write"] is False
    assert d["dstore"]["rewrites_on_partial_write"] is True
    assert "table_format" in list(tttrlib.registry_categories())


def test_no_call_but_write_table_changes_a_file(tmp_path, store):
    """Part 0's rule, asserted rather than assumed: a change happens in memory
    and a write is what puts it in a file."""
    import hashlib

    p = str(tmp_path / "frozen.dstore")
    tttrlib.write_table(p, store)
    before = hashlib.sha256(open(p, "rb").read()).hexdigest()

    tttrlib.read_table(p)
    tttrlib.read_table(p, group="results", columns=["Tau"], first_row=1, n_rows=2)
    tttrlib.table_groups(p)
    tttrlib.table_columns(p, "results")
    tttrlib.table_has(p, "results")

    assert hashlib.sha256(open(p, "rb").read()).hexdigest() == before


def test_write_table_takes_the_format_from_the_extension(tmp_path, store):
    """The one asymmetry with read_table, and it is inherent: the file need not
    exist yet, so there is nothing to sniff."""
    p = str(tmp_path / "fresh.dstore")
    assert tttrlib.write_table(p, store)
    assert tttrlib.load_store(p).names == ["Tau", "E"]


def test_a_write_to_an_unknown_extension_raises(tmp_path, store):
    with pytest.raises(RuntimeError) as e:
        tttrlib.write_table(str(tmp_path / "out.wat"), store)
    assert ".dstore" in str(e.value)


@hdf5_only
def test_a_pto_object_is_replaced_rather_than_duplicated(tmp_path, store):
    p = str(tmp_path / "r.pto")
    f = tttrlib.PtoFile()
    f.create(p, "t")
    tttrlib.pto_add_store(f, "table", "bursts", store)
    f.commit()
    f.close()

    smaller = tttrlib.DataStore()
    smaller.set_n_rows(2)
    smaller.add("Tau", np.array([9.0, 9.0]))
    tttrlib.write_table(p + "|bursts", smaller)

    back = tttrlib.read_table(p + "|bursts")
    np.testing.assert_array_equal(back["Tau"].numpy(), [9.0, 9.0])


# --- nothing regressed --------------------------------------------------------


@hdf5_only
def test_the_specific_readers_are_untouched(dstore, h5, csv, store):
    """`read_table` is a way of choosing between them, not a replacement: a
    caller who knows what they have still reaches for the reader with the knobs
    they want."""
    assert tttrlib.load_store(dstore, columns=["Tau"]).names == ["Tau"]
    assert tttrlib.read_hdf5(h5, "/results").names == ["Tau", "n"]
    assert tttrlib.read_csv(csv).names == ["Tau", "E"]

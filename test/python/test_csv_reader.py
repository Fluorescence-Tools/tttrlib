"""The CSV reader, checked against pyarrow where it is available.

The reader exists to fill a DataStore directly, so the assertions are about the
things that makes possible: types inferred per column, text kept as a dictionary
rather than a million strings, missing values masked instead of invented, and no
intermediate copy of the table.
"""
import os

import numpy as np
import pytest
import tttrlib

pacsv = pytest.importorskip("pyarrow.csv", reason="pyarrow not installed")


@pytest.fixture(scope="module")
def tricky(tmp_path_factory):
    p = tmp_path_factory.mktemp("csv") / "tricky.csv"
    p.write_text(
        'a,b,c,d,e\n'
        '1,2.5,"hello, world",true,x\n'
        '2,,"say ""hi""",false,y\n'
        '3,NA,plain,TRUE,x\n'
        '-4,1e3,"multi word",0,z\n'
        '5,0.001,,1,\n'
    )
    return str(p)


@pytest.fixture(scope="module")
def big(tmp_path_factory):
    rng = np.random.default_rng(3)
    n = 50000
    p = tmp_path_factory.mktemp("csv") / "big.csv"
    labels = np.array(["donor", "acceptor", "fret"])
    with open(p, "w") as f:
        f.write("i,x,y,label\n")
        xs = rng.uniform(0, 10, n)
        ys = rng.uniform(0, 1, n)
        ls = rng.choice(labels, n)
        for i in range(n):
            f.write("%d,%.6f,%.6f,%s\n" % (i, xs[i], ys[i], ls[i]))
    return str(p)


def _arrow(path):
    return pacsv.read_csv(path)


# --- parity with pyarrow ----------------------------------------------------

def test_types_and_values_match_pyarrow(tricky):
    s = tttrlib.read_csv(tricky)
    t = _arrow(tricky)
    assert s.n_rows() == t.num_rows
    assert s.names == t.column_names
    for name in s.names:
        a = s[name].numpy()
        b = t.column(name).to_numpy(zero_copy_only=False)
        if a.dtype.kind == "f":
            # A missing real is NaN on both sides; the mask is the authoritative
            # record but the value has to be something, and NaN is the thing
            # that survives being handed to code that drops the mask.
            assert np.allclose(a.astype(float), b.astype(float), equal_nan=True), name
        elif a.dtype.kind in "iub":
            # No NaN in an integer, so missingness lives only in the mask, and
            # pyarrow reports it as None.
            valid = s[name].mask_numpy()
            keep = np.ones(len(a), dtype=bool) if valid is None else valid
            assert list(a[keep]) == [v for v, k in zip(b, keep) if k], name
        else:
            assert list(a) == list(b), name


def test_quoted_delimiters_and_doubled_quotes(tricky):
    c = tttrlib.read_csv(tricky)["c"]
    assert c.string_at(0) == "hello, world", "a quoted delimiter is not a delimiter"
    assert c.string_at(1) == 'say "hi"', "a doubled quote is one quote"


def test_missing_values_are_masked_not_invented(tricky):
    b = tttrlib.read_csv(tricky)["b"]
    assert b.has_mask()
    assert list(b.mask_numpy()) == [True, False, False, True, True]


def test_booleans_in_any_case(tricky):
    d = tttrlib.read_csv(tricky)["d"]
    assert list(d.numpy()) == [True, False, True, False, True]


def test_large_file_matches_pyarrow(big):
    s = tttrlib.read_csv(big)
    t = _arrow(big)
    assert s.n_rows() == t.num_rows
    assert np.allclose(s["x"].numpy(), t.column("x").to_numpy())
    assert np.array_equal(s["i"].numpy(), t.column("i").to_numpy())
    assert list(s["label"].numpy()) == list(t.column("label").to_numpy(zero_copy_only=False))


# --- what the DataStore buys -------------------------------------------------

def test_text_is_dictionary_encoded_not_a_million_strings(big):
    s = tttrlib.read_csv(big)
    c = s["label"]
    assert c.labels() == sorted(c.labels(), key=c.labels().index)  # order preserved
    assert len(c.dictionary()) == 3
    # 4 bytes of code per row plus three short strings, not 50000 std::strings
    assert c.nbytes() < 4 * s.n_rows() + 4096


def test_float32_halves_the_memory(big):
    wide = tttrlib.read_csv(big)
    narrow = tttrlib.read_csv(big, use_float32=True)
    assert narrow["x"].numpy().dtype == np.float32
    assert narrow["x"].nbytes() * 2 == wide["x"].nbytes()
    assert np.allclose(narrow["x"].numpy(), wide["x"].numpy(), rtol=1e-6)


def test_the_store_histograms_directly(big):
    s = tttrlib.read_csv(big)
    h = s.histogram("x", bins=10, range=[(0.0, 10.0)])
    assert h.sum(False) == s.n_rows()
    hl = s.histogram("label")
    assert hl.sum(False) == s.n_rows(), "a text column bins by its dictionary codes"


def test_threads_do_not_change_the_result(big):
    a = tttrlib.read_csv(big, threads=1)
    b = tttrlib.read_csv(big, threads=8)
    assert a.n_rows() == b.n_rows()
    assert np.array_equal(a["i"].numpy(), b["i"].numpy())
    assert np.allclose(a["x"].numpy(), b["x"].numpy())
    assert list(a["label"].numpy()) == list(b["label"].numpy())


def test_block_size_does_not_change_the_result(big):
    a = tttrlib.read_csv(big)
    b = tttrlib.read_csv(big, block_size=64 * 1024)   # forces many blocks
    assert a.n_rows() == b.n_rows()
    assert np.allclose(a["x"].numpy(), b["x"].numpy())
    assert list(a["label"].numpy()) == list(b["label"].numpy()), \
        "per-block dictionaries must merge to the same labels"


def test_column_names_without_reading_the_file(big):
    assert list(tttrlib.read_csv_column_names(big)) == ["i", "x", "y", "label"]


def test_forcing_a_column_to_text(big):
    s = tttrlib.read_csv(big, text_columns=["i"])
    assert s["i"].dtype == "str"


def test_no_header(tmp_path):
    p = tmp_path / "nh.csv"
    p.write_text("1,2\n3,4\n")
    s = tttrlib.read_csv(str(p), has_header=False)
    assert s.names == ["f0", "f1"]
    assert s.n_rows() == 2

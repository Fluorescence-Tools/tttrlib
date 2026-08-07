"""The CSV writer, checked against pyarrow where it is available.

The writer is the export path: a DataStore that has to leave the library and be
opened by something that is not tttrlib. So the assertions are about what
survives the trip -- the values, the names, the order, the missing values, and
a double landing on the same double -- and about what deliberately does not,
because CSV cannot carry it.
"""
import numpy as np
import pytest
import tttrlib


def store_of(columns):
    """A DataStore from {name: array}, keeping each array's dtype."""
    s = tttrlib.DataStore()
    for name, values in columns.items():
        s.add(name, values)
    return s


@pytest.fixture
def mixed():
    return store_of({
        "i": np.array([1, -2, 3, 4, 5], dtype=np.int32),
        "chan": np.array([0, 1, 2, 3, 65], dtype=np.uint8),
        "x": np.array([0.1, 1 / 3, 1e300, 2.5, 7.0], dtype=np.float64),
        "f": np.array([0.1, 2.5, 3.25, 1e-30, 7.0], dtype=np.float32),
    })


# --- what comes back --------------------------------------------------------

def test_round_trip_values_and_names(mixed, tmp_path):
    p = str(tmp_path / "m.csv")
    tttrlib.write_csv(p, mixed)
    back = tttrlib.read_csv(p)

    assert back.names == mixed.names
    assert back.n_rows() == mixed.n_rows()
    for name in mixed.names:
        original = mixed[name].numpy()
        # Cast: the text says what the value is, not how many bytes it was held
        # in, so an int32 comes back int64 and a float32 comes back float64.
        # The VALUES are what a round trip through CSV promises.
        np.testing.assert_array_equal(back[name].numpy().astype(original.dtype),
                                      original)


def test_a_double_lands_on_the_same_double(tmp_path):
    """The point of shortest-round-trip formatting, on values that need it."""
    rng = np.random.default_rng(11)
    bits = rng.integers(0, 2**64, size=20000, dtype=np.uint64)
    x = bits.view(np.float64)
    x = np.ascontiguousarray(x[np.isfinite(x)])

    p = str(tmp_path / "d.csv")
    tttrlib.write_csv(p, store_of({"x": x}))
    # Bit-for-bit, not allclose: a value that comes back a ulp off has been
    # changed by being exported, and no tolerance makes that acceptable.
    np.testing.assert_array_equal(tttrlib.read_csv(p)["x"].numpy(), x)


@pytest.mark.parametrize("value,text", [
    (0.1, "0.1"),                  # not 0.10000000000000001
    (0.5, "0.5"),
    (2.5, "2.5"),
    (0.0, "0"),
    (-0.0, "-0"),
    (1e6, "1000000"),              # fixed while it is short enough to be
    (1e-4, "0.0001"),
    (1e-5, "1e-05"),               # ...and scientific once it is not
    (1e21, "1e+21"),
    (1234.5678, "1234.5678"),
    (-7.25e-9, "-7.25e-09"),
])
def test_the_shortest_spelling_is_the_one_written(value, text):
    """Not the shortest that looks right -- the shortest that reads back.

    Only values the writer can PROVE (fifteen significant digits, exponent
    within about ±22) are pinned here. Outside that it falls back to the
    platform, which may spell the same double with more digits; those are
    covered by the round-trip test instead, which is the property that matters.
    """
    s = store_of({"x": np.array([value])})
    assert tttrlib.write_csv(None, s).split("\n")[1] == text


def test_float32_is_written_short(tmp_path):
    """0.1f is "0.1", not the seventeen digits of the double it widens to."""
    p = str(tmp_path / "f.csv")
    x = np.array([0.1, 2.5, 1e-30], dtype=np.float32)
    tttrlib.write_csv(p, store_of({"f": x}))
    assert open(p).read().split("\n")[1] == "0.1"
    np.testing.assert_array_equal(
        tttrlib.read_csv(p)["f"].numpy().astype(np.float32), x)


def test_the_hard_doubles_round_trip(tmp_path):
    """The values a formatter gets wrong: powers of ten and their neighbours,
    subnormals, the extremes, and the ones needing all seventeen digits."""
    hard = [0.0, -0.0, 1.0, -1.0, 0.1, 1 / 3, 2 / 3, 5e-324, 2.2250738585072014e-308,
            1.7976931348623157e308, 9007199254740993.0, 0.30000000000000004,
            5.960464477539063e-08, 1e-30, 1e300, -1e-300]
    for e in range(-40, 41):
        p = 10.0 ** e
        hard += [p, -p, np.nextafter(p, np.inf), np.nextafter(p, -np.inf), 3.7 * p]
    x = np.array(hard, dtype=np.float64)

    p = str(tmp_path / "hard.csv")
    tttrlib.write_csv(p, store_of({"x": x}))
    np.testing.assert_array_equal(tttrlib.read_csv(p)["x"].numpy(), x)


def test_narrow_integers_are_numbers_not_characters(mixed):
    """A uint8 routing channel of 65 is 65, not 'A'."""
    text = tttrlib.write_csv(None, mixed)
    assert text.split("\n")[5].split(",")[1] == "65"


def test_booleans_round_trip_as_booleans(tmp_path):
    s = store_of({"ok": np.array([True, False, True, True])})
    p = str(tmp_path / "b.csv")
    tttrlib.write_csv(p, s)
    assert open(p).read() == "ok\ntrue\nfalse\ntrue\ntrue\n"
    np.testing.assert_array_equal(
        tttrlib.read_csv(p)["ok"].numpy().astype(bool), [True, False, True, True])


def test_text_is_quoted_only_where_it_has_to_be(tmp_path):
    values = ["plain", 'say "hi"', "a,b", "line\nbreak"]
    s = store_of({"label": np.array(values, dtype=object)})
    assert tttrlib.write_csv(None, s) == \
        'label\nplain\n"say ""hi"""\n"a,b"\n"line\nbreak"\n'

    p = str(tmp_path / "t.csv")
    tttrlib.write_csv(p, s)
    back = tttrlib.read_csv(p, newlines_in_values=True)
    assert list(back["label"].numpy()) == values


def test_missing_values_stay_missing(tmp_path):
    s = store_of({"i": np.array([1, 2, 3]), "x": np.array([1.0, 2.0, 3.0])})
    s["x"].set_mask(np.array([1, 0, 1], dtype=np.uint8))
    p = str(tmp_path / "n.csv")
    tttrlib.write_csv(p, s)
    assert open(p).read() == "i,x\n1,1\n2,\n3,3\n"

    back = tttrlib.read_csv(p)
    assert back["x"].has_mask()
    assert [back["x"].valid(i) for i in range(3)] == [True, False, True]


def test_a_lone_column_of_nulls_needs_an_na_rep(tmp_path):
    """The one case where the empty field cannot mean missing.

    A single column written with the default na_rep puts an empty LINE in the
    file, and an empty line is a blank line to every CSV reader there is --
    pyarrow drops it too. Nothing the writer can do about it except say so.
    """
    s = store_of({"x": np.array([1.0, 2.0, 3.0])})
    s["x"].set_mask(np.array([1, 0, 1], dtype=np.uint8))
    p = str(tmp_path / "lone.csv")

    tttrlib.write_csv(p, s)
    assert tttrlib.read_csv(p).n_rows() == 2               # the row vanished

    tttrlib.write_csv(p, s, na_rep="NA")
    back = tttrlib.read_csv(p)
    assert back.n_rows() == 3
    assert [back["x"].valid(i) for i in range(3)] == [True, False, True]


def test_na_rep_is_configurable():
    s = store_of({"x": np.array([1.0, 2.0])})
    s["x"].set_mask(np.array([1, 0], dtype=np.uint8))
    assert tttrlib.write_csv(None, s, na_rep="NA") == "x\n1\nNA\n"


# --- the options ------------------------------------------------------------

def test_selection_is_honoured(mixed):
    mixed.select(np.array([False, False, True, True, True]))
    assert tttrlib.write_csv(None, mixed, columns=["i"]) == "i\n3\n4\n5\n"
    assert tttrlib.write_csv(None, mixed, columns=["i"], selected_only=False) == \
        "i\n1\n-2\n3\n4\n5\n"


def test_column_subset_and_order(mixed):
    assert tttrlib.write_csv(None, mixed, columns=["chan", "i"]).split("\n")[0] == "chan,i"
    with pytest.raises(Exception):
        tttrlib.write_csv(None, mixed, columns=["nope"])


def test_no_header(mixed):
    assert tttrlib.write_csv(None, mixed, header=False).split("\n")[0] == "1,0,0.1,0.1"


def test_delimiter_and_eol(mixed):
    text = tttrlib.write_csv(None, mixed, delimiter="\t", eol="\r\n")
    assert text.split("\r\n")[0] == "i\tchan\tx\tf"


def test_quoting_all(mixed):
    text = tttrlib.write_csv(None, mixed, quoting="all")
    assert text.split("\n")[0] == '"i","chan","x","f"'
    assert text.split("\n")[1] == '"1","0","0.1","0.1"'


def test_quoting_none_refuses_a_value_that_needs_it():
    s = store_of({"label": np.array(["a,b"], dtype=object)})
    # Refuses rather than writing a file that reads back as a different table.
    with pytest.raises(Exception):
        tttrlib.write_csv(None, s, quoting="none")
    assert tttrlib.write_csv(None, store_of({"i": np.array([1, 2])}),
                             quoting="none") == "i\n1\n2\n"


def test_float_precision(mixed):
    assert tttrlib.write_csv(None, mixed, columns=["x"],
                             float_precision=4).split("\n")[2] == "0.3333"


def test_an_integral_float_can_keep_its_decimal_point():
    """Otherwise an all-integral float column arrives as an integer column.

    12.0 and 12 are the same double, so the shortest form drops the point --
    Arrow does too. What is lost is the dtype, for a reader that infers types
    from the text, and that matters when the file is merged column-wise with
    one another program wrote.
    """
    s = store_of({"x": np.array([12.0, 0.0, -3.0, 2.5, 1.2345678e-5])})
    assert tttrlib.write_csv(None, s).splitlines()[1:] == \
        ["12", "0", "-3", "2.5", "1.2345678e-05"]
    assert tttrlib.write_csv(None, s, keep_decimal_point=True).splitlines()[1:] == \
        ["12.0", "0.0", "-3.0", "2.5", "1.2345678e-05"]


def test_the_decimal_point_is_not_added_to_what_has_no_room_for_one():
    s = store_of({"x": np.array([np.nan, np.inf, -np.inf, 1e-30])})
    assert tttrlib.write_csv(None, s, keep_decimal_point=True).splitlines()[1:4] == \
        ["nan", "inf", "-inf"]


def test_keeping_the_point_does_not_change_the_value():
    rng = np.random.default_rng(5)
    x = np.ascontiguousarray(rng.uniform(-1e6, 1e6, 20000))
    text = tttrlib.write_csv(None, store_of({"x": x}), keep_decimal_point=True)
    assert np.array_equal(np.array([float(v) for v in text.splitlines()[1:]]), x)


def test_fixed_decimals_are_decimals_not_significant_digits():
    """What a format specified as %.6f means, which float_precision cannot say.

    The burst companion formats are written %.6f by their canonical writer, so
    a caller matching that layout needs decimals; six SIGNIFICANT digits writes
    1.23457e-05 where the format calls for 0.000012.
    """
    s = store_of({"x": np.array([12.0, 0.0, 2.5, 1.2345678e-5])})
    assert tttrlib.write_csv(None, s, float_decimals=6).splitlines()[1:] == \
        ["12.000000", "0.000000", "2.500000", "0.000012"]
    assert tttrlib.write_csv(None, s, float_precision=6).splitlines()[1:] == \
        ["12", "0", "2.5", "1.23457e-05"]


def test_a_nan_is_written_as_text_and_a_masked_value_is_not():
    """The asymmetry a caller converting frame-shaped data has to know about.

    In a store a NaN is a value and the mask says "missing", so the writer has
    nothing to translate. A caller with frame-shaped data, where NaN IS the
    missing marker, masks at this boundary and only at this boundary.
    """
    s = store_of({"i": np.array([1, 2]), "x": np.array([np.nan, 2.0])})
    assert tttrlib.write_csv(None, s).splitlines()[1] == "1,nan"

    s["x"].set_mask(np.array([0, 1], dtype=np.uint8))
    assert tttrlib.write_csv(None, s).splitlines()[1] == "1,"


def test_threads_do_not_change_a_byte():
    rng = np.random.default_rng(3)
    s = store_of({
        "x": rng.uniform(-1e6, 1e6, 100000),
        "i": rng.integers(0, 1000, 100000, dtype=np.int64),
    })
    assert tttrlib.write_csv(None, s, threads=1) == \
        tttrlib.write_csv(None, s, threads=8, block_rows=1000)


def test_a_failed_write_leaves_the_old_file_alone(mixed, tmp_path):
    p = tmp_path / "keep.csv"
    p.write_text("this was here first\n")
    with pytest.raises(Exception):
        tttrlib.write_csv(str(p), mixed, columns=["nope"])
    assert p.read_text() == "this was here first\n"


def test_a_text_column_is_escaped_once_per_distinct_value(tmp_path):
    """The dictionary is the unit of work, so a big column is still cheap."""
    labels = np.array(["donor", "acceptor", "fret"], dtype=object)
    s = store_of({"label": labels[np.arange(30000) % 3]})
    p = str(tmp_path / "big.csv")
    tttrlib.write_csv(p, s)
    back = tttrlib.read_csv(p)
    assert back["label"].labels() == ["donor", "acceptor", "fret"]
    np.testing.assert_array_equal(back["label"].numpy(), s["label"].numpy())


# --- parity with pyarrow ----------------------------------------------------

def test_pyarrow_reads_what_we_write(mixed, tmp_path):
    pacsv = pytest.importorskip("pyarrow.csv", reason="pyarrow not installed")
    p = str(tmp_path / "a.csv")
    tttrlib.write_csv(p, mixed)
    t = pacsv.read_csv(p)
    assert t.column_names == mixed.names
    for name in mixed.names:
        np.testing.assert_allclose(
            t.column(name).to_numpy(zero_copy_only=False), mixed[name].numpy(),
            rtol=1e-6)


def test_we_write_what_pyarrow_writes():
    """Same table, same bytes -- for values where CSV admits one spelling.

    Except the header: Arrow quotes column names whatever they hold, and this
    quotes them only where RFC 4180 says it has to. Both read back identically
    everywhere, so the divergence is deliberate rather than a bug to fix.
    """
    pa = pytest.importorskip("pyarrow", reason="pyarrow not installed")
    pacsv = pytest.importorskip("pyarrow.csv", reason="pyarrow not installed")

    columns = {"i": np.arange(50, dtype=np.int64), "x": np.arange(50) * 0.25}
    ours = tttrlib.write_csv(None, store_of(columns))

    sink = pa.BufferOutputStream()
    pacsv.write_csv(pa.table({k: pa.array(v) for k, v in columns.items()}), sink,
                    pacsv.WriteOptions(include_header=True))
    theirs = sink.getvalue().to_pybytes().decode()

    assert ours.split("\n")[1:] == theirs.split("\n")[1:]
    assert ours.split("\n")[0] == "i,x"
    assert theirs.split("\n")[0] == '"i","x"'


# -- a NaN is a value, and a masked cell is not ------------------------------


def test_a_nan_and_a_masked_cell_are_different_questions():
    """`na_rep` covers a cell the mask says was never measured. A `NaN` is a
    *value* -- a fit that diverged, a ratio with no denominator -- and the store
    keeps the two apart on purpose. CSV has one blank field for both, so the
    writer is where the caller has to be able to choose."""
    s = tttrlib.DataStore()
    s.set_n_rows(3)
    s.add("x", np.array([1.0, np.nan, 3.0]))
    s.add("n", np.array([1, 2, 3], dtype=np.int32))
    s["n"].set_mask(np.array([1, 0, 1], dtype=np.uint8))

    out = tttrlib.write_csv(None, s, nan_rep="NaN", na_rep="NA")
    assert out == "x,n\n1,1\nNaN,NA\n3,3\n"


def test_the_default_is_what_it_always_wrote():
    """`nan` -- so no existing file changes."""
    s = tttrlib.DataStore()
    s.set_n_rows(2)
    s.add("x", np.array([1.0, np.nan]))
    assert tttrlib.write_csv(None, s) == "x\n1\nnan\n"


def test_the_empty_field_is_reachable_without_touching_the_table():
    """What a data frame's writer produces, and the whole point of the option.

    The workaround was to mask every non-finite value first -- and the mask is
    part of the table, so doing that in place means *writing a table changes
    it*. The alternative was a whole-table copy per write, to express one
    formatting choice.
    """
    s = tttrlib.DataStore()
    s.set_n_rows(3)
    s.add("x", np.array([1.0, np.nan, 3.0]))

    assert tttrlib.write_csv(None, s, nan_rep="") == "x\n1\n\n3\n"
    assert not s["x"].has_mask(), "writing the table changed it"
    assert s["x"].valid(1), "writing the table changed it"


def test_infinity_is_left_alone():
    """It has an exact text that reads back as itself, and a frame writes it as
    `inf` too. Only the not-a-number is a formatting question."""
    s = tttrlib.DataStore()
    s.set_n_rows(3)
    s.add("x", np.array([np.inf, -np.inf, np.nan]))
    assert tttrlib.write_csv(None, s, nan_rep="") == "x\ninf\n-inf\n\n"


def test_a_float32_column_takes_it_too():
    s = tttrlib.DataStore()
    s.set_n_rows(2)
    s.add("x", np.array([1.5, np.nan], dtype=np.float32))
    assert tttrlib.write_csv(None, s, nan_rep="") == "x\n1.5\n\n"


def test_a_nan_replacement_is_quoted_when_it_needs_to_be():
    s = tttrlib.DataStore()
    s.set_n_rows(1)
    s.add("x", np.array([np.nan]))
    assert tttrlib.write_csv(None, s, nan_rep="a,b") == 'x\n"a,b"\n'


def test_neither_spelling_survives_a_round_trip_as_a_value():
    """Measured, and it decides what the option is FOR.

    `nan` is one of the reader's default `na_values`, so it comes back as a
    masked cell -- and the column then infers as an integer, having no
    non-integral text left in it. The empty field does the same thing for the
    same reason. So CSV conflates "not a number" with "not measured" whichever
    spelling is chosen, and `nan_rep` is about what OTHER programs read rather
    than about this library's own round trip. A caller who needs the
    distinction preserved wants `.dstore` or HDF5.
    """
    import os
    import tempfile

    s = tttrlib.DataStore()
    s.set_n_rows(3)
    s.add("x", np.array([1.0, np.nan, 3.0]))
    s.add("k", np.array([1, 2, 3], dtype=np.int32))   # so the row does not vanish

    d = tempfile.mkdtemp()
    for spelling in ("nan", ""):
        p = os.path.join(d, "r%s.csv" % len(spelling))
        tttrlib.write_csv(p, s, nan_rep=spelling)
        back = tttrlib.read_csv(p)["x"]
        assert back.size() == 3, spelling
        assert not back.valid(1), "%r came back as a value" % spelling


# -- two defects found while adding it ----------------------------------------


def test_a_constant_cell_is_quoted_like_every_other_cell():
    """`na_rep="a,b"` was written raw, so the file gained a phantom column and
    did not read back -- while the header three lines away quoted the same
    string correctly. The four constant cells now go through the same render."""
    s = tttrlib.DataStore()
    s.set_n_rows(2)
    s.add("n", np.array([7, 8], dtype=np.int32))
    s["n"].set_mask(np.array([1, 0], dtype=np.uint8))

    out = tttrlib.write_csv(None, s, na_rep="a,b")
    assert out == 'n\n7\n"a,b"\n'

    import tempfile, os
    p = os.path.join(tempfile.mkdtemp(), "q.csv")
    tttrlib.write_csv(p, s, na_rep="a,b")
    assert tttrlib.read_csv(p).n_columns() == 1, "the file grew a column"


def test_a_bool_word_that_needs_quoting_is_quoted_too():
    s = tttrlib.DataStore()
    s.set_n_rows(2)
    s.add("f", np.array([True, False]))
    assert tttrlib.write_csv(None, s, true_string="yes,really") == \
        'f\n"yes,really"\nfalse\n'


def test_quoting_takes_never_as_well_as_none():
    """The C++ enumerator is `Never` -- SWIG has to escape `None` -- so a caller
    reading the C++ side types the spelling this used to reject with a bare
    KeyError naming nothing."""
    s = tttrlib.DataStore()
    s.set_n_rows(1)
    s.add("x", np.zeros(1))
    assert tttrlib.write_csv(None, s, quoting="never") == \
           tttrlib.write_csv(None, s, quoting="none")


def test_an_unknown_quoting_names_the_ones_that_work():
    s = tttrlib.DataStore()
    s.set_n_rows(1)
    s.add("x", np.zeros(1))
    with pytest.raises(ValueError) as e:
        tttrlib.write_csv(None, s, quoting="bogus")
    assert "needed" in str(e.value) and "bogus" in str(e.value)


# -- what CSV loses, put back beside it ---------------------------------------


def _described():
    s = tttrlib.DataStore("acquisition")
    s.set_n_rows(3)
    s.add("Tau", np.array([1.0, 2.0, 3.0]))
    s.add("n", np.arange(3, dtype=np.int32))
    s["Tau"].set_units("ns")
    s["Tau"].set_attribute("of", "run.ptu")
    return s


def test_the_label_and_the_units_survive_csv(tmp_path):
    """CSV carries values and nothing else, so a table written to it loses its
    label and every column's units. A JSON Lines block beside the data puts
    them back without changing what the file IS."""
    s = _described()
    for where in ("leading", "trailing"):
        p = str(tmp_path / (where + ".csv"))
        tttrlib.write_csv(p, s, metadata=where)
        back = tttrlib.read_csv(p, comment="#")

        assert back.n_rows() == 3, where
        assert back.names == ["Tau", "n"], where
        assert back.label() == "acquisition", where
        assert back["Tau"].units() == "ns", where
        assert back["Tau"].attribute("of") == "run.ptu", where


def test_the_block_goes_where_it_was_asked_to(tmp_path):
    out = tttrlib.write_csv(None, _described(), metadata="leading")
    assert out.startswith("#{"), "leading means before the header"
    assert "\nTau,n\n" in out

    out = tttrlib.write_csv(None, _described(), metadata="trailing")
    assert out.startswith("Tau,n\n"), "trailing means after the data"
    assert out.rstrip().endswith("}")


def test_it_is_json_lines_and_not_one_blob():
    """One object per line, so a line a later version does not understand is
    skipped rather than making the block unreadable -- and grep still works."""
    import json

    out = tttrlib.write_csv(None, _described(), metadata="leading")
    lines = [l for l in out.split("\n") if l.startswith("#")]
    assert len(lines) == 2, "a header object and one per described column"
    head = json.loads(lines[0][1:])
    assert head["tttrlib"] == "table" and head["version"] == 1
    assert head["label"] == "acquisition" and head["n_rows"] == 3
    col = json.loads(lines[1][1:])
    assert col["column"] == "Tau" and col["dtype"] == "float64"
    assert col["metadata"]["units"] == "ns"


def test_a_column_with_nothing_to_say_gets_no_line():
    """Nothing acquires a description by being written."""
    out = tttrlib.write_csv(None, _described(), metadata="leading")
    assert '"column":"n"' not in out.replace(" ", "")


def test_the_default_writes_no_block():
    assert "#" not in tttrlib.write_csv(None, _described())


def test_a_reader_that_skips_comments_sees_the_same_table(tmp_path):
    """The property that makes this safe: the file is still the file. Checked
    against this library's own reader with the option off -- which is the same
    position pandas is in with `comment='#'` unset."""
    s = _described()
    plain = str(tmp_path / "plain.csv")
    described = str(tmp_path / "described.csv")
    tttrlib.write_csv(plain, s)
    tttrlib.write_csv(described, s, metadata="trailing")

    a = tttrlib.read_csv(plain)
    b = tttrlib.read_csv(described, comment="#")
    assert a.names == b.names and a.n_rows() == b.n_rows()
    np.testing.assert_array_equal(a["Tau"].numpy(), b["Tau"].numpy())


def test_a_comment_line_is_skipped_even_when_it_is_not_ours(tmp_path):
    """A comment is a comment first and metadata second: a file annotated by
    hand still reads."""
    p = str(tmp_path / "hand.csv")
    open(p, "w").write("# written by hand, not JSON at all\nx,y\n1,2\n#and a trailer\n")
    back = tttrlib.read_csv(p, comment="#")
    assert back.names == ["x", "y"] and back.n_rows() == 1
    np.testing.assert_array_equal(back["x"].numpy(), [1])


def test_a_metadata_line_for_a_column_that_is_gone_is_ignored(tmp_path):
    """Written whole, read as a subset -- the block names a column the caller
    did not ask for, and that is not an error."""
    p = str(tmp_path / "sub.csv")
    tttrlib.write_csv(p, _described(), metadata="leading", columns=["n"])
    back = tttrlib.read_csv(p, comment="#")
    assert back.names == ["n"]


def test_an_unknown_metadata_placement_names_the_ones_that_work():
    with pytest.raises(ValueError) as e:
        tttrlib.write_csv(None, _described(), metadata="sideways")
    assert "leading" in str(e.value) and "sideways" in str(e.value)


def test_the_comment_character_is_the_callers_choice(tmp_path):
    p = str(tmp_path / "semi.csv")
    tttrlib.write_csv(p, _described(), metadata="leading", comment=";")
    assert open(p).read().startswith(";{")
    back = tttrlib.read_csv(p, comment=";")
    assert back.label() == "acquisition" and back.n_rows() == 3

"""Gating a store with an expression: `"(g-b)/(r-b) > 0.3"` and friends.

The general selector, where `select_range` and the geometric ones are fixed
shapes. It is answered by `ExpressionEngine`, which compiles the query once and
runs it a block of rows at a time straight into the packed bits of a `BitMask`.

Three things are asserted here rather than assumed, because each has been wrong
in some evaluator this replaces:

* **The answer is numpy's.** Not "close to numpy's": a float32 store is
  evaluated in float32, so a gate on it is bit-for-bit what the same expression
  gives over the same array in numpy. A boundary case that lands one ulp either
  side of a threshold has to fall the same way in both.
* **Truthiness is numpy's.** Anything that is not zero is true, so a negative
  value and a NaN are both true. An evaluator that thresholded at `> 0.5` got
  both wrong.
* **A slot's type survives.** The block evaluator carries comparisons as one
  byte per row and numbers as a block of values, and an expression that mixes
  them -- `(g > 2) * 3`, `(g > 2) and r` -- must reconcile the two. Three real
  bugs lived exactly there.
"""
import numpy as np
import pytest

import tttrlib

# ndxplorer's own query set, which is where these shapes come from.
QUERIES = [
    "(g>2) & (r<10)",
    "g>5 | b<0.1",
    "~(g>2) & (r>1)",
    "(g-b)/(r-b) > 0.3",
    "g>2 and r<10",
    "g != 3",
]


def columns(n=2000, seed=0, dtype=np.float32):
    rng = np.random.default_rng(seed)
    return {"g": rng.uniform(0, 10, n).astype(dtype),
            "r": rng.uniform(0, 20, n).astype(dtype),
            "b": rng.uniform(0, 1, n).astype(dtype)}


def store_of(cols, n_rows=None):
    s = tttrlib.DataStore()
    s.set_n_rows(len(next(iter(cols.values()))) if n_rows is None else n_rows)
    for name, values in cols.items():
        s.add(name, values)
    return s


def mask_of(store, query):
    """The rows a query selects, as a bool array, leaving the selection alone."""
    store.select_expression(query)
    out = np.asarray(store.selection()).astype(bool)
    store.clear_row_mask()
    return out


def numpy_eval(query, cols):
    """The same six queries written out in numpy, rather than string-rewritten.

    A rewrite of `and` to `&` silently changes the precedence -- `&` binds
    tighter than a comparison and `and` binds looser -- so the oracle has to be
    written by hand or it is not one.
    """
    g, r, b = cols["g"], cols["r"], cols["b"]
    return {
        "(g>2) & (r<10)": (g > 2) & (r < 10),
        "g>5 | b<0.1": (g > 5) | (b < 0.1),
        "~(g>2) & (r>1)": ~(g > 2) & (r > 1),
        "(g-b)/(r-b) > 0.3": (g - b) / (r - b) > 0.3,
        "g>2 and r<10": (g > 2) & (r < 10),
        "g != 3": g != 3,
    }[query]


# --- the queries a caller actually writes ------------------------------------

@pytest.mark.parametrize("dtype", [np.float32, np.float64])
@pytest.mark.parametrize("query", QUERIES)
def test_a_query_agrees_with_numpy(query, dtype):
    cols = columns(dtype=dtype)
    got = mask_of(store_of(cols), query)
    np.testing.assert_array_equal(got, numpy_eval(query, cols), err_msg=query)


@pytest.mark.parametrize("query", QUERIES)
def test_count_is_the_number_of_selected_rows(query):
    cols = columns()
    s = store_of(cols)
    assert s.count_expression(query) == int(mask_of(s, query).sum())


def test_counting_does_not_touch_the_selection():
    s = store_of(columns())
    s.select_expression("g > 5")
    before = np.asarray(s.selection()).copy()
    s.count_expression("r < 1")
    np.testing.assert_array_equal(np.asarray(s.selection()), before)


@pytest.mark.parametrize("n", [1, 63, 64, 65, 511, 512, 513, 1000])
def test_a_row_count_that_is_not_a_whole_number_of_blocks(n):
    """512 rows to a block, 64 to a mask word. Every boundary is a place to
    write one bit too many, and the tail of the last word must stay clear."""
    cols = columns(n=n)
    got = mask_of(store_of(cols), "(g-b)/(r-b) > 0.3")
    np.testing.assert_array_equal(got, numpy_eval("(g-b)/(r-b) > 0.3", cols))


def test_an_empty_store_selects_nothing():
    s = tttrlib.DataStore()
    s.set_n_rows(0)
    s.add("g", np.zeros(0, dtype=np.float32))
    assert s.count_expression("g > 0") == 0


# --- truthiness ---------------------------------------------------------------

def test_a_bare_column_is_true_where_it_is_not_zero():
    """numpy's cast to bool, which is what pandas compares against: a negative
    value and a NaN are both true, and only an exact zero is false."""
    values = np.array([0.0, 1.0, -1.0, np.nan, np.inf, -0.0, 5e-324])
    s = store_of({"g": values})
    np.testing.assert_array_equal(mask_of(s, "g"), values.astype(bool))


def test_not_uses_the_same_rule():
    values = np.array([0.0, 1.0, -1.0, np.nan, 0.3])
    s = store_of({"g": values})
    np.testing.assert_array_equal(mask_of(s, "~g"), ~values.astype(bool))


def test_a_comparison_combined_with_a_plain_column():
    """`(g>2) and r` is legal: the right operand is a column of numbers, and
    the boolean combiner has to cast it rather than read whatever bytes the
    byte stack last held."""
    cols = {"g": np.array([1.0, 3.0, 3.0, 5.0]), "r": np.array([1.0, 0.0, 2.0, 0.0])}
    s = store_of(cols)
    np.testing.assert_array_equal(mask_of(s, "(g>2) and r"),
                                  (cols["g"] > 2) & (cols["r"] != 0))


def test_a_comparison_used_as_a_number():
    """`(g>2)*3` means 0 or 3. Reading the comparison off the wrong stack made
    this compute `g*3` instead."""
    g = np.array([1.0, 3.0, 0.5, 9.0])
    s = store_of({"g": g})
    np.testing.assert_array_equal(mask_of(s, "(g>2)*3 > 1"), g > 2)


def test_a_slot_is_not_still_a_mask_when_it_is_reused():
    """`a>0 and b<1 or c>2` loads `c` into a slot that last held `b<1`."""
    cols = {"a": np.array([1.0, -1.0, 1.0, -1.0]),
            "b": np.array([0.0, 0.0, 5.0, 5.0]),
            "c": np.array([3.0, 3.0, 0.0, 0.0])}
    s = store_of(cols)
    want = ((cols["a"] > 0) & (cols["b"] < 1)) | (cols["c"] > 2)
    np.testing.assert_array_equal(mask_of(s, "a>0 and b<1 or c>2"), want)


# --- arithmetic ---------------------------------------------------------------

def test_min_and_max_propagate_nan_as_numpy_does():
    """`numpy.minimum`, not C's `fmin` and not a ternary. A ternary made `min`
    non-commutative under NaN: `min(y, nan)` was `y` and `min(nan, y)` was
    `nan`."""
    nan = float("nan")
    g = np.array([1.0, 2.0, nan, 4.0, nan])
    r = np.array([3.0, nan, 5.0, 4.0, nan])
    s = store_of({"g": g, "r": r})
    for text, want in (("min(g, r) > 1.5", np.minimum(g, r) > 1.5),
                       ("min(r, g) > 1.5", np.minimum(r, g) > 1.5),
                       ("max(g, r) > 3.5", np.maximum(g, r) > 3.5),
                       ("max(r, g) > 3.5", np.maximum(r, g) > 3.5)):
        np.testing.assert_array_equal(mask_of(s, text), want, err_msg=text)


def test_python_precedence():
    g = np.array([2.0, 3.0])
    s = store_of({"g": g})
    # -2**2 is -4, and 2**3**2 is 512: ** binds tighter than unary minus and
    # is right-associative.
    assert s.count_expression("-g**2 < 0") == 2
    assert s.count_expression("g**3**2 > 100") == 2


def test_dividing_by_a_constant_is_a_real_division():
    """A divide by a broadcast value may only become a multiply by its
    reciprocal where that is exact. One ulp is a whole row in a gate."""
    g = np.arange(1, 20001, dtype=np.float64)
    s = store_of({"g": g})
    for divisor in (2.5, 3.0, 7.0, 2.0, 1024.0):
        want = (g / divisor) > 1234.5
        np.testing.assert_array_equal(mask_of(s, f"g/{divisor} > 1234.5"), want,
                                      err_msg=str(divisor))


# --- column types -------------------------------------------------------------

def test_integer_columns_are_read_through_their_own_type():
    cols = {"n": np.arange(-500, 500, dtype=np.int32),
            "u": np.arange(0, 1000, dtype=np.uint16)}
    s = store_of(cols)
    np.testing.assert_array_equal(mask_of(s, "n*2 > u"), cols["n"] * 2 > cols["u"])


def test_mixed_float32_and_int_columns():
    cols = {"g": np.linspace(0, 5, 300).astype(np.float32),
            "n": np.arange(300, dtype=np.int32)}
    s = store_of(cols)
    want = cols["g"].astype(np.float64) * 60 > cols["n"]
    np.testing.assert_array_equal(mask_of(s, "g*60 > n"), want)


def test_an_int64_column_beyond_2_to_the_53_is_refused():
    """Widening to double stops being exact there, and an `==` would silently
    match the wrong rows. Refuse rather than answer wrongly."""
    s = store_of({"id": np.array([1, 2, 2**60], dtype=np.int64)})
    with pytest.raises(ValueError):
        s.count_expression("id == 2")


def test_a_bool_column_still_gates():
    """Bool is bit-packed and has no numeric buffer for the block loader, so it
    is widened into the program's own doubles and evaluated by the same engine
    as everything else. Same answer, one copy slower. (Before 2026-09-02 this
    took an ExprTk fallback whose multi-argument functions were silently
    wrong — T-20260831-13.)"""
    ok = np.array([True, False, True, True, False])
    g = np.array([1.0, 2.0, 3.0, 0.5, 9.0])
    s = store_of({"ok": ok, "g": g})
    np.testing.assert_array_equal(mask_of(s, "ok and g>1"), ok & (g > 1))


def test_multi_argument_functions_are_right_over_a_widened_column():
    """The T-20260831-13 reproducer, on the one route that could still reach
    the old wrong answer: a Bool column used to force the whole query onto the
    ExprTk fallback, whose hypot/atan2 evaluated at element 0 and broadcast —
    hypot kept 8 rows where numpy keeps 6, atan2 kept 0 where numpy keeps 5.
    The board's exact vectors, asserting numpy's answers."""
    g = np.array([1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0])
    r = np.array([8.0, 1.0, 6.0, 2.0, 9.0, 3.0, 0.5, 4.0])
    ok = np.ones(8, dtype=bool)
    s = store_of({"g": g, "r": r, "ok": ok})
    got = mask_of(s, "ok and hypot(g, r) > 5")
    np.testing.assert_array_equal(got, np.hypot(g, r) > 5)
    assert got.sum() == 6
    got = mask_of(s, "ok and atan2(g, r) > 1.0")
    np.testing.assert_array_equal(got, np.arctan2(g, r) > 1.0)
    assert got.sum() == 5


def test_root_logn_and_frac_gate_like_numpy():
    """The last arity-1/2 names that used to force the fallback, now engine
    functions with ExprTk's semantics: root(x, n) = x**(1/n), logn(x, b) =
    log(x)/log(b), frac truncates toward zero."""
    z = np.array([0.25, 1.0, 4.0, 9.0, 16.0])
    s = store_of({"z": z})
    np.testing.assert_array_equal(mask_of(s, "root(z, 2) > 2"), np.sqrt(z) > 2)
    np.testing.assert_array_equal(mask_of(s, "logn(z, 2) > 2"), np.log2(z) > 2)
    f = np.array([-1.25, -0.5, 0.0, 0.5, 2.75])
    s2 = store_of({"f": f})
    np.testing.assert_array_equal(mask_of(s2, "frac(f) > 0.4"),
                                  (f - np.trunc(f)) > 0.4)


def test_what_the_evaluator_does_not_implement_is_refused_not_answered():
    """There is no second evaluator to fall through to any more. A reserved
    name the engine does not implement must raise — a gate that silently keeps
    the wrong rows is worse than one that refuses."""
    g = np.array([1.0, 2.0, 3.0])
    s = store_of({"g": g})
    for query in ("clamp(g, 0, 2) > 1",
                  "inrange(0, g, 2)",
                  "if(g > 1, 1, 0) > 0",
                  "avg(g) > 1",
                  "sum(g) > 1"):
        with pytest.raises(ValueError):
            s.count_expression(query)


# --- validity -----------------------------------------------------------------

def test_a_masked_row_is_never_selected():
    g = np.array([5.0, 5.0, 5.0, 5.0])
    s = store_of({"g": g})
    s["g"].set_mask(np.array([1, 0, 1, 0], dtype=np.uint8))
    np.testing.assert_array_equal(mask_of(s, "g > 1"),
                                  np.array([True, False, True, False]))


def test_a_row_recorded_as_never_measured_is_never_selected():
    """A column that stores its gaps as ranges rather than bits is honoured
    too. Asking `has_mask()` skipped those silently."""
    g = np.array([5.0, 5.0, 5.0, 5.0])
    s = store_of({"g": g})
    s["g"].add_na_range(1, 3, "that file did not have this column")
    np.testing.assert_array_equal(mask_of(s, "g > 1"),
                                  np.array([True, False, False, True]))


def test_only_the_columns_the_query_names_gate_it():
    """A mask on a column the expression never reads must not remove rows."""
    s = store_of({"g": np.array([5.0, 5.0]), "r": np.array([1.0, 1.0])})
    s["r"].set_mask(np.array([0, 0], dtype=np.uint8))
    np.testing.assert_array_equal(mask_of(s, "g > 1"), np.array([True, True]))


# --- composition and caching ---------------------------------------------------

def test_it_composes_with_the_other_gates():
    cols = columns(n=500)
    s = store_of(cols)
    s.select_expression("g > 5")
    s.select_expression("r < 10", tttrlib.DataStore.Combine_And)
    want = (cols["g"] > 5) & (cols["r"] < 10)
    np.testing.assert_array_equal(np.asarray(s.selection()).astype(bool), want)


def test_a_repeated_query_gives_the_same_answer_after_a_column_changes():
    """The compiled program is cached; the values it reads are not."""
    s = store_of({"g": np.array([1.0, 2.0, 3.0])})
    assert s.count_expression("g > 1.5") == 2
    s["g"].numpy()[:] = np.array([9.0, 9.0, 9.0])
    assert s.count_expression("g > 1.5") == 3


def test_an_unknown_column_is_refused():
    s = store_of(columns(n=10))
    with pytest.raises(ValueError):
        s.count_expression("nosuch > 1")


def test_a_query_that_does_not_parse_is_refused():
    s = store_of(columns(n=10))
    for text in ("g > ", "((g)", "g >< 1"):
        with pytest.raises(ValueError):
            s.count_expression(text)


# --- fuzz ----------------------------------------------------------------------

@pytest.mark.parametrize("seed", [0, 1])
def test_random_valid_queries_agree_with_numpy(seed):
    """A short run of the grammar fuzzer, so the harness cannot rot.

    `fuzz_expression.py` is the real thing -- 25,000 cases a seed, run by hand.
    This keeps enough of it in the suite to catch a regression in the typed
    stack, which is the part no hand-written test set reliably covers.
    """
    import fuzz_expression

    result = fuzz_expression.run(seed, cases=300, n_rows=1289)
    assert not result["failures"], result["failures"][:5]
    assert result["checked"] > 200, result

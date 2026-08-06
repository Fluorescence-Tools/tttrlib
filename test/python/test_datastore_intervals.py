"""Closed intervals, and what a missing value means to a gate.

``where()`` is the library's own rule: half-open, and a value the column marks
missing satisfies nothing. ``interval()`` exists because a front end whose gates
were written as ``(v >= lo) & (v <= hi)`` in numpy made both decisions the other
way, and reproducing its answers exactly is the difference between a migration
and a silent change to which points a figure contains.

The expectations here are written out by hand rather than computed with numpy,
because numpy's answer is precisely the thing one of the two conventions is
deliberately NOT.
"""
import numpy as np
import pytest
import tttrlib

#            0     1     2    3    4       5       6      7
VALUES = [-np.inf, -1.0, 0.0, 0.5, 1.0, np.inf, np.nan, 2.0]


def make_store(mask_non_finite=False):
    s = tttrlib.DataStore("intervals")
    s.set_n_rows(len(VALUES))
    s.add("v", np.array(VALUES, dtype=np.float64))
    if mask_non_finite:
        s["v"].mask_non_finite()
    return s


def selected(**kw):
    s = make_store(kw.pop("mask_non_finite", False))
    s.interval("v", **kw)
    return list(s.selection())


T, F = True, False


def test_closed_interval_keeps_both_endpoints():
    assert selected(lo=0.0, hi=1.0) == [F, F, T, T, T, F, F, F]


def test_half_open_upper_drops_the_upper_endpoint():
    """The reason a closed interval had to be added at all: half-open takes a
    visible bite out of a population binned or rounded onto the upper edge."""
    assert selected(lo=0.0, hi=1.0, hi_closed=False) == [F, F, T, T, F, F, F, F]


def test_half_open_lower_drops_the_lower_endpoint():
    assert selected(lo=0.0, hi=1.0, lo_closed=False) == [F, F, F, T, T, F, F, F]


def test_open_on_both_sides():
    assert selected(lo=0.0, hi=1.0, lo_closed=False, hi_closed=False) == \
        [F, F, F, T, F, F, F, F]


def test_nan_is_dropped_by_default_and_kept_when_asked():
    """The only value the two conventions disagree on, and they disagree on it
    completely: no comparison against NaN is true, so a gate written as a pair
    of comparisons never excludes one."""
    assert selected(lo=-np.inf, hi=np.inf)[6] == F
    assert selected(lo=-np.inf, hi=np.inf, missing_selected=True)[6] == T


def test_an_infinite_bound_is_a_comparison_not_a_missing_value():
    """A one-sided gate is written with an infinite bound, and a stored +inf is
    then inside a closed interval and outside an open one -- exactly as the
    arithmetic says, and not confused with "not measured"."""
    assert selected(lo=-np.inf, hi=np.inf) == [T, T, T, T, T, T, F, T]
    assert selected(lo=-np.inf, hi=np.inf, hi_closed=False) == \
        [T, T, T, T, T, F, F, T]
    assert selected(lo=None, hi=1.0) == [T, T, T, T, T, F, F, F]
    assert selected(lo=0.0, hi=None) == [F, F, T, T, T, T, F, T]


def test_a_masked_row_follows_the_same_rule_as_a_nan():
    """``mask_non_finite`` moves the non-finite values into the column's own
    "not measured" bit, and the gate has to treat them the same way it treats a
    NaN it can still see -- otherwise the answer depends on whether the caller
    happened to mask the column."""
    assert selected(lo=-np.inf, hi=np.inf, mask_non_finite=True) == \
        [F, T, T, T, T, F, F, T]
    assert selected(lo=-np.inf, hi=np.inf, mask_non_finite=True,
                    missing_selected=True) == [T, T, T, T, T, T, T, T]


def test_intervals_combine_like_every_other_condition():
    s = make_store()
    s.interval("v", lo=0.0, hi=None)
    s.interval("v", lo=None, hi=1.0, how="and")
    assert list(s.selection()) == [F, F, T, T, T, F, F, F]


def test_where_is_unchanged():
    """The library's own rule stays exactly what it was."""
    s = make_store()
    s.where("v", 0.0, 1.0)
    assert list(s.selection()) == [F, F, T, T, F, F, F, F]


@pytest.mark.parametrize("dtype", [np.float32, np.int32, np.int64])
def test_the_column_keeps_its_own_type(dtype):
    s = tttrlib.DataStore()
    s.set_n_rows(5)
    s.add("v", np.array([-2, 0, 1, 2, 3], dtype=dtype))
    s.interval("v", lo=0, hi=2)
    assert list(s.selection()) == [F, T, T, T, F]

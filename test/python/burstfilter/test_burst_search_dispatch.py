"""`TTTR::burst_search` dispatches on a name through a
table, not a chain of `if (mode == "...")`.

What the criterion is actually protecting: the chain lived inside the one
function every burst search must be reachable from, so adding a search meant
editing that function, and a search contributed from anywhere else could not be
reached by name at all. These tests pin the behaviour that has to survive the
change — every mode still reachable, still producing what its own entry point
produces, and an unknown name still falling back rather than raising.
"""
import numpy as np
import pytest

import tttrlib

from test_settings import settings, DATA_AVAILABLE  # type: ignore

pytestmark = pytest.mark.skipif(not DATA_AVAILABLE, reason="test data not available")

MODES = ["sliding_window", "cusum_sprt", "kalman", "maxtree",
         "bayesian_blocks", "bocpd"]


@pytest.fixture(scope="module")
def data():
    return tttrlib.TTTR(settings["spc132_filename"], "SPC-130")


@pytest.mark.parametrize("mode", MODES)
def test_every_mode_is_reachable_by_name(data, mode):
    out = np.asarray(data.burst_search(20, 10, 5e-4, mode), dtype=np.int64)
    assert out.ndim == 1
    assert out.size % 2 == 0, "bursts come back as flat [start, stop, ...] pairs"
    if out.size:
        starts, stops = out[0::2], out[1::2]
        assert np.all(stops >= starts)
        assert np.all(np.diff(starts) > 0), "bursts must be ordered and disjoint"


def test_sliding_window_matches_its_own_entry_point(data):
    by_name = np.asarray(data.burst_search(20, 10, 5e-4, "sliding_window"))
    direct = np.asarray(data.burst_search_sliding_window(20, 10, 5e-4))
    np.testing.assert_array_equal(by_name, direct)


def test_cusum_matches_its_own_entry_point(data):
    by_name = np.asarray(data.burst_search(20, 10, 5e-4, "cusum_sprt", 0.01, 0.01))
    direct = np.asarray(data.burst_search_cusum_sprt(20, 10, 5e-4, 0.01, 0.01))
    np.testing.assert_array_equal(by_name, direct)


def test_kalman_reinterprets_T_as_the_bin_width(data):
    """The narrow (L, m, T) signature cannot carry every algorithm's parameters,
    so three modes reinterpret `T`. That reinterpretation is behaviour, and it
    moved with the dispatch — so it is pinned here."""
    by_name = np.asarray(data.burst_search(20, 10, 1e-3, "kalman"))
    direct = np.asarray(data.burst_search_kalman(20, 1e-3))
    np.testing.assert_array_equal(by_name, direct)


def test_kalman_T_zero_keeps_the_default(data):
    by_name = np.asarray(data.burst_search(20, 10, 0.0, "kalman"))
    direct = np.asarray(data.burst_search_kalman(20, 1e-4))
    np.testing.assert_array_equal(by_name, direct)


def test_unknown_mode_falls_back_to_sliding_window(data):
    """Unvalidated on purpose: an unrecognised mode has always run the sliding
    window rather than failing, and callers rely on it."""
    fallback = np.asarray(data.burst_search(20, 10, 5e-4, "no_such_search"))
    sliding = np.asarray(data.burst_search_sliding_window(20, 10, 5e-4))
    np.testing.assert_array_equal(fallback, sliding)


def test_the_default_mode_is_the_sliding_window(data):
    np.testing.assert_array_equal(
        np.asarray(data.burst_search(20, 10, 5e-4)),
        np.asarray(data.burst_search_sliding_window(20, 10, 5e-4)))


@pytest.mark.slow
@pytest.mark.parametrize("mode", MODES)
def test_dispatch_is_stable_across_calls(data, mode):
    """A table keyed by name must not depend on call order or on which mode ran
    before it — the failure a shared mutable dispatcher would produce."""
    first = np.asarray(data.burst_search(20, 10, 5e-4, mode))
    for other in MODES:
        data.burst_search(20, 10, 5e-4, other)
    again = np.asarray(data.burst_search(20, 10, 5e-4, mode))
    np.testing.assert_array_equal(first, again)


def test_bocpd_matches_its_own_entry_point(data):
    by_name = np.asarray(data.burst_search(20, 10, 1e-3, "bocpd"))
    direct = np.asarray(data.burst_search_bocpd(20, 1e-3))
    np.testing.assert_array_equal(by_name, direct)


def test_coincident_says_so_instead_of_running_something_else(data):
    """`coincident` needs a channel grouping, which (L, m, T) cannot carry — so
    it cannot run through this door.

    What it used to do is the point: an unrecognised name falls back to the
    sliding window, `coincident` was unrecognised, and the call returned
    sliding-window bursts while the registry advertised a coincidence search. A
    plausible answer from the wrong algorithm is worse than an error."""
    with pytest.raises(Exception) as excinfo:
        data.burst_search(20, 10, 5e-4, "coincident")
    assert "burst_search_coincident" in str(excinfo.value), (
        "the error has to name the call that does work")


def test_every_registry_entry_with_a_method_is_dispatchable(data):
    """The registry advertises these names to any UI built on it. A name it
    lists that `burst_search` cannot reach is a promise the library does not
    keep.

    Asserting only the shape of the result is not enough, and was not: the
    fallback returns a well-shaped array for any name at all, so a search that
    silently ran the sliding window passed. Each name must produce what its own
    entry point produces, or say why it cannot."""
    import json
    entries = json.loads(tttrlib.TTTR.burst_search_algorithms_json())
    named = [n for n, e in entries.items() if e.get("method")]
    assert named, "the registry lists no dispatchable burst searches"

    sliding = np.asarray(data.burst_search(20, 10, 5e-4, "sliding_window"))
    for name in named:
        try:
            out = np.asarray(data.burst_search(20, 10, 5e-4, name), dtype=np.int64)
        except Exception as e:
            # Allowed only if it explains itself; see coincident.
            assert "instead" in str(e), f"{name}: unexplained failure: {e}"
            continue
        assert out.size % 2 == 0
        if name != "sliding_window":
            assert not (out.shape == sliding.shape and np.array_equal(out, sliding)), (
                f"{name} returned exactly the sliding window's bursts — it fell "
                f"through the dispatch rather than running")


# --- one registration path --------------------------------------------------
#
# The seven built-ins used to be described in a JSON literal
# (`kBurstSearchRegistry`) and dispatched from a separate table in another file.
# Two lists of the same algorithms, with nothing keeping them in step — and they
# did drift: `bocpd` and `coincident` were advertised with a `method` the
# dispatcher had never heard of, so calling them ran the sliding window instead.
#
# Each search now declares its description and its dispatch function in one
# `register_burst_search(descriptor, fn)` call, so the two cannot disagree.
# These tests pin what that buys, because the guarantee is structural and
# structure is exactly what a later refactor quietly gives up.

BUILTIN_SEARCHES = {"sliding_window", "cusum_sprt", "kalman", "bocpd",
                    "coincident", "maxtree", "bayesian_blocks"}

# Searches whose `method` carries a hand-written docstring, which replaces the
# signature SWIG would otherwise emit. Their schemas cannot be checked against
# the argument list; the set is asserted rather than assumed, so it cannot grow
# unnoticed.
NO_SIGNATURE_IN_DOC = {"coincident"}


def _builtin_entries():
    import json
    entries = json.loads(tttrlib.TTTR.burst_search_algorithms_json())
    return {n: e for n, e in entries.items()
            if e.get("provider", "builtin") == "builtin"}


def test_the_builtin_searches_are_exactly_the_seven():
    """A search added to the dispatch table but not described, or described but
    not dispatchable, is the failure this migration removes. Both halves come
    from one call now, so the set is one set."""
    assert set(_builtin_entries()) == BUILTIN_SEARCHES


@pytest.mark.parametrize("name", sorted(BUILTIN_SEARCHES))
def test_each_entry_identifies_itself_consistently(name):
    """`operation_type` is the dispatch key. If it disagreed with the key it is
    filed under, the registry would advertise one name and dispatch another —
    which is the original bug wearing a different hat."""
    e = _builtin_entries()[name]
    assert e["operation_type"] == name
    assert e["capability"] == "burst_search"
    assert e["provider"] == "builtin"
    assert e["name"] == name


@pytest.mark.parametrize("name", sorted(BUILTIN_SEARCHES))
def test_the_two_schema_names_are_one_schema(name):
    """`params_schema` is what ChiSurf, ndX and the web UI have always read;
    `settings_schema` is the descriptor's own name for it. They are the same
    object, not two that have to be maintained in parallel."""
    e = _builtin_entries()[name]
    assert e["params_schema"] == e["settings_schema"]
    assert e["params_schema"].get("type") == "object"
    assert e["params_schema"].get("properties"), "a search with no parameters"


@pytest.mark.parametrize("name", sorted(BUILTIN_SEARCHES))
def test_every_schema_property_names_a_real_keyword_argument(name):
    """The schema is what a UI builds a form from, and each property name is
    passed as a keyword argument to `method`. A property naming an argument the
    method does not take renders a control that cannot be submitted."""
    import re
    e = _builtin_entries()[name]
    method = getattr(tttrlib.TTTR, e["method"], None)
    assert method is not None, f"{name}: registry names a method that is absent"

    # The real parameter names, parsed out of the SWIG signature rather than
    # matched as substrings: `m` occurs inside `max_variation`, so a substring
    # test would accept a schema property that no argument is named after.
    # The SWIG signature is the occurrence whose argument list begins with
    # `self`. Taking the first occurrence instead finds the usage example in the
    # prose — `burst_search_coincident([[0, 1], [2, 3]])` — and parses its
    # brackets as argument names.
    doc = method.__doc__ or ""
    sig = re.search(re.escape(e["method"]) + r"\(\s*self\s*,([^)]*)\)", doc)
    if sig is None:
        # A hand-written `%feature("docstring")` replaces SWIG's autodoc, so
        # there is no signature to read. Named rather than skipped silently: if
        # a second method loses its signature this fails, instead of the test
        # quietly checking one fewer search every time someone writes a docstring.
        assert name in NO_SIGNATURE_IN_DOC, (
            f"{name}: no signature in {e['method']}'s docstring, and it is not "
            f"one of the known exceptions {sorted(NO_SIGNATURE_IN_DOC)}")
        pytest.skip(f"{e['method']} has a hand-written docstring, no signature")

    args = {a.split("=")[0].strip() for a in sig.group(1).split(",")}
    args.discard("")
    assert args, f"{name}: parsed no arguments out of {e['method']}"

    for prop in e["params_schema"]["properties"]:
        assert prop in args, (
            f"{name}: schema property '{prop}' is not an argument of "
            f"{e['method']}({', '.join(sorted(args))}); a form built from this "
            f"schema would submit an unknown keyword")

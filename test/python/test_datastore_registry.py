"""What is in memory, and that it goes away.

A session holds several tables at once -- a photon stream, the bursts from it,
a localisation table -- and each can be most of the memory in the process. Two
properties are asserted here: you can see what is there, and dropping the
Python handle actually frees it.
"""
import gc
import os

import numpy as np
import pytest
import tttrlib
from test_settings import DATA_ROOT, DATA_AVAILABLE  # type: ignore


def _ids():
    return {s["id"] for s in tttrlib.data_stores()}


def test_a_store_appears_and_disappears():
    # Collect first, so the baseline holds only stores that are genuinely still
    # reachable. Without this the baseline can include a store an earlier test
    # left unreachable but uncollected, the gc.collect() below reaps it too,
    # and the comparison fails saying the opposite of what happened -- the
    # store this test dropped did disappear; a different one did as well.
    # Python 3.13 is where it showed: the collector is lazier, so a leftover
    # survives long enough to be counted.
    gc.collect()
    before = _ids()
    s = tttrlib.DataStore("scratch")
    s.add("x", np.zeros(1000))
    assert set(_ids()) - before, "a live store is not listed"
    entry = [e for e in tttrlib.data_stores() if e["id"] == s.id()][0]
    assert entry["label"] == "scratch"
    assert entry["rows"] == 1000
    assert entry["columns"] == 1

    del s
    gc.collect()
    assert _ids() == before, "the store is still listed after being dropped"


def test_dropping_the_handle_frees_the_memory():
    """Refcount semantics: no explicit close, no leak."""
    before = tttrlib.live_data_store_bytes()
    s = tttrlib.DataStore("big")
    s.add("x", np.zeros(1_000_000))          # 8 MB
    assert tttrlib.live_data_store_bytes() - before > 7e6

    del s
    gc.collect()
    assert tttrlib.live_data_store_bytes() == before, "memory was not released"


def test_release_frees_without_dropping_the_handle():
    """For a cache eviction, where the handle cannot be dropped yet."""
    s = tttrlib.DataStore("evictable")
    s.add("x", np.zeros(500_000))
    assert s.nbytes() > 3e6
    s.release()
    assert s.nbytes() == 0
    assert s.n_columns() == 0
    assert s.id() in _ids(), "the store itself is still alive, just empty"


def test_a_copy_is_listed_separately():
    """A copy is a second table holding second memory, and should say so."""
    s = tttrlib.DataStore("original")
    s.add("x", np.zeros(100_000))
    n_before = len(tttrlib.data_stores())
    t = tttrlib.DataStore(s)
    assert len(tttrlib.data_stores()) == n_before + 1
    assert t.id() != s.id()
    del t
    gc.collect()
    assert len(tttrlib.data_stores()) == n_before


@pytest.mark.skipif(not DATA_AVAILABLE, reason="test data not available")
def test_a_tttr_object_is_a_store_and_is_released_with_it():
    """The photons live in a DataStore, and the TTTR is a view onto it."""
    path = None
    for dirpath, _, files in os.walk(DATA_ROOT):
        for fn in sorted(files):
            if fn.lower().endswith(".ht3"):
                path = os.path.join(dirpath, fn)
                break
        if path:
            break
    if path is None:
        pytest.skip("no .ht3 file in the test data")

    before = _ids()
    before_bytes = tttrlib.live_data_store_bytes()
    d = tttrlib.TTTR(path)

    new = set(_ids()) - before
    assert new, "a TTTR's photons are not registered as a store"
    entry = [e for e in tttrlib.data_stores() if e["id"] in new][0]
    assert "TTTR" in entry["label"]
    assert os.path.basename(path) in entry["label"], "the label says which file"
    assert entry["rows"] == d.n_valid_events
    assert entry["columns"] == 4

    store = d.data()
    assert store.names == ["macro_time", "micro_time", "routing_channel", "event_type"]

    del store, entry
    del d
    gc.collect()
    assert _ids() == before, "the photon store outlived the TTTR"
    assert tttrlib.live_data_store_bytes() == before_bytes


@pytest.mark.skipif(not DATA_AVAILABLE, reason="test data not available")
def test_the_photon_columns_are_the_arrays_tttr_exposes():
    """The store is not a copy of the events -- it IS the events."""
    path = None
    for dirpath, _, files in os.walk(DATA_ROOT):
        for fn in sorted(files):
            if fn.lower().endswith(".ht3"):
                path = os.path.join(dirpath, fn)
                break
        if path:
            break
    if path is None:
        pytest.skip("no .ht3 file in the test data")

    d = tttrlib.TTTR(path)
    s = d.data()
    assert np.array_equal(np.asarray(s["routing_channel"].numpy()),
                          np.asarray(d.routing_channels))
    assert np.array_equal(np.asarray(s["micro_time"].numpy()),
                          np.asarray(d.micro_times))


def test_the_report_is_printable():
    s = tttrlib.DataStore("printme")
    s.add("x", np.zeros(10))
    text = tttrlib.data_store_report()
    assert "printme" in text
    assert "total" in text

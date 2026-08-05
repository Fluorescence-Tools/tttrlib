"""The Python surface NDXplorer would use: zero copy, and safe.

Two things are asserted here that no C++ test can: that reading a histogram
does not copy it, and that a view cannot outlive the buffer it points into.
"""
import gc

import numpy as np
import pytest
import tttrlib


def _h(*axes, **kw):
    return tttrlib.HistogramNd(tttrlib.AxisVector(list(axes)), kw.get("variance", False))


# --- numpy compatibility ----------------------------------------------------

@pytest.fixture(scope="module")
def sample():
    rng = np.random.default_rng(11)
    return rng.uniform(-1, 11, 5000), rng.uniform(-1, 11, 5000)


def test_histogram_matches_numpy(sample):
    x, _ = sample
    c, e = tttrlib.histogram(x, bins=10, range=(0, 10))
    nc, ne = np.histogram(x, bins=10, range=(0, 10))
    assert np.array_equal(c, nc)
    assert np.allclose(e, ne)


def test_histogram2d_matches_numpy(sample):
    x, y = sample
    H, xe, ye = tttrlib.histogram2d(x, y, bins=(8, 5), range=((0, 10), (0, 10)))
    nH, nxe, nye = np.histogram2d(x, y, bins=(8, 5), range=((0, 10), (0, 10)))
    assert np.array_equal(H, nH)
    assert np.allclose(xe, nxe) and np.allclose(ye, nye)


def test_histogramdd_matches_numpy(sample):
    x, y = sample
    s = np.column_stack([x, y])
    H, edges = tttrlib.histogramdd(s, bins=[6, 7], range=[(0, 10), (0, 10)])
    nH, nedges = np.histogramdd(s, bins=[6, 7], range=[(0, 10), (0, 10)])
    assert np.array_equal(H, nH)
    for a, b in zip(edges, nedges):
        assert np.allclose(a, b)


def test_explicit_edges_are_accepted(sample):
    x, _ = sample
    edges = np.array([0.0, 1.0, 4.0, 9.0, 10.0])
    c, e = tttrlib.histogram(x, bins=edges)
    nc, ne = np.histogram(x, bins=edges)
    assert np.array_equal(c, nc)
    assert np.allclose(e, ne)


def test_flow_reports_what_numpy_discards(sample):
    x, _ = sample
    c, _ = tttrlib.histogram(x, bins=10, range=(0, 10), flow=True)
    assert c[0] == int((x < 0).sum())
    assert c[-1] == int((x >= 10).sum())
    assert c.sum() == len(x), "with flow bins nothing is lost"


def test_weights_are_honoured(sample):
    x, _ = sample
    w = np.full(len(x), 0.25)
    c, _ = tttrlib.histogram(x, bins=10, range=(0, 10), weights=w)
    nc, _ = np.histogram(x, bins=10, range=(0, 10), weights=w)
    assert np.allclose(c, nc)


# --- zero copy --------------------------------------------------------------

def test_view_does_not_copy():
    """Writing through the view writes into the histogram."""
    h = _h(tttrlib.Axis.regular(4, 0.0, 4.0))
    h.fill(np.array([0.5, 1.5]))
    v = h.view()
    v[0] = 99.0
    assert h.view()[0] == 99.0
    assert h.sum(False) == 100.0


def test_view_shares_memory_with_the_flow_view():
    h = _h(tttrlib.Axis.regular(4, 0.0, 4.0))
    h.fill(np.array([0.5]))
    assert np.shares_memory(h.view(), h.view(flow=True))


def test_a_large_histogram_is_free_to_look_at():
    """A copy would be 8 MB; a view is a pointer."""
    h = _h(tttrlib.Axis.regular(1024, 0.0, 1.0),
           tttrlib.Axis.regular(1024, 0.0, 1.0))
    v = h.view()
    assert v.shape == (1024, 1024)
    assert np.shares_memory(v, h.view())


# --- safety -----------------------------------------------------------------

def test_a_view_keeps_its_histogram_alive():
    """The dangling-pointer case, and the reason for the ndarray subclass.

    reshape and slice both make derived arrays, and numpy only propagates a
    subclass attribute through __array_finalize__ -- without it the owner is
    lost by the time view() returns.
    """
    h = _h(tttrlib.Axis.regular(4, 0.0, 4.0))
    h.fill(np.array([0.5, 1.5, 2.5]))
    v = h.view()
    assert v._owner is h

    del h
    gc.collect()
    assert v.sum() == 3.0, "the buffer was freed under the view"


def test_a_growing_axis_gets_a_copy_not_a_view():
    """A fill can reallocate a growing axis, and an array already handed out
    cannot be revoked -- so those never get a view in the first place."""
    h = _h(tttrlib.Axis.regular(2, 0.0, 2.0, tttrlib.AxisOptions.growing()))
    h.fill(np.array([0.5, 1.5]))
    v = h.view()
    assert not h.can_view()
    h.fill(np.array([50.0]))          # reallocates
    assert not np.shares_memory(v, h.view())
    assert v.sum() == 2.0             # the copy is still valid and unchanged
    assert h.sum(True) == 3.0


# --- the rest of the surface ------------------------------------------------

def test_axes_expose_edges_centers_and_widths():
    a = tttrlib.Axis.log(3, 1.0, 1000.0)
    assert np.allclose(a.edges, [1.0, 10.0, 100.0, 1000.0])
    assert len(a.centers) == 3
    assert np.allclose(a.widths, [9.0, 90.0, 900.0]), "a log axis has unequal widths"
    assert len(a) == 3


def test_fill_accepts_columns_or_rows():
    rng = np.random.default_rng(12)
    x, y, z = (rng.uniform(0, 10, 500) for _ in range(3))
    ax = [tttrlib.Axis.regular(5, 0.0, 10.0) for _ in range(3)]
    a = _h(*ax)
    a.fill(x, y, z)
    b = _h(*[tttrlib.Axis.regular(5, 0.0, 10.0) for _ in range(3)])
    b.fill(np.column_stack([x, y, z]))
    assert np.array_equal(a.view(), b.view())


def test_to_numpy_matches_histogramdd(sample):
    x, y = sample
    h = _h(tttrlib.Axis.regular(4, 0.0, 10.0), tttrlib.Axis.regular(5, 0.0, 10.0))
    h.fill(x, y)
    values, xe, ye = h.to_numpy()
    ref, (rxe, rye) = np.histogramdd(np.column_stack([x, y]),
                                     bins=[4, 5], range=[(0, 10), (0, 10)])
    assert np.array_equal(values, ref)
    assert np.allclose(xe, rxe) and np.allclose(ye, rye)


def test_asarray_works():
    h = _h(tttrlib.Axis.regular(3, 0.0, 3.0))
    h.fill(np.array([0.5, 1.5, 1.5]))
    assert np.array_equal(np.asarray(h), [1.0, 2.0, 0.0])


def test_reset_add_and_scale():
    h = _h(tttrlib.Axis.regular(3, 0.0, 3.0))
    h.fill(np.array([0.5, 1.5]))
    g = _h(tttrlib.Axis.regular(3, 0.0, 3.0))
    g.fill(np.array([0.5, 2.5]))
    h.add(g)
    assert h.sum(False) == 4.0
    h.scale(0.5)
    assert h.sum(False) == 2.0
    h.reset()
    assert h.empty(True)


def test_slice_keeps_the_total_and_crop_does_not():
    h = _h(tttrlib.Axis.regular(8, 0.0, 8.0))
    h.fill(np.arange(8) + 0.5)
    s = h.slice(0, 2, 5)
    assert s.axis(0).size() == 3
    assert s.sum(True) == h.sum(True), "a slice moves the rest into flow"
    assert s.sum(False) == 3.0

    c = h.crop(0, 2, 5)
    assert c.sum(True) == 3.0, "a crop discards what is outside"


def test_shrink_takes_values_not_indices():
    h = _h(tttrlib.Axis.regular(10, 0.0, 10.0))
    h.fill(np.arange(10) + 0.5)
    s = h.shrink(0, 2.0, 5.0)
    assert s.axis(0).lo() == 2.0
    assert s.sum(False) == 4.0        # bins covering 2..6
    assert s.sum(True) == h.sum(True)


def test_variance_view_is_none_without_tracking():
    h = _h(tttrlib.Axis.regular(3, 0.0, 3.0))
    h.fill(np.array([0.5]))
    assert h.variance_view() is None

    g = _h(tttrlib.Axis.regular(3, 0.0, 3.0), variance=True)
    g.fill(np.array([0.5]), weight=np.array([2.0]))
    assert np.allclose(g.variance_view(), [4.0, 0.0, 0.0])


def test_repr_says_what_the_axes_are():
    h = _h(tttrlib.Axis.regular(4, 0.0, 4.0), tttrlib.Axis.log(3, 1.0, 1000.0))
    assert "regular" in repr(h) and "log" in repr(h)

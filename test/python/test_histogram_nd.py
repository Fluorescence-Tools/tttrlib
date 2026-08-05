"""HistogramNd against boost-histogram, feature by feature.

The point of this file is parity, so every assertion that can be made against
boost is made against boost rather than against a hand-computed expectation --
a hand-computed expectation only proves tttrlib agrees with whoever wrote the
test.
"""
import numpy as np
import pytest
import tttrlib

bh = pytest.importorskip("boost_histogram", reason="boost-histogram not installed")


def _axes(*ax):
    return tttrlib.AxisVector(list(ax))


@pytest.fixture(scope="module")
def sample():
    rng = np.random.default_rng(20260805)
    # deliberately spills off both ends, so flow bins are exercised
    return rng.uniform(-2.0, 12.0, 20000), rng.uniform(-2.0, 12.0, 20000)


# --- axis kinds -------------------------------------------------------------

def test_regular_axis_matches_boost(sample):
    x, _ = sample
    h = tttrlib.HistogramNd(_axes(tttrlib.Axis.regular(10, 0.0, 10.0)))
    h.fill_1d(x)

    b = bh.Histogram(bh.axis.Regular(10, 0, 10))
    b.fill(x)

    assert np.array_equal(h.get_values_without_flow(), b.view())
    assert h.sum(False) == b.sum()
    assert h.sum(True) == b.sum(flow=True)


def test_flow_bins_match_boost(sample):
    """The values that fell off the ends, which histogram1D drops silently."""
    x, _ = sample
    h = tttrlib.HistogramNd(_axes(tttrlib.Axis.regular(10, 0.0, 10.0)))
    h.fill_1d(x)
    stored = h.get_values()          # underflow, 10 bins, overflow

    b = bh.Histogram(bh.axis.Regular(10, 0, 10))
    b.fill(x)
    b_flow = b.view(flow=True)

    assert len(stored) == 12
    assert np.array_equal(stored, b_flow)
    assert stored[0] == int((x < 0).sum())
    assert stored[-1] == int((x >= 10).sum())


def test_log_axis_matches_boost():
    rng = np.random.default_rng(1)
    x = rng.uniform(0.5, 2000.0, 20000)
    h = tttrlib.HistogramNd(_axes(tttrlib.Axis.log(12, 1.0, 1000.0)))
    h.fill_1d(x)

    b = bh.Histogram(bh.axis.Regular(12, 1, 1000, transform=bh.axis.transform.log))
    b.fill(x)
    assert np.array_equal(h.get_values_without_flow(), b.view())


def test_sqrt_axis_matches_boost():
    rng = np.random.default_rng(2)
    x = rng.uniform(0.0, 120.0, 20000)
    h = tttrlib.HistogramNd(_axes(tttrlib.Axis.sqrt_axis(10, 0.0, 100.0)))
    h.fill_1d(x)

    b = bh.Histogram(bh.axis.Regular(10, 0, 100, transform=bh.axis.transform.sqrt))
    b.fill(x)
    assert np.array_equal(h.get_values_without_flow(), b.view())


def test_pow_axis_matches_boost():
    rng = np.random.default_rng(3)
    x = rng.uniform(0.0, 120.0, 20000)
    h = tttrlib.HistogramNd(_axes(tttrlib.Axis.pow_axis(10, 1.0, 100.0, 0.5)))
    h.fill_1d(x)

    b = bh.Histogram(bh.axis.Regular(10, 1, 100, transform=bh.axis.transform.Pow(0.5)))
    b.fill(x)
    assert np.array_equal(h.get_values_without_flow(), b.view())


def test_variable_axis_matches_boost(sample):
    x, _ = sample
    edges = np.array([0.0, 0.5, 2.0, 3.0, 7.0, 10.0])
    h = tttrlib.HistogramNd(_axes(tttrlib.Axis.variable(edges)))
    h.fill_1d(x)

    b = bh.Histogram(bh.axis.Variable(edges))
    b.fill(x)
    assert np.array_equal(h.get_values_without_flow(), b.view())


def test_integer_axis_matches_boost():
    rng = np.random.default_rng(4)
    xi = rng.integers(-5, 10, 20000)
    h = tttrlib.HistogramNd(_axes(tttrlib.Axis.integer(-2, 5)))
    h.fill_1d(xi.astype(float))

    b = bh.Histogram(bh.axis.Integer(-2, 5))
    b.fill(xi)          # boost insists on an integer array for an integer axis
    assert np.array_equal(h.get_values_without_flow(), b.view())


def test_category_axis_matches_boost():
    rng = np.random.default_rng(5)
    xi = rng.choice([10, 20, 30, 99], 20000)
    cats = np.array([10, 20, 30], dtype=np.int32)
    h = tttrlib.HistogramNd(_axes(tttrlib.Axis.category(cats)))
    h.fill_1d(xi.astype(float))

    b = bh.Histogram(bh.axis.IntCategory([10, 20, 30], growth=False))
    b.fill(xi)          # as above
    assert np.array_equal(h.get_values_without_flow(), b.view())
    # everything that is not a listed category lands in the "other" bin
    assert h.get_values()[-1] == int((xi == 99).sum())


def test_boolean_axis():
    x = np.array([0.0, 1.0, 1.0, 0.0, 7.0])
    h = tttrlib.HistogramNd(_axes(tttrlib.Axis.boolean()))
    h.fill_1d(x)
    v = h.get_values()
    assert len(v) == 2          # no flow on a boolean
    assert list(v) == [2.0, 3.0]


def test_circular_axis_wraps():
    """Nothing falls off a circle, so a circular axis has no flow bins."""
    a = tttrlib.Axis.regular(4, 0.0, 4.0, tttrlib.AxisOptions.circular_())
    assert a.extent() == 4
    x = np.array([0.5, 4.5, -0.5, 8.5])
    h = tttrlib.HistogramNd(_axes(a))
    h.fill_1d(x)
    assert h.sum(True) == 4.0
    assert list(h.get_values()) == [3.0, 0.0, 0.0, 1.0]


def test_growth_extends_the_axis_and_keeps_the_counts():
    a = tttrlib.Axis.regular(2, 0.0, 2.0, tttrlib.AxisOptions.growing())
    h = tttrlib.HistogramNd(_axes(a))
    h.fill_1d(np.array([0.5, 1.5]))
    assert h.sum(True) == 2.0
    h.fill_1d(np.array([5.5]))
    assert h.axis(0).size() >= 6
    assert h.sum(True) == 3.0, "growing the axis lost what was already in it"


# --- storage ----------------------------------------------------------------

def test_weighted_fill_matches_boost_including_variance():
    rng = np.random.default_rng(6)
    x = rng.uniform(0, 10, 5000)
    w = rng.uniform(0.5, 2.0, 5000)

    h = tttrlib.HistogramNd(_axes(tttrlib.Axis.regular(10, 0.0, 10.0)),
                            True)   # track variance
    h.fill_1d_weighted(x, w)

    b = bh.Histogram(bh.axis.Regular(10, 0, 10), storage=bh.storage.Weight())
    b.fill(x, weight=w)

    assert np.allclose(h.get_values_without_flow(), b.view()["value"])
    var = h.get_variances()[1:-1]        # strip the flow bins
    assert np.allclose(var, b.view()["variance"])


def test_unweighted_fill_does_not_pay_for_variance():
    h = tttrlib.HistogramNd(_axes(tttrlib.Axis.regular(4, 0.0, 4.0)))
    h.fill_1d(np.array([0.5, 1.5]))
    assert not h.tracks_variance()
    assert len(h.get_variances()) == 0


# --- N dimensions and algorithms --------------------------------------------

def test_2d_matches_boost(sample):
    x, y = sample
    h = tttrlib.HistogramNd(_axes(tttrlib.Axis.regular(8, 0.0, 10.0),
                                  tttrlib.Axis.regular(5, 0.0, 10.0)))
    h.fill_2d(x, y)

    b = bh.Histogram(bh.axis.Regular(8, 0, 10), bh.axis.Regular(5, 0, 10))
    b.fill(x, y)

    assert np.array_equal(h.get_values_without_flow().reshape(8, 5), b.view())
    assert h.sum(True) == b.sum(flow=True)


def test_3d_and_projection_match_boost():
    rng = np.random.default_rng(7)
    x, y, z = (rng.uniform(0, 10, 8000) for _ in range(3))
    ax = [tttrlib.Axis.regular(4, 0.0, 10.0),
          tttrlib.Axis.regular(5, 0.0, 10.0),
          tttrlib.Axis.regular(6, 0.0, 10.0)]
    h = tttrlib.HistogramNd(_axes(*ax))
    h.fill_rows(np.column_stack([x, y, z]))

    b = bh.Histogram(bh.axis.Regular(4, 0, 10), bh.axis.Regular(5, 0, 10),
                     bh.axis.Regular(6, 0, 10))
    b.fill(x, y, z)
    assert np.array_equal(h.get_values_without_flow().reshape(4, 5, 6), b.view())

    p = h.project(np.array([0, 2], dtype=np.int32))
    bp = b.project(0, 2)
    assert np.array_equal(p.get_values_without_flow().reshape(4, 6), bp.view())

    # projecting keeps every count, flow included
    assert p.sum(True) == h.sum(True)


def test_projection_can_reorder_axes():
    rng = np.random.default_rng(8)
    x, y = rng.uniform(0, 10, 2000), rng.uniform(0, 10, 2000)
    h = tttrlib.HistogramNd(_axes(tttrlib.Axis.regular(4, 0.0, 10.0),
                                  tttrlib.Axis.regular(5, 0.0, 10.0)))
    h.fill_2d(x, y)
    a = h.get_values_without_flow().reshape(4, 5)
    t = h.project(np.array([1, 0], dtype=np.int32))
    assert np.array_equal(t.get_values_without_flow().reshape(5, 4), a.T)


def test_rebin_matches_boost_and_conserves(sample):
    x, _ = sample
    h = tttrlib.HistogramNd(_axes(tttrlib.Axis.regular(12, 0.0, 12.0)))
    h.fill_1d(x)
    r = h.rebin(0, 3)

    b = bh.Histogram(bh.axis.Regular(12, 0, 12))
    b.fill(x)
    br = b[bh.rebin(3)]

    assert r.axis(0).size() == 4
    assert np.array_equal(r.get_values_without_flow(), br.view())
    # nothing may be lost, flow included -- that is what makes it a rebin
    assert r.sum(True) == h.sum(True)
    assert r.sum(False) == h.sum(False)


def test_sum_and_empty():
    h = tttrlib.HistogramNd(_axes(tttrlib.Axis.regular(4, 0.0, 4.0)))
    assert h.empty(True)
    h.fill_1d(np.array([100.0]))     # overflow only
    assert not h.empty(True)
    assert h.empty(False), "an overflow-only histogram has an empty interior"
    assert h.sum(True) == 1.0
    assert h.sum(False) == 0.0


def test_threads_do_not_change_the_answer():
    rng = np.random.default_rng(9)
    x, y = rng.uniform(0, 10, 400000), rng.uniform(0, 10, 400000)
    ref = None
    for threads in (1, 2, 8):
        h = tttrlib.HistogramNd(_axes(tttrlib.Axis.regular(16, 0.0, 10.0),
                                      tttrlib.Axis.regular(16, 0.0, 10.0)))
        h.fill_2d(x, y, threads)
        v = h.get_values()
        if ref is None:
            ref = v
        else:
            assert np.array_equal(v, ref), f"{threads} threads disagreed"

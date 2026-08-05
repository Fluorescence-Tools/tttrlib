"""Two-dimensional histograms.

The invariant that matters is consistency with the one-dimensional path: a pair
belongs in bin (i, j) exactly when its x would land in bin i of a 1D histogram
over x and its y in bin j of one over y. Anything else makes 2D plots disagree
with the marginals drawn beside them.
"""
import numpy as np
import pytest
import tttrlib


def _hist2d(x, y, ex, ey, axis_x="lin", axis_y="lin", weights=None):
    w = np.ones_like(x, dtype=np.float64) if weights is None else weights
    h = np.zeros(len(ex) * len(ey), dtype=np.float64)
    tttrlib.histogram2D_double(x, y, w, ex, ey, h, axis_x, axis_y,
                               weights is not None)
    return h.reshape(len(ex), len(ey))


def _hist1d(v, edges, axis="lin"):
    h = np.zeros(len(edges), dtype=np.float64)
    tttrlib.histogram1D_double(v, np.ones_like(v), edges, h, axis, False)
    return h


@pytest.fixture(scope="module")
def sample():
    rng = np.random.default_rng(20260805)
    return rng.uniform(0, 10, 5000), rng.uniform(0, 10, 5000)


def test_marginals_agree_with_the_1d_histogram(sample):
    """The whole point: the 2D binning is the 1D binning, twice."""
    x, y = sample
    ex, ey = np.linspace(0, 10, 11), np.linspace(0, 10, 9)
    h = _hist2d(x, y, ex, ey)
    assert np.array_equal(h.sum(axis=1), _hist1d(x, ex))
    assert np.array_equal(h.sum(axis=0), _hist1d(y, ey))


def test_the_axes_are_independent(sample):
    """Different bin counts per axis, and x is the slow axis."""
    x, y = sample
    ex, ey = np.linspace(0, 10, 11), np.linspace(0, 10, 9)
    assert _hist2d(x, y, ex, ey).shape == (len(ex), len(ey))


def test_pairs_outside_either_axis_are_dropped_not_clamped():
    """Clamping would pile everything out of range onto the border bins,
    which reads as structure that is not in the data."""
    ex = ey = np.linspace(0, 10, 11)
    x = np.array([5.0, -1.0, 5.0, 99.0], dtype=np.float64)
    y = np.array([5.0, 5.0, -1.0, 99.0], dtype=np.float64)
    h = _hist2d(x, y, ex, ey)
    assert h.sum() == 1.0, "only the in-range pair should be counted"
    assert h[0, 0] == 0.0 and h[-1, -1] == 0.0


def test_weights_are_summed_instead_of_counted():
    ex = ey = np.linspace(0, 10, 11)
    x = np.array([2.0, 2.0, 8.0], dtype=np.float64)
    y = np.array([3.0, 3.0, 7.0], dtype=np.float64)
    w = np.array([0.5, 0.25, 4.0], dtype=np.float64)
    h = _hist2d(x, y, ex, ey, weights=w)
    assert h.sum() == pytest.approx(4.75)


def test_a_log_axis_can_be_paired_with_a_linear_one():
    """Lifetime against intensity wants exactly this, and pre-transforming the
    data in Python is what it exists to avoid."""
    x = np.array([1.0, 10.0, 100.0, 1000.0], dtype=np.float64)   # log axis
    y = np.array([1.0, 2.0, 3.0, 4.0], dtype=np.float64)         # linear
    ex = np.logspace(0, 3, 4)
    ey = np.linspace(0, 5, 6)
    h = _hist2d(x, y, ex, ey, axis_x="log10", axis_y="lin")
    assert h.sum() == 4.0
    # A decade apart in x means a different x-bin for each point.
    assert (h.sum(axis=1) > 0).sum() == 4


def test_a_non_positive_value_on_a_log_axis_is_dropped():
    """log10(0) is not a bin index."""
    x = np.array([0.0, -5.0, 10.0], dtype=np.float64)
    y = np.array([1.0, 1.0, 1.0], dtype=np.float64)
    h = _hist2d(x, y, np.logspace(0, 3, 4), np.linspace(0, 5, 6),
                axis_x="log10", axis_y="lin")
    assert h.sum() == 1.0


def test_mismatched_lengths_do_not_read_past_the_shorter_array():
    """The shorter array bounds the loop; over-reading would be silent."""
    ex = ey = np.linspace(0, 10, 11)
    x = np.array([1.0, 2.0, 3.0, 4.0, 5.0], dtype=np.float64)
    y = np.array([1.0, 2.0], dtype=np.float64)
    assert _hist2d(x, y, ex, ey).sum() == 2.0


def test_integer_data_works_too():
    ex = ey = np.arange(0, 11, dtype=np.int32)
    x = np.array([1, 5, 9], dtype=np.int32)
    y = np.array([2, 5, 8], dtype=np.int32)
    h = np.zeros(len(ex) * len(ey), dtype=np.float64)
    tttrlib.histogram2D_int(x, y, np.ones(len(x)), ex, ey, h, "lin", "lin", False)
    assert h.sum() == 3.0


def test_matches_numpy_bin_for_bin(sample):
    """Faster is only worth anything if it is also right.

    tttrlib's `n_bins` is the number of BINS and bin i starts at edges[i], so
    the equivalent numpy call needs one more edge appended.
    """
    x, y = sample
    ex, ey = np.linspace(0, 10, 33), np.linspace(0, 10, 17)
    h = _hist2d(x, y, ex, ey)
    wx, wy = ex[1] - ex[0], ey[1] - ey[0]
    ref, _, _ = np.histogram2d(
        x, y,
        bins=[np.concatenate([ex, [ex[-1] + wx]]),
              np.concatenate([ey, [ey[-1] + wy]])])
    assert np.array_equal(h, ref)


def test_1d_does_not_write_past_the_last_bin():
    """Regression: the bound was `<= n_bins`, so a value one bin above the top
    edge indexed hist[n_bins] -- and callers size hist to exactly n_bins."""
    edges = np.linspace(0.0, 10.0, 11)
    for v in (11.0, 11.9, 1e6):
        buf = np.zeros(20, dtype=np.float64)
        tttrlib.histogram1D_double(np.array([v]), np.ones(1), edges, buf,
                                   "lin", False)
        assert buf.sum() == 0.0, f"{v} was binned into index {np.nonzero(buf)[0]}"


# --- the range forms: "64 bins from 0 to 100" without an edges array ---------

def test_range_forms_match_the_edge_forms(sample):
    """A front end has a bin count and a range, not an array of edges."""
    x, y = sample
    nb = 33
    edges = np.zeros(nb)
    tttrlib.make_bin_edges_double(edges, 0.0, 10.0, False)
    assert np.allclose(edges, np.linspace(0, 10, nb))

    a = np.zeros(nb)
    tttrlib.histogram1D_double(x, np.ones_like(x), edges, a, "lin", False)
    b = np.zeros(nb)
    tttrlib.histogram1D_range_double(x, np.ones_like(x), 0.0, 10.0, nb, b,
                                     False, False)
    assert np.array_equal(a, b)

    nx, ny = 17, 9
    ex, ey = np.zeros(nx), np.zeros(ny)
    tttrlib.make_bin_edges_double(ex, 0.0, 10.0, False)
    tttrlib.make_bin_edges_double(ey, 0.0, 10.0, False)
    h2 = np.zeros(nx * ny)
    tttrlib.histogram2D_double(x, y, np.ones_like(x), ex, ey, h2, "lin", "lin", False)
    r2 = np.zeros(nx * ny)
    tttrlib.histogram2D_range_double(x, y, np.ones_like(x), 0.0, 10.0, nx,
                                     0.0, 10.0, ny, r2, False, False, False)
    assert np.array_equal(h2, r2)


def test_a_log_range_gives_geometric_edges():
    e = np.zeros(4)
    tttrlib.make_bin_edges_double(e, 1.0, 1000.0, True)
    assert np.allclose(e, [1.0, 10.0, 100.0, 1000.0])


def test_thread_count_does_not_change_the_answer(sample):
    """Private per-thread histograms summed at the end must give what one
    thread gives, whatever the split."""
    x, y = sample
    ex, ey = np.linspace(0, 10, 33), np.linspace(0, 10, 17)
    ref = None
    for threads in (1, 2, 3, 8):
        h = np.zeros(len(ex) * len(ey))
        tttrlib.histogram2D_double(x, y, np.ones_like(x), ex, ey, h,
                                   "lin", "lin", False, threads)
        if ref is None:
            ref = h
        else:
            assert np.array_equal(h, ref), f"{threads} threads disagreed"

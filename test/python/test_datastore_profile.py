"""Parameter maps: bin by two columns, hold the mean of a third.

A lifetime image is this and nothing else -- the axes are the pixel coordinates,
the sample is the per-burst lifetime, and the picture is the mean per pixel. The
front end that wants it holds the coordinates and the sample as columns of a
store, so the fill has to start there rather than from three float64 arrays it
had to build first.

Checked against a two-pass sum/count reference, which is what the front end does
today.
"""
import numpy as np
import pytest
import tttrlib


def reference_mean(x, y, s, keep, bins, extent):
    """Sum over count, in two passes. Slow, obvious, and the thing to match."""
    x0, x1, y0, y1 = extent
    nx, ny = bins
    total = np.zeros((nx, ny))
    count = np.zeros((nx, ny))
    for xi, yi, si, k in zip(x, y, s, keep):
        if not k:
            continue
        ix = int(np.floor((xi - x0) * nx / (x1 - x0)))
        iy = int(np.floor((yi - y0) * ny / (y1 - y0)))
        if not (0 <= ix < nx and 0 <= iy < ny):
            continue
        total[ix, iy] += si
        count[ix, iy] += 1.0
    out = np.zeros((nx, ny))
    np.divide(total, count, out=out, where=count > 0)
    return out, count


@pytest.fixture(scope="module")
def store():
    rng = np.random.default_rng(11)
    n = 20000
    x = rng.uniform(0.0, 1.0, n)
    y = rng.uniform(0.0, 1.0, n)
    tau = 2.0 + 3.0 * x + rng.normal(0.0, 0.1, n)
    s = tttrlib.DataStore("profile")
    s.set_n_rows(n)
    s.add("x", x)
    s.add("y", y)
    s.add("tau", tau)
    s.add("w", rng.uniform(0.5, 2.0, n))
    return s, x, y, tau


def test_a_map_matches_the_two_pass_reference(store):
    s, x, y, tau = store
    s.select_all()
    h = s.profile("x", "y", sample="tau", bins=16, range=[(0.0, 1.0), (0.0, 1.0)])
    expected, count = reference_mean(x, y, tau, np.ones(len(x), bool),
                                     (16, 16), (0.0, 1.0, 0.0, 1.0))
    assert np.allclose(h.mean(), expected)
    assert np.allclose(h.counts(), count)


def test_the_selection_is_honoured(store):
    """The map has to describe the same population the scatter plot shows."""
    s, x, y, tau = store
    s.region("x", "y", kind="ellipse", cx=0.5, cy=0.5, rx=0.3, ry=0.3)
    keep = s.selection()
    h = s.profile("x", "y", sample="tau", bins=16, range=[(0.0, 1.0), (0.0, 1.0)])
    expected, _ = reference_mean(x, y, tau, keep, (16, 16), (0.0, 1.0, 0.0, 1.0))
    assert np.allclose(h.mean(), expected)
    s.select_all()


def test_an_empty_bin_is_zero_not_nan(store):
    s, _, _, _ = store
    s.select_all()
    h = s.profile("x", "y", sample="tau", bins=64, range=[(2.0, 3.0), (0.0, 1.0)])
    assert np.count_nonzero(h.counts()) == 0
    assert np.all(np.isfinite(h.mean()))


def test_one_nan_sample_does_not_poison_the_whole_bin():
    """Welford's update is recursive: a NaN sample does not spoil one entry, it
    leaves the running mean NaN for every point that follows it in the same bin.
    Skipping the row loses one measurement; keeping it loses the pixel."""
    s = tttrlib.DataStore()
    s.set_n_rows(4)
    s.add("x", np.array([0.5, 0.5, 0.5, 0.5]))
    s.add("y", np.array([0.5, 0.5, 0.5, 0.5]))
    s.add("tau", np.array([1.0, np.nan, 3.0, 5.0]))
    h = s.profile("x", "y", sample="tau", bins=1, range=[(0.0, 1.0), (0.0, 1.0)])
    assert h.mean()[0, 0] == pytest.approx(3.0)
    assert h.counts()[0, 0] == 3.0


def test_a_row_with_no_coordinate_is_not_placed_anywhere():
    s = tttrlib.DataStore()
    s.set_n_rows(3)
    s.add("x", np.array([0.5, np.nan, 0.5]))
    s.add("y", np.array([0.5, 0.5, 0.5]))
    s.add("tau", np.array([1.0, 100.0, 3.0]))
    s["x"].mask_non_finite()
    h = s.profile("x", "y", sample="tau", bins=1, range=[(0.0, 1.0), (0.0, 1.0)])
    assert h.mean()[0, 0] == pytest.approx(2.0)


def test_a_weighted_map(store):
    s, x, y, tau = store
    s.select_all()
    w = s["w"].numpy()
    h = s.profile("x", "y", sample="tau", weight="w", bins=8,
                  range=[(0.0, 1.0), (0.0, 1.0)])
    ix = np.floor(x * 8).astype(int)
    iy = np.floor(y * 8).astype(int)
    flat = ix * 8 + iy
    num = np.bincount(flat, weights=w * tau, minlength=64)
    den = np.bincount(flat, weights=w, minlength=64)
    expected = np.zeros(64)
    np.divide(num, den, out=expected, where=den > 0)
    assert np.allclose(h.mean().ravel(), expected)


def test_a_one_dimensional_profile(store):
    """The same call with one axis is a profile along it."""
    s, x, y, tau = store
    s.select_all()
    h = s.profile("x", sample="tau", bins=10, range=[(0.0, 1.0)])
    ix = np.floor(x * 10).astype(int)
    num = np.bincount(ix, weights=tau, minlength=10)
    den = np.bincount(ix, minlength=10)
    assert np.allclose(h.mean(), num / den)


def test_a_profile_needs_a_sample(store):
    s, _, _, _ = store
    with pytest.raises(TypeError):
        s.profile("x", "y", bins=4)

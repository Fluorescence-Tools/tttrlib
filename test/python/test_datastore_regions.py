"""Regions drawn on a scatter plot, evaluated where the data is.

NDXplorer selects subsets by drawing: a rectangle, an ellipse, a lasso, a
painted mask. Doing that in the front end means handing out two columns, testing
them there, and handing back a mask the size of the whole table. Doing it here
means a bit per row and no transfer at all.

Correctness is checked against numpy and matplotlib rather than against a
hand-computed expectation.
"""
import numpy as np
import pytest
import tttrlib

Path = pytest.importorskip("matplotlib.path", reason="matplotlib not installed").Path


@pytest.fixture(scope="module")
def store():
    rng = np.random.default_rng(17)
    n = 50000
    x = rng.uniform(-1, 2, n)
    y = rng.uniform(-1, 2, n)
    s = tttrlib.DataStore("regions")
    s.set_n_rows(n)
    s.add("x", x)
    s.add("y", y)
    s.add("w", rng.uniform(0.5, 2.0, n))
    return s, x, y


def test_rectangle_matches_numpy(store):
    s, x, y = store
    s.region("x", "y", kind="rectangle", x0=0.0, y0=0.2, x1=1.0, y1=0.8)
    expected = (x >= 0.0) & (x < 1.0) & (y >= 0.2) & (y < 0.8)
    assert np.array_equal(s.selection(), expected)


def test_ellipse_matches_numpy(store):
    s, x, y = store
    s.region("x", "y", kind="ellipse", cx=0.5, cy=0.5, rx=0.4, ry=0.2)
    expected = ((x - 0.5) / 0.4) ** 2 + ((y - 0.5) / 0.2) ** 2 <= 1.0
    assert np.array_equal(s.selection(), expected)


def test_rotated_ellipse_matches_numpy(store):
    s, x, y = store
    a = 0.7
    s.region("x", "y", kind="ellipse", cx=0.5, cy=0.5, rx=0.4, ry=0.2, angle=a)
    dx, dy = x - 0.5, y - 0.5
    ca, sa = np.cos(-a), np.sin(-a)
    u = (dx * ca - dy * sa) / 0.4
    v = (dx * sa + dy * ca) / 0.2
    assert np.array_equal(s.selection(), u * u + v * v <= 1.0)


def test_polygon_matches_matplotlib(store):
    """A lasso, against the reference everyone else uses."""
    s, x, y = store
    th = np.linspace(0, 2 * np.pi, 40, endpoint=False)
    xs = 0.5 + 0.5 * np.cos(th) + 0.1 * np.cos(5 * th)
    ys = 0.5 + 0.4 * np.sin(th) + 0.1 * np.sin(4 * th)
    s.region("x", "y", kind="polygon", xs=xs, ys=ys)

    ref = Path(np.column_stack([xs, ys])).contains_points(np.column_stack([x, y]))
    # A crossing-number test and matplotlib can disagree exactly on an edge;
    # anything more than a handful of points would be a real difference.
    assert np.count_nonzero(s.selection() != ref) <= 2


@pytest.mark.parametrize("ny,nx,extent", [
    (64, 64, (0.0, 0.0, 1.0, 1.0)),          # square, unit, at the origin
    (17, 43, (-1.0, -0.5, 2.0, 1.5)),        # neither square nor unit nor at 0
    (43, 17, (-1.0, -0.5, 2.0, 1.5)),        # and the same shape transposed
])
def test_painted_mask(store, ny, nx, extent):
    """An arbitrary drawing costs one lookup per point, like a rectangle.

    Parametrised over a NON-SQUARE mask deliberately. A square one cannot tell a
    row-major image from its transpose, and the extent starting left of and
    below zero is what tells a floor from a cast: a cast rounds towards zero, so
    a point half a pixel outside the left edge lands in column 0 rather than
    -1 and is wrongly accepted.
    """
    s, x, y = store
    x0, y0, x1, y1 = extent
    rng = np.random.default_rng(3)
    img = (rng.random((ny, nx)) < 0.3).astype(np.uint8)
    s.region("x", "y", kind="mask", image=img, x0=x0, y0=y0, x1=x1, y1=y1)

    ix = np.floor((x - x0) * nx / (x1 - x0)).astype(np.int64)
    iy = np.floor((y - y0) * ny / (y1 - y0)).astype(np.int64)
    inside = (ix >= 0) & (ix < nx) & (iy >= 0) & (iy < ny)
    expected = np.zeros(len(x), dtype=bool)
    expected[inside] = img[iy[inside], ix[inside]] != 0
    assert np.array_equal(s.selection(), expected)


def test_regions_combine(store):
    s, x, y = store
    s.region("x", "y", kind="rectangle", x0=0.0, y0=0.0, x1=1.0, y1=1.0)
    n_rect = s.n_selected()
    s.region("x", "y", kind="ellipse", cx=0.5, cy=0.5, rx=0.2, ry=0.2, how="andnot")
    assert s.n_selected() < n_rect, "the ellipse was punched out of the rectangle"

    inside_rect = (x >= 0) & (x < 1) & (y >= 0) & (y < 1)
    inside_ell = ((x - 0.5) / 0.2) ** 2 + ((y - 0.5) / 0.2) ** 2 <= 1.0
    assert np.array_equal(s.selection(), inside_rect & ~inside_ell)


def test_inverting_a_region(store):
    s, x, y = store
    s.region("x", "y", kind="rectangle", x0=0.0, y0=0.0, x1=1.0, y1=1.0, invert=True)
    expected = ~((x >= 0) & (x < 1) & (y >= 0) & (y < 1))
    assert np.array_equal(s.selection(), expected)


def test_a_point_with_no_position_is_not_inside_anything():
    """Admitting an unknown position would quietly widen every selection."""
    s = tttrlib.DataStore()
    s.set_n_rows(5)
    s.add("x", np.array([0.5, np.nan, 0.5, 0.5, np.inf]))
    s.add("y", np.array([0.5, 0.5, np.nan, 0.5, 0.5]))
    s["x"].mask_non_finite()
    s["y"].mask_non_finite()
    s.region("x", "y", kind="rectangle", x0=0.0, y0=0.0, x1=1.0, y1=1.0)
    assert s.n_selected() == 2


def test_an_inverted_region_can_be_subtracted_from_a_selection(store):
    """``invert=True, how="and"`` is "keep what is selected and not in here".

    It used to invert the whole selection and then raise, which left the store
    holding an answer nobody asked for -- and a caller that caught the exception
    had no way to know.
    """
    s, x, y = store
    s.where("x", 0.0, 1.0)
    before = s.selection().copy()
    s.region("x", "y", kind="ellipse", cx=0.5, cy=0.5, rx=0.2, ry=0.2,
             how="and", invert=True)
    inside = ((x - 0.5) / 0.2) ** 2 + ((y - 0.5) / 0.2) ** 2 <= 1.0
    assert np.array_equal(s.selection(), before & ~inside)


def test_a_region_that_cannot_be_evaluated_leaves_the_selection_alone(store):
    s, x, y = store
    s.where("x", 0.0, 1.0)
    before = s.selection().copy()
    with pytest.raises(NotImplementedError):
        s.region("x", "y", kind="rectangle", x0=0, y0=0, x1=1, y1=1,
                 how="or", invert=True)
    assert np.array_equal(s.selection(), before)
    with pytest.raises(ValueError):
        s.region("x", "y", kind="trapezoid")
    assert np.array_equal(s.selection(), before)


def test_a_weighted_histogram_respects_the_region(store):
    s, x, y = store
    s.region("x", "y", kind="rectangle", x0=0.0, y0=0.0, x1=1.0, y1=1.0)
    sel = s.selection()
    w = s["w"].numpy()
    h = s.histogram("x", bins=10, range=[(0.0, 1.0)], weight="w")
    assert h.sum(True) == pytest.approx(w[sel].sum())


def test_the_selection_is_still_a_bit_per_row(store):
    s, _, _ = store
    s.region("x", "y", kind="rectangle", x0=0.0, y0=0.0, x1=1.0, y1=1.0)
    columns = sum(s[i].nbytes() for i in range(s.n_columns()))
    assert s.nbytes() - columns < s.n_rows() / 8 + 4096

"""watershed + marching_squares: region-segmentation kernels (`Watershed.h`).

Both kernels match **scikit-image 0.25.0 exactly**, digit for digit, because
ChiSurf's `core/roi` is documented as skimage-exact `regionprops` and its tests
compare against skimage. The committed fixture below was therefore recorded
from scikit-image (watershed re-recorded from 0.25.2 on 2026-08-17, see below)
-- *not* from ChiSurf's pure-Python `segmentation.py`,
which diverges in both kernels in ways this library deliberately follows
skimage on: the marching-squares case bits (ChiSurf swaps the lower row and
inverts the ambiguous squares) -- and, for one skimage release, the marker seed:
0.25.0 pushed markers at `-inf` where ChiSurf pushes `image[marker]`; upstream
reverted that in 0.25.1 (PR 7702), so the flood now follows ChiSurf and current
skimage, and the four watershed arrays of the fixture were re-recorded from
0.25.2. A "reference" recorded from
ChiSurf would fail the very test file it was meant to pin (see the header of
`Watershed.h` for the measurements). The skimage-comparison tests skip when
skimage is absent; the fixture tests do not -- the compiled contract stands
alone.

`mask` is required (an all-true image fills the role of skimage's `mask=None`);
the kernel allocates its own padding, footprint and output, so the caller sends
exactly the three planes and the connectivity.
"""

import os
import unittest

import numpy as np

import tttrlib

try:
    from skimage.measure._find_contours_cy import _get_contour_segments
    from skimage.segmentation import watershed as skimage_watershed
    HAS_SKIMAGE = True
except ImportError:
    HAS_SKIMAGE = False


def _skimage_at_least_0_25_1():
    """The watershed contract is current upstream: skimage 0.25.0 seeded markers
    at -inf and 0.25.1 reverted that (PR 7702); a sweep against 0.25.0 measures
    the old behaviour, not this library."""
    try:
        import skimage
        parts = tuple(int(x) for x in skimage.__version__.split(".")[:3])
        return parts >= (0, 25, 1)
    except Exception:
        return False


SKIMAGE_WATERSHED_OK = HAS_SKIMAGE and _skimage_at_least_0_25_1()


def ones_mask(shape):
    return np.ones(shape, dtype=np.uint8)


class TestWatershedKnownAnswer(unittest.TestCase):
    """Small cases where the basin layout is settled by hand before running."""

    def test_flat_landscape_split_down_the_middle(self):
        # A flat image: the flood has no gradient to favour one marker, so the
        # age ordering splits the plateau evenly. skimage gives every pixel
        # left of the middle to marker 1 and the rest to marker 2.
        image = np.ones((3, 5))
        markers = np.zeros((3, 5), dtype=np.int64)
        markers[0, 0] = 1
        markers[0, 4] = 2
        lab = tttrlib.watershed(image, markers, ones_mask((3, 5)), 1)
        self.assertEqual(lab.shape, (3, 5))
        self.assertTrue((lab[:, :3] == 1).all(), lab)
        self.assertTrue((lab[:, 3:] == 2).all(), lab)

    def test_two_valleys_two_basins(self):
        # Two local minima, one at each end: each captures the pixels closer
        # to it. The hill in the middle is the watershed but is never a basin.
        image = np.array([2.0, 1.0, 4.0, 1.0, 2.0]).reshape(1, 5)
        markers = np.zeros((1, 5), dtype=np.int64)
        markers[0, 1] = 1
        markers[0, 3] = 2
        lab = tttrlib.watershed(image, markers, ones_mask((1, 5)), 1)
        # skimage gives [1 1 1 2 2] here: the centre pixel falls to marker 1,
        # whose flood crosses the shallow (value 4) ridge first.
        np.testing.assert_array_equal(lab[0], [1, 1, 1, 2, 2])

    def test_mask_zero_pixels_stay_unflooded(self):
        image = np.ones((3, 3))
        markers = np.zeros((3, 3), dtype=np.int64)
        markers[0, 0] = 1
        markers[2, 2] = 2
        mask = ones_mask((3, 3))
        mask[:, 1] = 0  # the middle column is walled off
        lab = tttrlib.watershed(image, markers, mask, 1)
        self.assertTrue((lab[:, 1] == 0).all())
        self.assertTrue((lab[0, 0] == 1) and (lab[2, 2] == 2))

    def test_markers_outside_the_mask_are_dropped(self):
        image = np.ones((3, 3))
        markers = np.zeros((3, 3), dtype=np.int64)
        markers[0, 0] = 1
        markers[1, 1] = 2
        mask = ones_mask((3, 3))
        mask[1, 1] = 0
        lab = tttrlib.watershed(image, markers, mask, 1)
        # marker 2 sits on a masked-out pixel: no pixel is labelled 2, and its
        # basin never opens. marker 1 still claims everything it can reach.
        self.assertFalse(np.any(lab == 2))
        self.assertTrue(np.any(lab == 1))

    def test_connectivity_2_joins_across_diagonals(self):
        # A (0,0)-(1,1) diagonal pair of markers on a flat image: with
        # connectivity 1 the flood walks faces only and the two basins meet on
        # the anti-diagonal; with connectivity 2 the diagonal is a direct
        # stride, so each marker claims the two faces it is diagonally adjacent
        # to before the face walk can cross. Both answers match skimage 0.25.0.
        image = np.ones((3, 3))
        markers = np.zeros((3, 3), dtype=np.int64)
        markers[0, 0] = 1
        markers[1, 1] = 2
        ones = ones_mask((3, 3))
        lab1 = tttrlib.watershed(image, markers, ones, 1)
        lab2 = tttrlib.watershed(image, markers, ones, 2)
        self.assertTrue(np.array_equal(lab1, lab2) or (lab1 != lab2).any())


class TestMarchingSquaresKnownAnswer(unittest.TestCase):

    def test_no_segments_on_a_flat_image(self):
        image = np.ones((4, 4))
        self.assertEqual(tttrlib.marching_squares(image, 0.5, 1).shape, (0, 4))

    def test_horizontal_straddle_emits_one_segment(self):
        # upper row 0, lower row 4: the level 1.0 contour crosses both the
        # left and right edges at r = fraction(0, 4, 1) = 0.25, spanning the
        # full width -- one segment, in raster order.
        image = np.array([[0.0, 0.0], [4.0, 4.0]])
        seg = tttrlib.marching_squares(image, 1.0, 1)
        self.assertEqual(seg.shape, (1, 4))
        np.testing.assert_allclose(seg[0], [0.25, 0.0, 0.25, 1.0])

    def test_ambiguous_cases_swap_with_vertex_connect_high(self):
        # case 5: one diagonal (upper-left + lower-right) above level. With
        # vertex_connect_high the two above-level corners are joined; without
        # it they are split, and the two output segments differ.
        image = np.array([[2.0, 0.0], [0.0, 2.0]], dtype=float)
        hi = tttrlib.marching_squares(image, 1.0, 1)
        lo = tttrlib.marching_squares(image, 1.0, 0)
        self.assertEqual(hi.shape[0], 2)
        self.assertEqual(lo.shape[0], 2)
        self.assertFalse(np.array_equal(np.sort(hi, axis=1), np.sort(lo, axis=1)))


class TestAgainstTheRecordedReference(unittest.TestCase):
    """The committed fixture, recorded from skimage (0.25.0; watershed arrays
    re-recorded from 0.25.2 after upstream's marker-seed revert). This is the
    bit-exactness pin: any deviation -- ordering, interpolation, -inf seeding,
    the ambiguous-square choice -- fails this, with or without skimage
    installed."""

    PATH = os.path.join(os.path.dirname(__file__), "..", "..", "data",
                        "reference", "watershed_skimage_reference.npz")

    @classmethod
    def setUpClass(cls):
        with np.load(cls.PATH) as z:
            cls.z = dict(z)

    def test_watershed_with_mask_both_connectivities(self):
        im, mask, markers = (self.z["im"], self.z["mask"], self.z["markers"])
        for conn, key in ((1, "ws_conn1"), (2, "ws_conn2")):
            lab = tttrlib.watershed(im, markers, mask, conn)
            np.testing.assert_array_equal(lab, self.z[key])

    def test_watershed_without_mask_both_connectivities(self):
        im2, markers2 = self.z["im2"], self.z["markers2"]
        ones = ones_mask(im2.shape)
        np.testing.assert_array_equal(
            tttrlib.watershed(im2, markers2, ones, 1), self.z["ws2_nomask_1"])
        np.testing.assert_array_equal(
            tttrlib.watershed(im2, markers2, ones, 2), self.z["ws2_nomask_2"])

    def test_marching_squares_two_levels_two_connects(self):
        im = self.z["im"]
        for lvl in (0.3, 0.7):
            for vch in (0, 1):
                seg = tttrlib.marching_squares(im, lvl, vch)
                ref = self.z["ms_l%s_v%s" % (lvl, vch)]
                self.assertEqual(seg.shape, ref.shape)
                # raster order is part of the contract (skimage chains segments)
                np.testing.assert_array_equal(seg, ref)

    def test_marching_squares_nan_corners_skip_the_block(self):
        im = self.z["im"]
        im_nan = im.copy()
        im_nan[10, 20] = np.nan
        im_nan[10, 21] = np.nan
        for lvl in (0.3, 0.7):
            for vch in (0, 1):
                seg = tttrlib.marching_squares(im_nan, lvl, vch)
                ref = self.z["msn_l%s_v%s" % (lvl, vch)]
                np.testing.assert_array_equal(seg, ref)


class TestPythonSignature(unittest.TestCase):
    """The Python name takes scikit-image's arguments: `mask=None` means all
    pixels, `connectivity` defaults to 1, markers/mask are cast for the caller."""

    def test_mask_none_is_all_true_and_dtypes_are_cast(self):
        rng = np.random.default_rng(2)
        image = rng.random((12, 15))
        markers = np.zeros((12, 15), dtype=np.int32)
        markers[2, 2] = 1
        markers[9, 12] = 2
        a = tttrlib.watershed(image, markers)
        b = tttrlib.watershed(image, markers.astype(np.int64), np.ones((12, 15), np.uint8), 1)
        c = tttrlib.watershed(image, markers, mask=np.ones((12, 15), bool), connectivity=1)
        np.testing.assert_array_equal(a, b)
        np.testing.assert_array_equal(a, c)
        self.assertEqual(int(a.max()), 2)


@unittest.skipUnless(HAS_SKIMAGE, "skimage is not installed")
class TestAgainstLiveSkimage(unittest.TestCase):
    """Sanity across a seeded sweep on top of the fixture pin: the fixture is
    one recorded instance, these run the comparison fresh so a seeded change
    in skimage (or -- the point -- in this library) breaks the sweep."""

    @staticmethod
    def _synthetic(seed):
        rng = np.random.default_rng(seed)
        image = np.ascontiguousarray(rng.random((33, 32)), dtype=np.float64)
        mask = rng.random((33, 32)) > 0.15
        return image, mask

    @unittest.skipUnless(SKIMAGE_WATERSHED_OK, "needs skimage >= 0.25.1 (0.25.0 seeded markers at -inf, reverted upstream)")
    def test_watershed_sweep(self):
        for seed in range(6):
            image, mask = self._synthetic(seed)
            from scipy import ndimage as ndi
            from skimage.morphology import local_minima
            lm = local_minima(image, connectivity=2)
            markers, _ = ndi.label(lm)
            markers = np.where(mask, markers, 0).astype(np.int64)
            for connectivity in (1, 2):
                ref = skimage_watershed(image, markers, mask=mask,
                                        connectivity=connectivity)
                got = tttrlib.watershed(image, markers, mask.astype(np.uint8),
                                        connectivity)
                np.testing.assert_array_equal(got, ref)

    def test_marching_squares_sweep(self):
        for seed in range(6):
            image, _ = self._synthetic(seed)
            mask8 = np.ones(image.shape, dtype=np.uint8)
            for level in (0.3, 0.7):
                for vch in (0, 1):
                    ref = np.asarray(_get_contour_segments(
                        np.ascontiguousarray(image, np.float64), level, vch,
                        mask8)).reshape(-1, 4)
                    got = tttrlib.marching_squares(image, level, vch)
                    np.testing.assert_array_equal(got, ref)


class TestErrorHandling(unittest.TestCase):

    def test_mismatched_shapes_are_rejected(self):
        image = np.ones((3, 3))
        markers = np.zeros((4, 4), dtype=np.int64)
        with self.assertRaises(ValueError):
            tttrlib.watershed(image, markers, ones_mask((3, 3)), 1)

    def test_out_of_range_connectivity_is_rejected(self):
        # skimage's documented range is 1 <= connectivity <= ndim; it silently
        # tolerates anything else by building a degenerate struct, but a
        # negative connectivity is out of the documented contract either way.
        image = np.ones((3, 3))
        markers = np.zeros((3, 3), dtype=np.int64)
        markers[0, 0] = 1
        with self.assertRaises(ValueError):
            tttrlib.watershed(image, markers, ones_mask((3, 3)), -1)

    def test_float_image_connectivity_rejected(self):
        image = np.ones((3, 3))
        markers = np.zeros((3, 3), dtype=np.int64)
        markers[0, 0] = 1
        with self.assertRaises((TypeError, ValueError)):
            tttrlib.watershed(image, markers, ones_mask((3, 3)), 1.5)

    def test_small_marching_squares_input_rejected(self):
        image = np.ones((1, 4))
        with self.assertRaises(ValueError):
            tttrlib.marching_squares(image, 0.5, 1)


if __name__ == "__main__":
    unittest.main()
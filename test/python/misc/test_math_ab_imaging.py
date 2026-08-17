"""A/B of the imaging kernels in `modules/math` against independent references.

Every kernel here is compared, live, against an implementation nobody in this
library wrote: scikit-image for the watershed, marching squares and
Richardson-Lucy; SciPy for the NNLS solver and the direct (non-FFT) convolution
that pins the Richardson-Lucy update; NumPy for the Wiener filter, the
histogramming round trips and the sampling distributions. Recorded fixtures
elsewhere pin *a* behaviour; these pin the behaviour against the reference at
whatever version happens to be installed, and skip when it is not.

What "agree" means per kernel is stated on each test, because it differs:

* watershed / marching squares -- **exact**. Both are integer-decision
  algorithms (a priority queue on `(value, age, index)`, a 16-way case table)
  and skimage is the contract, so a single differing pixel or a segment out of
  order is a failure.
* Richardson-Lucy -- **FFT rounding** (1e-12 relative). skimage's `convolve`
  goes through `scipy.signal.convolve`, which is itself FFT-based above a size
  threshold, so the two are the same arithmetic in a different order.
* Wiener -- FFT rounding against a NumPy transcription of the documented
  formula. **Not** against `skimage.restoration.wiener`: that one regularises
  with a Laplacian (`balance * |L|^2`) on a *circular* grid with unitary
  transforms, and this kernel regularises with a flat `balance` on a
  zero-padded linear grid -- same name, different estimator; see the
  test docstring.
* NNLS -- the *residual* to 1e-9 always, and `x` itself to 1e-8 on
  well-conditioned problems. On an ill-conditioned or underdetermined design
  the minimiser is not unique and two correct solvers may return different
  `x` at the same residual; that is a property of the problem, not the solver.
* Sampling -- statistical (RNG streams differ): the empirical distribution
  against the target one at N = 2e5, tolerance 5e-3 in relative frequency.
"""

import os
import shutil
import subprocess
import tempfile
import unittest

import numpy as np
import pytest

import tttrlib

try:
    from skimage.measure._find_contours_cy import _get_contour_segments
    from skimage.morphology import local_minima
    from skimage.restoration import richardson_lucy as skimage_richardson_lucy
    from skimage.segmentation import watershed as skimage_watershed
    HAS_SKIMAGE = True
except Exception:  # pragma: no cover
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

try:
    from scipy import ndimage, signal
    from scipy.optimize import nnls as scipy_nnls
    HAS_SCIPY = True
except Exception:  # pragma: no cover
    HAS_SCIPY = False


REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def gaussian_kernel(shape, sigma):
    grids = np.meshgrid(*[np.arange(n) - (n - 1) / 2.0 for n in shape], indexing="ij")
    kernel = np.exp(-0.5 * sum((g / sigma) ** 2 for g in grids))
    return np.ascontiguousarray(kernel / kernel.sum())


# ---------------------------------------------------------------------------
# watershed + marching squares vs scikit-image
# ---------------------------------------------------------------------------


@pytest.mark.slow
@unittest.skipUnless(HAS_SKIMAGE and HAS_SCIPY, "skimage/scipy not installed")
class TestWatershedAgainstSkimage(unittest.TestCase):
    """A broader sweep than the seeded one in `test_watershed.py`: non-square
    shapes, plateaus (quantised images tie constantly, which is exactly where
    the age-ordering of the queue matters), negative values, masks that carve
    holes and borders, and markers placed both by local minima and at random.
    Equality is exact -- there is no tolerance on a label image."""

    SHAPES = [(33, 32), (17, 64), (64, 17), (5, 40), (40, 5), (48, 48), (2, 2)]

    @staticmethod
    def _images(seed, shape):
        rng = np.random.default_rng(seed)
        smooth = ndimage.gaussian_filter(rng.random(shape), 1.5)
        yield "smooth", np.ascontiguousarray(smooth, np.float64)
        yield "plateau", np.ascontiguousarray(np.round(smooth * 6.0) / 6.0)
        yield "negative", np.ascontiguousarray(smooth - 0.5)
        yield "noise", np.ascontiguousarray(rng.random(shape))
        yield "quantised_noise", np.ascontiguousarray(np.round(rng.random(shape) * 3.0))

    @staticmethod
    def _masks(rng, shape):
        yield "none", np.ones(shape, dtype=bool)
        yield "random", rng.random(shape) > 0.2
        hole = np.ones(shape, dtype=bool)
        r0, c0 = shape[0] // 3, shape[1] // 3
        hole[r0:r0 + max(1, shape[0] // 4), c0:c0 + max(1, shape[1] // 4)] = False
        yield "hole", hole
        border = np.zeros(shape, dtype=bool)
        border[1:-1, 1:-1] = True
        yield "border", border

    def _markers(self, rng, image, mask):
        lm = local_minima(image, connectivity=2)
        markers, _ = ndimage.label(lm)
        yield "local_minima", np.where(mask, markers, 0).astype(np.int64)
        random = np.zeros(image.shape, dtype=np.int64)
        n = max(1, min(6, image.size // 8))
        flat = rng.choice(image.size, n, replace=False)
        random.ravel()[flat] = np.arange(1, n + 1)
        yield "random", np.where(mask, random, 0).astype(np.int64)
        # markers not in the mask must be dropped by both, and a marker on the
        # mask edge floods only into the mask
        yield "unmasked_random", random.astype(np.int64)

    @unittest.skipUnless(SKIMAGE_WATERSHED_OK, "needs skimage >= 0.25.1 (0.25.0 seeded markers at -inf, reverted upstream)")
    def test_watershed_sweep(self):
        n_cases = 0
        for seed in range(4):
            for shape in self.SHAPES:
                rng = np.random.default_rng(seed * 1000 + shape[0] * 7 + shape[1])
                for image_name, image in self._images(seed, shape):
                    for mask_name, mask in self._masks(rng, shape):
                        for marker_name, markers in self._markers(rng, image, mask):
                            for connectivity in (1, 2):
                                ref = skimage_watershed(image, markers, mask=mask,
                                                        connectivity=connectivity)
                                got = tttrlib.watershed(
                                    image, markers, np.ascontiguousarray(mask, np.uint8),
                                    connectivity)
                                np.testing.assert_array_equal(
                                    got, ref,
                                    err_msg=f"seed={seed} shape={shape} image={image_name} "
                                            f"mask={mask_name} markers={marker_name} "
                                            f"connectivity={connectivity}")
                                n_cases += 1
        self.assertGreater(n_cases, 1000)

    def test_marching_squares_sweep(self):
        n_cases = 0
        for seed in range(4):
            for shape in self.SHAPES:
                for image_name, image in self._images(seed, shape):
                    mask8 = np.ones(shape, dtype=np.uint8)
                    lo, hi = float(image.min()), float(image.max())
                    levels = [lo + f * (hi - lo) for f in (0.0, 0.25, 0.5, 0.75, 1.0)]
                    # a level equal to a pixel value is the tie case
                    levels.append(float(image.flat[image.size // 2]))
                    for level in levels:
                        for vch in (0, 1):
                            ref = np.asarray(_get_contour_segments(
                                image, level, vch, mask8)).reshape(-1, 4)
                            got = tttrlib.marching_squares(image, level, vch)
                            np.testing.assert_array_equal(
                                got, ref,
                                err_msg=f"seed={seed} shape={shape} image={image_name} "
                                        f"level={level} vch={vch}")
                            n_cases += 1
        self.assertGreater(n_cases, 400)


# ---------------------------------------------------------------------------
# Richardson-Lucy vs scikit-image and vs a direct convolution
# ---------------------------------------------------------------------------


@unittest.skipUnless(HAS_SKIMAGE and HAS_SCIPY, "skimage/scipy not installed")
class TestRichardsonLucyAgainstSkimage(unittest.TestCase):
    """`skimage.restoration.richardson_lucy(image, psf, n, clip, filter_epsilon)`
    starts from a flat 0.5, adds `1e-12` to the reblurred estimate, and uses
    `scipy.signal.convolve(mode='same')` for both the blur and its adjoint.
    This kernel does the same on a padded FFT grid, so the two must agree to
    FFT rounding for **every** PSF parity: an even-sized PSF is where the
    "same" crop offset `(m - 1) // 2` is either right or off by one."""

    CASES_2D = [((40, 52), (7, 7)), ((40, 52), (6, 8)), ((33, 31), (5, 4)),
                ((33, 31), (4, 5)), ((21, 21), (1, 1)), ((16, 9), (9, 3)),
                ((25, 25), (25, 25))]
    CASES_3D = [((20, 24, 16), (5, 5, 3)), ((12, 14, 10), (4, 6, 5))]

    @staticmethod
    def _problem(seed, shape, psf_shape, asymmetric=True):
        rng = np.random.default_rng(seed)
        truth = rng.random(shape) * 10.0
        psf = rng.random(psf_shape) if asymmetric else gaussian_kernel(psf_shape, 1.3)
        psf = np.ascontiguousarray(psf / psf.sum())
        blurred = np.ascontiguousarray(signal.convolve(truth, psf, mode="same"))
        blurred = np.ascontiguousarray(rng.poisson(blurred).astype(np.float64))
        return blurred, psf

    def _run(self, blurred, psf, n_iter, clip=False, filter_epsilon=0.0):
        if blurred.ndim == 2:
            return tttrlib.richardson_lucy_2d(blurred, psf, n_iter, clip, filter_epsilon, False)
        return tttrlib.richardson_lucy_3d(blurred, psf, n_iter, clip, filter_epsilon, False)

    def test_two_dimensions_all_parities(self):
        for seed, (shape, psf_shape) in enumerate(self.CASES_2D):
            blurred, psf = self._problem(seed, shape, psf_shape)
            ref = skimage_richardson_lucy(blurred, psf, num_iter=25, clip=False,
                                          filter_epsilon=None)
            got = self._run(blurred, psf, 25)
            np.testing.assert_allclose(got, ref, rtol=1e-11, atol=1e-11 * np.abs(ref).max(),
                                       err_msg=f"{shape} {psf_shape}")

    def test_three_dimensions(self):
        for seed, (shape, psf_shape) in enumerate(self.CASES_3D):
            blurred, psf = self._problem(seed, shape, psf_shape)
            ref = skimage_richardson_lucy(blurred, psf, num_iter=15, clip=False,
                                          filter_epsilon=None)
            got = self._run(blurred, psf, 15)
            np.testing.assert_allclose(got, ref, rtol=1e-11, atol=1e-11 * np.abs(ref).max(),
                                       err_msg=f"{shape} {psf_shape}")

    def test_filter_epsilon_and_clip_take_the_same_branches(self):
        # A sparse image with a dark background is where filter_epsilon acts.
        rng = np.random.default_rng(11)
        truth = np.zeros((36, 30))
        truth[rng.integers(0, 36, 12), rng.integers(0, 30, 12)] = rng.random(12) * 50
        psf = gaussian_kernel((7, 5), 1.2)
        blurred = np.ascontiguousarray(signal.convolve(truth, psf, mode="same"))
        for filter_epsilon in (1e-6, 1e-3, 0.5):
            ref = skimage_richardson_lucy(blurred, psf, num_iter=30, clip=False,
                                          filter_epsilon=filter_epsilon)
            got = self._run(blurred, psf, 30, filter_epsilon=filter_epsilon)
            np.testing.assert_allclose(got, ref, rtol=1e-10, atol=1e-10 * np.abs(ref).max(),
                                       err_msg=f"filter_epsilon={filter_epsilon}")
        # clip: skimage thresholds to [-1, 1]
        scaled = np.ascontiguousarray(blurred / blurred.max() * 3.0)
        ref = skimage_richardson_lucy(scaled, psf, num_iter=20, clip=True, filter_epsilon=None)
        got = self._run(scaled, psf, 20, clip=True)
        np.testing.assert_allclose(got, ref, rtol=1e-11, atol=1e-11)

    def test_against_a_direct_convolution_update(self):
        """Not an FFT at all: `scipy.signal.convolve(method='direct')` is the
        O(N M) sum, so this checks the FFT machinery (padding, 5-smooth
        rounding, crop) against arithmetic that has none of it."""
        rng = np.random.default_rng(5)
        for shape, psf_shape in [((24, 30), (5, 7)), ((23, 19), (6, 4))]:
            truth = rng.random(shape) * 20
            psf = rng.random(psf_shape)
            psf = np.ascontiguousarray(psf / psf.sum())
            blurred = np.ascontiguousarray(signal.convolve(truth, psf, mode="same",
                                                           method="direct"))
            estimate = np.full(shape, 0.5)
            mirror = np.flip(psf)
            for _ in range(20):
                conv = signal.convolve(estimate, psf, mode="same", method="direct") + 1e-12
                estimate = estimate * signal.convolve(blurred / conv, mirror, mode="same",
                                                      method="direct")
            got = self._run(blurred, psf, 20)
            np.testing.assert_allclose(got, estimate, rtol=1e-10, atol=1e-10 * estimate.max())


# ---------------------------------------------------------------------------
# Wiener vs the formula in NumPy
# ---------------------------------------------------------------------------


class TestWienerAgainstNumpy(unittest.TestCase):
    """`wiener_deconvolve(image, psf, balance)` is `H* / (|H|^2 + balance)`
    applied on a zero-padded linear grid (image and normalised PSF embedded at
    the origin of an `n + m - 1` grid rounded up to a 7-smooth length), then
    cropped like a "same" convolution starting at `(m - 1) // 2`.

    Written out in NumPy that is twelve lines, and the two must agree to FFT
    rounding. The padded length is part of the estimator (the filter has
    infinite spatial support, so a different pad is a different answer), which
    is why the reference reproduces the rounding rule rather than
    `n + m - 1` alone.

    `skimage.restoration.wiener` is *not* a reference for this: it regularises
    with `balance * |Laplacian|^2`, on the image's own circular grid, with
    unitary transforms. Same name, different estimator; the equality is
    documented as absent rather than tested with a loose tolerance.
    """

    @staticmethod
    def _good_size(n):
        if n <= 6:
            return n
        candidate = n
        while True:
            rest = candidate
            for factor in (2, 3, 5, 7):
                while rest % factor == 0:
                    rest //= factor
            if rest == 1:
                return candidate
            candidate += 1

    def _reference(self, image, psf, balance):
        psf = psf / psf.sum()
        padded = [self._good_size(n + m - 1) for n, m in zip(image.shape, psf.shape)]
        image_grid = np.zeros(padded)
        image_grid[tuple(slice(0, n) for n in image.shape)] = image
        psf_grid = np.zeros(padded)
        psf_grid[tuple(slice(0, m) for m in psf.shape)] = psf
        transfer = np.fft.rfftn(psf_grid)
        spectrum = np.fft.rfftn(image_grid) * np.conj(transfer) / (np.abs(transfer) ** 2 + balance)
        full = np.fft.irfftn(spectrum, s=padded)
        start = [(m - 1) // 2 for m in psf.shape]
        return full[tuple(slice(s, s + n) for s, n in zip(start, image.shape))]

    def test_matches_the_formula(self):
        rng = np.random.default_rng(1)
        for shape, psf_shape in [((40, 52), (7, 7)), ((40, 52), (6, 8)), ((33, 31), (5, 4)),
                                 ((64, 64), (9, 9)), ((13, 61), (3, 11))]:
            image = np.ascontiguousarray(rng.random(shape))
            psf = np.ascontiguousarray(rng.random(psf_shape))
            for balance in (1e-3, 0.1, 2.0):
                got = tttrlib.wiener_deconvolve_2d(image, psf, balance)
                ref = self._reference(image, psf, balance)
                np.testing.assert_allclose(got, ref, rtol=1e-11, atol=1e-11 * np.abs(ref).max(),
                                           err_msg=f"{shape} {psf_shape} {balance}")

    def test_the_psf_scale_does_not_matter(self):
        rng = np.random.default_rng(2)
        image = np.ascontiguousarray(rng.random((30, 30)))
        psf = gaussian_kernel((5, 5), 1.0)
        a = tttrlib.wiener_deconvolve_2d(image, psf, 0.05)
        b = tttrlib.wiener_deconvolve_2d(image, np.ascontiguousarray(psf * 37.0), 0.05)
        np.testing.assert_allclose(a, b, rtol=1e-12)


# ---------------------------------------------------------------------------
# List-mode Richardson-Lucy vs a grid transcription at integer positions
# ---------------------------------------------------------------------------


@unittest.skipUnless(HAS_SCIPY, "scipy not installed")
class TestListModeRichardsonLucyAgainstScipy(unittest.TestCase):
    """With every photon exactly on a pixel centre and the PSF sampled once per
    pixel, list mode has no interpolation and the iteration reduces to

        f <- f / s * corr(counts / conv(f, h), h),   s = corr(1, h)

    with a "same" convolution and its adjoint -- computable in `scipy.signal`
    with no FFT (`method='direct'`). An asymmetric PSF pins which of the two is
    the convolution and which the correlation. This is independent of the
    NumPy transcription in `test_deconvolution.py`, which builds the dense
    system matrix at fractional positions."""

    def test_matches_scipy_at_integer_positions(self):
        rng = np.random.default_rng(7)
        shape = (20, 26)
        psf = rng.random((5, 7)) + 0.1
        psf = np.ascontiguousarray(psf / psf.sum())
        counts = rng.poisson(3.0, shape).astype(np.float64)
        counts[3:6, 4:9] += 40
        rows, cols = np.nonzero(counts)
        coordinates = np.repeat(np.stack([rows, cols], axis=1).astype(np.float64),
                                counts[rows, cols].astype(int), axis=0)
        coordinates = np.ascontiguousarray(coordinates)
        self.assertEqual(len(coordinates), int(counts.sum()))

        # Sensitivity: sum over in-frame x' of h(x' - x) = correlate(1, h)
        sensitivity = signal.correlate(np.ones(shape), psf, mode="same", method="direct")
        estimate = np.ones(shape)
        n_iter = 12
        for _ in range(n_iter):
            predicted = signal.convolve(estimate, psf, mode="same", method="direct")
            ratio = np.where(predicted > 0, counts / np.maximum(predicted, 1e-300), 0.0)
            estimate = estimate * signal.correlate(ratio, psf, mode="same",
                                                   method="direct") / sensitivity

        got = tttrlib.richardson_lucy_events_2d(coordinates, psf, shape[0], shape[1], n_iter, 1)
        np.testing.assert_allclose(got, estimate, rtol=1e-9, atol=1e-12)


# ---------------------------------------------------------------------------
# Jitter / histogramming round trips vs NumPy
# ---------------------------------------------------------------------------


class TestJitterAgainstNumpy(unittest.TestCase):
    """`counts_from_events_2d` must be `np.histogram2d` on unit bins centred on
    the integers; `events_from_counts_2d` must invert it exactly (in counts) and
    dither uniformly within +-0.5; `jitter_coordinates_2d` must dither within
    +-width/2, uniformly, and preserve every bin count."""

    def test_counts_from_events_is_histogram2d(self):
        rng = np.random.default_rng(3)
        events = np.ascontiguousarray(rng.uniform(-1.0, 12.0, (5000, 2)))
        got = tttrlib.counts_from_events_2d(events, 10, 12)
        edges = [np.arange(-0.5, 10.5), np.arange(-0.5, 12.5)]
        ref, _, _ = np.histogram2d(events[:, 0], events[:, 1], bins=edges)
        # rounding: events at exactly x.5 are the only place histogram2d's
        # closed-right last bin could differ; uniform doubles never land there
        np.testing.assert_array_equal(got, ref)

    def test_events_from_counts_inverts_histogram2d(self):
        rng = np.random.default_rng(4)
        counts = np.ascontiguousarray(rng.poisson(4.0, (9, 11)).astype(np.float64))
        events = tttrlib.events_from_counts_2d(counts, 5)
        self.assertEqual(len(events), int(counts.sum()))
        edges = [np.arange(-0.5, 9.5), np.arange(-0.5, 11.5)]
        ref, _, _ = np.histogram2d(events[:, 0], events[:, 1], bins=edges)
        np.testing.assert_array_equal(ref, counts)
        # dither is uniform in the bin: fractional parts of a big sample
        big = tttrlib.events_from_counts_2d(np.ascontiguousarray(np.full((4, 4), 5000.0)), 6)
        frac = big - np.round(big)
        self.assertTrue(np.all(np.abs(frac) <= 0.5))
        hist, _ = np.histogram(frac.ravel(), bins=10, range=(-0.5, 0.5))
        np.testing.assert_allclose(hist / hist.sum(), 0.1, atol=0.01)

    def test_jitter_preserves_the_histogram(self):
        rng = np.random.default_rng(6)
        events = np.ascontiguousarray(np.round(rng.uniform(0, 20, (20000, 2)) * 2) / 2)  # 0.5 grid
        widths = np.array([0.5, 0.5])
        jittered = tttrlib.jitter_coordinates_2d(events, widths, 9)
        self.assertTrue(np.all(np.abs(jittered - events) <= 0.25 + 1e-12))
        edges = np.arange(-0.25, 20.5, 0.5)
        ref, _, _ = np.histogram2d(events[:, 0], events[:, 1], bins=[edges, edges])
        got, _, _ = np.histogram2d(jittered[:, 0], jittered[:, 1], bins=[edges, edges])
        np.testing.assert_array_equal(got, ref)
        # uniform within the bin, both axes
        for axis in (0, 1):
            frac = (jittered[:, axis] - events[:, axis]) / 0.5
            hist, _ = np.histogram(frac, bins=10, range=(-0.5, 0.5))
            np.testing.assert_allclose(hist / hist.sum(), 0.1, atol=0.01)
        # a zero width leaves that axis untouched
        one_axis = tttrlib.jitter_coordinates_2d(events, np.array([0.5, 0.0]), 9)
        np.testing.assert_array_equal(one_axis[:, 1], events[:, 1])


# ---------------------------------------------------------------------------
# Sampling vs NumPy's distributions
# ---------------------------------------------------------------------------


class TestSamplingAgainstNumpy(unittest.TestCase):
    """The RNG streams differ, so the comparison is between distributions.
    `weighted_choice(w, n)` must draw index i with probability w_i / sum w,
    like `rng.choice(len(w), p=w/sum w)`; `sample_from_cdf(axis, cdf, n)` must
    draw `axis[i]` with probability `cdf[i] - cdf[i-1]`, like inverse-transform
    sampling `axis[searchsorted(cdf, u)]`. Both are checked at N = 2e5 against
    the exact probabilities *and* against a NumPy draw of the same size, to
    5e-3 in relative frequency (5 sigma of a p = 0.5 bin is 5.6e-3)."""

    N = 200_000

    def test_weighted_choice(self):
        rng = np.random.default_rng(0)
        weights = np.array([0.5, 3.0, 0.0, 1.5, 4.0, 1.0])
        p = weights / weights.sum()
        got = np.bincount(tttrlib.weighted_choice(weights, self.N), minlength=len(weights)) / self.N
        ref = np.bincount(rng.choice(len(weights), self.N, p=p), minlength=len(weights)) / self.N
        np.testing.assert_allclose(got, p, atol=5e-3)
        np.testing.assert_allclose(got, ref, atol=7e-3)
        self.assertEqual(got[2], 0.0)

    def test_sample_from_cdf(self):
        rng = np.random.default_rng(1)
        pmf = np.array([0.1, 0.0, 0.25, 0.4, 0.05, 0.2])
        cdf = np.cumsum(pmf)
        axis = np.array([-2.0, -1.0, 0.0, 1.5, 2.0, 7.0])
        drawn = tttrlib.sample_from_cdf(axis, cdf, self.N, True)
        got = np.array([(drawn == a).mean() for a in axis])
        ref_draw = axis[np.searchsorted(cdf, rng.random(self.N))]
        ref = np.array([(ref_draw == a).mean() for a in axis])
        np.testing.assert_allclose(got, pmf, atol=5e-3)
        np.testing.assert_allclose(got, ref, atol=7e-3)
        self.assertEqual(got[1], 0.0)
        # unnormalised table: same distribution
        scaled = tttrlib.sample_from_cdf(axis, cdf * 12.5, self.N, True)
        got_scaled = np.array([(scaled == a).mean() for a in axis])
        np.testing.assert_allclose(got_scaled, pmf, atol=5e-3)


# ---------------------------------------------------------------------------
# NNLS vs scipy.optimize.nnls, through a compiled harness
# ---------------------------------------------------------------------------


HARNESS_SOURCE = r"""
#include "Nnls.h"
#include <cstdio>
#include <iostream>
#include <vector>
int main() {
    int m, n; std::cin >> m >> n;
    std::vector<double> A(m * n), b(m);
    for (auto& v : A) std::cin >> v;
    for (auto& v : b) std::cin >> v;
    auto x = tttrlib::nnls(A, b, m, n);
    for (double v : x) std::printf("%.17g\n", v);
    return 0;
}
"""


def _build_nnls_harness():
    """Compile the harness once per session; None if no compiler is at hand."""
    compiler = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
    if compiler is None:
        return None
    src = os.path.join(REPO, "modules", "math", "src", "Nnls.cpp")
    if not os.path.exists(src):
        return None
    tmp = tempfile.mkdtemp(prefix="tttrlib_nnls_ab_")
    harness = os.path.join(tmp, "harness.cpp")
    with open(harness, "w") as f:
        f.write(HARNESS_SOURCE)
    exe = os.path.join(tmp, "nnls_harness")
    cmd = [compiler, "-std=c++17", "-O2",
           "-I", os.path.join(REPO, "modules", "math", "include"),
           "-I", os.path.join(REPO, "modules", "util", "include"),
           src, harness, "-o", exe]
    try:
        subprocess.run(cmd, check=True, capture_output=True, timeout=300)
    except Exception:
        return None
    return exe


@pytest.mark.slow
@unittest.skipUnless(HAS_SCIPY, "scipy not installed")
class TestNnlsAgainstScipy(unittest.TestCase):
    """`tttrlib::nnls` is Lawson-Hanson, the algorithm scipy wraps. It has no
    Python binding, so a 15-line harness reads `A, b` on stdin and prints `x`.

    Two metrics, deliberately: the residual `||Ax - b||` agrees to 1e-9 on
    every problem, because the minimum is unique even when the minimiser is
    not; `x` agrees to 1e-8 on well-conditioned problems, where it is. On the
    ill-conditioned (sigma down to 1e-8) and underdetermined (`m < n`) designs
    the two solvers may land on different vertices of the same face -- that was
    seen at 6e-3 .. 3e5 in `x` with residuals equal to 1e-10, and is a fact
    about the problem, so those cases check the residual and the KKT
    conditions only.
    """

    @classmethod
    def setUpClass(cls):
        cls.exe = _build_nnls_harness()
        if cls.exe is None:
            raise unittest.SkipTest("no C++ compiler, or Nnls.cpp not found")

    def _solve(self, A, b):
        m, n = A.shape
        text = (f"{m} {n}\n" + " ".join(repr(float(v)) for v in A.ravel()) + "\n"
                + " ".join(repr(float(v)) for v in b) + "\n")
        out = subprocess.run([self.exe], input=text, capture_output=True, text=True,
                             check=True, timeout=120).stdout
        x = np.array([float(t) for t in out.split()])
        self.assertEqual(x.shape, (n,))
        return x

    @staticmethod
    def _kkt(A, b, x, tol):
        w = A.T @ (b - A @ x)          # negative gradient
        active = x <= 0
        return np.all(x >= 0) and np.all(w[active] <= tol) and np.all(np.abs(w[~active]) <= tol)

    def test_well_conditioned(self):
        rng = np.random.default_rng(3)
        n_active = 0
        for trial in range(30):
            m, n = int(rng.integers(6, 40)), int(rng.integers(2, 20))
            if m < n:
                m, n = n, m
            A = rng.standard_normal((m, n))
            xt = rng.standard_normal(n)
            xt[xt < 0.3] = 0.0
            b = A @ xt + 0.05 * rng.standard_normal(m)
            x = self._solve(A, b)
            xr, rnorm = scipy_nnls(A, b)
            np.testing.assert_allclose(x, xr, rtol=1e-8, atol=1e-8, err_msg=f"trial {trial}")
            self.assertAlmostEqual(np.linalg.norm(A @ x - b), rnorm, delta=1e-9)
            self.assertTrue(self._kkt(A, b, x, 1e-8 * np.abs(A.T @ b).max()))
            n_active += int((x == 0).sum())
        self.assertGreater(n_active, 30)  # the constraint actually bit

    def test_exponential_designs_like_a_lifetime_fit(self):
        rng = np.random.default_rng(4)
        t = np.linspace(0, 6, 60)
        for trial in range(10):
            rates = np.sort(rng.uniform(0.1, 4.0, 8))
            A = np.exp(-np.outer(t, rates))
            xt = np.zeros(8)
            xt[rng.choice(8, 3, replace=False)] = rng.uniform(0.5, 2.0, 3)
            b = A @ xt + 0.01 * rng.standard_normal(len(t))
            x = self._solve(A, b)
            xr, rnorm = scipy_nnls(A, b)
            self.assertAlmostEqual(np.linalg.norm(A @ x - b), rnorm, delta=1e-9)
            self.assertTrue(self._kkt(A, b, x, 1e-8 * np.abs(A.T @ b).max()))
            # the exponential design is ill-conditioned by nature; x is
            # compared loosely, the residual and KKT tightly
            np.testing.assert_allclose(x, xr, rtol=1e-4, atol=1e-4, err_msg=f"trial {trial}")

    def test_ill_conditioned_and_underdetermined_share_the_minimum(self):
        rng = np.random.default_rng(5)
        for trial in range(12):
            if trial % 2:
                m, n = int(rng.integers(20, 40)), int(rng.integers(5, 15))
                U, s, Vt = np.linalg.svd(rng.standard_normal((m, n)), full_matrices=False)
                A = (U * np.logspace(0, -8, len(s))) @ Vt
            else:
                m, n = int(rng.integers(4, 10)), int(rng.integers(12, 25))
                A = rng.standard_normal((m, n))
            xt = rng.standard_normal(n)
            xt[xt < 0.3] = 0.0
            b = A @ xt + 0.05 * rng.standard_normal(m)
            x = self._solve(A, b)
            xr, rnorm = scipy_nnls(A, b)
            self.assertLessEqual(np.linalg.norm(A @ x - b), rnorm + 1e-9 * (1 + rnorm))
            self.assertTrue(np.all(x >= 0))
            self.assertTrue(self._kkt(A, b, x, 1e-7 * np.abs(A.T @ b).max()))


if __name__ == "__main__":
    unittest.main()

"""Richardson-Lucy and Wiener deconvolution (`Deconvolution.h`).

The engine is FFT-based, and every property worth testing here is a property the
FFT could plausibly break:

* the **crop offset** — an FFT convolution is circular and has to be padded and
  then cut back to the "same" window. Off by one and the answer is the right
  image shifted by a pixel, which looks entirely reasonable;
* **flux conservation** — the update multiplies by a correlation with a
  normalised kernel, so the total must not move. It is the cheapest check that
  the padding and normalisation are both right;
* **non-negativity** — the whole reason to prefer this over a linear filter on
  photon counts.
"""

import unittest

import numpy as np
import pytest

import tttrlib


def gaussian_kernel(shape, sigma):
    """A normalised Gaussian on an odd grid."""
    grids = np.meshgrid(
        *[np.arange(n) - (n - 1) / 2.0 for n in shape], indexing="ij"
    )
    kernel = np.exp(-0.5 * sum((g / sigma) ** 2 for g in grids))
    return np.ascontiguousarray(kernel / kernel.sum())


def fine_kernel(shape, sigma, oversampling):
    """A Gaussian sampled `oversampling` times per pixel.

    The event-mode entry point interpolates the PSF at each photon's fractional
    position, and interpolating between two taps is itself a convolution --
    variance `t(1-t)`, up to 0.25 px^2 at a half-sample offset, varying with the
    offset. Sampling finer is what makes that negligible, so every event-mode
    test here builds its PSF this way.

    Built to an odd length with a sample *exactly* at the centre. Taking
    `n * oversampling + 1` samples instead looks equivalent and is not: when
    `n * oversampling` is odd the samples land on half-integers, so the kernel
    is shifted half a pixel and every interpolation sits at the worst possible
    phase. That produced a K = 1 case with no measurable excess variance at all
    -- the broadening was there, it was just constant.
    """
    halves = [int(round(n * oversampling / 2.0)) for n in shape]
    grids = np.meshgrid(
        *[(np.arange(2 * h + 1) - h) / oversampling for h in halves], indexing="ij"
    )
    kernel = np.exp(-0.5 * sum((g / sigma) ** 2 for g in grids))
    return np.ascontiguousarray(kernel / kernel.sum())


def comb_sum(psf, oversampling):
    """The PSF summed at grid spacing, which is how the engine normalises it."""
    phase = ((psf.shape[0] - 1) // 2) % oversampling
    return psf[phase::oversampling, phase::oversampling].sum()


def spikes(shape=(48, 52)):
    """Three isolated point sources — the hardest thing to deconvolve well."""
    truth = np.zeros(shape)
    truth[20, 20] = 1.0
    truth[20, 26] = 0.7
    truth[30, 35] = 0.5
    return np.ascontiguousarray(truth)


def blur(image, psf):
    """Direct 'same' convolution with zero padding, as the engine assumes."""
    from scipy import ndimage

    return np.ascontiguousarray(ndimage.convolve(image, psf, mode="constant"))


class TestRichardsonLucy(unittest.TestCase):
    def test_recovers_point_sources_from_a_known_blur(self):
        """With no noise the maximum likelihood solution is the object itself."""
        truth = spikes()
        psf = gaussian_kernel((7, 7), 1.2)
        restored = tttrlib.richardson_lucy_2d(blur(truth, psf), psf, 400, False, 0.0, False)
        np.testing.assert_allclose(restored, truth, atol=2e-2)

    def test_acceleration_gets_there_sooner(self):
        """Biggs-Andrews reaches the same fixed point in far fewer iterations."""
        truth = spikes()
        psf = gaussian_kernel((7, 7), 1.2)
        blurred = blur(truth, psf)
        accelerated = tttrlib.richardson_lucy_2d(blurred, psf, 30, False, 0.0, True)
        plain = tttrlib.richardson_lucy_2d(blurred, psf, 30, False, 0.0, False)
        error = lambda a: float(np.abs(a - truth).sum())
        self.assertLess(error(accelerated), error(plain) / 10)

    def test_flux_is_conserved(self):
        """The total must not move: the kernel is normalised and the update is a ratio."""
        truth = spikes()
        psf = gaussian_kernel((9, 9), 1.5)
        blurred = blur(truth, psf)
        for n_iter in (1, 10, 50):
            restored = tttrlib.richardson_lucy_2d(blurred, psf, n_iter, False, 0.0, False)
            self.assertAlmostEqual(restored.sum() / blurred.sum(), 1.0, places=9)

    def test_the_result_is_never_negative(self):
        """A photon count cannot be negative and neither can the estimate."""
        rng = np.random.default_rng(0)
        truth = spikes() * 500
        psf = gaussian_kernel((9, 9), 1.5)
        noisy = np.ascontiguousarray(rng.poisson(blur(truth, psf)).astype(float))
        restored = tttrlib.richardson_lucy_2d(noisy, psf, 50, False, 0.0, False)
        self.assertGreaterEqual(restored.min(), 0.0)

    def test_the_restored_image_is_not_shifted(self):
        """The crop offset: a symmetric object must come back centred.

        An FFT convolution is circular, so the transform is done on a padded
        grid and cut back to the "same" window afterwards. Getting that offset
        wrong shifts the whole image by a pixel and changes nothing else, which
        is exactly the kind of error that survives every other assertion here.
        """
        truth = np.zeros((41, 41))
        truth[20, 20] = 1.0
        psf = gaussian_kernel((9, 9), 1.5)
        restored = tttrlib.richardson_lucy_2d(blur(truth, psf), psf, 100, False, 0.0, False)
        peak = np.unravel_index(int(np.argmax(restored)), restored.shape)
        self.assertEqual(peak, (20, 20))

    def test_an_asymmetric_kernel_does_not_shift_it_either(self):
        """The same, with an even-sided offset in play on one axis."""
        truth = np.zeros((41, 45))
        truth[20, 22] = 1.0
        psf = gaussian_kernel((5, 11), 1.2)
        restored = tttrlib.richardson_lucy_2d(blur(truth, psf), psf, 100, False, 0.0, False)
        peak = np.unravel_index(int(np.argmax(restored)), restored.shape)
        self.assertEqual(peak, (20, 22))

    def test_three_dimensions(self):
        """A stack takes the same path; the axial axis is just another axis."""
        truth = np.zeros((16, 24, 24))
        truth[8, 10, 12] = 1.0
        psf = gaussian_kernel((5, 7, 7), 1.2)
        blurred = blur(truth, psf)
        restored = tttrlib.richardson_lucy_3d(blurred, psf, 100, False, 0.0, False)
        self.assertEqual(restored.shape, truth.shape)
        peak = np.unravel_index(int(np.argmax(restored)), restored.shape)
        self.assertEqual(peak, (8, 10, 12))
        self.assertAlmostEqual(restored.sum() / blurred.sum(), 1.0, places=9)

    def test_clip(self):
        image = np.ascontiguousarray(np.random.default_rng(0).random((24, 24)))
        psf = gaussian_kernel((5, 5), 1.0)
        clipped = tttrlib.richardson_lucy_2d(image, psf, 30, True, 0.0, False)
        self.assertLessEqual(clipped.max(), 1.0)
        self.assertGreaterEqual(clipped.min(), -1.0)

    def test_refusals(self):
        image = np.ascontiguousarray(np.ones((16, 16)))
        with self.assertRaises(Exception):  # PSF bigger than the image
            tttrlib.richardson_lucy_2d(image, gaussian_kernel((21, 21), 2.0), 5, False, 0.0, False)
        with self.assertRaises(Exception):  # a PSF that sums to zero
            tttrlib.richardson_lucy_2d(image, np.zeros((5, 5)), 5, False, 0.0, False)


class TestWiener(unittest.TestCase):
    def test_sharpens(self):
        truth = spikes()
        psf = gaussian_kernel((9, 9), 1.5)
        blurred = blur(truth, psf)
        restored = tttrlib.wiener_deconvolve_2d(blurred, psf, 0.001)
        self.assertGreater(restored.max(), 1.5 * blurred.max())

    def test_a_non_positive_balance_is_refused(self):
        image = np.ascontiguousarray(np.ones((16, 16)))
        with self.assertRaises(Exception):
            tttrlib.wiener_deconvolve_2d(image, gaussian_kernel((5, 5), 1.0), 0.0)



class TestEventMode(unittest.TestCase):
    """List-mode reconstruction: photons, not a grid of counts.

    A scanned photon-counting image arrives as detections with times, and the
    grid is imposed by the reader. Two things are lost in imposing it, and this
    is what the event-mode entry point exists to keep:

    * *where in the pixel* the photon landed, which the macro time within the
      line gives to a fraction of a pixel;
    * the fact that the beam **moved** during the dwell, so a binned pixel is a
      line integral — a rectangle one pixel wide, sigma = 1/sqrt(12) = 0.289 px,
      convolved on top of the optics and invisible to a deconvolution that only
      knows the PSF.
    """

    def test_flux_is_conserved(self):
        rng = np.random.default_rng(0)
        coordinates = rng.normal(16.0, 1.0, (4000, 2))
        psf = fine_kernel((9, 9), 1.3, 8)
        restored = tttrlib.richardson_lucy_events_2d(
            np.ascontiguousarray(coordinates), psf, 32, 32, 25, 8
        )
        self.assertAlmostEqual(restored.sum() / 4000.0, 1.0, places=9)
        self.assertGreaterEqual(restored.min(), 0.0)

    def test_a_single_photon_reconstructs_at_its_own_sub_pixel_position(self):
        """One photon carries the whole point: it is placed, not rounded.

        With a single detection the maximum-likelihood answer is the PSF
        centred on that detection, so the reconstruction's centre of mass has
        to sit at the fractional coordinate given — not at the pixel centre a
        binning would have snapped it to.
        """
        # 19 px of support at sigma 1.5 is 6.3 sigma; see the truncation test
        # below for why that number and not a smaller one.
        psf = fine_kernel((19, 19), 1.5, 8)
        for offset in (0.0, 0.25, 0.5, 0.75):
            coordinates = np.ascontiguousarray([[24.0, 24.0 + offset]])
            restored = tttrlib.richardson_lucy_events_2d(
                coordinates, psf, 48, 48, 1, 8
            )
            columns = np.arange(48)
            centroid = (restored.sum(axis=0) * columns).sum() / restored.sum()
            self.assertAlmostEqual(centroid, 24.0 + offset, places=8)

    def test_the_psf_support_sets_the_positional_accuracy(self):
        """Truncating the PSF biases where a photon lands, and by how much.

        For a photon at a fractional position the kernel is sampled at offsets
        that are *not* symmetric about it, so cutting the tails cuts unequally
        and drags the centroid. Interpolation is not the culprit -- this error
        does not move when the PSF is sampled finer -- and it falls off very
        fast with support, which makes it a cheap thing to buy out of:

            3.7 sigma  ->  8e-4 px      6.3 sigma  ->  2e-9 px
            5.0 sigma  ->  3e-6 px      7.7 sigma  ->  1e-13 px

        Five sigma is the number to remember.
        """
        errors = []
        for support in (11, 15, 19):
            psf = fine_kernel((support, support), 1.5, 8)
            worst = 0.0
            for offset in (0.25, 0.5, 0.75):
                restored = tttrlib.richardson_lucy_events_2d(
                    np.ascontiguousarray([[24.0, 24.0 + offset]]), psf, 48, 48, 1, 8
                )
                columns = np.arange(48)
                centroid = (restored.sum(axis=0) * columns).sum() / restored.sum()
                worst = max(worst, abs(centroid - 24.0 - offset))
            errors.append(worst)
        self.assertLess(errors[0], 1e-3)
        self.assertLess(errors[1], 1e-5)
        self.assertLess(errors[2], 1e-8)

    def test_two_photons_a_fraction_of_a_pixel_apart_stay_apart(self):
        """The information binning destroys, stated as a test.

        Both photons round to the same pixel, so any grid-based method sees one
        location and cannot see otherwise. Event-wise, the reconstruction is
        two PSFs at two distinct centroids.
        """
        psf = fine_kernel((11, 11), 1.2, 8)
        near = np.ascontiguousarray([[16.0, 15.7], [16.0, 16.3]])
        far = np.ascontiguousarray([[16.0, 16.0], [16.0, 16.0]])
        a = tttrlib.richardson_lucy_events_2d(near, psf, 32, 32, 1, 8)
        b = tttrlib.richardson_lucy_events_2d(far, psf, 32, 32, 1, 8)
        self.assertFalse(np.allclose(a, b, atol=1e-6))
        self.assertAlmostEqual(self._variance(a) - self._variance(b), 0.3**2, places=2)

    @staticmethod
    def _variance(image):
        marginal = image.sum(axis=0)
        columns = np.arange(len(marginal))
        mean = (marginal * columns).sum() / marginal.sum()
        return (marginal * (columns - mean) ** 2).sum() / marginal.sum()

    def test_oversampling_the_psf_removes_the_interpolation_broadening(self):
        """The reason `psf_oversampling` exists, measured.

        Two point sources 0.6 px apart are a distribution of variance 0.3^2
        about their midpoint, and nothing else. What the engine adds on top is
        the variance of the linear interpolation itself -- `t(1-t)` where `t` is
        the photon's offset in *samples* -- so at one sample per pixel a 0.09
        separation reads as 0.30, more than three times too wide, and the excess
        swings with sub-pixel position. Sampling K times finer divides it by
        K^2, which this walks down:

            K = 1  ->  0.21        K = 8   ->  0.0037
            K = 4  ->  0.0100      K = 16  ->  0.0006
        """
        near = np.ascontiguousarray([[16.0, 15.7], [16.0, 16.3]])
        far = np.ascontiguousarray([[16.0, 16.0], [16.0, 16.0]])
        excess = []
        for oversampling in (1, 4, 16):
            psf = fine_kernel((11, 11), 1.2, oversampling)
            a = tttrlib.richardson_lucy_events_2d(near, psf, 32, 32, 1, oversampling)
            b = tttrlib.richardson_lucy_events_2d(far, psf, 32, 32, 1, oversampling)
            excess.append(self._variance(a) - self._variance(b) - 0.3**2)
        self.assertGreater(excess[0], 0.15)
        self.assertLess(excess[1], 0.02)
        self.assertLess(excess[2], 0.002)
        # Not merely smaller -- smaller like 1/K^2.
        self.assertGreater(excess[0] / excess[1], 8.0)
        self.assertGreater(excess[1] / excess[2], 8.0)

    def test_it_matches_a_transcription_of_the_list_mode_iteration(self):
        """The engine against the formula, written out plainly in NumPy.

        Deliberately *not* against the grid engine: the two are different
        estimators, because list mode divides by a sensitivity the grid form has
        no term for. Checking one against the other would only measure how far
        apart they are supposed to be, and would pass or fail for reasons that
        have nothing to do with either being right.
        """
        rng = np.random.default_rng(3)
        coordinates = np.ascontiguousarray(rng.normal(12.0, 2.0, (300, 2)))
        shape = (24, 24)
        psf = fine_kernel((7, 7), 1.1, 4)
        n_iter = 8

        # Dense system matrix: A[i, x] = h(u_i - x). Only tractable because the
        # grid is tiny, which is the point of doing it this way.
        rows, columns = np.mgrid[0 : shape[0], 0 : shape[1]]
        centre = (np.array(psf.shape) - 1) / 2.0
        sample = (
            np.stack(
                [
                    coordinates[:, 0, None, None] - rows[None],
                    coordinates[:, 1, None, None] - columns[None],
                ],
                axis=-1,
            )
            * 4
            + centre
        )
        base = np.floor(sample).astype(int)
        fraction = sample - base
        matrix = np.zeros((len(coordinates), shape[0] * shape[1]))
        padded = np.zeros(np.array(psf.shape) + 1)
        padded[: psf.shape[0], : psf.shape[1]] = psf / comb_sum(psf, 4)
        for dr in (0, 1):
            for dc in (0, 1):
                r = np.clip(base[..., 0] + dr, 0, padded.shape[0] - 1)
                c = np.clip(base[..., 1] + dc, 0, padded.shape[1] - 1)
                inside = (
                    (base[..., 0] + dr >= 0)
                    & (base[..., 0] + dr < psf.shape[0])
                    & (base[..., 1] + dc >= 0)
                    & (base[..., 1] + dc < psf.shape[1])
                )
                weight = (fraction[..., 0] if dr else 1 - fraction[..., 0]) * (
                    fraction[..., 1] if dc else 1 - fraction[..., 1]
                )
                matrix += (np.where(inside, weight * padded[r, c], 0.0)).reshape(
                    len(coordinates), -1
                )

        sensitivity = self._sensitivity(psf, shape, 4)
        estimate = np.ones(shape[0] * shape[1])
        for _ in range(n_iter):
            predicted = matrix @ estimate
            ratio = np.where(predicted > 0, 1.0 / np.maximum(predicted, 1e-300), 0.0)
            estimate = estimate * (matrix.T @ ratio) / sensitivity

        engine = tttrlib.richardson_lucy_events_2d(
            coordinates, psf, shape[0], shape[1], n_iter, 4
        )
        np.testing.assert_allclose(
            engine.ravel(), estimate, rtol=1e-9, atol=1e-12
        )

    @staticmethod
    def _sensitivity(psf, shape, oversampling):
        """sum over in-frame grid points of h(x' - x), by direct summation."""
        reach = (psf.shape[0] // 2 + oversampling - 1) // oversampling + 1
        centre = (np.array(psf.shape) - 1) / 2.0
        out = np.zeros(shape)
        for row in range(shape[0]):
            for column in range(shape[1]):
                total = 0.0
                for dr in range(-reach, reach + 1):
                    for dc in range(-reach, reach + 1):
                        r, c = row + dr, column + dc
                        if not (0 <= r < shape[0] and 0 <= c < shape[1]):
                            continue
                        p = np.array([dr, dc]) * oversampling + centre
                        b = np.floor(p).astype(int)
                        f = p - b
                        value = 0.0
                        for ir in (0, 1):
                            for ic in (0, 1):
                                rr, cc = b[0] + ir, b[1] + ic
                                if not (0 <= rr < psf.shape[0] and 0 <= cc < psf.shape[1]):
                                    continue
                                w = (f[0] if ir else 1 - f[0]) * (f[1] if ic else 1 - f[1])
                                value += w * psf[rr, cc] / comb_sum(psf, oversampling)
                        total += value
                out[row, column] = total if total > 0 else 1.0
        return out.ravel()

    @pytest.mark.heavy
    def test_the_standard_and_photon_forms_answer_the_same_question(self):
        """The library's rule, as a test: both forms, and they agree.

        Bin the photons and run the grid engine; jitter the same histogram back
        into photons and run the list-mode engine. Neither is a reformulation of
        the other, so they will not agree pixel for pixel -- what has to hold is
        that they recover the same object, which for a pair of well-separated
        spots means the same positions and comparable concentration.
        """
        rng = np.random.default_rng(9)
        truth = np.zeros((32, 32))
        truth[10, 10] = 1.0
        truth[22, 20] = 1.0
        photons = np.vstack(
            [
                rng.normal([10, 10], 1.3, (20_000, 2)),
                rng.normal([22, 20], 1.3, (20_000, 2)),
            ]
        )
        counts = tttrlib.counts_from_events_2d(np.ascontiguousarray(photons), 32, 32)

        grid = tttrlib.richardson_lucy_2d(
            np.ascontiguousarray(counts), gaussian_kernel((9, 9), 1.3),
            40, False, 0.0, False,
        )
        jittered = tttrlib.events_from_counts_2d(np.ascontiguousarray(counts), 1)
        listed = tttrlib.richardson_lucy_events_2d(
            np.ascontiguousarray(jittered), fine_kernel((9, 9), 1.3, 8), 32, 32, 40, 8
        )

        for image in (grid, listed):
            found = sorted(
                tuple(np.unravel_index(int(i), image.shape))
                for i in np.argsort(image.ravel())[-2:]
            )
            self.assertEqual(found, [(10, 10), (22, 20)])

    def test_weights_scale_a_photon_without_moving_it(self):
        psf = fine_kernel((9, 9), 1.3, 8)
        rng = np.random.default_rng(7)
        coordinates = np.ascontiguousarray(rng.normal(16.0, 1.5, (500, 2)))
        once = tttrlib.richardson_lucy_events_2d(coordinates, psf, 32, 32, 10, 8)
        twice = tttrlib.richardson_lucy_events_2d(
            np.ascontiguousarray(np.vstack([coordinates, coordinates])),
            psf, 32, 32, 10, 8,
        )
        np.testing.assert_allclose(twice, 2.0 * once, rtol=1e-9, atol=1e-12)

    @pytest.mark.heavy
    def test_the_border_is_not_driven_up_to_explain_undetectable_photons(self):
        """What the sensitivity term buys.

        Near an edge part of a pixel's PSF falls outside the frame, so a source
        there produces fewer detections than the same source in the middle.
        Without the correction the iteration compensates by inflating the edge
        into a bright rim; with it, a uniform source reconstructs uniform.
        """
        rng = np.random.default_rng(11)
        coordinates = np.ascontiguousarray(rng.uniform(0.0, 31.0, (200_000, 2)))
        psf = fine_kernel((9, 9), 1.2, 4)
        restored = tttrlib.richardson_lucy_events_2d(coordinates, psf, 32, 32, 20, 4)
        edge = np.concatenate([restored[0], restored[-1], restored[:, 0], restored[:, -1]])
        middle = restored[8:24, 8:24]
        self.assertLess(edge.mean() / middle.mean(), 1.30)


class TestScanBlur(unittest.TestCase):
    """The blur the scan adds, which the optics know nothing about."""

    @staticmethod
    def moments(kernel, oversampling):
        positions = (np.arange(len(kernel)) - len(kernel) // 2) / oversampling
        mean = (kernel * positions).sum()
        return mean, np.sqrt((kernel * (positions - mean) ** 2).sum())

    def test_the_dwell_rectangle_has_the_variance_of_a_unit_rectangle(self):
        """sigma = 1/sqrt(12). Everything downstream is calibrated on it."""
        kernel = tttrlib.scan_blur_kernel_1d(1e-6, 0.0, 0.0, 32, True)
        mean, sigma = self.moments(kernel, 32)
        self.assertAlmostEqual(kernel.sum(), 1.0, places=12)
        # Exactly centred. The fine grid is integrated down by overlap rather
        # than to the nearest output sample, because `refine` is even and
        # rounding sent every boundary sample the same way -- a kernel whose
        # mean sat 1/4096 px off centre, which shifts a whole reconstruction.
        self.assertAlmostEqual(mean, 0.0, places=12)
        self.assertAlmostEqual(sigma, 1.0 / np.sqrt(12.0), places=3)

    def test_without_the_dwell_only_the_timing_terms_remain(self):
        """Event-wise, the rectangle was never applied, so it must come off."""
        kernel = tttrlib.scan_blur_kernel_1d(1e-6, 100e-12, 25e-9, 32, False)
        _, sigma = self.moments(kernel, 32)
        # 100 ps jitter and a 25 ns clock over a 1 us dwell: far below a pixel.
        self.assertLess(sigma, 0.02)
        self.assertAlmostEqual(kernel.sum(), 1.0, places=12)

    def test_jitter_enters_through_the_scan_speed(self):
        """Halve the dwell and the same jitter is twice the position error."""
        slow = self.moments(tttrlib.scan_blur_kernel_1d(1e-6, 50e-9, 0.0, 64, False), 64)[1]
        fast = self.moments(tttrlib.scan_blur_kernel_1d(5e-7, 50e-9, 0.0, 64, False), 64)[1]
        # Not exact: each kernel is resampled onto its own output grid, and at
        # these widths that discretisation is worth a fraction of a percent.
        self.assertAlmostEqual(fast / slow, 2.0, delta=0.02)
        # And at a fast enough scan it stops being negligible.
        self.assertGreater(fast, 0.09)

    def test_the_terms_add_in_variance(self):
        dwell = 1e-6
        jitter = 200e-9
        both = self.moments(
            tttrlib.scan_blur_kernel_1d(dwell, jitter, 0.0, 64, True), 64
        )[1]
        sweep = self.moments(tttrlib.scan_blur_kernel_1d(dwell, 0.0, 0.0, 64, True), 64)[1]
        alone = self.moments(tttrlib.scan_blur_kernel_1d(dwell, jitter, 0.0, 64, False), 64)[1]
        self.assertAlmostEqual(both**2, sweep**2 + alone**2, places=3)

if __name__ == "__main__":
    unittest.main()

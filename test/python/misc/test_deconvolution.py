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

import tttrlib


def gaussian_kernel(shape, sigma):
    """A normalised Gaussian on an odd grid."""
    grids = np.meshgrid(
        *[np.arange(n) - (n - 1) / 2.0 for n in shape], indexing="ij"
    )
    kernel = np.exp(-0.5 * sum((g / sigma) ** 2 for g in grids))
    return np.ascontiguousarray(kernel / kernel.sum())


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


if __name__ == "__main__":
    unittest.main()

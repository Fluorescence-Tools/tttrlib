"""CLSMImage.compute_ics: output shape, frame pairing and input validation.

These tests need no data files: they correlate a small synthetic image stack.

The shape test is a memory-safety regression. compute_ics allocates one
correlation map per correlated frame *pair*, but used to report the number of
input *frames* as the first output dimension. Whenever fewer pairs than frames
were correlated -- which is every frame lag greater than zero -- the returned
array over-declared its length, and reading the tail walked off the allocation
and segfaulted the interpreter.
"""
from __future__ import division

import os
import sys
import unittest

import numpy as np

import tttrlib


def _stack(n_frames=8, ny=16, nx=16, seed=42):
    """Return a small synthetic image stack with some spatial correlation."""
    rng = np.random.default_rng(seed)
    images = rng.poisson(20.0, size=(n_frames, ny, nx)).astype(float)
    for k in range(n_frames):
        images[k] = (images[k]
                     + np.roll(images[k], 1, axis=0)
                     + np.roll(images[k], 1, axis=1)) / 3.0
    return np.ascontiguousarray(images)


def _ics(images, pairs=None):
    """Run compute_ics on an image stack, optionally with explicit frame pairs."""
    kwargs = dict(images=images, x_range=[0, -1], y_range=[0, -1],
                  subtract_average="frame")
    if pairs is not None:
        kwargs["frames_index_pairs"] = pairs
    return np.asarray(tttrlib.CLSMImage.compute_ics(**kwargs))


class TestComputeIcs(unittest.TestCase):

    def test_output_length_equals_number_of_pairs(self):
        """The first output axis counts correlated pairs, not input frames."""
        images = _stack(n_frames=8)
        for lag in (0, 1, 2, 5):
            pairs = [(i, i + lag) for i in range(8 - lag)]
            out = _ics(images, pairs)
            self.assertEqual(out.shape[0], len(pairs),
                             f"lag {lag}: declared {out.shape[0]} maps for "
                             f"{len(pairs)} pairs")
            # Touch every element. An over-declared array reads past the
            # allocation here, which is what used to crash the interpreter.
            self.assertTrue(np.isfinite(out).all())

    def test_default_pairs_correlate_every_frame_with_itself(self):
        """Without explicit pairs the auto-correlation of each frame is used."""
        images = _stack(n_frames=8)
        auto = _ics(images)
        self.assertEqual(auto.shape, (8, 16, 16))
        explicit = _ics(images, [(i, i) for i in range(8)])
        np.testing.assert_allclose(auto, explicit)

    def test_frame_pairs_are_bounds_checked(self):
        """Pairs addressing frames outside the stack are dropped, not read."""
        images = _stack(n_frames=8)
        out = _ics(images, [(0, 0), (1, 999), (-4, 2), (3, 3)])
        self.assertEqual(out.shape[0], 2)  # only (0, 0) and (3, 3) are valid
        self.assertTrue(np.isfinite(out).all())

    def test_all_pairs_invalid_yields_an_empty_result(self):
        """A fully invalid pair list returns an empty array rather than crashing."""
        images = _stack(n_frames=8)
        out = _ics(images, [(77, 88), (-1, -1)])
        self.assertEqual(out.shape[0], 0)

    def test_lag_zero_matches_a_longer_pair_list_prefix(self):
        """Correlating a lag in isolation equals correlating it among others."""
        images = _stack(n_frames=8)
        alone = _ics(images, [(0, 2)])
        together = _ics(images, [(0, 2), (1, 3), (2, 4)])
        np.testing.assert_allclose(alone[0], together[0])


def _numpy_reference(a, b=None):
    """The correlation compute_ics is supposed to produce, straight from NumPy.

    ``ifft2(fft2(A) * conj(fft2(B)))``, real part. This is the same convention the
    published implementations use -- PAM's ``Do_2D_XCor.m`` and the Kolin/Wiseman STICS
    reference both compute exactly this and then divide by ``N * mean^2`` to normalise,
    which is what ChiSurf's ``normalise_ics`` does. Only the convention was read from
    those; no code was taken (both are GPL-3, against this project's GPL-2.0).
    """
    b = a if b is None else b
    return np.real(np.fft.ifft2(np.fft.fft2(a) * np.conj(np.fft.fft2(b))))


def _raw_ics(a, b=None):
    """compute_ics on one frame pair with no averaging, so it is directly comparable."""
    second = a if b is None else b
    images = np.ascontiguousarray(np.stack([a, second]).astype(float))
    pairs = [(0, 0)] if b is None else [(0, 1)]
    return np.asarray(tttrlib.CLSMImage.compute_ics(
        images=images, x_range=[0, -1], y_range=[0, -1],
        subtract_average="", frames_index_pairs=pairs))[0]


class TestAgainstNumpy(unittest.TestCase):
    """A/B the correlation kernel against NumPy, on shapes that are not square.

    Every other test in this file uses a 16x16 stack, and that is exactly why two
    defects lived here for so long: `get_roi` indexed rows with the line count instead
    of the pixel count, which is identical when the frame is square, and `compute_ics`
    fed an `r2c` half spectrum to a full `c2c` inverse. Together they scrambled every
    non-square correlation and read past the buffer on the last frame of a tall one.
    Neither showed up as a failure, because a fit of a shape with a free amplitude
    absorbs both.
    """

    SHAPES = [(16, 16), (16, 32), (32, 16), (64, 8), (8, 64), (33, 17), (17, 33)]

    def test_autocorrelation_matches_numpy(self):
        rng = np.random.default_rng(5)
        for ny, nx in self.SHAPES:
            a = rng.poisson(4.0, size=(ny, nx)).astype(float)
            got, want = _raw_ics(a), _numpy_reference(a)
            scale = max(np.abs(want).max(), 1e-30)
            self.assertLess(np.abs(got - want).max() / scale, 1e-12,
                            f"auto-correlation differs from NumPy at {ny}x{nx}")

    def test_crosscorrelation_matches_numpy(self):
        rng = np.random.default_rng(7)
        for ny, nx in self.SHAPES:
            a = rng.poisson(4.0, size=(ny, nx)).astype(float)
            b = rng.poisson(4.0, size=(ny, nx)).astype(float)
            got, want = _raw_ics(a, b), _numpy_reference(a, b)
            scale = max(np.abs(want).max(), 1e-30)
            self.assertLess(np.abs(got - want).max() / scale, 1e-12,
                            f"cross-correlation differs from NumPy at {ny}x{nx}")

    def test_autocorrelation_of_a_delta_is_a_delta(self):
        """The acceptance test, and the one that made the defect visible.

        It needs no agreement about normalisation: whatever the scale, every lag other
        than zero must be zero. Before the fix a delta at (4, 7) in a 16x32 frame gave
        1.0625 at the peak with 0.0625 = 2/nx smeared across every even column.
        """
        for pos in [(0, 0), (2, 3), (4, 7), (10, 25), (15, 31)]:
            a = np.zeros((16, 32))
            a[pos] = 1.0
            g = _raw_ics(a)
            self.assertAlmostEqual(g[0, 0], 1.0, places=10,
                                   msg=f"delta at {pos}: peak should be 1")
            off_peak = np.abs(np.delete(g.ravel(), 0)).max()
            self.assertLess(off_peak, 1e-10,
                            f"delta at {pos}: off-peak should be 0, got {off_peak}")

    def test_autocorrelation_does_not_depend_on_position(self):
        """Translation invariance -- a property, not a reference value.

        Shifting the image cannot change its autocorrelation. It did: the row-stride
        defect made the answer depend on where the signal sat, and one position lost the
        signal altogether.
        """
        rng = np.random.default_rng(11)
        a = rng.poisson(4.0, size=(16, 32)).astype(float)
        base = _raw_ics(a)
        for shift, axis in ((1, 0), (5, 0), (1, 1), (13, 1)):
            rolled = np.ascontiguousarray(np.roll(a, shift, axis=axis))
            np.testing.assert_allclose(
                _raw_ics(rolled), base, rtol=1e-10, atol=1e-8,
                err_msg=f"autocorrelation changed after a shift of {shift} on axis {axis}")

    def test_non_square_stacks_are_finite(self):
        """Guards the non-finite frames: a tall ROI used to read past its own buffer.

        It was always the LAST frame and only for a self-pair, because the overread of
        any earlier frame lands harmlessly in the next one.
        """
        rng = np.random.default_rng(0)
        for shape in [(12, 32, 16), (12, 64, 8), (12, 33, 17), (24, 32, 16), (12, 16, 32)]:
            images = np.ascontiguousarray(rng.poisson(3.0, size=shape).astype(float))
            out = np.asarray(tttrlib.CLSMImage.compute_ics(
                images=images, x_range=[0, -1], y_range=[0, -1],
                subtract_average="frame"))
            self.assertTrue(np.isfinite(out).all(),
                            f"{shape}: {int((~np.isfinite(out)).sum())} non-finite values")

    def test_pam_normalisation_reproduces_the_published_form(self):
        """G = corr / (N * mean^2) - 1, the form PAM and the STICS reference both use."""
        rng = np.random.default_rng(3)
        a = rng.poisson(25.0, size=(16, 32)).astype(float)
        n = a.size
        g_pam = _numpy_reference(a) / (n * a.mean() ** 2) - 1.0
        g_ours = _raw_ics(a) / (n * a.mean() ** 2) - 1.0
        np.testing.assert_allclose(g_ours, g_pam, rtol=1e-10, atol=1e-12)
        # and the zero-lag amplitude is the usual 1/N_particles-style quantity
        self.assertGreater(g_ours[0, 0], 0.0)


class TestAgainstPysimfcs(unittest.TestCase):
    """An implementation that is not ours: Jay Unruh's ``pysimfcs``
    ``analysis_utils.autocorr2d`` (NumPy port of the Jay_Plugins ICS): the raw
    correlation divided by ``N * mean^2`` minus one, fftshifted. Identical
    (0.0) on even shapes; on an odd width pysimfcs' ``irfft2`` without ``s=``
    drops the last column (their limitation), so only even shapes are compared.
    Skips when junk/pysimfcs is absent."""

    PYSIMFCS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "..",
                            "chisurf", "junk", "pysimfcs")

    def test_normalised_autocorrelation_identical(self):
        if not os.path.isdir(self.PYSIMFCS):
            self.skipTest("junk/pysimfcs not present")
        import importlib
        sys.path.insert(0, self.PYSIMFCS)
        try:
            au = importlib.import_module("analysis_utils")
        finally:
            sys.path.pop(0)
        rng = np.random.default_rng(0)
        for shape in ((16, 16), (32, 48), (64, 8), (8, 64)):
            with self.subTest(shape=shape):
                a = rng.poisson(20.0, shape).astype(float)
                ours = _raw_ics(a) / (a.size * a.mean() ** 2) - 1.0
                np.testing.assert_allclose(np.fft.fftshift(ours), au.autocorr2d(a), rtol=0, atol=1e-12)


class TestAgainstKolinWisemanOctave(unittest.TestCase):
    """David Kolin's ``stics.m`` / ``corrfunc.m`` (2003, the code behind
    Hebert, Costantino & Wiseman 2005), recorded from Octave by
    ``gen_ab_stics_kolin_reference.py``: the raw time-lag correlation averaged
    over frame pairs and the per-frame normalised spatial ACF. ``compute_ics``
    with ``frames_index_pairs = [(i, i + tau)]`` averaged over the pairs is
    STICS at lag tau; both agree with the MATLAB code bit for bit."""

    FIX = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "data", "reference",
                       "stics_kolin_octave_reference.npz")

    def test_stics_and_normalised_acf_identical(self):
        if not os.path.exists(self.FIX):
            self.skipTest("stics_kolin_octave_reference.npz not present")
        d = np.load(self.FIX)
        ser = np.ascontiguousarray(d["imgser"])
        T = ser.shape[0]
        for tau in range(int(d["n_tau"])):
            with self.subTest(tau=tau):
                pairs = [(i, i + tau) for i in range(T - tau)]
                ours = np.asarray(tttrlib.CLSMImage.compute_ics(
                    images=ser, x_range=[0, -1], y_range=[0, -1], subtract_average="",
                    frames_index_pairs=pairs))
                np.testing.assert_allclose(np.fft.fftshift(ours.mean(0)), d["timecorr"][tau], rtol=0, atol=1e-6)
        for z in range(T):
            with self.subTest(frame=z):
                a = ser[z]
                np.testing.assert_allclose(np.fft.fftshift(_raw_ics(a) / (a.size * a.mean() ** 2) - 1.0),
                                           d["G"][z], rtol=0, atol=1e-12)


if __name__ == '__main__':
    unittest.main()

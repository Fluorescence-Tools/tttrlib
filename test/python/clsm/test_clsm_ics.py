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


if __name__ == '__main__':
    unittest.main()

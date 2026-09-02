"""The batched species (fFCS) matrix against the per-pair composition it replaces.

``Correlator.species_matrix_correlation`` walks the photon stream ONCE for all
``n*(n+1)/2`` species pairs, where composing the same matrix from one
``Correlator`` per pair walks it that many times. The claim is not "close": with
one thread the two are bit-identical, because the only weight-dependent step of
the multi-tau coarsening is a zero-drop that cannot change the estimator. These
tests pin that, plus the packed pair ordering and the input validation.
"""
from __future__ import division

import os
import unittest

import numpy as np

import tttrlib

N_BINS, N_CASC = 8, 20


def _stream(n_photons=20000, n_species=3, seed=0):
    """A photon stream with realistic fFCS filter weights (signed, some zeros)."""
    rng = np.random.default_rng(seed)
    macro = np.cumsum(rng.integers(1, 200, size=n_photons)).astype(np.uint64)
    w = rng.normal(0.0, 1.0, size=(n_species, n_photons))
    w[rng.random(w.shape) < 0.02] = 0.0
    return macro, np.ascontiguousarray(w, dtype=np.float64)


def _pair_loop(macro, weights, n_bins=N_BINS, n_casc=N_CASC, method="wahl"):
    """The composition being replaced: one Correlator per (i, j), i <= j."""
    n_species = weights.shape[0]
    curves = []
    x = None
    for i in range(n_species):
        for j in range(i, n_species):
            c = tttrlib.Correlator()
            c.n_bins, c.n_casc = n_bins, n_casc
            c.method = method
            c.set_macrotimes(macro, macro)
            c.set_weights(
                np.ascontiguousarray(weights[i]), np.ascontiguousarray(weights[j])
            )
            c.run()
            x = np.asarray(c.get_x_axis(), dtype=float)
            curves.append(np.asarray(c.get_corr_normalized(), dtype=float))
    return x, np.asarray(curves)


def _pair_index(i, j, n):
    """Where pair (i, j), i <= j, sits in the packed upper-triangular matrix."""
    return i * n - i * (i - 1) // 2 + (j - i)


class Tests(unittest.TestCase):

    def test_matches_pair_loop(self):
        """Every element of the batched matrix equals the per-pair curve."""
        macro, w = _stream()
        x_ref, ref = _pair_loop(macro, w)
        x, matrix = tttrlib.Correlator.species_matrix_correlation(
            macro, w, N_BINS, N_CASC, "wahl"
        )
        self.assertEqual(matrix.shape, ref.shape)
        np.testing.assert_array_equal(x, x_ref)
        # Single-threaded the agreement is exact; with the thread split armed
        # the partial-sum reduction order differs, as it does between two runs
        # of the per-pair path itself.
        if os.environ.get("TTTRLIB_NUM_THREADS") == "1":
            np.testing.assert_array_equal(matrix, ref)
        else:
            np.testing.assert_allclose(matrix, ref, rtol=1e-9, atol=0.0)

    def test_pair_ordering(self):
        """The packed index maps back onto the auto/cross curves it claims."""
        macro, w = _stream(n_photons=8000, n_species=4, seed=3)
        n = w.shape[0]
        _, matrix = tttrlib.Correlator.species_matrix_correlation(
            macro, w, N_BINS, N_CASC, "wahl"
        )
        self.assertEqual(matrix.shape[0], n * (n + 1) // 2)
        for i in range(n):
            for j in range(i, n):
                c = tttrlib.Correlator()
                c.n_bins, c.n_casc = N_BINS, N_CASC
                c.set_macrotimes(macro, macro)
                c.set_weights(
                    np.ascontiguousarray(w[i]), np.ascontiguousarray(w[j])
                )
                c.run()
                np.testing.assert_allclose(
                    matrix[_pair_index(i, j, n)],
                    np.asarray(c.get_corr_normalized(), dtype=float),
                    rtol=1e-9, atol=0.0,
                )

    def test_single_species(self):
        """n=1 is one autocorrelation, not a degenerate empty matrix."""
        macro, w = _stream(n_photons=5000, n_species=1, seed=7)
        _, matrix = tttrlib.Correlator.species_matrix_correlation(
            macro, w, N_BINS, N_CASC, "wahl"
        )
        self.assertEqual(matrix.shape[0], 1)
        _, ref = _pair_loop(macro, w)
        np.testing.assert_allclose(matrix, ref, rtol=1e-9, atol=0.0)

    def test_other_methods_compose_per_pair(self):
        """A method without a shared-axis kernel still returns the right matrix."""
        macro, w = _stream(n_photons=4000, n_species=2, seed=11)
        for method in ("felekyan", "laurence"):
            x_ref, ref = _pair_loop(macro, w, method=method)
            x, matrix = tttrlib.Correlator.species_matrix_correlation(
                macro, w, N_BINS, N_CASC, method
            )
            np.testing.assert_array_equal(x, x_ref)
            np.testing.assert_allclose(matrix, ref, rtol=1e-12, atol=0.0)

    def test_rejects_mismatched_weights(self):
        """A weight matrix that is not (n_species, n_photons) is refused."""
        macro, w = _stream(n_photons=1000, n_species=2)
        with self.assertRaises(ValueError):
            tttrlib.Correlator.species_matrix_correlation(
                macro[:-1], w, N_BINS, N_CASC, "wahl"
            )


if __name__ == "__main__":
    unittest.main()

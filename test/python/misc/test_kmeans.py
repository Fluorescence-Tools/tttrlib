"""k-means: the whole fit in one compiled call (`KMeans.h`).

The kernel is a bit-exact port of the reference Python implementation ChiSurf
runs (``core/ml/cluster/_kmeans.py``) -- same uniforms in, same centres out,
digit for digit -- because ChiSurf seeds this from a caller-controlled stream
(Gaussian-HMM emission initialisation) and ranks restarts on the inertia, so a
one-ulp drift is a different answer. The fixture below was recorded from that
implementation; everything else here is a known-answer simulation or a
structural property, and nothing imports the implementation being replaced.
"""

import os
import unittest

import numpy as np

import tttrlib


def uniforms_for(n_clusters, n_init, seed):
    """The caller-side stream: n_init * n_clusters * (2 + floor(ln(k)))."""
    n_trials = 2 + int(np.log(n_clusters))
    return np.random.default_rng(seed).random(n_init * n_clusters * n_trials)


def blobs(seed=5, n_per=200):
    """Three separated blobs in 4D, membership known before clustering."""
    rng = np.random.default_rng(seed)
    x = np.vstack([rng.normal(0, 1, (n_per, 4)),
                   rng.normal(8, 1, (n_per, 4)),
                   rng.normal([0, 8, 0, 8], 1, (n_per // 2, 4))])
    return np.ascontiguousarray(x), np.repeat([0, 1, 2], [n_per, n_per, n_per // 2])


class TestTheFitRecoversTheBlobs(unittest.TestCase):

    def test_three_blobs_three_clusters_and_no_mixing(self):
        x, truth = blobs()
        c, l, s = tttrlib.kmeans(x, 3, uniforms_for(3, 4, seed=1), 4, 300, 1e-4)
        labels = np.asarray(l, dtype=np.int64)
        self.assertEqual(len(c), 3)
        for cl in np.unique(labels):
            members = truth[labels == cl]
            self.assertEqual(np.unique(members).size, 1,
                             "cluster %d mixes blobs %s" % (cl, np.unique(members)))
        # Lloyd centres are the means of their members, so every occupied
        # cluster sits at its blob's mean within that blob's own scatter.
        # (An *empty* cluster can legitimately survive as an orphan re-seeded
        # on a data point -- the reference behaves the same -- which is why
        # the assertion is over occupied clusters, not over range(k).)
        occupied = [cl for cl in np.unique(labels)
                    if int((labels == cl).sum()) >= 10]
        self.assertGreaterEqual(len(occupied), 2)
        for cl in occupied:
            centre_error = np.linalg.norm(c[cl] - x[truth == truth[labels == cl][0]].mean(axis=0))
            self.assertLess(centre_error, 0.5)

    def test_one_cluster_converges_to_the_mean(self):
        x, _ = blobs(seed=9)
        c, l, s = tttrlib.kmeans(x, 1, uniforms_for(1, 1, seed=2), 1, 300, 1e-4)
        self.assertLess(np.linalg.norm(c[0] - x.mean(axis=0)), 1e-6)


class TestDeterminism(unittest.TestCase):
    """The randomness is the caller's array, so the fit must be reproducible
    byte for byte -- that is the contract ChiSurf's HMM relies on."""

    def test_same_uniforms_same_answer_bit_for_bit(self):
        x, _ = blobs(seed=13)
        u = uniforms_for(5, 3, seed=7)
        a = tttrlib.kmeans(x, 5, u, 3, 100, 1e-6)
        b = tttrlib.kmeans(x, 5, u, 3, 100, 1e-6)
        np.testing.assert_array_equal(a[0], b[0])
        np.testing.assert_array_equal(a[1], b[1])
        self.assertEqual(a[2][0], b[2][0])
        self.assertEqual(a[2][1], b[2][1])

    def test_different_uniforms_may_differ(self):
        """Guards the guard: if two streams agreed something is ignoring u."""
        x, _ = blobs(seed=13)
        a = tttrlib.kmeans(x, 5, uniforms_for(5, 1, seed=7), 1, 100, 1e-4)
        b = tttrlib.kmeans(x, 5, uniforms_for(5, 1, seed=8), 1, 100, 1e-4)
        self.assertFalse(np.array_equal(a[0], b[0]) and a[2][0] == b[2][0],
                         "the seeding stream changed nothing")

    def test_a_wrong_length_stream_is_rejected(self):
        x, _ = blobs(seed=13, n_per=20)
        need = 2 * 3 * (2 + int(np.log(3)))
        with self.assertRaises(ValueError) as ctx:
            tttrlib.kmeans(x, 3, np.zeros(need - 1), 2, 10, 1e-4)
        self.assertIn(str(need), str(ctx.exception),
                      "the error must state the required length")

    def test_degenerate_fewer_samples_than_clusters(self):
        """Defined, as in the reference: centres are the data padded by the
        mean, labels are arange % k, inertia is zero, uniforms unused -- but
        still validated."""
        x = np.ascontiguousarray(np.arange(15.0).reshape(3, 5))
        u = np.zeros(1 * 4 * (2 + int(np.log(4))))
        c, l, s = tttrlib.kmeans(x, 4, u, 1, 10, 1e-4)
        np.testing.assert_array_equal(c[:3], x)
        np.testing.assert_array_equal(np.asarray(l), [0, 1, 2])
        self.assertEqual(s[0], 0.0)
        self.assertEqual(int(s[1]), 0)


class TestAgainstTheRecordedReference(unittest.TestCase):
    """The committed fixture, recorded from the reference implementation on a
    fixed stream: the bit-exactness pin. One ulp anywhere -- seeding, Lloyd,
    the empty-cluster re-seed, the final assignment pass -- fails this."""

    PATH = os.path.join(os.path.dirname(__file__), "..", "..", "data",
                        "reference", "kmeans_chisurf_reference.npz")

    @classmethod
    def setUpClass(cls):
        with np.load(cls.PATH) as z:
            cls.x = z["X"]
            cls.u = z["uniforms"]
            cls.k = int(z["n_clusters"])
            cls.n_init = int(z["n_init"])
            cls.ref = (z["centers"], z["labels"], float(z["inertia"]),
                       int(z["n_iter"]))

    def test_bit_identical_centres_labels_inertia(self):
        c, l, s = tttrlib.kmeans(self.x, self.k, self.u,
                                 self.n_init, 300, 1e-4)
        np.testing.assert_array_equal(c, self.ref[0])
        np.testing.assert_array_equal(np.asarray(l, dtype=np.int64),
                                      self.ref[1])
        self.assertEqual(s[0], self.ref[2])       # the full double, all digits
        self.assertEqual(int(s[1]), self.ref[3])

    def test_the_inertia_is_the_returned_centres(self):
        """The ranked number must describe what is returned, not the sweep's
        starting centres -- the reference re-measures and so must this."""
        c, l, s = tttrlib.kmeans(self.x, self.k, self.u, self.n_init, 300, 1e-4)
        labels = np.asarray(l, dtype=np.int64)
        inertia = 0.0
        for t in range(self.x.shape[0]):
            diff = self.x[t] - c[labels[t]]
            inertia += float(diff @ diff)
        self.assertEqual(inertia, s[0])


if __name__ == "__main__":
    unittest.main()

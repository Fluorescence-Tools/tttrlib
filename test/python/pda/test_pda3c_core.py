"""The three-colour PDA core kernels, against what defines them.

Every test here used to compare against ChiSurf's `pda3c` behind
`sys.path.insert(0, '/Users/tpeulen/dev/chisurf')`, which is not a valid
reference on two counts: this library is ChiSurf's upstream, so agreement only
says that two things that move together still move together; and an absolute
path to somebody's checkout means the tests skipped everywhere else, including
CI. Three of the five ran on one machine in the world.

They are replaced by references that stand on their own:

* `gauss_hermite_grid` against the property a quadrature grid exists to have --
  **moment exactness**. A Gauss-Hermite rule with n nodes integrates
  polynomials up to degree 2n-1 exactly, so the grid for N(mu, Sigma) must
  reproduce mu and Sigma to machine precision. No reference implementation is
  involved, and a grid that is subtly wrong (bad scaling, missing sqrt(2),
  un-normalised weights) cannot pass.
* `transfer_matrix_3c` against a NumPy transcription of the competing-acceptor
  cascade, and against the two limits it must satisfy exactly.
* `species_forward_model` against the composition of the two above plus the
  emission mixing, assembled in NumPy.
"""
import unittest

import numpy as np

import tttrlib

R0 = [47.0, 47.0, 47.0]

#: blue-laser excitation and an emission matrix with realistic crosstalk
EXCITATION = np.array([1.0, 0.06, 0.02])
EMISSION = np.array([[0.88, 0.10, 0.02],
                     [0.04, 0.85, 0.11],
                     [0.01, 0.07, 0.92]])


def cascade_transfer(distances, forster_radii):
    """Row i = where an excitation on dye i is finally emitted.

    Competing acceptors: dye a transfers to each b > a at k_ab = (R0/R)^6
    against its own decay (rate 1), so it emits with probability
    1 / (1 + sum_b k_ab) and passes k_ab / (1 + sum_b k_ab) on to b, which
    cascades. Transcribed from that statement.
    """
    d01, d02, d12 = distances
    r01, r02, r12 = forster_radii
    D = np.array([[0.0, d01, d02], [d01, 0.0, d12], [d02, d12, 0.0]])
    R = np.array([[0.0, r01, r02], [r01, 0.0, r12], [r02, r12, 0.0]])
    T = np.zeros((3, 3))
    for i in range(3):
        occupancy = np.zeros(3)
        occupancy[i] = 1.0
        for a in range(i, 3):
            if occupancy[a] == 0.0:
                continue
            k = np.array([(R[a, b] / D[a, b]) ** 6 if b > a else 0.0 for b in range(3)])
            denom = 1.0 + k.sum()
            T[i, a] += occupancy[a] / denom
            for b in range(a + 1, 3):
                occupancy[b] += occupancy[a] * k[b] / denom
    return T


def channel_probabilities(transfer, excitation=EXCITATION, emission=EMISSION):
    """normalise(excitation @ transfer @ emission) -- see the docstring of
    `test_channel_probabilities_against_the_defining_composition`."""
    p = np.asarray(excitation) @ np.asarray(transfer) @ np.asarray(emission)
    return p / p.sum()


def unpack_grid(flat, K=3):
    """The kernel returns (point..., weight) tuples interleaved."""
    flat = np.asarray(flat)
    M = len(flat) // (K + 1)
    grid = flat.reshape(M, K + 1)
    return grid[:, :K], grid[:, K]


class TestGaussHermiteGrid(unittest.TestCase):
    """The grid, against moment exactness rather than another grid."""

    @staticmethod
    def _grid(means, cholesky, n_nodes=5):
        nodes, weights = np.polynomial.hermite.hermgauss(n_nodes)
        return unpack_grid(tttrlib.gauss_hermite_grid(
            list(means), list(np.asarray(cholesky).flatten()),
            nodes.tolist(), weights.tolist(), 3))

    def test_the_weights_are_a_probability(self):
        L = np.diag([3.0, 4.0, 5.0])
        _points, w = self._grid([40.0, 50.0, 60.0], L)
        self.assertAlmostEqual(w.sum(), 1.0, places=12)
        self.assertTrue(np.all(w > 0.0))

    def test_it_reproduces_the_mean_and_the_covariance(self):
        """Degree 2n-1 exactness: with n >= 2 nodes the first and second
        moments of the Gaussian must come back exactly. This is the test a
        wrong scaling cannot survive -- drop the sqrt(2) and the covariance
        comes back halved."""
        means = np.array([40.0, 50.0, 60.0])
        # a genuinely correlated covariance, so an implementation that ignores
        # the off-diagonals of L fails here
        A = np.array([[3.0, 0.0, 0.0], [1.2, 4.0, 0.0], [0.5, -0.8, 5.0]])
        sigma = A @ A.T
        for n_nodes in (3, 5, 7):
            with self.subTest(n_nodes=n_nodes):
                points, w = self._grid(means, A, n_nodes)
                self.assertEqual(len(points), n_nodes ** 3)
                np.testing.assert_allclose(w @ points, means, rtol=1e-10, atol=1e-9)
                centred = points - means
                got = np.einsum('m,mi,mj->ij', w, centred, centred)
                np.testing.assert_allclose(got, sigma, rtol=1e-9, atol=1e-8)

    def test_it_integrates_a_cubic_exactly(self):
        """Third moments too, which is where a rule that is merely 'about
        right' stops agreeing: E[(x-mu)^3] = 0 for a Gaussian."""
        means = np.array([40.0, 50.0, 60.0])
        A = np.diag([3.0, 4.0, 5.0])
        points, w = self._grid(means, A, 5)
        third = np.einsum('m,mi->i', w, (points - means) ** 3)
        np.testing.assert_allclose(third, np.zeros(3), atol=1e-6)


class TestTransferMatrix(unittest.TestCase):

    def test_against_the_cascade_formula(self):
        rng = np.random.default_rng(11)
        for trial in range(20):
            with self.subTest(trial=trial):
                d = rng.uniform(25.0, 90.0, size=3)
                got = np.asarray(
                    tttrlib.transfer_matrix_3c(d.tolist(), R0, 3)).reshape(3, 3)
                np.testing.assert_allclose(got, cascade_transfer(d, R0),
                                           rtol=1e-12, atol=1e-14)

    def test_every_excitation_is_emitted_by_someone(self):
        got = np.asarray(
            tttrlib.transfer_matrix_3c([40.0, 50.0, 60.0], R0, 3)).reshape(3, 3)
        np.testing.assert_allclose(got.sum(axis=1), 1.0, atol=1e-12)

    def test_distant_dyes_do_not_transfer(self):
        got = np.asarray(
            tttrlib.transfer_matrix_3c([1e6, 1e6, 1e6], R0, 3)).reshape(3, 3)
        np.testing.assert_allclose(got, np.eye(3), atol=1e-10)

    def test_at_the_forster_radius_half_the_excitation_transfers(self):
        """The definition of R0, and the one number in this file that can be
        checked by hand: with only one acceptor in range, E = 1/2 at R = R0."""
        far = 1e6
        got = np.asarray(tttrlib.transfer_matrix_3c(
            [47.0, far, far], R0, 3)).reshape(3, 3)
        self.assertAlmostEqual(got[0, 0], 0.5, places=10)
        self.assertAlmostEqual(got[0, 1], 0.5, places=10)


class TestSpeciesForwardModel(unittest.TestCase):

    def test_against_the_composition_it_is(self):
        """The forward model is the quadrature-weighted average of the channel
        probabilities over the distance distribution. Assembled here from the
        NumPy cascade and the defining composition, so nothing in the reference
        comes from the kernel under test."""
        means = np.array([40.0, 50.0, 60.0])
        A = np.array([[3.0, 0.0, 0.0], [1.2, 4.0, 0.0], [0.5, -0.8, 5.0]])
        nodes, weights = np.polynomial.hermite.hermgauss(5)

        points, w = unpack_grid(tttrlib.gauss_hermite_grid(
            means.tolist(), A.flatten().tolist(), nodes.tolist(),
            weights.tolist(), 3))
        per_point = np.array([channel_probabilities(cascade_transfer(p, R0))
                              for p in points])
        ref = w @ per_point

        got = np.asarray(tttrlib.species_forward_model(
            means.tolist(), A.flatten().tolist(), nodes.tolist(),
            weights.tolist(), R0, EXCITATION.tolist(),
            EMISSION.flatten().tolist(), 3, 3))
        np.testing.assert_allclose(got, ref, rtol=1e-10, atol=1e-12)
        self.assertAlmostEqual(got.sum(), 1.0, places=10)

    def test_the_distance_distribution_moves_the_answer(self):
        """Otherwise the test above would pass for a model that averages
        nothing: close and distant species must not look the same."""
        A = np.diag([3.0, 3.0, 3.0])
        nodes, weights = np.polynomial.hermite.hermgauss(5)

        def model(means):
            return np.asarray(tttrlib.species_forward_model(
                list(means), A.flatten().tolist(), nodes.tolist(),
                weights.tolist(), R0, EXCITATION.tolist(),
                EMISSION.flatten().tolist(), 3, 3))

        near, far = model([30.0, 32.0, 31.0]), model([90.0, 95.0, 92.0])
        self.assertGreater(np.max(np.abs(near - far)), 0.1)


if __name__ == '__main__':
    unittest.main()

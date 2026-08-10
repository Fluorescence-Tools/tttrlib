import unittest
import numpy as np
import tttrlib

try:
    import sys
    sys.path.insert(0, '/Users/tpeulen/dev/chisurf')
    from chisurf.core.fluorescence.pda3c.species import (
        gauss_hermite_grid as py_ghg,
        covariance_from_statistics, covariance_to_cholesky,
    )
    from chisurf.core.fluorescence.pda3c.physics import (
        transfer_matrix as py_transfer,
        distances_to_matrix,
    )
    _HAVE_REF = True
except Exception:
    _HAVE_REF = False


class TestPda3cCore(unittest.TestCase):

    def _ref_setup(self):
        from chisurf.core.fluorescence.pda3c.physics import ThreeColorSetup
        return ThreeColorSetup.from_scalars(
            r0_bg=47.0, r0_br=47.0, r0_gr=47.0)

    def test_gauss_hermite_matches_ref(self):
        if not _HAVE_REF:
            self.skipTest("ChiSurf reference not available")
        means = np.array([40.0, 50.0, 60.0])
        cov = covariance_from_statistics([3.0, 4.0, 5.0], [0.3, 0.2, 0.1])
        L = covariance_to_cholesky(cov)
        nodes, w = np.polynomial.hermite.hermgauss(5)
        py_pts, py_wts = py_ghg(means, L, n_nodes=5)

        res = np.asarray(tttrlib.gauss_hermite_grid(
            means.tolist(), L.flatten().tolist(), nodes.tolist(), w.tolist(), 3))
        K = 3
        M = len(res) // (K + 1)
        cpp_pts = np.array([res[m * 4:m * 4 + 3] for m in range(M)])
        cpp_wts = np.array([res[m * 4 + 3] for m in range(M)])
        self.assertAlmostEqual(cpp_wts.sum(), 1.0, places=10)

        def sort_rows(X, W):
            o = np.lexsort(X.T)
            return X[o], W[o]
        a, b = sort_rows(py_pts, py_wts)
        c, d = sort_rows(cpp_pts, cpp_wts)
        self.assertTrue(np.allclose(a, c, atol=1e-10))
        self.assertTrue(np.allclose(b, d, atol=1e-10))

    def test_transfer_matrix_matches_ref(self):
        if not _HAVE_REF:
            self.skipTest("ChiSurf reference not available")
        setup = self._ref_setup()
        dist = np.array([40.0, 50.0, 60.0])
        ref = py_transfer(distances_to_matrix(dist, 3), setup)
        cpp = np.asarray(
            tttrlib.transfer_matrix_3c(dist.tolist(), [47.0] * 3, 3)
        ).reshape(3, 3)
        self.assertTrue(np.allclose(ref, cpp, atol=1e-10))

    def test_transfer_row_stochastic(self):
        cpp = np.asarray(
            tttrlib.transfer_matrix_3c([40.0, 50.0, 60.0], [47.0] * 3, 3)
        ).reshape(3, 3)
        # Each row sums to 1 (excitation is conserved)
        self.assertTrue(np.allclose(cpp.sum(axis=1), 1.0, atol=1e-10))

    def test_simple_identity_transfer(self):
        # Very large distances -> no transfer -> identity
        cpp = np.asarray(
            tttrlib.transfer_matrix_3c([1e6, 1e6, 1e6], [47.0] * 3, 3)
        ).reshape(3, 3)
        self.assertTrue(np.allclose(cpp, np.eye(3), atol=1e-10))

    def test_species_forward_model_matches_ref(self):
        if not _HAVE_REF:
            self.skipTest("ChiSurf reference not available")
        from chisurf.core.fluorescence.pda3c.physics import (
            transfer_matrix as py_transfer, distances_to_matrix,
            ThreeColorSetup,
        )
        from chisurf.core.fluorescence.crosstalk import apply_mixing
        setup = ThreeColorSetup.from_scalars(
            r0_bg=47.0, r0_br=47.0, r0_gr=47.0)
        means = np.array([40.0, 50.0, 60.0])
        cov = covariance_from_statistics([3.0, 4.0, 5.0], [0.3, 0.2, 0.1])
        L = covariance_to_cholesky(cov)
        nodes, w = np.polynomial.hermite.hermgauss(5)

        py_pts, py_wts = py_ghg(means, L, n_nodes=5)
        dist_mats = np.array([distances_to_matrix(pt, 3) for pt in py_pts])
        T = py_transfer(dist_mats, setup)
        emitted = np.einsum('d,...dj->...j', setup.excitation[0], T)
        ch = apply_mixing(setup.emission, emitted.T).T
        ref = np.einsum('mc,m->c', ch, py_wts)

        cpp = np.asarray(tttrlib.species_forward_model(
            means.tolist(), L.flatten().tolist(), nodes.tolist(), w.tolist(),
            [47.0] * 3, setup.excitation[0].tolist(),
            setup.emission.flatten().tolist(), 3, 3))
        self.assertTrue(np.allclose(ref, cpp, atol=1e-8))


if __name__ == '__main__':
    unittest.main()

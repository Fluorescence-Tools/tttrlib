"""The k-d tree and the mutual-reachability spanning tree (`Cluster.h`).

Two properties are load-bearing and neither is obvious from the signatures.

**The spanning tree must be the same one every algorithm finds.** Two kernels
are here — Borůvka over the tree, which is the one production uses, and Prim,
which is kept because it is obviously correct — and a downstream caller keeps a
third implementation in Python for environments without this library. A mutual-reachability weight is very often a
*core distance*, and one core distance is the weight of every edge it dominates,
so hundreds of edges tie and the minimum spanning tree is not unique. The edge
order is therefore total (weight, then the sorted endpoint pair), which makes it
unique; these tests are what stops that being quietly relaxed.

**The neighbour search must be exact.** A k-d tree that prunes a shade too
eagerly returns *almost* the right neighbours, which no assertion on a
downstream clustering would catch.
"""

import unittest

import numpy as np

import tttrlib


def blobs(n_samples=600, n_features=3, seed=3):
    """Three Gaussian blobs plus uniform noise, the shape a burst table has."""
    rng = np.random.default_rng(seed)
    which = rng.integers(0, 4, n_samples)
    noise = rng.uniform(-4, 12, (n_samples, n_features))
    signal = rng.normal(0, 0.4, (n_samples, n_features)) + 3.0 * which[:, None]
    return np.ascontiguousarray(np.where((which == 3)[:, None], noise, signal))


def brute_force_core_distances(data, k):
    """The k-th nearest neighbour distance, computed the slow obvious way."""
    diff = data[:, None, :] - data[None, :, :]
    squared = np.einsum("ijk,ijk->ij", diff, diff)
    return np.sqrt(np.sort(squared, axis=1)[:, k - 1])


def canonical(mst):
    """Sorted (weight, low endpoint, high endpoint) rows, for comparing trees."""
    mst = np.asarray(mst, dtype=float).reshape(-1, 3)
    low = np.minimum(mst[:, 0], mst[:, 1])
    high = np.maximum(mst[:, 0], mst[:, 1])
    rows = np.column_stack((mst[:, 2], low, high))
    return rows[np.lexsort((rows[:, 2], rows[:, 1], rows[:, 0]))]


class TestKDTree(unittest.TestCase):
    def test_core_distances_are_exact(self):
        """The tree must agree with brute force to the last bit, not merely closely."""
        for n_features in (1, 2, 5, 12):
            data = blobs(400, n_features)
            for k in (1, 5, 20):
                np.testing.assert_allclose(
                    np.asarray(tttrlib.core_distances(data, k)),
                    brute_force_core_distances(data, k),
                    rtol=1e-15,
                    atol=0.0,
                )

    def test_core_distance_of_k_equals_one_is_zero(self):
        """The first neighbour of a point is itself."""
        data = blobs(200, 3)
        np.testing.assert_allclose(np.asarray(tttrlib.core_distances(data, 1)), 0.0)

    def test_identical_points_do_not_break_the_split(self):
        """A node whose points are all equal cannot be split, and must not try."""
        data = np.ascontiguousarray(np.zeros((300, 4)))
        np.testing.assert_allclose(np.asarray(tttrlib.core_distances(data, 5)), 0.0)
        mst = np.asarray(tttrlib.mutual_reachability_mst(data, 5, 1.0))
        self.assertEqual(mst.shape, (299, 3))
        np.testing.assert_allclose(mst[:, 2], 0.0)


class TestMutualReachabilityMST(unittest.TestCase):
    def test_is_a_spanning_tree(self):
        """n - 1 edges, every vertex reached, no cycle."""
        data = blobs(500, 3)
        mst = np.asarray(tttrlib.mutual_reachability_mst(data, 5, 1.0))
        self.assertEqual(mst.shape, (499, 3))
        self.assertEqual(
            set(mst[:, :2].astype(int).ravel().tolist()), set(range(500))
        )
        parent = np.arange(500)

        def find(node):
            while parent[node] != node:
                parent[node] = parent[parent[node]]
                node = parent[node]
            return node

        for source, target, _ in mst:
            a, b = find(int(source)), find(int(target))
            self.assertNotEqual(a, b, "the spanning tree contains a cycle")
            parent[b] = a

    def test_boruvka_and_prim_return_the_same_tree(self):
        """The two kernels must agree edge for edge, including on ties.

        This is the assertion that the total edge order exists for. Weight alone
        does not determine the tree, and without the endpoint tie-break these two
        drift apart on exactly the data the method is used on.

        Swept over several shapes and seeds rather than checked once, because
        the failure it guards against is a *tie* being skipped: it appears on
        some point sets and not others, and a single fixture passed happily
        while a comparison in squared space was quietly dropping tied edges.
        """
        for n_features in (1, 2, 3, 8, 16):
            for seed in (3, 11, 29):
                for n_samples in (400, 900):
                    data = blobs(n_samples, n_features, seed)
                    tree = tttrlib.KDTree(
                        data, tttrlib.KDTree.default_leaf_size(n_features)
                    )
                    core = tree.core_distances(5)
                    boruvka = tree.mutual_reachability_mst(core, 1.0)
                    prim = tree.mst_prim(core, 1.0)
                    # The weights alone agreeing is the weaker claim and would
                    # pass for any valid spanning tree; the edges must match.
                    np.testing.assert_allclose(
                        np.asarray(boruvka).reshape(-1, 3)[:, 2].sum(),
                        np.asarray(prim).reshape(-1, 3)[:, 2].sum(),
                        rtol=1e-12,
                    )
                    np.testing.assert_array_equal(
                        canonical(boruvka), canonical(prim),
                        err_msg=f"n={n_samples} d={n_features} seed={seed}",
                    )

    def test_weight_is_never_below_either_core_distance(self):
        """Every edge weight is a mutual reachability, so it obeys its definition."""
        data = blobs(400, 3)
        core = np.asarray(tttrlib.core_distances(data, 5))
        mst = np.asarray(tttrlib.mutual_reachability_mst(data, 5, 1.0))
        source = mst[:, 0].astype(int)
        target = mst[:, 1].astype(int)
        floor = np.maximum(core[source], core[target])
        self.assertTrue(np.all(mst[:, 2] >= floor - 1e-12))

    def test_alpha_scales_the_distance_not_the_core(self):
        """`alpha` divides the plain distance, so it can only lower a weight to its core floor."""
        data = blobs(400, 3)
        core = np.asarray(tttrlib.core_distances(data, 5))
        plain = np.asarray(tttrlib.mutual_reachability_mst(data, 5, 1.0))
        damped = np.asarray(tttrlib.mutual_reachability_mst(data, 5, 2.0))
        self.assertLessEqual(damped[:, 2].sum(), plain[:, 2].sum())
        source = damped[:, 0].astype(int)
        target = damped[:, 1].astype(int)
        floor = np.maximum(core[source], core[target])
        self.assertTrue(np.all(damped[:, 2] >= floor - 1e-12))

    def test_refuses_a_non_positive_alpha(self):
        data = blobs(100, 2)
        with self.assertRaises(Exception):
            tttrlib.mutual_reachability_mst(data, 5, 0.0)


if __name__ == "__main__":
    unittest.main()

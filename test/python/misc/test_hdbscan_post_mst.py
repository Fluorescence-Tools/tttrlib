"""HDBSCAN downstream of the MST: condensation and labelling (`Cluster.h`).

`core_distances` and `mutual_reachability_mst` are the first half. These are the
rest — union-find single linkage, dendrogram condensation, and reading a root
off per point — which is 43% of a compiled run and none of which an array
language expresses: union-find, a breadth-first tree walk and a dynamic
compaction are pointer-chasing.

**Two calls, not one, and the boundary is deliberate.** Cluster *selection*
sits between condensation and labelling and is policy — excess-of-mass versus
leaf selection, `allow_single_cluster`, `cluster_selection_epsilon`, the map
from node id to output label. Compiling that in would put user-facing options
in C++, so the split is exactly there.

Nothing here imports the implementation being replaced. A test that reaches into
a sibling project becomes a skip the day that project moves, and a skip reads
like a pass — so the checks are either structural invariants of the condensed
tree or a recovery test against data whose answer is known in advance.
"""

import unittest

import numpy as np

import tttrlib


def three_blobs(seed=4):
    """Well-separated blobs, with the true membership known before clustering."""
    rng = np.random.default_rng(seed)
    x = np.vstack([rng.normal(0, 1, (120, 2)),
                   rng.normal(8, 1, (120, 2)),
                   rng.normal([0, 8], 1, (60, 2))])
    truth = np.repeat([0, 1, 2], [120, 120, 60])
    return np.ascontiguousarray(x), truth


def sorted_mst(x, k=5):
    """The MST edge list, ascending in weight — what the linkage requires."""
    mst = np.asarray(tttrlib.mutual_reachability_mst(x, k, 1.0))
    order = np.argsort(mst[:, 2], kind="stable")
    return (np.ascontiguousarray(mst[order, 0].astype(np.int64)),
            np.ascontiguousarray(mst[order, 1].astype(np.int64)),
            np.ascontiguousarray(mst[order, 2].astype(np.float64)))


def leaf_clusters(parent, child, n_points):
    """Clusters with no *cluster* child.

    Note what this is not: "a cluster that never appears as a parent". Every
    cluster is the parent of the individual points it sheds, so that definition
    finds nothing — it returned an empty set on the first attempt here and made
    a labelling test pass vacuously with everything collapsed to the root.
    """
    has_cluster_child = np.unique(parent[child >= n_points])
    clusters = np.unique(np.concatenate([[n_points], child[child >= n_points]]))
    return np.setdiff1d(clusters, has_cluster_child)


class TestTheCondensedTree(unittest.TestCase):

    def setUp(self):
        self.x, self.truth = three_blobs()
        self.src, self.tgt, self.w = sorted_mst(self.x)
        self.parent, self.child, self.value, self.size = \
            tttrlib.hdbscan_condensed_tree(self.src, self.tgt, self.w, 5)
        self.n_points = len(self.x)

    def test_every_point_falls_out_of_exactly_one_cluster(self):
        """The structural invariant that makes the tree a partition."""
        points = self.child[self.child < self.n_points]
        self.assertEqual(points.size, self.n_points)
        np.testing.assert_array_equal(np.sort(points), np.arange(self.n_points))

    def test_a_parent_is_always_a_cluster_never_a_point(self):
        self.assertTrue((self.parent >= self.n_points).all())

    def test_the_root_is_the_point_count(self):
        """Node ids are renumbered so `n_samples` is the root — callers rely on
        it to tell an unclaimed point from a clustered one."""
        self.assertEqual(int(self.parent.min()), self.n_points)

    def test_a_cluster_child_never_falls_below_min_cluster_size(self):
        """The whole point of condensing: a split both sides survive."""
        sizes = self.size[self.child >= self.n_points]
        self.assertTrue((sizes >= 5).all(), "a split kept a side below the minimum")

    def test_lambda_is_one_over_the_merge_distance(self):
        finite = np.isfinite(self.value)
        self.assertTrue((self.value[finite] > 0).all())

    def test_unsorted_edges_are_rejected(self):
        """Linkage is order-dependent: unsorted input gives a plausible, wrong
        dendrogram rather than an error, so the error is made here."""
        shuffled = self.w.copy()
        shuffled[0], shuffled[-1] = shuffled[-1], shuffled[0]
        with self.assertRaises(ValueError):
            tttrlib.hdbscan_condensed_tree(self.src, self.tgt, shuffled, 5)

    def test_it_is_deterministic(self):
        again = tttrlib.hdbscan_condensed_tree(self.src, self.tgt, self.w, 5)
        for a, b in zip((self.parent, self.child, self.value, self.size), again):
            np.testing.assert_array_equal(a, b)


class TestLabellingRecoversTheBlobs(unittest.TestCase):
    """The method test: does clustering return the structure that was put in."""

    def setUp(self):
        self.x, self.truth = three_blobs()
        src, tgt, w = sorted_mst(self.x)
        self.parent, self.child, _, _ = tttrlib.hdbscan_condensed_tree(src, tgt, w, 5)
        self.n_points = len(self.x)
        self.selected = np.zeros(int(self.parent.max()) + 1, dtype=np.uint8)
        self.selected[leaf_clusters(self.parent, self.child, self.n_points)] = 1

    def roots(self):
        return np.asarray(tttrlib.hdbscan_label_points(
            self.parent, self.child, self.selected, self.n_points))

    def test_no_cluster_mixes_two_blobs(self):
        """Purity, not count: leaf selection deliberately over-fragments, so
        "three clusters" is not the claim — "no cluster spans two blobs" is."""
        roots = self.roots()
        for root in np.unique(roots):
            if root == self.n_points:
                continue                      # unclaimed; the caller calls it noise
            members = self.truth[roots == root]
            self.assertEqual(np.unique(members).size, 1,
                             "cluster %d mixes blobs %s" % (root, np.unique(members)))

    def test_most_points_are_claimed(self):
        roots = self.roots()
        claimed = (roots != self.n_points).mean()
        self.assertGreater(claimed, 0.1, "almost nothing was clustered")

    def test_selecting_nothing_leaves_every_point_at_the_root(self):
        self.selected[:] = 0
        np.testing.assert_array_equal(
            self.roots(), np.full(self.n_points, self.n_points, dtype=np.int64))

    def test_a_selected_cluster_is_its_own_root(self):
        """Nothing above a selected cluster may absorb it — that is what lets a
        label be read off at all."""
        roots = self.roots()
        for root in np.unique(roots):
            if root != self.n_points:
                self.assertTrue(bool(self.selected[root]),
                                "point landed on an unselected cluster %d" % root)

    def test_a_child_id_outside_the_selection_is_rejected(self):
        with self.assertRaises(ValueError):
            tttrlib.hdbscan_label_points(self.parent, self.child,
                                         np.zeros(3, dtype=np.uint8), self.n_points)


if __name__ == "__main__":
    unittest.main()

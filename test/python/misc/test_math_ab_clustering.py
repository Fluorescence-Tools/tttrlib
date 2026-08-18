"""A/B of the clustering kernels in `modules/math` against independent references.

The kernels under test are `core_distances`, `mutual_reachability_mst`,
`hdbscan_condensed_tree` + `hdbscan_label_points` (`Cluster.h`) and `kmeans`
(`KMeans.h`). The sibling files pin them with known-answer simulations and
recorded fixtures; this file pins them against *other people's* implementations
of the same mathematics, so a shared mistake between the port and its origin
cannot pass:

* scikit-learn's `NearestNeighbors` for the k-th neighbour distance,
* SciPy's `minimum_spanning_tree` for the MST weight multiset (an MST is not
  unique under ties, its weight multiset is),
* scikit-learn's `HDBSCAN` for the condense-and-label half -- fed the *same*
  single-linkage tree, both sides must return the same partition, and they do;
  the full pipelines are compared too, and there the only admissible difference
  is which of several equal-weight MSTs each side picked (sklearn sorts its MST
  with an unstable `argsort`, so its tie order is not reproducible),
* scikit-learn's `KMeans(algorithm="lloyd")` from the same initial centres, and
  as a fixed-point check from our final centres,
* ChiSurf's pure-Python `_hdbscan.py` / `_kmeans.py` (the implementations the
  kernels were ported from) bit for bit, where ChiSurf is importable. Those
  tests skip loudly (their names say `chisurf`) when it is not.

Every reference is optional at import time; a missing one skips the tests that
need it and nothing else.
"""

import os
import sys
import unittest
import warnings

import numpy as np

import tttrlib

try:
    import sklearn  # noqa: F401
    from sklearn.cluster import HDBSCAN as SkHDBSCAN
    from sklearn.cluster import KMeans as SkKMeans
    from sklearn.datasets import make_blobs, make_moons
    from sklearn.metrics import adjusted_rand_score
    from sklearn.neighbors import NearestNeighbors
    HAVE_SKLEARN = True
except Exception:  # pragma: no cover - environment dependent
    HAVE_SKLEARN = False

try:
    from scipy.sparse.csgraph import minimum_spanning_tree
    from scipy.spatial.distance import cdist
    HAVE_SCIPY = True
except Exception:  # pragma: no cover
    HAVE_SCIPY = False


def _import_chisurf_cluster():
    """ChiSurf's clustering package, from the installed package or the sibling
    checkout. Returns None when neither is there."""
    here = os.path.dirname(os.path.abspath(__file__))
    sibling = os.path.abspath(os.path.join(here, "..", "..", "..", "..", "chisurf"))
    for attempt in (None, sibling):
        try:
            if attempt is not None and attempt not in sys.path:
                sys.path.insert(0, attempt)
            # import_module, not `import ... as`: the package re-exports a
            # *function* named `_kmeans`, which shadows the submodule attribute.
            import importlib
            h = importlib.import_module("chisurf.core.ml.cluster._hdbscan")
            k = importlib.import_module("chisurf.core.ml.cluster._kmeans")
            return h, k
        except Exception:
            continue
    return None


_CHISURF = _import_chisurf_cluster()
HAVE_CHISURF = _CHISURF is not None


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------

def sorted_mst(x, k, alpha=1.0):
    """tttrlib's MST as (low, high, weight), in the total order the linkage wants."""
    mst = np.asarray(tttrlib.mutual_reachability_mst(x, k, alpha))
    lo = np.minimum(mst[:, 0], mst[:, 1])
    hi = np.maximum(mst[:, 0], mst[:, 1])
    order = np.lexsort((hi, lo, mst[:, 2]))
    return (np.ascontiguousarray(lo[order].astype(np.int64)),
            np.ascontiguousarray(hi[order].astype(np.int64)),
            np.ascontiguousarray(mst[order, 2].astype(np.float64)))


def eom_select(parent, child, value, size, n_points):
    """Excess-of-mass cluster selection with allow_single_cluster=False.

    Cluster selection is deliberately *not* in the kernel (it is policy), so the
    reference policy is written out here: stability = sum over a cluster's rows
    of (lambda - lambda_birth) * size; bottom-up, a cluster is selected when it
    is at least as stable as its selected descendants, and the root never is.
    """
    nodes = np.unique(np.concatenate([[n_points], child[child >= n_points]]))
    birth = {int(n_points): 0.0}
    for c, v in zip(child, value):
        if c >= n_points:
            birth[int(c)] = float(v)
    stability = {int(c): 0.0 for c in nodes}
    for p, v, s in zip(parent, value, size):
        stability[int(p)] += (float(v) - birth[int(p)]) * float(s)
    children = {int(c): [] for c in nodes}
    for p, c in zip(parent, child):
        if c >= n_points:
            children[int(p)].append(int(c))
    selected = {}
    for c in sorted(int(c) for c in nodes)[::-1]:
        if c == n_points:
            selected[c] = False
            continue
        if not children[c]:
            selected[c] = True
            continue
        sub = sum(stability[k] for k in children[c])
        if sub > stability[c]:
            stability[c] = sub
            selected[c] = False
        else:
            selected[c] = True
            stack = list(children[c])
            while stack:
                k = stack.pop()
                selected[k] = False
                stack.extend(children[k])
    out = np.zeros(int(parent.max()) + 1, dtype=np.uint8)
    for k, v in selected.items():
        if v:
            out[k] = 1
    return out


def labels_from_edges(src, tgt, w, n_points, min_cluster_size):
    """tttrlib condense -> EOM selection -> tttrlib label read-off -> flat labels."""
    parent, child, value, size = tttrlib.hdbscan_condensed_tree(
        src, tgt, w, min_cluster_size)
    selected = eom_select(parent, child, value, size, n_points)
    roots = np.asarray(tttrlib.hdbscan_label_points(parent, child, selected, n_points))
    labels = np.full(n_points, -1, dtype=np.int64)
    for i, r in enumerate(np.unique(roots[roots != n_points])):
        labels[roots == r] = i
    return labels


def tttrlib_hdbscan(x, min_cluster_size, min_samples):
    src, tgt, w = sorted_mst(x, min_samples)
    return labels_from_edges(src, tgt, w, len(x), min_cluster_size)


def same_partition(a, b):
    """Identical clustering up to label permutation, noise matched exactly."""
    return (adjusted_rand_score(a, b) == 1.0
            and np.array_equal(np.asarray(a) == -1, np.asarray(b) == -1))


def ground_truth_sets():
    """Six data sets covering blobs, noise, a bridge, a single blob, moons."""
    rng = np.random.default_rng(0)
    return {
        "blobs3": make_blobs(300, centers=3, cluster_std=0.6, random_state=1)[0],
        "blobs+noise": np.vstack([
            make_blobs(300, centers=3, cluster_std=0.6, random_state=2)[0],
            rng.uniform(-12, 12, (60, 2))]),
        "moons": make_moons(300, noise=0.06, random_state=3)[0],
        "single": rng.normal(size=(200, 2)),
        "noise": rng.uniform(size=(200, 3)),
        "bridge": np.vstack([rng.normal(0, 1, (150, 2)),
                             rng.normal(8, 1, (150, 2)),
                             np.c_[np.linspace(0, 8, 20), np.linspace(0, 8, 20)]]),
    }


HDBSCAN_SETTINGS = [(5, 5), (10, 5), (15, 15)]     # (min_cluster_size, min_samples)


# ---------------------------------------------------------------------------
# core_distances
# ---------------------------------------------------------------------------

@unittest.skipUnless(HAVE_SKLEARN, "scikit-learn not installed")
class TestCoreDistancesAgainstSklearn(unittest.TestCase):
    """`k` counts the point itself on both sides (sklearn queries the training
    set, so column 0 is the self-match). Agreement to a few ulp: sklearn's tree
    accumulates in a different order and the last place can differ."""

    def test_kth_neighbour_distance(self):
        rng = np.random.default_rng(0)
        for n, d in [(200, 2), (1000, 3), (3000, 8)]:
            x = rng.normal(size=(n, d))
            x[:10] = x[10:20]                       # duplicates: exact zeros must survive
            for k in (1, 2, 5, 15):
                with self.subTest(n=n, d=d, k=k):
                    ours = np.asarray(tttrlib.core_distances(x, k))
                    dist, _ = NearestNeighbors(n_neighbors=k).fit(x).kneighbors(x)
                    np.testing.assert_allclose(ours, dist[:, k - 1], rtol=0, atol=1e-12)


# ---------------------------------------------------------------------------
# mutual_reachability_mst
# ---------------------------------------------------------------------------

@unittest.skipUnless(HAVE_SCIPY, "scipy not installed")
class TestMstAgainstScipy(unittest.TestCase):
    """SciPy's Kruskal on the dense mutual-reachability matrix. The tree is not
    unique under ties, but every MST of a graph has the same weight multiset, so
    that is what is compared -- edge for edge is the ChiSurf test below."""

    def test_weight_multiset_and_total(self):
        rng = np.random.default_rng(1)
        for n, d in [(200, 2), (600, 3), (1500, 5)]:
            x = rng.normal(size=(n, d))             # continuous: no zero-weight edges
            for k in (2, 5):
                with self.subTest(n=n, d=d, k=k):
                    core = np.asarray(tttrlib.core_distances(x, k))
                    mst = np.asarray(tttrlib.mutual_reachability_mst(x, k, 1.0))
                    dist = cdist(x, x)
                    reach = np.maximum(dist, np.maximum(core[:, None], core[None, :]))
                    np.fill_diagonal(reach, 0.0)    # scipy: 0 means "no edge"
                    ref = np.sort(minimum_spanning_tree(reach).data)
                    self.assertEqual(mst.shape, (n - 1, 3))
                    np.testing.assert_allclose(np.sort(mst[:, 2]), ref, rtol=1e-12, atol=0)
                    self.assertAlmostEqual(mst[:, 2].sum() / ref.sum(), 1.0, places=9)


@unittest.skipUnless(HAVE_CHISURF, "chisurf not importable")
class TestMstAgainstChisurfPrim(unittest.TestCase):
    """ChiSurf's `_prim_mst` under the shared total edge order: the same tree,
    edge for edge, not merely the same weight."""

    def test_edge_for_edge(self):
        H, _ = _CHISURF
        rng = np.random.default_rng(1)
        for n, d in [(150, 2), (300, 4)]:
            x = rng.normal(size=(n, d))
            x[:5] = x[5:10]
            for k in (2, 5):
                with self.subTest(n=n, d=d, k=k):
                    core = np.asarray(tttrlib.core_distances(x, k))
                    ours = np.column_stack(sorted_mst(x, k))
                    s, t, w = H._prim_mst(x, core, 1.0)
                    lo, hi = np.minimum(s, t), np.maximum(s, t)
                    order = np.lexsort((hi, lo, w))
                    theirs = np.column_stack([lo[order], hi[order], w[order]])
                    np.testing.assert_array_equal(ours, theirs)


# ---------------------------------------------------------------------------
# hdbscan_condensed_tree + hdbscan_label_points
# ---------------------------------------------------------------------------

@unittest.skipUnless(HAVE_SKLEARN, "scikit-learn not installed")
class TestHdbscanAgainstSklearn(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.sets = {k: np.ascontiguousarray(v, dtype=np.float64)
                    for k, v in ground_truth_sets().items()}

    @staticmethod
    def edges_from_single_linkage(slt, n_points):
        """An MST edge list equivalent to a single-linkage tree, in its merge
        order: each merge becomes an edge between a representative point of
        either side. Union-find linkage over it rebuilds the same dendrogram."""
        rep = list(range(n_points))
        src, tgt, w = [], [], []
        for row in slt:
            a, b = int(row["left_node"]), int(row["right_node"])
            src.append(rep[a])
            tgt.append(rep[b])
            w.append(float(row["value"]))
            rep.append(min(rep[a], rep[b]))
        return (np.array(src, dtype=np.int64), np.array(tgt, dtype=np.int64),
                np.array(w, dtype=np.float64))

    def test_condense_and_label_reproduce_sklearn_from_its_own_tree(self):
        """The decisive comparison: hand tttrlib's condense + EOM + label the
        exact single-linkage tree sklearn built, and require sklearn's labels.
        This isolates the kernels under test from the MST tie order."""
        for name, x in self.sets.items():
            for mcs, ms in HDBSCAN_SETTINGS:
                with self.subTest(data=name, min_cluster_size=mcs, min_samples=ms):
                    est = SkHDBSCAN(min_cluster_size=mcs, min_samples=ms).fit(x)
                    slt = getattr(est, "_single_linkage_tree_", None)
                    if slt is None:
                        self.skipTest("sklearn no longer exposes _single_linkage_tree_")
                    src, tgt, w = self.edges_from_single_linkage(slt, len(x))
                    ours = labels_from_edges(src, tgt, w, len(x), mcs)
                    self.assertTrue(same_partition(ours, est.labels_),
                                    "ARI %.4f" % adjusted_rand_score(ours, est.labels_))

    def test_sklearn_downstream_reproduces_ours_from_our_tree(self):
        """The mirror image: sklearn's condense/select/label on tttrlib's MST
        must return tttrlib's partition."""
        try:
            import sklearn.cluster._hdbscan.hdbscan as skh
            make_single_linkage = skh.make_single_linkage
            tree_to_labels = skh.tree_to_labels
            edge_dtype = skh.MST_edge_dtype
        except Exception:
            self.skipTest("sklearn private HDBSCAN helpers not available")
        for name, x in self.sets.items():
            for mcs, ms in HDBSCAN_SETTINGS:
                with self.subTest(data=name, min_cluster_size=mcs, min_samples=ms):
                    src, tgt, w = sorted_mst(x, ms)
                    edges = np.empty(len(w), dtype=edge_dtype)
                    edges["current_node"] = src
                    edges["next_node"] = tgt
                    edges["distance"] = w
                    theirs = tree_to_labels(make_single_linkage(edges), mcs,
                                            "eom", False, 0.0, None)[0]
                    ours = labels_from_edges(src, tgt, w, len(x), mcs)
                    self.assertTrue(same_partition(ours, theirs))

    def test_full_pipeline_agrees_up_to_mst_ties(self):
        """End to end against `sklearn.cluster.HDBSCAN`. Under a total edge
        order tttrlib's MST is one of possibly many; sklearn's is another and
        its tie order is unstable-sorted, so exact agreement is not owed. What
        is owed: the same number of clusters within one, ARI >= 0.85 everywhere,
        exact agreement on a clear majority."""
        exact, total = 0, 0
        for name, x in self.sets.items():
            for mcs, ms in HDBSCAN_SETTINGS:
                with self.subTest(data=name, min_cluster_size=mcs, min_samples=ms):
                    ours = tttrlib_hdbscan(x, mcs, ms)
                    theirs = SkHDBSCAN(min_cluster_size=mcs, min_samples=ms).fit_predict(x)
                    ari = adjusted_rand_score(ours, theirs)
                    self.assertGreaterEqual(ari, 0.85, "ARI %.4f" % ari)
                    self.assertLessEqual(abs(int(ours.max()) - int(theirs.max())), 1)
                    total += 1
                    exact += same_partition(ours, theirs)
        self.assertGreaterEqual(exact, total // 3,
                                "only %d of %d partitions exact" % (exact, total))


@unittest.skipUnless(HAVE_CHISURF and HAVE_SKLEARN, "chisurf and scikit-learn needed")
class TestHdbscanAgainstChisurf(unittest.TestCase):
    """The origin of the port. The MST is shared (ChiSurf calls tttrlib for it
    when importable) so this pins the condensation bit for bit and the whole
    estimator's labels exactly, on the same six sets."""

    @classmethod
    def setUpClass(cls):
        cls.H, _ = _CHISURF
        cls.sets = {k: np.ascontiguousarray(v, dtype=np.float64)
                    for k, v in ground_truth_sets().items()}

    def test_condensed_tree_bit_identical(self):
        H = self.H
        rng = np.random.default_rng(1)
        for n, d in [(150, 2), (300, 4)]:
            x = rng.normal(size=(n, d))
            x[:5] = x[5:10]
            for k in (2, 5):
                mst = np.asarray(tttrlib.mutual_reachability_mst(x, k, 1.0))
                hierarchy = H.single_linkage_tree(mst)
                src, tgt, w = sorted_mst(x, k)
                for mcs in (3, 5, 10):
                    with self.subTest(n=n, d=d, k=k, min_cluster_size=mcs):
                        ref = H.condense_tree(hierarchy, mcs)
                        p, c, v, s = tttrlib.hdbscan_condensed_tree(src, tgt, w, mcs)
                        np.testing.assert_array_equal(p, ref["parent"])
                        np.testing.assert_array_equal(c, ref["child"])
                        np.testing.assert_array_equal(v, ref["value"])
                        np.testing.assert_array_equal(s, ref["cluster_size"])

    def test_estimator_labels_identical(self):
        H = self.H
        for name, x in self.sets.items():
            for mcs, ms in HDBSCAN_SETTINGS:
                with self.subTest(data=name, min_cluster_size=mcs, min_samples=ms):
                    ours = tttrlib_hdbscan(x, mcs, ms)
                    theirs = H.HDBSCAN(min_cluster_size=mcs, min_samples=ms).fit_predict(x)
                    self.assertTrue(same_partition(ours, theirs))


# ---------------------------------------------------------------------------
# kmeans
# ---------------------------------------------------------------------------

def uniforms_for(n_clusters, n_init, seed):
    """The caller-side stream: n_init * n_clusters * (2 + floor(ln k))."""
    n_trials = 2 + int(np.log(n_clusters))
    return np.random.default_rng(seed).random(n_init * n_clusters * n_trials)


def kmeans_sets():
    """Separated and overlapping mixtures; the overlapping ones take Lloyd
    tens of sweeps, which is where two implementations can drift apart."""
    rng = np.random.default_rng(3)
    out = []
    for n, d, k, spread in [(300, 2, 3, 10.0), (600, 4, 5, 10.0), (1000, 3, 8, 10.0),
                            (400, 6, 2, 10.0), (800, 2, 4, 2.0), (1200, 3, 6, 1.5)]:
        x = np.vstack([rng.normal(rng.uniform(-spread, spread, d), 1.0, (n // k, d))
                       for _ in range(k)])
        out.append((np.ascontiguousarray(x), k))
    return out


@unittest.skipUnless(HAVE_SKLEARN, "scikit-learn not installed")
class TestKmeansUniformHelpers(unittest.TestCase):
    """`kmeans_n_uniforms` / `kmeans_uniforms` size the caller-owned stream the
    way the kernel consumes it, so a fit from `kmeans_uniforms(k, n_init, seed)`
    reproduces exactly and a wrong length is still rejected."""

    def test_helpers_size_the_stream_and_reproduce_a_fit(self):
        for k, n_init in ((2, 1), (7, 3), (12, 2)):
            self.assertEqual(tttrlib.kmeans_n_uniforms(k, n_init), n_init * k * (2 + int(np.log(k))))
            self.assertEqual(tttrlib.kmeans_uniforms(k, n_init, seed=5).size, tttrlib.kmeans_n_uniforms(k, n_init))
        x = np.random.default_rng(0).normal(size=(400, 3))
        a = tttrlib.kmeans(x, 4, tttrlib.kmeans_uniforms(4, 2, seed=11), 2, 100, 1e-9)
        b = tttrlib.kmeans(x, 4, tttrlib.kmeans_uniforms(4, 2, seed=11), 2, 100, 1e-9)
        np.testing.assert_array_equal(np.asarray(a[0]), np.asarray(b[0]))
        with self.assertRaises(ValueError):
            tttrlib.kmeans(x, 4, tttrlib.kmeans_uniforms(4, 1, seed=11), 2, 100, 1e-9)


@unittest.skipUnless(HAVE_SKLEARN, "scikit-learn not installed")
class TestKmeansAgainstSklearn(unittest.TestCase):
    """`KMeans(algorithm="lloyd", n_init=1, tol=0)` from a given `init` runs the
    same Lloyd recursion until the labels stop changing; tttrlib with a tiny
    `tol` does the same. Same start, same fixed point.

    Tolerance semantics differ and are not compared: tttrlib (like ChiSurf)
    stops when the Frobenius shift of the centres is <= tol, sklearn when the
    *squared* shift is <= tol * mean per-feature variance of X.
    """

    def test_our_final_centres_are_a_fixed_point_of_sklearn(self):
        """Independent of any seeding: hand sklearn our answer as `init`; it
        must not move, and must report the same inertia."""
        for x, k in kmeans_sets():
            with self.subTest(n=len(x), k=k):
                c, l, s = tttrlib.kmeans(x, k, uniforms_for(k, 3, seed=len(x)), 3, 300, 1e-12)
                with warnings.catch_warnings():
                    warnings.simplefilter("ignore")
                    sk = SkKMeans(n_clusters=k, init=np.asarray(c), n_init=1,
                                  algorithm="lloyd", tol=0.0, max_iter=300).fit(x)
                np.testing.assert_allclose(sk.cluster_centers_, c, rtol=0, atol=1e-9)
                np.testing.assert_array_equal(sk.labels_, np.asarray(l))
                self.assertAlmostEqual(sk.inertia_ / s[0], 1.0, places=12)

    @unittest.skipUnless(HAVE_CHISURF, "chisurf not importable (supplies the seed)")
    def test_same_seed_same_fixed_point(self):
        """From identical k-means++ centres (ChiSurf's seeding on our uniforms),
        sklearn's Lloyd and ours land on the same centres, labels and inertia."""
        _, K = _CHISURF
        for x, k in kmeans_sets():
            with self.subTest(n=len(x), k=k):
                u = uniforms_for(k, 1, seed=len(x))
                seed = K._kmeanspp_seed(x, k, u)
                c, l, s = tttrlib.kmeans(x, k, u, 1, 300, 1e-12)
                with warnings.catch_warnings():
                    warnings.simplefilter("ignore")
                    sk = SkKMeans(n_clusters=k, init=seed, n_init=1,
                                  algorithm="lloyd", tol=0.0, max_iter=300).fit(x)
                np.testing.assert_allclose(sk.cluster_centers_, c, rtol=0, atol=1e-9)
                np.testing.assert_array_equal(sk.labels_, np.asarray(l))
                self.assertAlmostEqual(sk.inertia_ / s[0], 1.0, places=12)


@unittest.skipUnless(HAVE_CHISURF, "chisurf not importable")
class TestKmeansAgainstChisurf(unittest.TestCase):
    """Live bit-for-bit against `_kmeans.py` (the committed fixture in
    test_kmeans.py is the offline pin of the same contract): centres, labels,
    inertia and sweep count, with restarts ranked on the inertia."""

    def test_bit_identical(self):
        _, K = _CHISURF
        for x, k in kmeans_sets():
            for n_init in (1, 3):
                with self.subTest(n=len(x), k=k, n_init=n_init):
                    seed = 1000 * len(x) + n_init
                    u = uniforms_for(k, n_init, seed=seed)
                    c, l, s = tttrlib.kmeans(x, k, u, n_init, 300, 1e-4)
                    rc, rl, ri, rn = K._kmeans(x, k, np.random.default_rng(seed),
                                               n_init=n_init, max_iter=300, tol=1e-4)
                    np.testing.assert_array_equal(np.asarray(c), rc)
                    np.testing.assert_array_equal(np.asarray(l, dtype=np.int64), rl)
                    self.assertEqual(s[0], ri)
                    self.assertEqual(int(s[1]), rn)


if __name__ == "__main__":
    unittest.main()

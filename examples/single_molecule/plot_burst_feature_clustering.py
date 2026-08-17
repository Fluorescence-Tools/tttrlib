"""Clustering burst features: k-means and HDBSCAN
==============================================

A burst table is a point cloud — one row per burst, one column per feature
(FRET efficiency :math:`E`, stoichiometry :math:`S`, donor lifetime, size, …).
Two questions recur: *how many populations are there* and *which burst belongs
to which*. tttrlib ships the two standard answers as compiled kernels, ported
bit-for-bit from ChiSurf and validated against scikit-learn:

**k-means** (`tttrlib.kmeans`)
    Lloyd's algorithm with greedy k-means++ seeding (2 + ⌊ln k⌋ candidate
    draws per centre, Arthur & Vassilvitskii 2007). The seeding consumes
    *caller-supplied* uniforms, so a fit is reproducible from a stream you own —
    the kernel draws no randomness. From the same seed it lands on the same
    fixed point as scikit-learn's ``KMeans(algorithm="lloyd")`` to 1e-14.

**HDBSCAN** (Campello, Moulavi & Sander 2013)
    Density-based, no ``k``: ``core_distances`` → ``mutual_reachability_mst`` →
    ``hdbscan_condensed_tree`` → *cluster selection* → ``hdbscan_label_points``.
    The selection step (excess of mass) is deliberately **not** in the kernel:
    it is policy — which clusters to keep — and different tools make different
    choices, so it lives in the caller. Fed either side's tree, tttrlib and
    scikit-learn's ``HDBSCAN`` produce identical partitions.

This example simulates two FRET populations plus noise bursts and runs both.
"""
import numpy as np
import matplotlib.pyplot as plt

import tttrlib

rng = np.random.default_rng(7)

# %%
# Simulated burst table
# ---------------------
# Two doubly-labelled populations (low-E and high-E), a donor-only tail, and a
# few noise bursts. Features: E, S, donor lifetime (ns), log burst size.
def population(n, e, s, tau, size):
    return np.column_stack([
        np.clip(rng.normal(e, 0.05, n), 0, 1),
        np.clip(rng.normal(s, 0.04, n), 0, 1),
        rng.normal(tau, 0.25, n),
        np.log10(rng.lognormal(np.log(size), 0.3, n)),
    ])

X = np.vstack([
    population(500, 0.25, 0.55, 3.2, 120),   # low FRET
    population(400, 0.75, 0.55, 1.4, 110),   # high FRET
    population(250, 0.05, 0.92, 3.9, 90),    # donor-only
    np.column_stack([rng.uniform(0, 1, 60), rng.uniform(0, 1, 60),
                     rng.uniform(0.5, 4.5, 60), rng.uniform(1.5, 2.6, 60)]),  # noise
])
truth = np.repeat([0, 1, 2, -1], [500, 400, 250, 60])
X = np.ascontiguousarray(X, dtype=np.float64)

# standardise so no feature dominates the Euclidean distance
Xs = (X - X.mean(0)) / X.std(0)

# %%
# k-means with a caller-owned random stream
# ------------------------------------------
# ``n_init`` restarts × ``n_clusters`` centres × ``(2 + int(ln k))`` trials
# uniforms; the best restart (lowest inertia of the *returned* centres) wins.
k, n_init = 3, 5
# (`kmeans_n_uniforms(k, n_init)` is that count; `kmeans_uniforms(k, n_init,
# seed)` draws a stream of the right length from numpy's default_rng -- the
# seed is still yours.)
uniforms = tttrlib.kmeans_uniforms(k, n_init, seed=2026)
centres, labels_km, stats = tttrlib.kmeans(Xs, k, uniforms, n_init, 300, 1e-12)
centres = np.asarray(centres).reshape(k, -1)
labels_km = np.asarray(labels_km)
print(f"k-means: inertia {stats[0]:.1f}, {int(stats[1])} Lloyd sweeps in the winning restart")

# %%
# HDBSCAN: the kernels plus the excess-of-mass selection policy
# --------------------------------------------------------------
def eom_select(parent, child, value, size, n_points):
    """Excess-of-mass selection, allow_single_cluster=False (the sklearn/hdbscan
    policy). Stability = sum over a cluster's rows of (lambda - lambda_birth) *
    size; bottom-up, keep a cluster when it beats its selected descendants."""
    parent = np.asarray(parent); child = np.asarray(child)
    value = np.asarray(value); size = np.asarray(size)
    nodes = np.unique(np.concatenate([[n_points], child[child >= n_points]]))
    birth = {int(n_points): 0.0}
    for c, v in zip(child, value):
        if c >= n_points:
            birth[int(c)] = float(v)
    stability = {int(c): 0.0 for c in nodes}
    for p, v, s_ in zip(parent, value, size):
        stability[int(p)] += (float(v) - birth[int(p)]) * float(s_)
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
        sub = sum(stability[q] for q in children[c])
        if sub > stability[c]:
            stability[c] = sub
            selected[c] = False
        else:
            selected[c] = True
            stack = list(children[c])
            while stack:
                q = stack.pop()
                selected[q] = False
                stack.extend(children[q])
    out = np.zeros(int(parent.max()) + 1, dtype=np.uint8)
    for q, v in selected.items():
        if v:
            out[q] = 1
    return out


def hdbscan_labels(x, min_cluster_size, min_samples):
    n = x.shape[0]
    mst = np.asarray(tttrlib.mutual_reachability_mst(x, min_samples, 1.0))
    # (low, high, weight) in the total order the single linkage wants
    lo, hi = np.minimum(mst[:, 0], mst[:, 1]), np.maximum(mst[:, 0], mst[:, 1])
    order = np.lexsort((hi, lo, mst[:, 2]))
    src = np.ascontiguousarray(lo[order].astype(np.int64))
    tgt = np.ascontiguousarray(hi[order].astype(np.int64))
    w = np.ascontiguousarray(mst[order, 2].astype(np.float64))
    parent, child, value, size = tttrlib.hdbscan_condensed_tree(src, tgt, w, min_cluster_size)
    selected = eom_select(parent, child, value, size, n)
    roots = np.asarray(tttrlib.hdbscan_label_points(parent, child, selected, n))
    labels = np.full(n, -1, dtype=np.int64)
    for i, r in enumerate(np.unique(roots[roots != n])):
        labels[roots == r] = i
    return labels


labels_hd = hdbscan_labels(Xs, min_cluster_size=25, min_samples=10)
print(f"HDBSCAN: {labels_hd.max() + 1} clusters, {np.sum(labels_hd < 0)} points labelled noise")

# %%
# The two partitions against the truth
# ------------------------------------
fig, axes = plt.subplots(1, 3, figsize=(13, 4), sharex=True, sharey=True)
for ax, lab, title in zip(axes, (truth, labels_km, labels_hd),
                          ("truth", "k-means (k = 3)", "HDBSCAN")):
    noise = lab < 0
    ax.scatter(X[~noise, 0], X[~noise, 1], c=lab[~noise], cmap="tab10", s=8, vmin=0, vmax=9)
    ax.scatter(X[noise, 0], X[noise, 1], c="0.6", s=6, marker="x", label="noise")
    ax.set_title(title)
    ax.set_xlabel("FRET efficiency E")
axes[0].set_ylabel("stoichiometry S")
axes[2].legend(loc="lower right")
fig.tight_layout()

# %%
# k-means must place every point (the noise gets absorbed by the nearest
# centre); HDBSCAN can say "no cluster", which is what a burst table with
# spurious events needs.
def purity(lab, ref):
    ok = 0
    for c in np.unique(lab[lab >= 0]):
        m = lab == c
        ok += np.bincount(ref[m] + 1).max()
    return ok / np.sum(lab >= 0)

print(f"purity of clustered points: k-means {purity(labels_km, truth):.3f}, "
      f"HDBSCAN {purity(labels_hd, truth):.3f}")
plt.show()

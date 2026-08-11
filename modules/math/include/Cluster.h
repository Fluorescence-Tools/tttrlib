// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_CLUSTER_H
#define TTTRLIB_CLUSTER_H

// Cluster.h -- k-d tree nearest neighbours, and the two kernels that HDBSCAN
// spends all of its time in.
//
// Why this lives in `math` and not in an analysis module: none of it knows what
// a photon is. A k-d tree over an (n x d) table of doubles is wanted in several
// places at once -- burst feature spaces, localisation tables, and the
// density-based clustering the analysis GUIs offer -- and writing it once here
// is what keeps a fourth copy from appearing.
//
// The two kernels are:
//
//   core_distances(X, k)        distance from every point to its k-th nearest
//                               neighbour, the local density estimate HDBSCAN
//                               is built on. O(n log n) through the tree.
//
//   mutual_reachability_mst()   minimum spanning tree of the graph whose edge
//                               weight is max(core_i, core_j, d(i,j)). Boruvka
//                               with tree pruning, O(n log n) in practice
//                               against the O(n^2) of the textbook Prim, which
//                               is kept beside it as the obviously-correct
//                               implementation the fast one is checked against.
//
// ---------------------------------------------------------------------------
// The tie-break is part of the contract
// ---------------------------------------------------------------------------
// Mutual-reachability weights tie constantly: whenever the max is a *core*
// distance, every edge that core distance dominates carries the same weight, so
// hundreds of edges can share one value. An MST is then not unique, and Boruvka
// and Prim pick different ones -- which changes the dendrogram, and with it the
// cluster count. Callers that can fall back to their own Prim (chisurf does)
// would silently get a different answer depending on whether this library was
// importable.
//
// So the edge order here is TOTAL: by weight, then by the sorted endpoint pair
// (min(u,v), max(u,v)). Under a total edge order the MST is unique and every
// correct algorithm returns the same one. `edge_less` below is that order and
// must stay identical to the caller's.

#include <cstddef>
#include <vector>

namespace tttrlib {

/// Total order on weighted edges: weight first, then the sorted endpoint pair.
/// Read the header comment before changing it -- it is a compatibility surface.
inline bool edge_less(double w1, int u1, int v1, double w2, int u2, int v2) {
    if (w1 != w2) return w1 < w2;
    const int a1 = u1 < v1 ? u1 : v1, b1 = u1 < v1 ? v1 : u1;
    const int a2 = u2 < v2 ? u2 : v2, b2 = u2 < v2 ? v2 : u2;
    if (a1 != a2) return a1 < a2;
    return b1 < b2;
}

/// A static k-d tree over a row-major (n_samples x n_features) table.
///
/// Built once and queried many times. The data is not copied; the caller must
/// keep it alive for the lifetime of the tree.
class KDTree {
public:
    /// Build the tree over a copy of `data`. `leaf_size` is the point count
    /// below which a node stops splitting; 32 keeps the traversal shallow
    /// without letting the linear scan inside a leaf dominate.
    ///
    /// The copy is deliberate: the tree outlives the call that built it, and
    /// the array it is built from is routinely a temporary owned by a language
    /// binding.
    KDTree(const double* data, int n_samples, int n_features, int leaf_size = 32);

    int n_samples() const { return n_samples_; }
    int n_features() const { return n_features_; }

    /// Squared distances and indices of the `k` nearest neighbours of `point`,
    /// including the point itself when it is part of the data. Both output
    /// buffers must hold `k` entries; results come back sorted ascending.
    void query(const double* point, int k, int* out_index, double* out_sq_dist) const;

    /// Distance to the `k`-th nearest neighbour of every point in the tree.
    /// `k` counts the point itself, so `k == 1` gives zeros.
    ///
    /// The `k` neighbours themselves are kept, because
    /// `mutual_reachability_mst` needs a cheap upper bound on each point's
    /// shortest outgoing edge and its own nearest neighbours are the best one
    /// available. Calling this before the MST is therefore not just the natural
    /// order, it is the fast path.
    std::vector<double> core_distances(int k) const;

    /// Minimum spanning tree of the mutual-reachability graph, as `3 * (n-1)`
    /// doubles laid out row-major as `[source, target, weight]`.
    ///
    /// `core` must hold one core distance per point (typically from
    /// `core_distances`); `alpha` divides the plain distance before the
    /// inflation, so values above one make the hierarchy more conservative.
    ///
    /// Borůvka over the tree, `O(n log n)` while the tree prunes. This is the
    /// kernel to use; `mst_prim` returns the same tree and exists to check it.
    std::vector<double> mutual_reachability_mst(const std::vector<double>& core,
                                                double alpha = 1.0) const;

    /// The same tree by Prim's algorithm: `O(n^2 d)`, no tree, no pruning,
    /// nothing clever. It is kept because it is *obviously* correct, which
    /// makes it the thing the Borůvka above is checked against — the two must
    /// return the same tree edge for edge, and that is what proves the total
    /// edge order is doing its job. It was also the faster of the two above
    /// about ten dimensions until the tie comparison moved into distance
    /// space; that is no longer true at any dimension measured, so nothing
    /// dispatches to it.
    std::vector<double> mst_prim(const std::vector<double>& core,
                                 double alpha = 1.0) const;

    /// Leaf size to build with when the caller has no opinion.
    ///
    /// Small leaves pay off only while the bounding boxes are tight enough to
    /// prune: in two or three dimensions a 16-point leaf beats a 128-point one
    /// by a third, and by sixteen dimensions the ranking has reversed, because
    /// every box is visited anyway and only the per-node overhead is left.
    static int default_leaf_size(int n_features) {
        if (n_features <= 4) return 16;
        if (n_features <= 10) return 32;
        return 128;
    }

private:
    struct Node {
        int start = 0;   ///< first index into index_ owned by this node
        int stop = 0;    ///< one past the last
        int left = -1;   ///< child node, or -1 for a leaf
        int right = -1;
    };

    /// Squared distance from `point` to the bounding box of node `node`.
    double node_lower_bound(int node, const double* point) const;
    /// Recursive k-NN descent; `heap` holds the k best so far as a max-heap.
    void query_node(int node, const double* point, int k, int* out_index,
                    double* out_sq_dist, int& filled) const;

    int build(int start, int stop, int depth);

    std::vector<double> owned_;   ///< the copy the tree is built over
    const double* data_ = nullptr;
    int n_samples_ = 0;
    int n_features_ = 0;
    int leaf_size_ = 32;
    std::vector<int> index_;      ///< permutation of 0..n-1, leaves are ranges
    std::vector<Node> nodes_;
    std::vector<double> bounds_;  ///< per node: n_features lows then highs

    // Neighbour cache filled by core_distances() and read by the MST.
    mutable std::vector<int> knn_index_;
    mutable std::vector<double> knn_sq_dist_;
    mutable int knn_k_ = 0;         ///< neighbours stored per point
    mutable int knn_core_rank_ = 0; ///< which of them is the core distance
};

// ---------------------------------------------------------------------------
// Flat entry points (these are what the language bindings expose)
// ---------------------------------------------------------------------------

/// Distance from every row of `input` to its `k`-th nearest neighbour.
void core_distances(double* input, int n_input1, int n_input2, int k,
                    double** output, int* n_output);

/// Minimum spanning tree of the mutual-reachability graph of `input`.
/// The result is `(n_samples - 1) x 3`, each row `[source, target, weight]`,
/// in the total order documented at the top of this file.
void mutual_reachability_mst(double* input, int n_input1, int n_input2,
                             int min_samples, double alpha, double** output,
                             int* n_output1, int* n_output2);

}  // namespace tttrlib

// ---------------------------------------------------------------------------
// The other half of HDBSCAN: everything downstream of the MST
// ---------------------------------------------------------------------------
// `core_distances` and `mutual_reachability_mst` above are the first half. The
// rest of a run -- union-find linkage, dendrogram condensation, and reading a
// label off per point -- is 43% of the compiled time (measured at n=100,000:
// MST 117.6 ms against linkage 25.4 and condense+label 65.1), and none of it is
// expressible in an array language: union-find, a breadth-first tree walk and a
// dynamic compaction are pointer-chasing.
//
// **Two calls, not one, and that is deliberate.** The obvious surface is a
// single `hdbscan_labels(mst, min_cluster_size)`. It does not fit: *cluster
// selection* sits between condensation and labelling, and it is policy --
// excess-of-mass or leaf selection, `allow_single_cluster`,
// `cluster_selection_epsilon`, and the map from node id to output label. Those
// are user-facing options that belong with the caller, not compiled in. So the
// split is at the policy boundary:
//
//   hdbscan_condensed_tree()  MST edges -> condensed (parent, child, lambda, size)
//   << caller selects clusters from the condensed tree's stabilities >>
//   hdbscan_label_points()    condensed tree + selection -> a root per point
//
// Both halves are one call each, so the loops stay whole in C++.

namespace tttrlib {

/*!
 * \brief Single-linkage dendrogram from an MST, condensed at min_cluster_size.
 *
 * \param sources,targets,weights  the MST edge list, **ascending in weight**.
 *        Rejected otherwise: the linkage is order-dependent and unsorted input
 *        produces a plausible, wrong dendrogram rather than an error.
 * \param min_cluster_size  a split counts only when *both* sides hold at least
 *        this many points; otherwise the small side is recorded as points
 *        falling out of the surviving cluster, at that merge's lambda.
 * \param out_parent,out_child,out_value,out_size  the condensed edge list,
 *        allocated here. Node ids are renumbered so `n_samples` is the root.
 */
void hdbscan_condensed_tree(
        long long* sources, int n_sources,
        long long* targets, int n_targets,
        double* weights, int n_weights,
        int min_cluster_size,
        long long** out_parent, int* n_out_parent,
        long long** out_child, int* n_out_child,
        double** out_value, int* n_out_value,
        long long** out_size, int* n_out_size);

/*!
 * \brief Collapse unselected clusters into their parents; return each point's root.
 *
 * \param is_selected  one byte per node id; the caller's selection.
 * \param n_points     the point count, which is also the root cluster's id.
 * \param out          [n_points] root node per point, allocated here. A point
 *        whose root is the root cluster was not claimed by any selected
 *        cluster -- the caller maps that to noise.
 */
void hdbscan_label_points(
        long long* parents, int n_parents,
        long long* children, int n_children,
        unsigned char* is_selected, int n_is_selected,
        int n_points,
        long long** out, int* n_out);

}  // namespace tttrlib

#endif  // TTTRLIB_CLUSTER_H

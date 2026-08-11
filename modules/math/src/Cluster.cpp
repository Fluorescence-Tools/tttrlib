// SPDX-License-Identifier: BSD-3-Clause
#include "Cluster.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <numeric>
#include <stdexcept>
#include <utility>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace tttrlib {

namespace {

/// Sift the last element of a k-element max-heap of (distance, index) up.
inline void heap_push(double* dist, int* idx, int size, double d, int i) {
    int pos = size;
    dist[pos] = d;
    idx[pos] = i;
    while (pos > 0) {
        const int parent = (pos - 1) / 2;
        if (dist[parent] >= dist[pos]) break;
        std::swap(dist[parent], dist[pos]);
        std::swap(idx[parent], idx[pos]);
        pos = parent;
    }
}

/// Replace the largest element of a full k-element max-heap and re-heapify.
inline void heap_replace_top(double* dist, int* idx, int size, double d, int i) {
    dist[0] = d;
    idx[0] = i;
    int pos = 0;
    for (;;) {
        const int l = 2 * pos + 1, r = l + 1;
        int largest = pos;
        if (l < size && dist[l] > dist[largest]) largest = l;
        if (r < size && dist[r] > dist[largest]) largest = r;
        if (largest == pos) break;
        std::swap(dist[largest], dist[pos]);
        std::swap(idx[largest], idx[pos]);
        pos = largest;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

KDTree::KDTree(const double* data, int n_samples, int n_features, int leaf_size)
    : n_samples_(n_samples),
      n_features_(n_features),
      leaf_size_(leaf_size < 1 ? 1 : leaf_size) {
    if (n_samples <= 0 || n_features <= 0)
        throw std::invalid_argument("KDTree: the data must not be empty");
    if (data == nullptr) throw std::invalid_argument("KDTree: the data is null");
    owned_.assign(data, data + static_cast<size_t>(n_samples) * n_features);
    data_ = owned_.data();
    index_.resize(static_cast<size_t>(n_samples));
    std::iota(index_.begin(), index_.end(), 0);
    // A balanced tree over n points has fewer than 2n nodes; reserving avoids
    // the reallocation that would invalidate the references build() holds.
    nodes_.reserve(static_cast<size_t>(2 * n_samples / leaf_size_ + 2));
    bounds_.reserve(static_cast<size_t>(2 * n_features * (2 * n_samples / leaf_size_ + 2)));
    build(0, n_samples, 0);
}

int KDTree::build(int start, int stop, int depth) {
    const int node_id = static_cast<int>(nodes_.size());
    nodes_.push_back(Node{});
    bounds_.resize(static_cast<size_t>(2 * n_features_) * (node_id + 1));

    double* lo = &bounds_[static_cast<size_t>(2 * n_features_) * node_id];
    double* hi = lo + n_features_;
    for (int f = 0; f < n_features_; ++f) {
        lo[f] = std::numeric_limits<double>::infinity();
        hi[f] = -std::numeric_limits<double>::infinity();
    }
    for (int i = start; i < stop; ++i) {
        const double* row = data_ + static_cast<size_t>(index_[i]) * n_features_;
        for (int f = 0; f < n_features_; ++f) {
            if (row[f] < lo[f]) lo[f] = row[f];
            if (row[f] > hi[f]) hi[f] = row[f];
        }
    }

    nodes_[node_id].start = start;
    nodes_[node_id].stop = stop;
    if (stop - start <= leaf_size_) return node_id;

    // Split the widest side, not a dimension chosen by depth: with a table
    // whose columns are physically different quantities (a count next to a
    // time) round-robin splitting produces slivers and the pruning stops
    // working.
    int split_dim = 0;
    double widest = -1.0;
    for (int f = 0; f < n_features_; ++f) {
        const double extent = hi[f] - lo[f];
        if (extent > widest) {
            widest = extent;
            split_dim = f;
        }
    }
    if (!(widest > 0.0)) return node_id;  // every point identical: stop here

    const int mid = start + (stop - start) / 2;
    const double* base = data_;
    const int nf = n_features_;
    std::nth_element(index_.begin() + start, index_.begin() + mid,
                     index_.begin() + stop, [base, nf, split_dim](int a, int b) {
                         return base[static_cast<size_t>(a) * nf + split_dim] <
                                base[static_cast<size_t>(b) * nf + split_dim];
                     });

    const int left = build(start, mid, depth + 1);
    const int right = build(mid, stop, depth + 1);
    nodes_[node_id].left = left;
    nodes_[node_id].right = right;
    return node_id;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

double KDTree::node_lower_bound(int node, const double* point) const {
    const double* lo = &bounds_[static_cast<size_t>(2 * n_features_) * node];
    const double* hi = lo + n_features_;
    double acc = 0.0;
    for (int f = 0; f < n_features_; ++f) {
        double gap = 0.0;
        if (point[f] < lo[f]) gap = lo[f] - point[f];
        else if (point[f] > hi[f]) gap = point[f] - hi[f];
        acc += gap * gap;
    }
    return acc;
}

void KDTree::query_node(int node, const double* point, int k, int* out_index,
                        double* out_sq_dist, int& filled) const {
    const Node& n = nodes_[node];
    if (n.left < 0) {
        for (int i = n.start; i < n.stop; ++i) {
            const int j = index_[i];
            const double* row = data_ + static_cast<size_t>(j) * n_features_;
            const double worst = filled == k ? out_sq_dist[0]
                                             : std::numeric_limits<double>::infinity();
            double acc = 0.0;
            for (int f = 0; f < n_features_; ++f) {
                const double diff = point[f] - row[f];
                acc += diff * diff;
                if (acc >= worst) break;
            }
            if (acc < worst) {
                if (filled < k) heap_push(out_sq_dist, out_index, filled++, acc, j);
                else heap_replace_top(out_sq_dist, out_index, k, acc, j);
            }
        }
        return;
    }

    // Descend into the nearer child first: the bound it establishes is what
    // prunes the farther one.
    const double dl = node_lower_bound(n.left, point);
    const double dr = node_lower_bound(n.right, point);
    const int first = dl <= dr ? n.left : n.right;
    const int second = dl <= dr ? n.right : n.left;
    const double second_bound = dl <= dr ? dr : dl;

    query_node(first, point, k, out_index, out_sq_dist, filled);
    if (filled < k || second_bound < out_sq_dist[0])
        query_node(second, point, k, out_index, out_sq_dist, filled);
}

void KDTree::query(const double* point, int k, int* out_index,
                   double* out_sq_dist) const {
    if (k < 1) throw std::invalid_argument("KDTree::query: k must be >= 1");
    if (k > n_samples_) k = n_samples_;
    int filled = 0;
    query_node(0, point, k, out_index, out_sq_dist, filled);
    // The heap is unordered; the caller wants ascending distances.
    std::vector<int> order(static_cast<size_t>(filled));
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(),
              [out_sq_dist](int a, int b) { return out_sq_dist[a] < out_sq_dist[b]; });
    std::vector<double> d(static_cast<size_t>(filled));
    std::vector<int> ix(static_cast<size_t>(filled));
    for (int i = 0; i < filled; ++i) {
        d[i] = out_sq_dist[order[i]];
        ix[i] = out_index[order[i]];
    }
    std::copy(d.begin(), d.end(), out_sq_dist);
    std::copy(ix.begin(), ix.end(), out_index);
}

std::vector<double> KDTree::core_distances(int k) const {
    if (k < 1) throw std::invalid_argument("core_distances: k must be >= 1");
    const int kk = k > n_samples_ ? n_samples_ : k;
    // One neighbour past the core distance is fetched, and it is not a
    // rounding-up: `mutual_reachability_mst` uses it to decide whether the
    // stored list contains *every* point within the core distance. Without that
    // guarantee its first-round shortcut could miss a tied neighbour sitting
    // exactly on the boundary, and pick a different (still minimal) edge.
    const int stored = kk < n_samples_ ? kk + 1 : kk;
    std::vector<double> out(static_cast<size_t>(n_samples_));
    knn_k_ = stored;
    knn_core_rank_ = kk;
    knn_index_.assign(static_cast<size_t>(n_samples_) * stored, -1);
    knn_sq_dist_.assign(static_cast<size_t>(n_samples_) * stored,
                        std::numeric_limits<double>::infinity());
    // Measured, not assumed: a flat brute-force scan was tried here for the
    // high-dimension case, on the theory that the tree stops pruning, and it
    // came out slower at sixteen dimensions as well. The tree still pays for
    // the neighbour search long after it has stopped paying for the spanning
    // tree, which is why only the MST switches to Prim above the crossover.
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int i = 0; i < n_samples_; ++i) {
        const size_t base = static_cast<size_t>(i) * stored;
        int* idx = knn_index_.data() + base;
        double* dist = knn_sq_dist_.data() + base;
        int filled = 0;
        query_node(0, data_ + static_cast<size_t>(i) * n_features_, stored, idx, dist,
                   filled);
        // Sorted ascending, because the caller reads the k-th and the (k+1)-th
        // by position; the heap only guarantees the largest is at the root.
        for (int a = 1; a < filled; ++a) {
            const double d = dist[a];
            const int j = idx[a];
            int b = a - 1;
            while (b >= 0 && dist[b] > d) {
                dist[b + 1] = dist[b];
                idx[b + 1] = idx[b];
                --b;
            }
            dist[b + 1] = d;
            idx[b + 1] = j;
        }
        out[static_cast<size_t>(i)] = std::sqrt(dist[kk - 1]);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Boruvka over the mutual-reachability graph
// ---------------------------------------------------------------------------

namespace {

struct UnionFind {
    std::vector<int> parent;
    std::vector<int> rank_;
    explicit UnionFind(int n) : parent(static_cast<size_t>(n)), rank_(static_cast<size_t>(n), 0) {
        std::iota(parent.begin(), parent.end(), 0);
    }
    int find(int x) {
        while (parent[static_cast<size_t>(x)] != x) {
            parent[static_cast<size_t>(x)] =
                parent[static_cast<size_t>(parent[static_cast<size_t>(x)])];
            x = parent[static_cast<size_t>(x)];
        }
        return x;
    }
    bool unite(int a, int b) {
        a = find(a);
        b = find(b);
        if (a == b) return false;
        if (rank_[static_cast<size_t>(a)] < rank_[static_cast<size_t>(b)]) std::swap(a, b);
        parent[static_cast<size_t>(b)] = a;
        if (rank_[static_cast<size_t>(a)] == rank_[static_cast<size_t>(b)])
            ++rank_[static_cast<size_t>(a)];
        return true;
    }
};

}  // namespace

std::vector<double> KDTree::mutual_reachability_mst(const std::vector<double>& core,
                                                    double alpha) const {
    if (static_cast<int>(core.size()) != n_samples_)
        throw std::invalid_argument(
            "mutual_reachability_mst: one core distance per sample is required");
    if (!(alpha > 0.0))
        throw std::invalid_argument("mutual_reachability_mst: alpha must be positive");

    const int n = n_samples_;
    std::vector<double> mst;
    mst.reserve(static_cast<size_t>(3 * (n > 0 ? n - 1 : 0)));
    if (n < 2) return mst;

    // Smallest core distance per subtree. A node whose points are all sparser
    // than the current best edge cannot hold a better one, whatever the
    // geometry says, so this prunes where the bounding box alone would not.
    std::vector<double> min_core(nodes_.size(), 0.0);
    for (int node = static_cast<int>(nodes_.size()) - 1; node >= 0; --node) {
        const Node& nd = nodes_[static_cast<size_t>(node)];
        if (nd.left < 0) {
            double m = std::numeric_limits<double>::infinity();
            for (int i = nd.start; i < nd.stop; ++i)
                m = std::min(m, core[static_cast<size_t>(index_[i])]);
            min_core[static_cast<size_t>(node)] = m;
        } else {
            min_core[static_cast<size_t>(node)] =
                std::min(min_core[static_cast<size_t>(nd.left)],
                         min_core[static_cast<size_t>(nd.right)]);
        }
    }

    UnionFind uf(n);
    std::vector<int> component(static_cast<size_t>(n));
    std::vector<int> node_component(nodes_.size(), -1);
    // Best outgoing edge found this round, per component root.
    std::vector<double> best_w(static_cast<size_t>(n));
    std::vector<int> best_u(static_cast<size_t>(n)), best_v(static_cast<size_t>(n));
    // Per-point result of this round's search.
    //
    // Deliberately *not* carried over between rounds. Reusing last round's
    // winner while it is still external is valid only while each point's stored
    // edge is that point's own minimum -- and the shared component bound below
    // breaks exactly that, since a point may stop early against another point's
    // edge. The two optimisations are mutually exclusive and the shared bound
    // is much the stronger of them; keeping both silently produced a different
    // (still valid) spanning tree from Prim's, which is the one thing this file
    // may not do.
    std::vector<double> point_w(static_cast<size_t>(n));
    std::vector<int> point_v(static_cast<size_t>(n), -1);
    // The bound every point of a component prunes against. A per-*point* bound
    // only tightens once that point has found something; sharing it across the
    // component means the first short edge anyone finds prunes the search for
    // all of them, which is what makes the late rounds cheap.
    std::vector<double> component_bound(static_cast<size_t>(n));

    int n_components = n;

    // Round zero comes free from the neighbour list. For a point i, every edge
    // has weight at least core[i]; if any of i's neighbours j is at least as
    // dense (core[j] <= core[i]) then that edge weighs exactly core[i] and is
    // therefore *provably* i's minimum, with no search at all. Most points have
    // such a neighbour, so this collapses the round that would otherwise be the
    // most expensive one — every component is a single point and nothing prunes.
    //
    // The stored list must contain every point within core[i] for the choice to
    // be the canonical one, which is what the extra neighbour fetched by
    // core_distances is for: if the (k+1)-th is strictly further than the k-th,
    // nothing outside the list can tie.
    // Not done here, and the reason is worth keeping: the reference
    // implementation gets a whole Borůvka round free from the neighbour list.
    // For a point i every edge weighs at least core[i], so if any neighbour j is
    // at least as dense (core[j] <= core[i]) that edge weighs exactly core[i]
    // and is i's minimum with no search at all. It was implemented, measured --
    // and produced a *different spanning tree from Prim's* on the same data.
    // Both are valid minimum spanning trees; only one of them can be this
    // library's answer, and a shortcut whose agreement with the total edge order
    // could not be established does not get to decide which. The shared
    // component bound below turned out to be the larger win anyway.

    while (n_components > 1) {
        for (int i = 0; i < n; ++i) component[static_cast<size_t>(i)] = uf.find(i);
        std::fill(component_bound.begin(), component_bound.end(),
                  std::numeric_limits<double>::infinity());

        // A node whose points all sit in one component can be skipped outright
        // by every point of that component -- the single most effective prune,
        // because late rounds are mostly self-queries.
        for (int node = static_cast<int>(nodes_.size()) - 1; node >= 0; --node) {
            const Node& nd = nodes_[static_cast<size_t>(node)];
            if (nd.left < 0) {
                int c = component[static_cast<size_t>(index_[nd.start])];
                for (int i = nd.start + 1; i < nd.stop; ++i) {
                    if (component[static_cast<size_t>(index_[i])] != c) {
                        c = -1;
                        break;
                    }
                }
                node_component[static_cast<size_t>(node)] = c;
            } else {
                const int cl = node_component[static_cast<size_t>(nd.left)];
                const int cr = node_component[static_cast<size_t>(nd.right)];
                node_component[static_cast<size_t>(node)] = (cl == cr) ? cl : -1;
            }
        }

        std::fill(best_w.begin(), best_w.end(), std::numeric_limits<double>::infinity());
        std::fill(best_u.begin(), best_u.end(), -1);
        std::fill(best_v.begin(), best_v.end(), -1);

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 64)
#endif
        for (int i = 0; i < n; ++i) {
            const double* point = data_ + static_cast<size_t>(i) * n_features_;
            const int my_component = component[static_cast<size_t>(i)];
            const double core_i = core[static_cast<size_t>(i)];

            double best = std::numeric_limits<double>::infinity();
            int best_j = -1;

            // The component's bound, read without synchronisation on purpose.
            // It only ever decreases and it is only used to prune, so a stale
            // (larger) read costs a little extra work and can never change the
            // answer — the winning edge is chosen by an exact reduction below.
            // A point whose own core distance already exceeds it cannot
            // contribute at all, because every edge from it weighs at least
            // that much: one scalar test skips the entire search.
            const double shared = component_bound[static_cast<size_t>(my_component)];
            if (core_i > shared) {
                point_v[static_cast<size_t>(i)] = -1;
                continue;
            }
            // The pruning threshold is kept apart from the best edge *found
            // here*: it starts at the component's, which may come from a
            // different point entirely, and only tightens. Equality never
            // prunes, so an edge that ties can still win the reduction on its
            // endpoints.
            double bound = shared;

            // Seed from the cached neighbours. Any real edge is an upper bound,
            // and a point's own nearest neighbours are the tightest one going;
            // without it the traversal starts at infinity and prunes nothing
            // until it stumbles onto its first candidate.
            if (knn_k_ > 0) {
                const size_t base = static_cast<size_t>(i) * knn_k_;
                for (int t = 0; t < knn_k_; ++t) {
                    const int j = knn_index_[base + t];
                    if (j < 0 || component[static_cast<size_t>(j)] == my_component) continue;
                    double w = std::sqrt(knn_sq_dist_[base + t]) / alpha;
                    if (w < core_i) w = core_i;
                    if (w < core[static_cast<size_t>(j)]) w = core[static_cast<size_t>(j)];
                    if (best_j < 0 || edge_less(w, i, j, best, i, best_j)) {
                        best = w;
                        best_j = j;
                        if (w < bound) bound = w;
                    }
                }
            }

            // Explicit stack of (node, squared lower bound). A depth-first
            // descent that does not go towards the point first establishes no
            // bound and walks the whole tree; the ordering is what makes this
            // O(log n). The bound travels with the node so it is computed once
            // rather than once per push and once per pop — at eight dimensions
            // that recomputation was a fifth of the run time. A balanced tree
            // halves at every split, so the stack cannot grow past one entry
            // per level.
            int stack_node[128];
            double stack_bound[128];
            int top = 0;
            stack_node[top] = 0;
            stack_bound[top] = node_lower_bound(0, point);
            ++top;
            while (top > 0) {
                --top;
                const int node = stack_node[top];
                if (node_component[static_cast<size_t>(node)] == my_component) continue;
                // Equality must NOT prune: a tied edge can still win on the
                // endpoint tie-break, and the whole point of the total order is
                // that the answer does not depend on the traversal. Compared
                // squared, so the descent costs no square roots.
                if (bound < std::numeric_limits<double>::infinity()) {
                    if (min_core[static_cast<size_t>(node)] > bound) continue;
                    const double limit = bound * alpha;
                    if (stack_bound[top] > limit * limit) continue;
                }

                const Node& nd = nodes_[static_cast<size_t>(node)];
                if (nd.left < 0) {
                    for (int s = nd.start; s < nd.stop; ++s) {
                        const int j = index_[s];
                        if (component[static_cast<size_t>(j)] == my_component) continue;
                        const double core_j = core[static_cast<size_t>(j)];
                        // A sparser neighbour cannot give a shorter edge,
                        // whatever the geometry: the weight is at least its
                        // core distance. One scalar test skips the whole
                        // distance loop, which is what pays in high dimensions.
                        if (core_j > bound) continue;
                        const double* row = data_ + static_cast<size_t>(j) * n_features_;
                        // Distance space, not squared -- see the note in the
                        // dual-tree kernel: `bound` is a square root, and
                        // comparing acc against its square skips tied edges.
                        double acc = 0.0;
                        for (int f = 0; f < n_features_; ++f) {
                            const double diff = point[f] - row[f];
                            acc += diff * diff;
                        }
                        double w = std::sqrt(acc) / alpha;
                        if (w > bound) continue;
                        if (w < core_i) w = core_i;
                        if (w < core_j) w = core_j;
                        if (best_j < 0 || edge_less(w, i, j, best, i, best_j)) {
                            best = w;
                            best_j = j;
                            if (w < bound) bound = w;
                        }
                    }
                } else {
                    const double dl = node_lower_bound(nd.left, point);
                    const double dr = node_lower_bound(nd.right, point);
                    if (dl <= dr) {
                        stack_node[top] = nd.right;
                        stack_bound[top] = dr;
                        ++top;
                        stack_node[top] = nd.left;
                        stack_bound[top] = dl;
                        ++top;
                    } else {
                        stack_node[top] = nd.left;
                        stack_bound[top] = dl;
                        ++top;
                        stack_node[top] = nd.right;
                        stack_bound[top] = dr;
                        ++top;
                    }
                }
            }
            point_w[static_cast<size_t>(i)] = best;
            point_v[static_cast<size_t>(i)] = best_j;
            // Publish for the rest of the component. Racy by design: the worst
            // a lost update can do is leave someone else pruning against a
            // slightly looser bound.
            if (best_j >= 0 && best < component_bound[static_cast<size_t>(my_component)])
                component_bound[static_cast<size_t>(my_component)] = best;
        }

        for (int i = 0; i < n; ++i) {
            const int j = point_v[static_cast<size_t>(i)];
            if (j < 0) continue;
            const int c = component[static_cast<size_t>(i)];
            if (best_v[static_cast<size_t>(c)] < 0 ||
                edge_less(point_w[static_cast<size_t>(i)], i, j,
                          best_w[static_cast<size_t>(c)], best_u[static_cast<size_t>(c)],
                          best_v[static_cast<size_t>(c)])) {
                best_w[static_cast<size_t>(c)] = point_w[static_cast<size_t>(i)];
                best_u[static_cast<size_t>(c)] = i;
                best_v[static_cast<size_t>(c)] = j;
            }
        }

        int added = 0;
        for (int c = 0; c < n; ++c) {
            if (best_v[static_cast<size_t>(c)] < 0) continue;
            if (!uf.unite(best_u[static_cast<size_t>(c)], best_v[static_cast<size_t>(c)]))
                continue;  // the other endpoint's component already added it
            mst.push_back(static_cast<double>(best_u[static_cast<size_t>(c)]));
            mst.push_back(static_cast<double>(best_v[static_cast<size_t>(c)]));
            mst.push_back(best_w[static_cast<size_t>(c)]);
            --n_components;
            ++added;
        }
        if (added == 0)
            throw std::runtime_error(
                "mutual_reachability_mst: no edge joined two components; the data "
                "is not connected, which cannot happen for a complete graph");
    }
    return mst;
}

std::vector<double> KDTree::mst_prim(const std::vector<double>& core, double alpha) const {
    if (static_cast<int>(core.size()) != n_samples_)
        throw std::invalid_argument("mst_prim: one core distance per sample is required");
    if (!(alpha > 0.0)) throw std::invalid_argument("mst_prim: alpha must be positive");

    const int n = n_samples_;
    std::vector<double> mst;
    if (n < 2) return mst;
    mst.resize(static_cast<size_t>(3 * (n - 1)));

    std::vector<unsigned char> in_tree(static_cast<size_t>(n), 0);
    std::vector<double> min_reach(static_cast<size_t>(n),
                                  std::numeric_limits<double>::infinity());
    std::vector<int> source_of(static_cast<size_t>(n), 0);

    // Serial on purpose. Each round is one pass over the points and there are
    // n - 1 of them, so the parallel version needs two rendezvous per round --
    // forty thousand at twenty thousand points. An OpenMP barrier costs more
    // than the round it separates and made this *slower* than the serial loop;
    // a spin barrier was faster in isolation but burns every core for the
    // duration, which a library called from a GUI has no business doing. The
    // inner loop vectorises, which is where the speed actually comes from.
    int current = 0;
    for (int step = 0; step < n - 1; ++step) {
        in_tree[static_cast<size_t>(current)] = 1;
        const double core_current = core[static_cast<size_t>(current)];
        const double* point = data_ + static_cast<size_t>(current) * n_features_;
        double best_w = std::numeric_limits<double>::infinity();
        int best_u = -1, best_v = -1;
        for (int j = 0; j < n; ++j) {
            if (in_tree[static_cast<size_t>(j)]) continue;
            const double* row = data_ + static_cast<size_t>(j) * n_features_;
            double acc = 0.0;
            for (int f = 0; f < n_features_; ++f) {
                const double diff = point[f] - row[f];
                acc += diff * diff;
            }
            double reach = std::sqrt(acc) / alpha;
            if (reach < core_current) reach = core_current;
            if (reach < core[static_cast<size_t>(j)]) reach = core[static_cast<size_t>(j)];
            // Both candidates end at j, so the endpoint tie-break of edge_less
            // collapses to "the smaller source wins".
            if (reach < min_reach[static_cast<size_t>(j)] ||
                (reach == min_reach[static_cast<size_t>(j)] &&
                 current < source_of[static_cast<size_t>(j)])) {
                min_reach[static_cast<size_t>(j)] = reach;
                source_of[static_cast<size_t>(j)] = current;
            }
            if (best_v < 0 ||
                edge_less(min_reach[static_cast<size_t>(j)], source_of[static_cast<size_t>(j)],
                          j, best_w, best_u, best_v)) {
                best_w = min_reach[static_cast<size_t>(j)];
                best_u = source_of[static_cast<size_t>(j)];
                best_v = j;
            }
        }
        if (best_v < 0) throw std::runtime_error("mst_prim: no candidate edge");
        mst[static_cast<size_t>(3 * step)] = static_cast<double>(best_u);
        mst[static_cast<size_t>(3 * step + 1)] = static_cast<double>(best_v);
        mst[static_cast<size_t>(3 * step + 2)] = best_w;
        current = best_v;
    }
    return mst;
}

// ---------------------------------------------------------------------------
// Flat entry points
// ---------------------------------------------------------------------------

void core_distances(double* input, int n_input1, int n_input2, int k, double** output,
                    int* n_output) {
    KDTree tree(input, n_input1, n_input2, KDTree::default_leaf_size(n_input2));
    const std::vector<double> values = tree.core_distances(k);
    *n_output = static_cast<int>(values.size());
    auto* buffer = static_cast<double*>(std::malloc(values.size() * sizeof(double)));
    if (buffer == nullptr) throw std::bad_alloc();
    std::memcpy(buffer, values.data(), values.size() * sizeof(double));
    *output = buffer;
}

void mutual_reachability_mst(double* input, int n_input1, int n_input2, int min_samples,
                             double alpha, double** output, int* n_output1,
                             int* n_output2) {
    KDTree tree(input, n_input1, n_input2, KDTree::default_leaf_size(n_input2));
    const std::vector<double> core = tree.core_distances(min_samples);
    const std::vector<double> mst = tree.mutual_reachability_mst(core, alpha);
    *n_output1 = static_cast<int>(mst.size() / 3);
    *n_output2 = 3;
    const size_t bytes = mst.size() * sizeof(double);
    auto* buffer = static_cast<double*>(std::malloc(bytes == 0 ? sizeof(double) : bytes));
    if (buffer == nullptr) throw std::bad_alloc();
    if (bytes) std::memcpy(buffer, mst.data(), bytes);
    *output = buffer;
}

}  // namespace tttrlib

// ===========================================================================
// The post-MST half of HDBSCAN: linkage, condensation, labelling
// ===========================================================================
// Ported from the numba kernels this replaces, structure for structure, so the
// two can be compared line by line. See Cluster.h for why this is two calls.

namespace tttrlib {

namespace {

// Union-find root with path halving -- the same walk the numba kernels use, and
// the reason none of this is expressible in an array language.
inline long long uf_find(std::vector<long long>& parent, long long a) {
    while (parent[static_cast<size_t>(a)] != a) {
        parent[static_cast<size_t>(a)] =
            parent[static_cast<size_t>(parent[static_cast<size_t>(a)])];
        a = parent[static_cast<size_t>(a)];
    }
    return a;
}

// Nodes under `root` in breadth-first order, written into `out`; returns count.
long long bfs_nodes(const std::vector<long long>& left,
                    const std::vector<long long>& right,
                    long long n_samples, long long root,
                    std::vector<long long>& out) {
    out[0] = root;
    long long head = 0, tail = 1;
    while (head < tail) {
        const long long node = out[static_cast<size_t>(head++)];
        if (node >= n_samples) {
            const size_t row = static_cast<size_t>(node - n_samples);
            out[static_cast<size_t>(tail++)] = left[row];
            out[static_cast<size_t>(tail++)] = right[row];
        }
    }
    return tail;
}

long long* malloc_i64(size_t n) {
    return static_cast<long long*>(std::malloc(std::max<size_t>(n, 1) * sizeof(long long)));
}

}  // namespace

void hdbscan_condensed_tree(
        long long* sources, int n_sources,
        long long* targets, int n_targets,
        double* weights, int n_weights,
        int min_cluster_size,
        long long** out_parent, int* n_out_parent,
        long long** out_child, int* n_out_child,
        double** out_value, int* n_out_value,
        long long** out_size, int* n_out_size) {
    if (n_sources != n_targets || n_sources != n_weights)
        throw std::invalid_argument(
            "hdbscan: sources, targets and weights must be the same length");
    if (n_sources < 1)
        throw std::invalid_argument("hdbscan: the MST needs at least one edge");
    if (min_cluster_size < 2)
        throw std::invalid_argument("hdbscan: min_cluster_size must be at least 2");
    for (int i = 1; i < n_weights; ++i)
        if (weights[i] < weights[i - 1])
            throw std::invalid_argument(
                "hdbscan: the MST edges must be ascending in weight -- linkage "
                "is order-dependent, so unsorted input yields a plausible and "
                "wrong dendrogram rather than an error");

    const long long n_edges = n_sources;
    const long long n_samples = n_edges + 1;

    // ---- single linkage: union-find over the sorted edges --------------------
    std::vector<long long> parent(static_cast<size_t>(2 * n_samples - 1));
    std::vector<long long> size_of(static_cast<size_t>(2 * n_samples - 1), 1);
    std::vector<long long> component(static_cast<size_t>(2 * n_samples - 1));
    for (size_t i = 0; i < parent.size(); ++i) {
        parent[i] = static_cast<long long>(i);
        component[i] = static_cast<long long>(i);
    }
    std::vector<long long> left(static_cast<size_t>(n_edges));
    std::vector<long long> right(static_cast<size_t>(n_edges));
    std::vector<double> value(static_cast<size_t>(n_edges));
    std::vector<long long> csize(static_cast<size_t>(n_edges));

    for (long long i = 0; i < n_edges; ++i) {
        long long a = uf_find(parent, sources[i]);
        long long b = uf_find(parent, targets[i]);
        left[static_cast<size_t>(i)] = component[static_cast<size_t>(a)];
        right[static_cast<size_t>(i)] = component[static_cast<size_t>(b)];
        value[static_cast<size_t>(i)] = weights[i];
        csize[static_cast<size_t>(i)] =
            size_of[static_cast<size_t>(a)] + size_of[static_cast<size_t>(b)];
        if (size_of[static_cast<size_t>(a)] < size_of[static_cast<size_t>(b)]) std::swap(a, b);
        parent[static_cast<size_t>(b)] = a;
        size_of[static_cast<size_t>(a)] += size_of[static_cast<size_t>(b)];
        component[static_cast<size_t>(a)] = n_samples + i;
    }

    // ---- condense ------------------------------------------------------------
    const long long root = 2 * n_edges;
    const long long n_nodes = root + 1;
    // A point leaves exactly one cluster and a split contributes two rows, so
    // this is a hard ceiling rather than a guess.
    const size_t cap = static_cast<size_t>(3 * n_samples + 3);
    std::vector<long long> cp(cap), cc(cap), cs(cap);
    std::vector<double> cv(cap);
    size_t n_out = 0;

    std::vector<long long> relabel(static_cast<size_t>(n_nodes), 0);
    std::vector<unsigned char> ignore(static_cast<size_t>(n_nodes), 0);
    relabel[static_cast<size_t>(root)] = n_samples;
    long long next_label = n_samples + 1;

    std::vector<long long> order(static_cast<size_t>(n_nodes));
    std::vector<long long> sub(static_cast<size_t>(n_nodes));
    const long long n_order = bfs_nodes(left, right, n_samples, root, order);

    for (long long idx = 0; idx < n_order; ++idx) {
        const long long node = order[static_cast<size_t>(idx)];
        if (ignore[static_cast<size_t>(node)] || node < n_samples) continue;
        const size_t row = static_cast<size_t>(node - n_samples);
        const long long node_left = left[row], node_right = right[row];
        const double distance = value[row];
        const double lambda_value =
            distance > 0.0 ? 1.0 / distance : std::numeric_limits<double>::infinity();

        const long long left_count =
            node_left >= n_samples ? csize[static_cast<size_t>(node_left - n_samples)] : 1;
        const long long right_count =
            node_right >= n_samples ? csize[static_cast<size_t>(node_right - n_samples)] : 1;
        const bool big_left = left_count >= min_cluster_size;
        const bool big_right = right_count >= min_cluster_size;

        if (big_left && big_right) {
            relabel[static_cast<size_t>(node_left)] = next_label++;
            cp[n_out] = relabel[static_cast<size_t>(node)];
            cc[n_out] = relabel[static_cast<size_t>(node_left)];
            cv[n_out] = lambda_value;
            cs[n_out] = left_count;
            ++n_out;
            relabel[static_cast<size_t>(node_right)] = next_label++;
            cp[n_out] = relabel[static_cast<size_t>(node)];
            cc[n_out] = relabel[static_cast<size_t>(node_right)];
            cv[n_out] = lambda_value;
            cs[n_out] = right_count;
            ++n_out;
            continue;
        }

        bool shed_left, shed_right;
        if (!big_left && !big_right) {
            shed_left = shed_right = true;
        } else if (!big_left) {
            relabel[static_cast<size_t>(node_right)] = relabel[static_cast<size_t>(node)];
            shed_left = true; shed_right = false;
        } else {
            relabel[static_cast<size_t>(node_left)] = relabel[static_cast<size_t>(node)];
            shed_left = false; shed_right = true;
        }

        for (int side = 0; side < 2; ++side) {
            const bool shed = side == 0 ? shed_left : shed_right;
            if (!shed) continue;
            const long long start = side == 0 ? node_left : node_right;
            const long long n_sub = bfs_nodes(left, right, n_samples, start, sub);
            for (long long s = 0; s < n_sub; ++s) {
                const long long sub_node = sub[static_cast<size_t>(s)];
                if (sub_node < n_samples) {
                    cp[n_out] = relabel[static_cast<size_t>(node)];
                    cc[n_out] = sub_node;
                    cv[n_out] = lambda_value;
                    cs[n_out] = 1;
                    ++n_out;
                }
                ignore[static_cast<size_t>(sub_node)] = 1;
            }
        }
    }

    // ARGOUTVIEWM hands ownership to the host language, which frees with free().
    *out_parent = malloc_i64(n_out);
    *out_child = malloc_i64(n_out);
    *out_size = malloc_i64(n_out);
    *out_value = static_cast<double*>(std::malloc(std::max<size_t>(n_out, 1) * sizeof(double)));
    for (size_t i = 0; i < n_out; ++i) {
        (*out_parent)[i] = cp[i];
        (*out_child)[i] = cc[i];
        (*out_value)[i] = cv[i];
        (*out_size)[i] = cs[i];
    }
    *n_out_parent = *n_out_child = *n_out_value = *n_out_size = static_cast<int>(n_out);
}

void hdbscan_label_points(
        long long* parents, int n_parents,
        long long* children, int n_children,
        unsigned char* is_selected, int n_is_selected,
        int n_points,
        long long** out, int* n_out) {
    if (n_parents != n_children)
        throw std::invalid_argument("hdbscan: parents and children must be the same length");
    if (n_points <= 0)
        throw std::invalid_argument("hdbscan: n_points must be positive");
    if (n_is_selected <= 0)
        throw std::invalid_argument("hdbscan: is_selected must not be empty");

    std::vector<long long> uf(static_cast<size_t>(n_is_selected));
    for (size_t i = 0; i < uf.size(); ++i) uf[i] = static_cast<long long>(i);

    for (int i = 0; i < n_parents; ++i) {
        const long long child = children[i];
        if (child < 0 || child >= n_is_selected)
            throw std::invalid_argument(
                "hdbscan: a child id is outside is_selected -- its length must "
                "cover every node id in the condensed tree");
        // A selected cluster is a root of its own: nothing above it may absorb
        // it, which is what lets the loop below read a label off.
        if (is_selected[child]) continue;
        const long long a = uf_find(uf, parents[i]);
        const long long b = uf_find(uf, child);
        // The parent's side always survives, so a component is named by its
        // topmost node -- the selected cluster, or the root.
        if (a != b) uf[static_cast<size_t>(b)] = a;
    }

    *out = malloc_i64(static_cast<size_t>(n_points));
    for (int n = 0; n < n_points; ++n)
        (*out)[n] = uf_find(uf, n);
    *n_out = n_points;
}

}  // namespace tttrlib

%{
#include "BurstSearchMaxTree.h"
%}

// The 1-D max-tree (component tree) the max-tree burst search filters. Exposed
// as an (n_nodes, 4) int64 array [level, lo, hi, parent] so it can be inspected,
// filtered by other attributes, and A/B-tested against
// skimage.morphology.max_tree (identical component set,
// test/python/burstfilter/test_ab_burst_reference.py). Shared by all bindings:
// the IN_ARRAY1 / ARGOUTVIEWM_ARRAY2 names exist in ext/{r,java,js}/*arrays.i.
%apply (int* IN_ARRAY1, int DIM1) {(const int* levels, int n_levels)}
%apply (long long** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {(long long** out_nodes, int* out_rows, int* out_cols)}
%inline %{
void max_tree_1d(const int* levels, int n_levels,
                 long long** out_nodes, int* out_rows, int* out_cols) {
    std::vector<int> lv(levels, levels + n_levels);
    std::vector<tttrlib::MaxTreeNode> nodes = tttrlib::build_max_tree_1d(lv);
    long long* out = (long long*) malloc(sizeof(long long) * (nodes.size() * 4 + 1));
    for (size_t i = 0; i < nodes.size(); ++i) {
        out[i * 4 + 0] = nodes[i].level;
        out[i * 4 + 1] = nodes[i].lo;
        out[i * 4 + 2] = nodes[i].hi;
        out[i * 4 + 3] = nodes[i].parent;
    }
    *out_nodes = out; *out_rows = (int) nodes.size(); *out_cols = 4;
}
%}
%clear (const int* levels, int n_levels);
%clear (long long** out_nodes, int* out_rows, int* out_cols);

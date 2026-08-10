// SPDX-License-Identifier: BSD-3-Clause
%{
#include "Cluster.h"
%}

// The sample table arrives as a 2D NumPy array (rows = samples).
%apply(double* IN_ARRAY2, int DIM1, int DIM2) {(const double* data, int n_samples, int n_features)}

// Both kernels are long-running and touch no Python object.
TTTRLIB_NOGIL(tttrlib::core_distances)
TTTRLIB_NOGIL(tttrlib::mutual_reachability_mst)
TTTRLIB_NOGIL(tttrlib::KDTree::core_distances)
TTTRLIB_NOGIL(tttrlib::KDTree::mutual_reachability_mst)

%exception {
    try {
        $action
    } catch (const std::exception& e) {
        SWIG_exception(SWIG_RuntimeError, e.what());
    }
}

// `query` writes into caller-supplied buffers, which has no useful Python
// spelling; the Python-facing surface is the two free functions plus the
// tree's own core_distances / mutual_reachability_mst.
%ignore tttrlib::KDTree::query;

%include "Cluster.h"

#ifdef SWIGPYTHON
%extend tttrlib::KDTree {
    %pythoncode %{
    def __repr__(self):
        return "KDTree({} x {})".format(self.n_samples(), self.n_features())
    %}
}
#endif

// Reset the catch-all so the modules included after this one keep theirs.
%exception;

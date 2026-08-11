// SPDX-License-Identifier: BSD-3-Clause
%{
#include "Cluster.h"
%}

// The sample table arrives as a 2D NumPy array (rows = samples).
%apply(double* IN_ARRAY2, int DIM1, int DIM2) {(const double* data, int n_samples, int n_features)}

// The post-MST half: edge lists in, trees and labels out. Same typemap names
// work in r/java/js (rarrays.i, jarrays.i, jsarrays.i), so one %apply serves
// every binding that includes this file.
%apply (long long* IN_ARRAY1, int DIM1) {(long long* sources, int n_sources)}
%apply (long long* IN_ARRAY1, int DIM1) {(long long* targets, int n_targets)}
%apply (double* IN_ARRAY1, int DIM1) {(double* weights, int n_weights)}
%apply (long long* IN_ARRAY1, int DIM1) {(long long* parents, int n_parents)}
%apply (long long* IN_ARRAY1, int DIM1) {(long long* children, int n_children)}
%apply (unsigned char* IN_ARRAY1, int DIM1) {(unsigned char* is_selected, int n_is_selected)}
%apply (long long** ARGOUTVIEWM_ARRAY1, int* DIM1) {(long long** out_parent, int* n_out_parent)}
%apply (long long** ARGOUTVIEWM_ARRAY1, int* DIM1) {(long long** out_child, int* n_out_child)}
%apply (double** ARGOUTVIEWM_ARRAY1, int* DIM1) {(double** out_value, int* n_out_value)}
%apply (long long** ARGOUTVIEWM_ARRAY1, int* DIM1) {(long long** out_size, int* n_out_size)}
%apply (long long** ARGOUTVIEWM_ARRAY1, int* DIM1) {(long long** out, int* n_out)}

// Both kernels are long-running and touch no Python object.
TTTRLIB_NOGIL(tttrlib::hdbscan_condensed_tree)
TTTRLIB_NOGIL(tttrlib::hdbscan_label_points)
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

// Restore the global handler rather than clearing it. A bare `%exception;`
// resets to NOTHING -- not to whatever was in force before, which is what the
// old comment here claimed -- so it disarms every interface included after
// this one, and a C++ throw from any of them terminates the interpreter
// instead of raising (BUGS 2026-08-11: a bare clear in Fdc2D.i aborted the
// whole test suite at CLSMSuperRes.temporal_combine). These files are safe
// today only because they happen to precede MicrotimeLinearization.i, which
// reinstalls it; that is include order, not design. Keep this body identical
// to MicrotimeLinearization.i's.
%exception {
    try {
        $action
    } catch (const std::invalid_argument& e) {
        SWIG_exception(SWIG_ValueError, e.what());
    } catch (const std::exception& e) {
        SWIG_exception(SWIG_RuntimeError, e.what());
    } catch (...) {
        SWIG_exception(SWIG_UnknownError, "Unknown exception");
    }
}

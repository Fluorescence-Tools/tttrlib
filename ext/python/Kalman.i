// SPDX-License-Identifier: BSD-3-Clause
%{
#include "Kalman.h"
%}

// The count-rate trace arrives as a 2D NumPy array (T x dim, rows = bins).
%apply (double* IN_ARRAY2, int DIM1, int DIM2) {(const double* y, int T, int dim)}

// In state: x0 (dim), P0 (dim x dim), Q (dim x dim); each array carries its
// own dims, matching the Cluster family convention.
%apply (double* IN_ARRAY1, int DIM1) {(const double* x0, int n_x0)}
%apply (double* IN_ARRAY2, int DIM1, int DIM2) {(const double* P0, int n_P1, int n_P2)}
%apply (double* IN_ARRAY2, int DIM1, int DIM2) {(const double* Q, int n_Q1, int n_Q2)}

// Out: x_filt (T x dim), P_filt (T x dim x dim), D (T).
%apply (double** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {(double** out_x_filt, int* out_T1, int* out_dim1)}
%apply (double** ARGOUTVIEWM_ARRAY3, int* DIM1, int* DIM2, int* DIM3) {(double** out_P_filt, int* out_T2, int* out_dim2a, int* out_dim2b)}
%apply (double** ARGOUTVIEWM_ARRAY1, int* DIM1) {(double** out_D, int* out_T3)}

// Long-running recursion; touches no Python object.
TTTRLIB_NOGIL(tttrlib::kalman_filter)

%exception {
    try {
        $action
    } catch (const std::invalid_argument& e) {
        SWIG_exception(SWIG_ValueError, e.what());
    } catch (const std::exception& e) {
        SWIG_exception(SWIG_RuntimeError, e.what());
    }
}

%include "Kalman.h"
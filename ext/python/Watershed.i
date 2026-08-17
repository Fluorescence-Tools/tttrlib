// SPDX-License-Identifier: BSD-3-Clause
%{
#include "Watershed.h"
%}

// The landscape, the marker labels and the mask all arrive as 2D arrays of the
// same shape; each carries its own dims, matching the Cluster family
// convention.
%apply (double* IN_ARRAY2, int DIM1, int DIM2) {(const double* image, int n_rows, int n_cols)}
%apply (long long* IN_ARRAY2, int DIM1, int DIM2) {(const long long* markers, int m_rows, int m_cols)}
%apply (unsigned char* IN_ARRAY2, int DIM1, int DIM2) {(const unsigned char* mask, int k_rows, int k_cols)}

// Out: the label image (rows x cols), and the contour segments (n x 4).
%apply (long long** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {(long long** out_labels, int* out_rows, int* out_cols)}
%apply (double** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {(double** out_segments, int* out_n_segments, int* out_n_cols)}

// `connectivity` is optional and means "faces only", matching skimage's
// default of 1. Same device as GopichSzabo.i's `offsets`: SWIG treats the
// argument it has a `default` typemap for as optional.
%typemap(default) (int connectivity) {
    $1 = 1;
}

// Both kernels are long-running and touch no Python object.
TTTRLIB_NOGIL(tttrlib::watershed)
TTTRLIB_NOGIL(tttrlib::marching_squares)

// The marching-squares flood is the only frame-level float loop here, but it
// is a subtraction and a division per edge -- see Watershed.cpp for why the
// fp-contract discipline matters.
%exception {
    try {
        $action
    } catch (const std::invalid_argument& e) {
        SWIG_exception(SWIG_ValueError, e.what());
    } catch (const std::exception& e) {
        SWIG_exception(SWIG_RuntimeError, e.what());
    }
}

%include "Watershed.h"
// SPDX-License-Identifier: BSD-3-Clause
%module tttrlib
%{
#include "Histogram.h"
#include "HistogramAxis.h"
%}

%apply (double* IN_ARRAY1, int DIM1) {
    (double* data, int n_data),
    (double* weights, int n_weights),
    (double* bin_edges, int n_bins),
    (double* hist, int n_hist)
}
%apply (int* IN_ARRAY1, int DIM1) {
    (int* data, int n_data),
    (int* bin_edges, int n_bins),
    (int* hist, int n_hist)
}
// histogram2D takes two data arrays and one pair of bin edges per axis, so its
// parameters need their own maps.
//
// The names matter more than they look: %apply is GLOBAL and keyed by name, so
// mapping (double* x, int n_x) here silently redefines what that pair means for
// every other interface file too. DecayConvolution.i already maps it, and doing
// it again produced generated code that would not compile -- in fconv, which
// has nothing to do with histograms. Hence data_x / data_y.
%apply (double* IN_ARRAY1, int DIM1) {
    (double* data_x, int n_data_x),
    (double* data_y, int n_data_y),
    (double* bin_edges_x, int n_bins_x),
    (double* bin_edges_y, int n_bins_y)
}
%apply (int* IN_ARRAY1, int DIM1) {
    (int* data_x, int n_data_x),
    (int* data_y, int n_data_y),
    (int* bin_edges_x, int n_bins_x),
    (int* bin_edges_y, int n_bins_y)
}
%apply (double* IN_ARRAY2, int DIM1, int DIM2) {
    (double *data, int n_rows_data, int n_cols_data)
}
%apply (double** ARGOUTVIEWM_ARRAY1, int* DIM1 ) {
    (double** hist, int* dim)
}

%include "Histogram.h"
%include "HistogramAxis.h"

%template(histogram1D_int) histogram1D<int>;
%template(histogram1D_double) histogram1D<double>;
%template(histogram2D_int) histogram2D<int>;
%template(histogram2D_double) histogram2D<double>;
%template(doubleAxis) HistogramAxis<double>;
%template(doubleHistogram) Histogram<double>;


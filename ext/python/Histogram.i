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
// make_bin_edges WRITES its array, so it needs INPLACE rather than IN. Its
// parameter is named edges_out for exactly that reason -- see the note above.
%apply (double* INPLACE_ARRAY1, int DIM1) {
    (double* edges_out, int n_edges_out)
}

// bincount1D(int* data, int n_data, int* bins, int n_bins) as declared shares
// (int* data, n_data) with the IN map above but its output pair is named like an
// INPUT elsewhere; give it its own array-in / inplace-out wrapper instead of
// letting SWIG expose the raw pointers (unreachable from Python until 2026-08-17).
%ignore bincount1D;
%rename (bincount1D) bincount1D_arrays;
%apply (int* IN_ARRAY1, int DIM1) { (const int* bincount_data, int n_bincount_data) }
%apply (int* INPLACE_ARRAY1, int DIM1) { (int* bincount_out, int n_bincount_out) }
%inline %{
void bincount1D_arrays(const int* bincount_data, int n_bincount_data,
                       int* bincount_out, int n_bincount_out) {
    bincount1D(const_cast<int*>(bincount_data), n_bincount_data,
               bincount_out, n_bincount_out);
}
%}
%clear (const int* bincount_data, int n_bincount_data);
%clear (int* bincount_out, int n_bincount_out);

%include "Histogram.h"
%include "HistogramAxis.h"

%template(histogram1D_int) histogram1D<int>;
%template(histogram1D_double) histogram1D<double>;
%template(histogram2D_int) histogram2D<int>;
%template(histogram2D_double) histogram2D<double>;
// Range forms: "64 bins from 0 to 100" without building the edge array. What a
// plotting front end actually has.
%template(histogram1D_range_double) histogram1D_range<double>;
%template(histogram2D_range_double) histogram2D_range<double>;
%template(make_bin_edges_double) make_bin_edges<double>;
%template(doubleAxis) HistogramAxis<double>;
%template(doubleHistogram) Histogram<double>;


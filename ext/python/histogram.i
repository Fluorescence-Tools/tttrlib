%module tttrlib
%{
    #include "../include/histogram.h"
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
%apply (double* IN_ARRAY2, int DIM1, int DIM2) {
    (double *data, int n_rows_data, int n_cols_data)
}
%apply (double** ARGOUTVIEWM_ARRAY1, int* DIM1 ) {
    (double** hist, int* dim)
}

%include "../include/histogram.h"

%template(histogram1D_int) histogram1D<int>;
%template(histogram1D_double) histogram1D<double>;
%template(doubleAxis) HistogramAxis<double>;
%template(doubleHistogram) Histogram<double>;


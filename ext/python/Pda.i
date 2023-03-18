%module tttrlib
%{
#include "Pda.h"
#include "PdaCallback.h"
%}

//// internal
%attribute(Pda, bool, hist_sgsr_valid, is_valid_sgsr, set_valid_sgsr);

// 1D histogram
%apply (double** ARGOUTVIEW_ARRAY1, int* DIM1) {
    (double **histogram_x, int *n_histogram_x),
    (double **histogram_y, int *n_histogram_y)
}

// output of make_s1s2 //
// the 2d matrix
%apply(double** ARGOUTVIEW_ARRAY2, int* DIM1, int* DIM2) {(double** s1s2, int* dim1, int* dim2)}
%apply(double** ARGOUTVIEW_ARRAY1, int* DIM1) {(double** ps, int* dim_ps)}
%apply(int** ARGOUTVIEW_ARRAY1, int* DIM1) {(int** tttr_indices, int* n_tttr_indices)}
%apply(double* IN_ARRAY1, int DIM1) {(double* pF, int n_pF)}

// Pda Model attributes
%attribute(Pda, double, background_ch2, get_ch2_background, set_ch2_background);
%attribute(Pda, double, background_ch1, get_ch1_background, set_ch1_background);

// 2d histogram attributes
%attribute(Pda, unsigned int, hist2d_nmin, get_min_number_of_photons, set_min_number_of_photons);
%attribute(Pda, unsigned int, hist2d_nmax, get_max_number_of_photons, set_max_number_of_photons);
%attribute(Pda, bool, hist2d_valid, is_valid_sgsr, set_valid_sgsr);
%extend Pda{%pythoncode "./ext/python/Pda.py"}

// Used for PdaCallback
// see https://github.com/swig/swig/tree/master/Examples/python/callback
%feature("director") PdaCallback;
%include "Pda.h"
%include "PdaCallback.h"

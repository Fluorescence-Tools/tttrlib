%module tttrlib
%{
    #include "../include/pda.h"

%}

%include "numpy.i"
%init %{
    import_array();
%}

%apply (double* IN_ARRAY1, int DIM1) {(double *input, int n_input)}
%apply (double** ARGOUTVIEW_ARRAY1, int* DIM1) {(double** output, int* n_output)}
%apply (double** ARGOUTVIEW_ARRAY2, int* DIM1, int* DIM2) {(double** output, int* n_output1, int* n_output2)}

%apply (double** ARGOUTVIEW_ARRAY1, int* DIM1) {(double **histogram_x, int *n_histogram_x)}
%apply (double** ARGOUTVIEW_ARRAY1, int* DIM1) {(double **histogram_y, int *n_histogram_y)}

%include "../include/pda.h"


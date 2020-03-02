%module tttrlib
%{
    #include "../include/pda.h"

%}

%apply (double* IN_ARRAY1, int DIM1) {(double *in, int n_in)}
%apply (double** ARGOUTVIEW_ARRAY1, int* DIM1) {(double** out, int* dim1)}
%apply (double** ARGOUTVIEW_ARRAY2, int* DIM1, int* DIM2) {(double** out, int* dim1, int* dim2)}

%include "../include/pda.h"


%module tttrlib
%{
    #include "../include/pda.h"

%}

%apply (double** ARGOUTVIEW_ARRAY1, int* DIM1) {(double **histogram_x, int *n_histogram_x)}
%apply (double** ARGOUTVIEW_ARRAY1, int* DIM1) {(double **histogram_y, int *n_histogram_y)}

%include "../include/pda.h"


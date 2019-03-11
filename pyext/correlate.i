%module tttrlib
%{
    #include "../include/correlate.h"
%}


%apply (unsigned long* IN_ARRAY1, int DIM1) {(unsigned long *t1, int n_t1), (unsigned long *t2, int n_t2)}
%apply (unsigned long** ARGOUTVIEWM_ARRAY1, int* DIM1) {(unsigned long** x_axis, int* n_out)}
%apply (double** ARGOUTVIEWM_ARRAY1, int* DIM1) {(double** corr, int* n_out)}
%apply (double* IN_ARRAY1, int DIM1) {(double* weight_ch1, int n_weights_ch1), (double* weight_ch2, int n_weights_ch2)}

%include "../include/correlate.h"


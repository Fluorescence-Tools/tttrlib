%module(directors="1", package="tttrlib") tttrlib
%feature("kwargs", 1);
%{
#include "../include/tttr.h"
#include "../include/header.h"
%}

%pythonbegin "./ext/python/python_imports.py"

%include "std_vector.i";
%include "std_wstring.i";
%include "std_string.i";

%apply (unsigned long long* IN_ARRAY1, int DIM1) {
    (unsigned long long *time, int n_time)
}
%apply (unsigned long long* IN_ARRAY1, int DIM1) {
    (unsigned long long *selection, int n_selection)
}
%apply (long long* IN_ARRAY1, int DIM1) {
    (long long *selection, int n_selection)
}
%apply (int** ARGOUTVIEWM_ARRAY1, int* DIM1) {
    (int **selection, int *n_selection)
}
%apply (int* IN_ARRAY1, int DIM1) {
    (int* in, int n_input)
}
%apply (long long* IN_ARRAY1, int DIM1) {
    (long long *input, int n_input)
}
%apply (unsigned long long* IN_ARRAY1, int DIM1) {
    (unsigned long long *input, int n_input)
}
%apply (int** ARGOUTVIEWM_ARRAY1, int* DIM1) {
    (int **ranges, int *n_range)
}
%apply (unsigned long long** ARGOUTVIEWM_ARRAY1, int* DIM1) {
    (unsigned long long** output, int* n_output)
}
%apply (long long** ARGOUTVIEWM_ARRAY1, int* DIM1) {
    (long long **output, int *n_output)
}
%apply (unsigned int** ARGOUTVIEWM_ARRAY1, int* DIM1) {
    (unsigned int** output, int* n_output)
}
%apply (short** ARGOUTVIEWM_ARRAY1, int* DIM1) {
    (short** output, int* n_output)
}
%apply (int** ARGOUTVIEWM_ARRAY1, int* DIM1) {
    (int** output, int* n_output)
}

%extend TTTR{

    %pythoncode "./ext/python/tttr_extension.py"
}

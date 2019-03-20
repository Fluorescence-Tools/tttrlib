%module tttrlib
%{
    #include "../include/TTTR.h"
    #include "../include/Header.h"
%}

%import std_vector.i

%apply (unsigned long long* IN_ARRAY1, int DIM1) {(unsigned long long *time, int n_time)}

%apply (unsigned long long* IN_ARRAY1, int DIM1) {(unsigned long long *selection, int n_selection)}
%apply (long long* IN_ARRAY1, int DIM1) {(long long *selection, int n_selection)}
%apply (int** ARGOUTVIEWM_ARRAY1, int* DIM1) {(int **selection, int *n_selection)}

%apply (int* IN_ARRAY1, int DIM1) {(int* in, int n_in)}
%apply (long long* IN_ARRAY1, int DIM1) {(long long *in, int n_in)}

%apply (unsigned long long* IN_ARRAY1, int DIM1) {(unsigned long long *in, int n_in)}

%apply (int** ARGOUTVIEWM_ARRAY1, int* DIM1) {(int **ranges, int *n_range)}

%apply (unsigned long long** ARGOUTVIEWM_ARRAY1, int* DIM1) {(unsigned long long** out, int* n_out)}
%apply (long long** ARGOUTVIEWM_ARRAY1, int* DIM1) {(long long **out, int *n_out)}
%apply (unsigned int** ARGOUTVIEWM_ARRAY1, int* DIM1) {(unsigned int** out, int* n_out)}

%apply (short** ARGOUTVIEWM_ARRAY1, int* DIM1) {(short** out, int* n_out)}
%apply (int** ARGOUTVIEWM_ARRAY1, int* DIM1) {(int** out, int* n_out)}


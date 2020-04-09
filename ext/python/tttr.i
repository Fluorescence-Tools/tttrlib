%module(directors="1", package="tttrlib") tttrlib
%feature("kwargs", 1);
%{
#include "../include/tttr.h"
#include "../include/header.h"
%}

%extend TTTR{
    %pythoncode "./ext/python/tttr_extension.py"
}

%apply (unsigned long long* IN_ARRAY1, int DIM1) {(unsigned long long *time, int n_time)}
%apply (unsigned long long* IN_ARRAY1, int DIM1) {(unsigned long long *selection, int n_selection)}
%apply (unsigned long long** ARGOUTVIEWM_ARRAY1, int* DIM1) {(unsigned long long **selection, int *n_selection)}
%apply (int* IN_ARRAY1, int DIM1) {(int* input, int n_input)}
%apply (long long* IN_ARRAY1, int DIM1) {(long long *input, int n_input)}
%apply (unsigned long long* IN_ARRAY1, int DIM1) {(unsigned long long *input, int n_input)}
%apply (unsigned long long** ARGOUTVIEWM_ARRAY1, int* DIM1) {(unsigned long long **ranges, int *n_range)}
%apply (unsigned long long** ARGOUTVIEWM_ARRAY1, int* DIM1) {(unsigned long long** output, int* n_output)}
%apply (long long** ARGOUTVIEWM_ARRAY1, int* DIM1) {(long long **output, int *n_output)}
%apply (unsigned int** ARGOUTVIEWM_ARRAY1, int* DIM1) {(unsigned int** output, int* n_output)}
%apply (short** ARGOUTVIEWM_ARRAY1, int* DIM1) {(short** output, int* n_output)}
%apply (int** ARGOUTVIEWM_ARRAY1, int* DIM1) {(int** output, int* n_output)}

// Used in a TTTR constructor
%apply (unsigned long long* IN_ARRAY1, int DIM1) {(unsigned long long *macro_times, int n_macrotimes)}
%apply (unsigned int* IN_ARRAY1, int DIM1) {(unsigned int *micro_times, int n_microtimes)}
%apply (short * IN_ARRAY1, int DIM1) {(short *routing_channels, int n_routing_channels)}
%apply (short * IN_ARRAY1, int DIM1) {(short *event_types, int n_event_types)}

%include "../include/header.h"
%include "../include/tttr.h"

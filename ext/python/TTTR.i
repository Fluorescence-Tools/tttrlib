%{
#include "TTTR.h"
%}
// Use shared_prt for TTTR to pass TTTR around
%shared_ptr(TTTR)

// used in selection and ranges
%apply (unsigned long long* IN_ARRAY1, int DIM1) {(unsigned long long *time, int n_time)}
%apply (int* IN_ARRAY1, int DIM1) {(int *selection, int n_selection)}
%apply (int** ARGOUTVIEWM_ARRAY1, int* DIM1) {(int **selection, int *n_selection)}
%apply (unsigned int** ARGOUTVIEWM_ARRAY1, int* DIM1) {(unsigned int **ranges, int *n_range)}

// for microtime_histogram
%apply(double** ARGOUTVIEWM_ARRAY1, int* DIM1){
    (double** histogram, int* n_histogram),
    (double** time, int* n_time)
};

// Used in a TTTR constructor
%apply (unsigned long long* IN_ARRAY1, int DIM1) {(unsigned long long *macro_times, int n_macrotimes)}
%apply (unsigned short* IN_ARRAY1, int DIM1) {(unsigned short *micro_times, int n_microtimes)}
%apply (signed char * IN_ARRAY1, int DIM1) {(signed char *routing_channels, int n_routing_channels)}
%apply (signed char * IN_ARRAY1, int DIM1) {(signed char *event_types, int n_event_types)}

%extend TTTR{%pythoncode "./ext/python/TTTR.py"}

%include "TTTR.h"


%{
#include "../include/TTTRSelection.h"
%}
%attributeval(TTTRSelection, std::vector<int>, tttr_indices, get_tttr_indices);

// used in selection and ranges
%apply (unsigned long long* IN_ARRAY1, int DIM1) {(unsigned long long *time, int n_time)}
%apply (int* IN_ARRAY1, int DIM1) {(int *selection, int n_selection)}
%apply (int** ARGOUTVIEWM_ARRAY1, int* DIM1) {(int **selection, int *n_selection)}
%apply (unsigned int** ARGOUTVIEWM_ARRAY1, int* DIM1) {(unsigned int **ranges, int *n_range)}

// Used in a TTTR constructor
%apply (unsigned long long* IN_ARRAY1, int DIM1) {(unsigned long long *macro_times, int n_macrotimes)}
%apply (unsigned short* IN_ARRAY1, int DIM1) {(unsigned short *micro_times, int n_microtimes)}
%apply (signed char * IN_ARRAY1, int DIM1) {(signed char *routing_channels, int n_routing_channels)}
%apply (signed char * IN_ARRAY1, int DIM1) {(signed char *event_types, int n_event_types)}

%include "../include/TTTRSelection.h"


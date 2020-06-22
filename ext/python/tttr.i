%{
#include "../include/tttr.h"
#include "../include/header.h"
%}

// used in selection and ranges
%apply (unsigned long long* IN_ARRAY1, int DIM1) {(unsigned long long *time, int n_time)}
%apply (int* IN_ARRAY1, int DIM1) {(int *selection, int n_selection)}
%apply (int** ARGOUTVIEWM_ARRAY1, int* DIM1) {(int **selection, int *n_selection)}
%apply (unsigned int** ARGOUTVIEWM_ARRAY1, int* DIM1) {(unsigned int **ranges, int *n_range)}

// Used in a TTTR constructor
%apply (unsigned long long* IN_ARRAY1, int DIM1) {(unsigned long long *macro_times, int n_macrotimes)}
%apply (unsigned int* IN_ARRAY1, int DIM1) {(unsigned int *micro_times, int n_microtimes)}
%apply (short * IN_ARRAY1, int DIM1) {(short *routing_channels, int n_routing_channels)}
%apply (short * IN_ARRAY1, int DIM1) {(short *event_types, int n_event_types)}

// documentation see
// https://github.com/swig/swig/blob/6f2399e86da13a9feb436e3977e15d2b9738294e/Lib/typemaps/attribute.swg
%attribute2(TTTRRange, std::vector<int>, tttr_indices, get_tttr_indices);
%attributeval(TTTRRange, std::vector<int>, start_stop, get_start_stop);
%attribute(TTTRRange, int, start, get_start, set_start);
%attribute(TTTRRange, int, stop, get_stop, set_stop);
%attribute(TTTRRange, unsigned int, start_time, get_start_time, set_start_time);
%attribute(TTTRRange, unsigned int, stop_time, get_stop_time, set_stop_time);

// Python does not support overloading. Thus, ignore the copy constructor
%ignore TTTRRange(const TTTRRange& p2);
%extend TTTR{%pythoncode "./ext/python/tttr/tttr_extension.py"}

// Use shared_prt for TTTR to pass TTTR around
%shared_ptr(TTTR)

%include "../include/header.h"
%include "../include/tttr.h"

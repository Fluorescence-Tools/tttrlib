%{
#include "../include/tttr.h"
#include "../include/header.h"
%}

%apply (unsigned long long* IN_ARRAY1, int DIM1) {(unsigned long long *time, int n_time)}
%apply (long long* IN_ARRAY1, int DIM1) {(long long *selection, int n_selection)}
%apply (long long** ARGOUTVIEWM_ARRAY1, int* DIM1) {(long long **selection, int *n_selection)}
%apply (int* IN_ARRAY1, int DIM1) {(int* input, int n_input)}
%apply (long long* IN_ARRAY1, int DIM1) {(long long *input, int n_input)}
%apply (unsigned long long* IN_ARRAY1, int DIM1) {(unsigned long long *input, int n_input)}
%apply (long long* IN_ARRAY1, int DIM1) {(long long *input, int n_input)}
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

// documentation see
// https://github.com/swig/swig/blob/6f2399e86da13a9feb436e3977e15d2b9738294e/Lib/typemaps/attribute.swg
%include attribute.i
%attribute2(TTTRRange, %arg(std::vector<long long>), tttr_indices, get_tttr_indices);
%attributeval(TTTRRange, std::vector<long long>, start_stop, get_start_stop);
%attribute(TTTRRange, long long, start, get_start, set_start);
%attribute(TTTRRange, long long, stop, get_stop, set_stop);
%attribute(TTTRRange, long long, start_time, get_start_time, set_start_time);
%attribute(TTTRRange, long long, stop_time, get_stop_time, set_stop_time);

%extend Correlator{
        %pythoncode "../ext/python/correlation/correlator_extension.py"
}

// Python does not support overloading. Thus, ignore the copy constructor
%ignore TTTRRange(const TTTRRange& p2);
%extend TTTR{
        %pythoncode "./ext/python/tttr/tttr_extension.py"
}

%include "../include/header.h"
%include "../include/tttr.h"

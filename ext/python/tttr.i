%{
#include "../include/TTTR.h"
#include "../include/TTTRHeader.h"
#include "../include/TTTRRange.h"
%}

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

// documentation see
// https://github.com/swig/swig/blob/6f2399e86da13a9feb436e3977e15d2b9738294e/Lib/typemaps/attribute.swg
// TTTRRange
%attribute2(TTTRRange, std::vector<int>, tttr_indices, get_tttr_indices);
%attributeval(TTTRRange, std::vector<int>, start_stop, get_start_stop);
%attribute(TTTRRange, int, start, get_start, set_start);
%attribute(TTTRRange, int, stop, get_stop, set_stop);
%attribute(TTTRRange, unsigned int, start_time, get_start_time, set_start_time);
%attribute(TTTRRange, unsigned int, stop_time, get_stop_time, set_stop_time);
// TTTRHeader
%attribute(TTTRHeader, size_t, number_of_micro_time_channels, get_number_of_micro_time_channels);
%attribute(TTTRHeader, double, macro_time_resolution, get_macro_time_resolution);
%attribute(TTTRHeader, double, micro_time_resolution, get_micro_time_resolution);
%attribute(TTTRHeader, int, tttr_record_type, get_tttr_record_type, set_tttr_record_type);
%attribute(TTTRHeader, int, tttr_container_type, get_tttr_container_type, set_tttr_container_type);

// Python does not support overloading. Thus, ignore the copy constructor
%ignore TTTRRange(const TTTRRange& p2);
%extend TTTR{%pythoncode "./ext/python/tttr/tttr_extension.py"}
%extend TTTRHeader{%pythoncode "./ext/python/tttr/tttrheader_extension.py"}

// Use shared_prt for TTTR to pass TTTR around
%shared_ptr(TTTR)

%include "../include/TTTRHeader.h"
%include "../include/TTTR.h"
%include "../include/TTTRRange.h"

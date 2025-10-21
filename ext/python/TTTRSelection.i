%{
#include "TTTRSelection.h"
%}

// Python does not support overloading. Thus, ignore the copy constructor
%ignore TTTRSelection(const TTTRSelection& p2);
%ignore TTTRSelection(int start, int stop);

// Ignore the vector-returning version of get_tttr_indices (slow, creates copy)
// Use the pointer-based overload instead for efficient NumPy wrapping
%ignore TTTRSelection::get_tttr_indices() const;

// https://github.com/swig/swig/blob/6f2399e86da13a9feb436e3977e15d2b9738294e/Lib/typemaps/attribute.swg
%attributeval(TTTRSelection, std::vector<int>, start_stop, get_start_stop);
%attribute(TTTRSelection, int, start, get_start);
%attribute(TTTRSelection, int, stop, get_stop);

// for microtime_histogram
%apply(double** ARGOUTVIEWM_ARRAY1, int* DIM1){
    (double** histogram, int* n_histogram),
    (double** time, int* n_time)
};

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

%include "TTTRSelection.h"


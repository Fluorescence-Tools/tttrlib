%{
#include "TTTRRange.h"
%}

// Python does not support overloading. Thus, ignore the copy constructor
%ignore TTTRRange(const TTTRRange& p2);
%ignore TTTRRange(int start, int stop);

// Ignore the vector-returning version of get_tttr_indices (slow, creates copy)
// Use the pointer-based overload instead for efficient NumPy wrapping
// The mapping (int** output, int* n_output) is already defined in misc_types.i
%ignore TTTRRange::get_tttr_indices() const;

// https://github.com/swig/swig/blob/6f2399e86da13a9feb436e3977e15d2b9738294e/Lib/typemaps/attribute.swg
%attributeval(TTTRRange, std::vector<int>, start_stop, get_start_stop);
%attribute(TTTRRange, int, start, get_start);
%attribute(TTTRRange, int, stop, get_stop);

// for microtime_histogram
%apply(double** ARGOUTVIEWM_ARRAY1, int* DIM1){
    (double** histogram, int* n_histogram),
    (double** time, int* n_time)
};

%include "TTTRRange.h"

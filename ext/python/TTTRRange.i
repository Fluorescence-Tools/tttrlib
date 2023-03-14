%{
#include "TTTRRange.h"
%}

// Python does not support overloading. Thus, ignore the copy constructor
%ignore TTTRRange(const TTTRRange& p2);
%ignore TTTRRange(int start, int stop);

// https://github.com/swig/swig/blob/6f2399e86da13a9feb436e3977e15d2b9738294e/Lib/typemaps/attribute.swg
%attributeval(TTTRRange, std::vector<int>, tttr_indices, get_tttr_indices);
%attributeval(TTTRRange, std::vector<int>, start_stop, get_start_stop);
%attribute(TTTRRange, int, start, get_start);
%attribute(TTTRRange, int, stop, get_stop);

// for microtime_histogram
%apply(double** ARGOUTVIEWM_ARRAY1, int* DIM1){
    (double** histogram, int* n_histogram),
    (double** time, int* n_time)
};

%include "TTTRRange.h"

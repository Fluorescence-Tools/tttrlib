%{
#include "../include/TTTRRange.h"
%}

// Python does not support overloading. Thus, ignore the copy constructor
%ignore TTTRRange(const TTTRRange& p2);

// https://github.com/swig/swig/blob/6f2399e86da13a9feb436e3977e15d2b9738294e/Lib/typemaps/attribute.swg
%attribute2(TTTRRange, std::vector<int>, tttr_indices, get_tttr_indices);
%attributeval(TTTRRange, std::vector<int>, start_stop, get_start_stop);
%attribute(TTTRRange, int, start, get_start, set_start);
%attribute(TTTRRange, int, stop, get_stop, set_stop);
%attribute(TTTRRange, unsigned int, start_time, get_start_time, set_start_time);
%attribute(TTTRRange, unsigned int, stop_time, get_stop_time, set_stop_time);

%include "../include/TTTRRange.h"

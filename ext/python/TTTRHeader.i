%{
#include "../include/TTTRHeader.h"
%}

//%ignore TTTRHeader();
//%ignore TTTRHeader(int tttr_container_type);
//%ignore TTTRHeader(const TTTRHeader &p2);
%ignore TTTRHeader(std::FILE *fpin, int tttr_container_type=0, bool close_file=false);

// TTTRHeader
%attribute(TTTRHeader, size_t, number_of_micro_time_channels, get_number_of_micro_time_channels);
%attribute(TTTRHeader, double, macro_time_resolution, get_macro_time_resolution);
%attribute(TTTRHeader, double, micro_time_resolution, get_micro_time_resolution);
%attribute(TTTRHeader, int, tttr_record_type, get_tttr_record_type, set_tttr_record_type);
%attribute(TTTRHeader, int, tttr_container_type, get_tttr_container_type, set_tttr_container_type);

%include "../include/TTTRHeader.h"

%extend TTTRHeader{%pythoncode "./ext/python/TTTRHeader.py"}
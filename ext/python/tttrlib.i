%module tttrlib
%include "documentation.i"

%{
    #define SWIG_FILE_WITH_INIT
    #include "../include/tttr.h"
    #include "../include/histogram.h"
    #include "../include/correlate.h"
    #include "../include/image.h"
    #include "../include/pda.h"
%}

%include <typemaps.i>
%include "stl.i"
//%include "std_wstring.i"
%include std_string.i
%include "std_map.i"
%include "include/numpy.i"
%init %{
    import_array();
%}

%template(map_string_string) std::map<std::string, std::string>;

%include "tttr.i"
%include "histogram.i"
%include "correlate.i"
%include "image.i"
%include "pda.i"

%include "../include/tttr.h"
%include "../include/header.h"
%include "../include/histogram.h"
%include "../include/correlate.h"
%include "../include/image.h"
%include "../include/pda.h"

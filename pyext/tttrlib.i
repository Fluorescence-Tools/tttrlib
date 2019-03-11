%include "documentation.i"

%{
    #define SWIG_FILE_WITH_INIT
    #include "../include/TTTR.h"
    #include "../include/Histogram.h"
    #include "../include/correlate.h"
%}

%include <typemaps.i>
%include "stl.i"
%include "std_wstring.i"
%include std_string.i
%include "std_map.i"

%include "numpy.i"
%init %{
    import_array();
%}

namespace std {
    %template(map_string_string) map<string, string>;
}

%inline %{
    using namespace std;
%}


%include "tttr.i"
%include "histogram.i"
%include "correlate.i"

%include "../include/TTTR.h"
%include "../include/Header.h"
%include "../include/Histogram.h"
%include "../include/correlate.h"

%module(directors="1", package="tttrlib") tttrlib
%feature("kwargs", 1);
#pragma SWIG nowarn=511

%include "documentation.i"

%{
    #define SWIG_FILE_WITH_INIT
%}
%pythonbegin "./ext/python/python_imports.py"

%include <typemaps.i>
%include "stl.i"
%include "std_string.i"
%include "std_wstring.i";
%include "std_map.i"
%include "stdint.i"

%include "numpy.i"
%include "std_vector.i";
%init %{
import_array();
%}

%template(map_string_string) std::map<std::string, std::string>;
%template(VectorUint64) std::vector<unsigned long long>;
%template(VectorUint32) std::vector<unsigned int>;
%template(VectorInt16) std::vector<short>;
%template(vectorUint32_3D) std::vector<std::vector<std::vector<unsigned int>>>;

%include "tttr.i"
%include "histogram.i"
%include "correlation.i"
%include "image.i"
%include "pda.i"

%{
// This is needed for numpy as you need SWIG_FILE_WITH_INIT
// if you have an init section
#define SWIG_FILE_WITH_INIT
%}
%module(directors="1", package="tttrlib") tttrlib
%feature("kwargs", 1);
%include "documentation.i"

%pythonbegin "./ext/python/python_imports.py"

%include "typemaps.i";
%include "stl.i";
%include "std_string.i";
%include "std_wstring.i";
%include "std_map.i";
%include "std_vector.i";
%include "std_list.i";
%include "std_shared_ptr.i";
%include "cpointer.i"
%include "numpy.i"

%init %{
import_array();
%}

%template(MapStringString) std::map<std::string, std::string>;
%template(VectorUint64) std::vector<unsigned long long>;
%template(VectorInt64) std::vector<long long>;
%template(VectorUint32) std::vector<unsigned int>;
%template(VectorInt32) std::vector<int>;
%template(VectorInt16) std::vector<short>;
%template(VectorUint32_3D) std::vector<std::vector<std::vector<unsigned int>>>;

%include "tttr.i"
%include "histogram.i"
%include "correlation.i"
%include "image.i"
%include "pda.i"

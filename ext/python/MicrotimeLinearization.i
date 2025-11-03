%module(package="tttrlib") MicrotimeLinearization

%{
#include "include/MicrotimeLinearization.h"
%}

%include <std_vector.i>
%include <std_string.i>
%include <std_map.i>

namespace std {
    %template(VectorFloat) vector<float>;
    %template(VectorInt) vector<int>;
    %template(MapIntVectorFloat) map<int, vector<float>>;
    %template(MapIntInt) map<int, int>;
}

%include "include/MicrotimeLinearization.h"

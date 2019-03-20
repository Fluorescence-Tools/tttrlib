%module tttrlib
%{
#include "include/Image.h"
#include "include/TTTR.h"
%}


%include "std_vector.i";
%include "std_list.i";
namespace std {
    %template(vector_CLSMFrame) vector<CLSMFrame*>;
    %template(vector_CLSMLine) vector<CLSMLine*>;
    %template(vector_CLSMPixel) vector<CLSMPixel*>;
    %template(vector_uint) vector<unsigned int>;
    %template(vector_ulonglong) vector<unsigned long long>;
    %template(vector3D_uint) vector<vector<vector<unsigned int>>>;
    //%template(vector4D_uchar) vector<vector<vector<vector<char>>>>;
}

%apply (unsigned int** ARGOUTVIEWM_ARRAY3, int* DIM1, int* DIM2, int* DIM3) {(unsigned int** out, int* dim1, int* dim2, int* dim3)}
%apply (double** ARGOUTVIEWM_ARRAY3, int* DIM1, int* DIM2, int* DIM3) {(double** out, int* dim1, int* dim2, int* dim3)}
%apply (unsigned char** ARGOUTVIEWM_ARRAY4, int* DIM1, int* DIM2, int* DIM3, int* DIM4) {(unsigned char** out, int* dim1, int* dim2, int* dim3, int* dim4)}


%include "../include/TTTR.h"
%include "../include/Image.h"

%module(directors="1") tttrlib
%{
#include "include/image.h"
#include "include/tttr.h"
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

%apply (unsigned int** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {
    (unsigned int** out, int* dim1, int* dim2)
}

%apply (unsigned int** ARGOUTVIEWM_ARRAY3, int* DIM1, int* DIM2, int* DIM3) {
    (unsigned int** out, int* dim1, int* dim2, int* dim3)
}
%apply (double** ARGOUTVIEWM_ARRAY3, int* DIM1, int* DIM2, int* DIM3) {
    (double** out, int* dim1, int* dim2, int* dim3)
}
%apply (
        unsigned char** ARGOUTVIEWM_ARRAY4,
        int* DIM1, int* DIM2, int* DIM3, int* DIM4
        ) {
    (unsigned char** out, int* dim1, int* dim2, int* dim3, int* dim4)
}

%apply (
        short* IN_ARRAY3, int DIM1, int DIM2, int DIM3
) {
    (short* selection, int d_selection_1, int d_selection_2, int d_selection_3)
}

%extend CLSMImage {
        CLSMFrame* __getitem__(unsigned int i) {
            return (*($self))[i];
        }
}

%extend CLSMFrame {
        CLSMLine* __getitem__(unsigned int i) {
            return (*($self))[i];
        }
}

%extend CLSMLine {
        CLSMPixel* __getitem__(unsigned int i) {
            return (*($self))[i];
        }
}

%include "../include/tttr.h"
%include "../include/image.h"
